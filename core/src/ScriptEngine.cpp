#include "ScriptEngine.h"
#include "ScriptLibrary.h"
#include "ScriptRunner.h"
#include "ScriptSchema.h"
#include "ScriptSource.h"
#include <QCryptographicHash>
#include <QFileDevice>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QScopedValueRollback>
#include <QTimer>
#include <algorithm>
#include <atomic>
#include <utility>

namespace ScriptHost
{

namespace
{

const int kMaxRuns = 100;

QString compactJson(const QJsonValue& value)
{
    // QJsonDocument cannot hold a scalar in Qt 5, so serialise via an array.
    const QByteArray json = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(json.mid(1, json.size() - 2));
}

QString valuePayload(const QJsonValue& value)
{
    return QStringLiteral("{\"v\":") + compactJson(value.isUndefined() ? QJsonValue() : value)
           + QLatin1Char('}');
}

QString errorPayload(const QString& message)
{
    return QString::fromUtf8(QJsonDocument(QJsonObject{{"e", message}}).toJson(QJsonDocument::Compact));
}

QJsonObject scriptError(const QString& phase, const QString& message)
{
    return {{"status", "error"}, {"phase", phase}, {"message", message}};
}

// In the order of ScriptEngine::ScriptKind.
const char* const kKindKeys[ScriptEngine::ScriptKindCount] = {
    "new_reading", "new_changing", "saved_reading", "saved_changing", "dry_run"};

bool presetRequires(ScriptEngine::Approval approval, ScriptEngine::ScriptKind kind)
{
    if (approval == ScriptEngine::Approval::ChangingScripts)
        return kind == ScriptEngine::ScriptKind::NewChanging || kind == ScriptEngine::ScriptKind::SavedChanging;
    return approval == ScriptEngine::Approval::Always;
}

// First sentence of a description, for one-line listings.
QString summaryOf(const QString& description)
{
    QString summary = description.simplified();
    const int end = summary.indexOf(QStringLiteral(". "));
    if (end > 0)
        summary.truncate(end + 1);
    return summary.left(120);
}

} // namespace

// ------------------------------------------------------------ completion

struct ScriptCompletion::State
{
    QString                             name;       // of the method, for the message
    std::function<void(const QString&)> deliver;    // takes the payload; any thread
    std::atomic<bool>                   done{false};
};

void ScriptCompletion::finish(const QJsonValue& value) const
{
    if (!d || d->done.exchange(true))
        return;
    d->deliver(valuePayload(value));
}

void ScriptCompletion::fail(const QString& message) const
{
    if (!d || d->done.exchange(true))
        return;
    d->deliver(errorPayload(d->name + QStringLiteral(": ") + message));
}

// ------------------------------------------------------------ construction

struct ScriptEngine::Run
{
    ScriptRunner*                   runner = nullptr;
    QSharedPointer<ScriptRunShared> shared;
    QHash<QString, Entry>           entries;    // the methods as they were at the start
    RunCallback                     callback;
    RunRecord                       record;
    bool                            debug = false;
};

ScriptEngine::ScriptEngine(QObject* parent)
    : QObject(parent)
    , m_requesterName(QStringLiteral("another program"))
{
    // For queued connections and QSignalSpy, which know the run* signals'
    // parameter types by name.
    qRegisterMetaType<RunId>("RunId");
    registerBuiltins();
}

ScriptEngine::~ScriptEngine()
{
    // All stopped first, so that they end side by side.
    for (Run* active : std::as_const(m_active))
        active->runner->requestStop(QStringLiteral("cancelled"));
    for (Run* active : std::as_const(m_active)) {
        active->runner->wait();
        delete active->runner;
        delete active;
    }
}

void ScriptEngine::registerBuiltins()
{
    auto add = [this](const ScriptMethod& method) {
        Entry entry;
        entry.method = method;
        entry.local = true;
        m_registered.append(entry);
    };
    const QString group = QStringLiteral("Script");

    ScriptMethod log(QStringLiteral("log"));
    log.setDescription(QStringLiteral("Writes a line to the script log: the user sees it, and the last "
                                      "lines come back with the result. Takes any number of values."))
       .setGroup(group).setReadOnly(true)
       .addRawParameter(QStringLiteral("value"), QJsonObject{{"description", "What to write"}});
    add(log);

    // The same as log, under the names known from elsewhere.
    for (const char* name : {"print", "console.log"}) {
        ScriptMethod alias(QString::fromLatin1(name));
        alias.setDescription(QStringLiteral("The same as log: writes a line to the script log. Takes any "
                                            "number of values."))
             .setGroup(group).setReadOnly(true)
             .addRawParameter(QStringLiteral("value"), QJsonObject{{"description", "What to write"}});
        add(alias);
    }

    ScriptMethod progress(QStringLiteral("progress"));
    progress.setDescription(QStringLiteral("Shows how far the script is, in the status line."))
            .setGroup(group).setReadOnly(true)
            .addParameter(QStringLiteral("done"), QStringLiteral("number"), QStringLiteral("Items done"))
            .addParameter(QStringLiteral("total"), QStringLiteral("number"), QStringLiteral("Items in all"))
            .addParameter(QStringLiteral("text"), QStringLiteral("string"), QStringLiteral("What is being done"), false);
    add(progress);

    ScriptMethod sleep(QStringLiteral("sleep"));
    sleep.setDescription(QStringLiteral("Waits. The application stays responsive."))
         .setGroup(group).setReadOnly(true)
         .addParameter(QStringLiteral("ms"), QStringLiteral("number"), QStringLiteral("Milliseconds"));
    add(sleep);

    ScriptMethod pause(QStringLiteral("pause"));
    pause.setDescription(QStringLiteral("Stops here when the script runs in the debugger; does nothing otherwise."))
         .setGroup(group).setReadOnly(true)
         .addParameter(QStringLiteral("label"), QStringLiteral("string"), QStringLiteral("Shown in the debugger"), false);
    add(pause);

    // Timers. The runner makes them JavaScript functions of its own: they
    // take a function, which cannot cross to the application as JSON.
    const QJsonObject fn{{"description", "The function to call"}};
    const QJsonObject rest{{"description", "Further arguments: passed on to fn"}};
    ScriptMethod setTimeout(QStringLiteral("setTimeout"));
    setTimeout.setDescription(QStringLiteral("Calls fn once, after ms milliseconds. The script runs on, after its "
                                             "end, until no timer is left."))
              .setGroup(group).setReadOnly(true)
              .addRawParameter(QStringLiteral("fn"), fn)
              .addParameter(QStringLiteral("ms"), QStringLiteral("number"), QStringLiteral("Milliseconds"), false)
              .addRawParameter(QStringLiteral("args"), rest, false)
              .setReturns(QStringLiteral("number"), QStringLiteral("The timer, for clearTimeout"));
    add(setTimeout);

    ScriptMethod setInterval(QStringLiteral("setInterval"));
    setInterval.setDescription(QStringLiteral("Calls fn every ms milliseconds (at least 1). The script runs on, "
                                              "after its end, until no timer is left."))
               .setGroup(group).setReadOnly(true)
               .addRawParameter(QStringLiteral("fn"), fn)
               .addParameter(QStringLiteral("ms"), QStringLiteral("number"), QStringLiteral("Milliseconds"), false)
               .addRawParameter(QStringLiteral("args"), rest, false)
               .setReturns(QStringLiteral("number"), QStringLiteral("The timer, for clearInterval"));
    add(setInterval);

    ScriptMethod clearTimeout(QStringLiteral("clearTimeout"));
    clearTimeout.setDescription(QStringLiteral("Cancels a timer of setTimeout."))
                .setGroup(group).setReadOnly(true)
                .addParameter(QStringLiteral("id"), QStringLiteral("number"), QStringLiteral("What setTimeout returned"));
    add(clearTimeout);

    // Events. Handled by the runner, which keeps the handlers in JavaScript.
    ScriptMethod on(QStringLiteral("on"));
    on.setDescription(QStringLiteral("Calls fn with the data of each event of this name the application posts, "
                                     "after the script's end. The script runs on until it has no handler and "
                                     "no timer left."))
      .setGroup(group).setReadOnly(true)
      .addParameter(QStringLiteral("name"), QStringLiteral("string"),
                    QStringLiteral("The event, e.g. \"key\" or \"mouse\""))
      .addRawParameter(QStringLiteral("fn"), QJsonObject{{"description", "Called with the event's data"}});
    add(on);

    ScriptMethod off(QStringLiteral("off"));
    off.setDescription(QStringLiteral("Removes fn as a handler of the event; without fn, every handler of it."))
       .setGroup(group).setReadOnly(true)
       .addParameter(QStringLiteral("name"), QStringLiteral("string"), QStringLiteral("The event"))
       .addRawParameter(QStringLiteral("fn"), QJsonObject{{"description", "The handler given to on()"}}, false);
    add(off);

    ScriptMethod clearInterval(QStringLiteral("clearInterval"));
    clearInterval.setDescription(QStringLiteral("Stops a timer of setInterval."))
                 .setGroup(group).setReadOnly(true)
                 .addParameter(QStringLiteral("id"), QStringLiteral("number"), QStringLiteral("What setInterval returned"));
    add(clearInterval);
}

// ------------------------------------------------------------ switches

void ScriptEngine::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;
    m_enabled = enabled;
    emit enabledChanged(enabled);
}

void ScriptEngine::setEnabledUserConfigurable(bool configurable)
{
    if (m_enabledUserConfigurable == configurable)
        return;
    m_enabledUserConfigurable = configurable;
    emit settingsChanged();
}

bool ScriptEngine::setEnabledByUser(bool enabled)
{
    if (!m_enabledUserConfigurable)
        return false;
    setEnabled(enabled);
    return true;
}

void ScriptEngine::setRequesterName(const QString& name)
{
    if (m_requesterName == name)
        return;
    m_requesterName = name;
    emit settingsChanged();
}

QString ScriptEngine::scriptKindKey(ScriptKind kind)
{
    return QString::fromLatin1(kKindKeys[int(kind)]);
}

void ScriptEngine::setApprovalRequired(ScriptKind kind, bool required)
{
    if (m_approvalRequired[int(kind)] == required)
        return;
    m_approvalRequired[int(kind)] = required;
    emit settingsChanged();
}

bool ScriptEngine::isApprovalRequired(ScriptKind kind) const
{
    return m_approvalRequired[int(kind)];
}

void ScriptEngine::setApproval(Approval approval)
{
    bool differs = false;
    for (int i = 0; i < ScriptKindCount; ++i) {
        const bool required = presetRequires(approval, static_cast<ScriptKind>(i));
        differs = differs || m_approvalRequired[i] != required;
        m_approvalRequired[i] = required;
    }
    if (differs)
        emit settingsChanged();
}

void ScriptEngine::setApprovalUserConfigurable(ScriptKind kind, bool configurable)
{
    if (m_approvalConfigurable[int(kind)] == configurable)
        return;
    m_approvalConfigurable[int(kind)] = configurable;
    emit settingsChanged();
}

void ScriptEngine::setApprovalUserConfigurable(bool configurable)
{
    bool differs = false;
    for (int i = 0; i < ScriptKindCount; ++i) {
        differs = differs || m_approvalConfigurable[i] != configurable;
        m_approvalConfigurable[i] = configurable;
    }
    if (differs)
        emit settingsChanged();
}

bool ScriptEngine::isApprovalUserConfigurable(ScriptKind kind) const
{
    return m_approvalConfigurable[int(kind)];
}

bool ScriptEngine::setApprovalRequiredByUser(ScriptKind kind, bool required)
{
    if (!m_approvalConfigurable[int(kind)])
        return false;
    setApprovalRequired(kind, required);
    return true;
}

void ScriptEngine::setApprovalHandler(ApprovalHandler handler)
{
    m_approvalHandler = std::move(handler);
}

void ScriptEngine::setLimits(const Limits& limits)
{
    m_limits = limits;
    emit settingsChanged();
}

void ScriptEngine::setApiBudgetChars(int chars)
{
    m_apiBudget = chars;
}

void ScriptEngine::setLibrary(ScriptLibrary* library)
{
    if (m_library == library)
        return;
    m_library = library;
    emit settingsChanged();
}

ScriptLibrary* ScriptEngine::library() const
{
    return m_library;
}

void ScriptEngine::setUserMayEditScripts(bool allowed)
{
    if (m_userMayEdit == allowed)
        return;
    m_userMayEdit = allowed;
    emit settingsChanged();
}

void ScriptEngine::setDebuggerEnabled(bool enabled)
{
    if (m_debuggerEnabled == enabled)
        return;
    m_debuggerEnabled = enabled;
    emit settingsChanged();
}

// ------------------------------------------------------------ methods

void ScriptEngine::registerMethod(const ScriptMethod& method, ScriptMethodHandler handler)
{
    if (!ScriptMethod::isValidName(method.name())) {
        qWarning("ScriptHost::ScriptEngine: '%s' is not a valid method name", qPrintable(method.name()));
        return;
    }
    Entry entry;
    entry.method = method;
    entry.handler = std::move(handler);
    for (int i = 0; i < m_registered.size(); ++i) {
        if (m_registered[i].method.name() == method.name()) {
            m_registered[i] = entry;
            emit methodsChanged();
            return;
        }
    }
    m_registered.append(entry);
    emit methodsChanged();
}

void ScriptEngine::registerAsyncMethod(const ScriptMethod& method, AsyncScriptMethodHandler handler)
{
    if (!ScriptMethod::isValidName(method.name())) {
        qWarning("ScriptHost::ScriptEngine: '%s' is not a valid method name", qPrintable(method.name()));
        return;
    }
    Entry entry;
    entry.method = method;
    entry.asyncHandler = std::move(handler);
    for (int i = 0; i < m_registered.size(); ++i) {
        if (m_registered[i].method.name() == method.name()) {
            m_registered[i] = entry;
            emit methodsChanged();
            return;
        }
    }
    m_registered.append(entry);
    emit methodsChanged();
}

void ScriptEngine::unregisterMethod(const QString& name)
{
    for (int i = 0; i < m_registered.size(); ++i) {
        if (m_registered[i].method.name() == name) {
            m_registered.removeAt(i);
            emit methodsChanged();
            return;
        }
    }
}

const ScriptEngine::Entry* ScriptEngine::findEntry(const QString& name) const
{
    for (const Entry& entry : m_registered) {
        if (entry.method.name() == name)
            return &entry;
    }
    return nullptr;
}

QList<ScriptMethod> ScriptEngine::methods() const
{
    QList<ScriptMethod> list;
    for (const Entry& entry : m_registered)
        list.append(entry.method);
    std::sort(list.begin(), list.end(), [](const ScriptMethod& a, const ScriptMethod& b) {
        return a.name() < b.name();
    });
    return list;
}

bool ScriptEngine::hasMethod(const QString& name) const
{
    return findEntry(name) != nullptr;
}

bool ScriptEngine::policy(const QHash<QString, bool>& byName, const QHash<QString, bool>& byGroup,
                          const ScriptMethod& method, bool fallback) const
{
    auto it = byName.constFind(method.name());
    if (it != byName.cend())
        return it.value();
    it = byGroup.constFind(method.group());
    if (it != byGroup.cend())
        return it.value();
    return fallback;
}

void ScriptEngine::setMethodEnabled(const QString& name, bool enabled)
{
    m_methodEnabled.insert(name, enabled);
    emit methodsChanged();
}

void ScriptEngine::setMethodGroupEnabled(const QString& group, bool enabled)
{
    m_groupEnabled.insert(group, enabled);
    emit methodsChanged();
}

bool ScriptEngine::isMethodEnabled(const QString& name) const
{
    const Entry* entry = findEntry(name);
    return entry && policy(m_methodEnabled, m_groupEnabled, entry->method, true);
}

void ScriptEngine::setMethodUserConfigurable(const QString& name, bool configurable)
{
    m_methodConfigurable.insert(name, configurable);
    emit methodsChanged();
}

void ScriptEngine::setMethodGroupUserConfigurable(const QString& group, bool configurable)
{
    m_groupConfigurable.insert(group, configurable);
    emit methodsChanged();
}

bool ScriptEngine::isMethodUserConfigurable(const QString& name) const
{
    const Entry* entry = findEntry(name);
    return !entry || policy(m_methodConfigurable, m_groupConfigurable, entry->method, true);
}

bool ScriptEngine::setMethodEnabledByUser(const QString& name, bool enabled)
{
    if (!hasMethod(name) || !isMethodUserConfigurable(name))
        return false;
    setMethodEnabled(name, enabled);
    return true;
}

// ------------------------------------------------------------ definition

QString ScriptEngine::apiDefinition(const QString& query) const
{
    QList<ScriptMethod> list;
    {
        const QList<Entry>& all = m_registered;
        for (const Entry& entry : all) {
            if (policy(m_methodEnabled, m_groupEnabled, entry.method, true))
                list.append(entry.method);
        }
    }
    std::sort(list.begin(), list.end(), [](const ScriptMethod& a, const ScriptMethod& b) {
        return a.name() < b.name();
    });

    auto index = [&list]() {
        QString text = QStringLiteral("// %1 methods. Ask for the declarations with a query: a method name, "
                                      "a namespace, keywords.\n").arg(list.size());
        for (const ScriptMethod& method : list)
            text += method.name() + QStringLiteral(" - ") + summaryOf(method.description()) + QLatin1Char('\n');
        return text;
    };
    auto render = [](const QList<ScriptMethod>& methods) {
        QString text = QStringLiteral(
            "// Methods a script can call. Calls are synchronous; a failing call throws.\n");
        QStringList nameSpaces;
        for (const ScriptMethod& method : methods) {
            if (!method.nameSpace().isEmpty() && !nameSpaces.contains(method.nameSpace()))
                nameSpaces << method.nameSpace();
        }
        for (const QString& nameSpace : std::as_const(nameSpaces)) {
            text += QStringLiteral("declare namespace %1 {\n").arg(nameSpace);
            for (const ScriptMethod& method : methods) {
                if (method.nameSpace() == nameSpace)
                    text += method.declaration(QStringLiteral("  "));
            }
            text += QStringLiteral("}\n");
        }
        for (const ScriptMethod& method : methods) {
            if (method.nameSpace().isEmpty())
                text += method.declaration(QString(), QStringLiteral("declare function"));
        }
        return text;
    };
    const QString argsNote = QStringLiteral("/** The arguments of this run. */\ndeclare const args: any;\n");

    const QStringList words = query.toLower().split(QRegularExpression(QStringLiteral("[^a-z0-9_.$]+")),
                                                     Qt::SkipEmptyParts);
    if (words.isEmpty()) {
        const QString all = render(list) + argsNote;
        return m_apiBudget > 0 && all.size() > m_apiBudget ? index() : all;
    }

    struct Match { int score; ScriptMethod method; };
    QList<Match> matches;
    for (const ScriptMethod& method : std::as_const(list)) {
        const QString name = method.name().toLower();
        // Everything the definition says: description, parameters, return value.
        const QString text = (method.declaration() + QLatin1Char(' ') + method.group()).toLower();
        int score = 0;
        for (const QString& word : words) {
            if (name == word || method.nameSpace().toLower() == word)
                score += 5;
            else if (name.contains(word))
                score += 3;
            else if (text.contains(word))
                score += 1;
        }
        if (score > 0)
            matches.append(Match{score, method});
    }
    if (matches.isEmpty())
        return QStringLiteral("// Nothing matches '%1'.\n").arg(query) + index();

    std::stable_sort(matches.begin(), matches.end(),
                     [](const Match& a, const Match& b) { return a.score > b.score; });
    // Best matches first, as many as fit; then back in name order.
    QList<ScriptMethod> chosen;
    int size = 0;
    for (const Match& match : std::as_const(matches)) {
        size += match.method.declaration(QStringLiteral("  ")).size();
        if (!chosen.isEmpty() && m_apiBudget > 0 && size > m_apiBudget)
            break;
        chosen.append(match.method);
    }
    std::sort(chosen.begin(), chosen.end(), [](const ScriptMethod& a, const ScriptMethod& b) {
        return a.name() < b.name();
    });
    QString text = render(chosen);
    if (chosen.size() < matches.size())
        text += QStringLiteral("// %1 more match; narrow the query.\n").arg(matches.size() - chosen.size());
    return text;
}

// ------------------------------------------------------------ check

QJsonObject ScriptEngine::Check::toJson() const
{
    if (ok)
        return {{"status", "ok"}};
    QJsonObject json{{"status", "error"}, {"phase", phase}, {"message", message}};
    if (line > 0)
        json["line"] = line;
    if (!didYouMean.isEmpty())
        json["did_you_mean"] = didYouMean;
    return json;
}

ScriptEngine::Check ScriptEngine::check(const QString& code) const
{
    Check result;
    // Syntax. Compiled on a thread of its own so that code written to run at
    // compile time (by closing the function early) can be stopped; it finds
    // no methods there in any case.
    {
        ScriptRunConfig config;
        config.code = code;
        config.compileOnly = true;
        ScriptRunner compiler(config, QSharedPointer<ScriptRunShared>::create());
        compiler.start();
        if (!compiler.wait(3000)) {
            compiler.requestStop(QStringLiteral("timeout"));
            compiler.wait();
            result.phase = QStringLiteral("syntax");
            result.message = QStringLiteral("the script is not the body of a function: it runs code while it is read");
            return result;
        }
        const QJsonObject compiled = compiler.result();
        if (compiled.value("status").toString() != QLatin1String("ok")) {
            result.phase = QStringLiteral("syntax");
            result.line = compiled.value("line").toInt();
            result.message = compiled.value("message").toString();
            return result;
        }
    }

    // What it uses.
    const QList<Entry>& all = m_registered;
    QStringList nameSpaces, globals;
    for (const Entry& entry : all) {
        const QString nameSpace = entry.method.nameSpace();
        if (nameSpace.isEmpty())
            globals << entry.method.name();
        else if (!nameSpaces.contains(nameSpace))
            nameSpaces << nameSpace;
    }

    result.dynamic = ScriptSource::usesDynamicAccess(code);
    const QList<ScriptSource::Reference> references = ScriptSource::references(code, nameSpaces, globals);
    for (const ScriptSource::Reference& reference : references) {
        if (reference.function.isEmpty()) {
            result.dynamic = true;      // db[name], f(db): cannot tell what gets called
            continue;
        }
        const QString name = reference.nameSpace.isEmpty()
            ? reference.function
            : reference.nameSpace + QLatin1Char('.') + reference.function;
        const Entry* entry = findEntry(name);
        const bool enabled = entry && policy(m_methodEnabled, m_groupEnabled, entry->method, true);
        if (!enabled) {
            result.phase = QStringLiteral("check");
            result.line = reference.line;
            if (entry) {
                result.message = QStringLiteral("the method '%1' is disabled").arg(name);
                return result;
            }
            QStringList siblings;
            for (const Entry& other : all) {
                if (other.method.nameSpace() == reference.nameSpace
                    && policy(m_methodEnabled, m_groupEnabled, other.method, true))
                    siblings << other.method.name();
            }
            result.message = QStringLiteral("no method '%1'").arg(name);
            result.didYouMean = ScriptSchema::closestName(name, siblings);
            if (result.didYouMean.isEmpty() && !siblings.isEmpty() && siblings.size() <= 15)
                result.message += QStringLiteral("; '%1' has: %2").arg(reference.nameSpace, siblings.join(QStringLiteral(", ")));
            return result;
        }
        if (!result.methods.contains(name)) {
            result.methods << name;
            if (!entry->method.isReadOnly())
                result.changingMethods << name;
        }
    }
    result.ok = true;
    return result;
}

// ------------------------------------------------------------ run

bool ScriptEngine::isRunning() const
{
    return !m_active.isEmpty();
}

bool ScriptEngine::isRunning(RunId id) const
{
    return m_active.contains(id);
}

QList<ScriptEngine::RunId> ScriptEngine::activeRuns() const
{
    return m_active.keys();
}

void ScriptEngine::setOutput(QIODevice* device)
{
    m_output = device;
}

QIODevice* ScriptEngine::output() const
{
    return m_output;
}

ScriptEngine::RunRecord ScriptEngine::activeRun(RunId id) const
{
    const Run* active = m_active.value(id);
    return active ? active->record : RunRecord();
}

bool ScriptEngine::isExternalRun(RunId id) const
{
    const Run* active = m_active.value(id);
    return active && active->record.external;
}

void ScriptEngine::stop(RunId id)
{
    if (const Run* active = m_active.value(id))
        active->runner->requestStop(QStringLiteral("cancelled"));
}

void ScriptEngine::stopAll()
{
    for (const Run* active : std::as_const(m_active))
        active->runner->requestStop(QStringLiteral("cancelled"));
}

namespace
{

void queueEvent(ScriptRunShared& shared, const QString& name, const QString& json)
{
    QMutexLocker lock(&shared.mutex);
    if (!shared.listening.contains(name))
        return;
    shared.events.append({name, json});
    shared.wake.wakeAll();
}

} // namespace

void ScriptEngine::postEvent(const QString& name, const QJsonObject& data)
{
    const QString json = compactJson(data);
    for (const Run* active : std::as_const(m_active))
        queueEvent(*active->shared, name, json);
}

void ScriptEngine::postEvent(RunId id, const QString& name, const QJsonObject& data)
{
    if (const Run* active = m_active.value(id))
        queueEvent(*active->shared, name, compactJson(data));
}

void ScriptEngine::allowEventForRequestedRuns(const QString& name, bool allow)
{
    if (allow)
        m_requestedEvents.insert(name);
    else
        m_requestedEvents.remove(name);
}

bool ScriptEngine::isEventAllowedForRequestedRuns(const QString& name) const
{
    return m_requestedEvents.contains(name);
}

ScriptEngine::Run* ScriptEngine::debugRun() const
{
    for (Run* active : m_active) {
        if (active->debug)
            return active;
    }
    return nullptr;
}

QString ScriptEngine::unavailableReason(bool debug) const
{
    if (m_limits.maxConcurrentRuns > 0 && m_active.size() >= m_limits.maxConcurrentRuns)
        return QStringLiteral("the limit of %1 scripts running at once is reached; wait for one to finish")
            .arg(m_limits.maxConcurrentRuns);
    if (debug && debugRun())
        return QStringLiteral("another script runs in the debugger; stop it first");
    return QString();
}

void ScriptEngine::stepStatement()
{
    const Run* active = debugRun();
    if (!active)
        return;
    QMutexLocker lock(&active->shared->mutex);
    active->shared->resume = 3;
    active->shared->wake.wakeAll();
}

void ScriptEngine::stepRun()
{
    const Run* active = debugRun();
    if (!active)
        return;
    QMutexLocker lock(&active->shared->mutex);
    active->shared->resume = 1;
    active->shared->wake.wakeAll();
}

void ScriptEngine::continueRun()
{
    const Run* active = debugRun();
    if (!active)
        return;
    QMutexLocker lock(&active->shared->mutex);
    active->shared->resume = 2;
    active->shared->wake.wakeAll();
}

void ScriptEngine::setBreakpoints(const QSet<int>& lines)
{
    const Run* active = debugRun();
    if (!active)
        return;
    QMutexLocker lock(&active->shared->mutex);
    active->shared->breakpoints = lines;
}

void ScriptEngine::clearRuns()
{
    m_runs.clear();
    emit runsChanged();
}

ScriptEngine::RunId ScriptEngine::run(const QString& code, const RunOptions& options, RunCallback done)
{
    const QString unavailable = unavailableReason(options.debug);
    if (!unavailable.isEmpty()) {
        if (done)
            done(scriptError(QStringLiteral("unavailable"), unavailable));
        return 0;
    }

    ScriptRunConfig config;
    config.code = code;
    config.argsJson = compactJson(options.args);
    config.maxCalls = options.maxCalls < 0 ? m_limits.maxCalls : options.maxCalls;
    config.maxLogLines = m_limits.maxLogLines;
    config.dryRun = options.dryRun;
    config.debug = options.debug;
    config.statements = options.debug && options.statements;
    config.readOnlyOnly = options.readOnlyOnly;
    config.requested = options.external;
    config.requestedEvents = m_requestedEvents;

    const RunId id = ++m_lastRunId;
    Run* active = new Run;
    active->debug = options.debug;

    // The methods as they are now; what changes during the run does not reach it.
    const QList<Entry>& all = m_registered;
    for (const Entry& entry : all) {
        if (!policy(m_methodEnabled, m_groupEnabled, entry.method, true))
            continue;
        ScriptMethodInfo info;
        info.name = entry.method.name();
        info.readOnly = entry.method.isReadOnly();
        info.local = entry.local;
        info.maxCalls = entry.method.maxCallsPerRun();
        if (!entry.method.returnExample().isUndefined())
            info.exampleJson = compactJson(entry.method.returnExample());
        config.methods.append(info);
        active->entries.insert(info.name, entry);
    }

    active->shared = QSharedPointer<ScriptRunShared>::create();
    active->shared->stepMode = !(options.debug && options.stepFromStart) ? ScriptRunShared::RunOn
                               : options.statements ? ScriptRunShared::ToStatement
                                                    : ScriptRunShared::ToCall;
    active->shared->breakpoints = options.breakpoints;
    active->callback = std::move(done);
    active->record.id = id;
    active->record.started = QDateTime::currentDateTime();
    active->record.name = options.name;
    active->record.code = code;
    active->record.args = options.args;
    active->record.dryRun = options.dryRun;
    active->record.external = options.external;

    ScriptRunner* runner = new ScriptRunner(config, active->shared, this);
    active->runner = runner;
    m_active.insert(id, active);
    // The runner object lives on this thread and signals from its own, so
    // these arrive queued - all of them before `completed`.
    connect(runner, &ScriptRunner::callRequested, this,
            [this, id](quint64 call, const QString& name, const QString& argsJson) {
        onCallRequested(id, call, name, argsJson);
    });
    connect(runner, &ScriptRunner::paused, this,
            [this, id](int line, const QString& name, const QString& argsJson, const QString& variablesJson) {
        emit runPaused(id, line, name, QJsonDocument::fromJson(argsJson.toUtf8()).array(),
                       QJsonDocument::fromJson(variablesJson.toUtf8()).object());
    });
    connect(runner, &ScriptRunner::callReturned, this,
            [this, id](int line, const QString& name, const QString& argsJson, const QString& payloadJson) {
        const QJsonObject payload = QJsonDocument::fromJson(payloadJson.toUtf8()).object();
        emit runCallFinished(id, line, name, QJsonDocument::fromJson(argsJson.toUtf8()).array(),
                             payload.value("v"), payload.value("e").toString());
    });
    connect(runner, &ScriptRunner::logged, this, [this, id](const QString& text) {
        if (m_output) {
            const Run* current = m_active.value(id);
            const QString name = current && !current->record.name.isEmpty() ? current->record.name
                                                                             : QStringLiteral("#%1").arg(id);
            m_output->write(QStringLiteral("[%1] %2\n").arg(name, text).toUtf8());
            if (auto* file = qobject_cast<QFileDevice*>(m_output.data()))
                file->flush();
        }
        emit runLog(id, text);
    });
    connect(runner, &ScriptRunner::progressed, this,
            [this, id](double doneCount, double total, const QString& text) {
        emit runProgress(id, doneCount, total, text);
    });
    connect(runner, &ScriptRunner::completed, this, [this, id](const QJsonObject& result) {
        onRunCompleted(id, result);
    });

    // A paused script is waiting for the user, not running away. The timer
    // goes with the runner.
    const int timeoutMs = options.timeoutMs < 0 ? m_limits.timeoutMs : options.timeoutMs;
    if (!options.debug && timeoutMs > 0)
        QTimer::singleShot(timeoutMs, runner, [runner]() { runner->requestStop(QStringLiteral("timeout")); });
    emit runStarted(id, options.name, options.external);
    runner->start();
    return id;
}

void ScriptEngine::onRunCompleted(RunId id, const QJsonObject& result)
{
    Run* active = m_active.take(id);
    if (!active)
        return;
    active->runner->wait();
    active->runner->deleteLater();

    active->record.result = result;
    m_runs.append(active->record);
    while (m_runs.size() > kMaxRuns)
        m_runs.removeFirst();

    // The callback may start the next script.
    const RunCallback callback = std::move(active->callback);
    delete active;
    emit runsChanged();
    emit runFinished(id, result);
    if (callback)
        callback(result);
}

void ScriptEngine::onCallRequested(RunId id, quint64 call, const QString& name, const QString& argsJson)
{
    const Run* active = m_active.value(id);
    if (!active)
        return;
    // Hands the answer to the waiting script. Holds the shared state, not
    // this object: an asynchronous method may answer from any thread, and
    // after this object is gone.
    const QSharedPointer<ScriptRunShared> shared = active->shared;
    auto deliver = [shared, call](const QString& payload) {
        QMutexLocker lock(&shared->mutex);
        if (shared->pendingId != call || shared->resultReady)
            return;     // the script is not waiting for this any more
        shared->result = payload;
        shared->resultReady = true;
        shared->wake.wakeAll();
    };

    const auto it = active->entries.constFind(name);
    if (it == active->entries.cend()) {
        deliver(errorPayload(QStringLiteral("no method '%1'").arg(name)));
        return;
    }
    const Entry entry = it.value();
    const QScopedValueRollback<RunId> calling(m_callingRun, id);

    QJsonObject args;
    QString problem;
    if (!entry.method.bindArguments(QJsonDocument::fromJson(argsJson.toUtf8()).array(), args, &problem)) {
        deliver(errorPayload(QStringLiteral("%1: %2. Signature: %3")
                                 .arg(name, problem, entry.method.signature())));
        return;
    }

    if (entry.asyncHandler) {
        ScriptCompletion completion;
        completion.d = QSharedPointer<ScriptCompletion::State>::create();
        completion.d->name = name;
        completion.d->deliver = deliver;
        try {
            entry.asyncHandler(args, completion);
        } catch (const std::exception& e) {
            completion.fail(QString::fromUtf8(e.what()));
        } catch (...) {
            completion.fail(QStringLiteral("unknown exception in the method"));
        }
        return;
    }

    if (!entry.handler) {
        deliver(errorPayload(name + QStringLiteral(": the method has no handler")));
        return;
    }
    try {
        deliver(valuePayload(entry.handler(args)));
    } catch (const std::exception& e) {
        deliver(errorPayload(name + QStringLiteral(": ") + QString::fromUtf8(e.what())));
    } catch (...) {
        deliver(errorPayload(name + QStringLiteral(": unknown exception in the method")));
    }
}

// ------------------------------------------------------------ user settings

QJsonObject ScriptEngine::userSettings() const
{
    QJsonObject settings;
    if (m_enabledUserConfigurable)
        settings["enabled"] = m_enabled;
    QJsonObject approval;
    for (int i = 0; i < ScriptKindCount; ++i) {
        if (m_approvalConfigurable[i])
            approval.insert(QString::fromLatin1(kKindKeys[i]), m_approvalRequired[i]);
    }
    if (!approval.isEmpty())
        settings["approval"] = approval;
    QJsonObject methods;
    const QList<Entry>& all = m_registered;
    for (const Entry& entry : all) {
        if (policy(m_methodConfigurable, m_groupConfigurable, entry.method, true))
            methods.insert(entry.method.name(), policy(m_methodEnabled, m_groupEnabled, entry.method, true));
    }
    settings["methods"] = methods;
    return settings;
}

void ScriptEngine::applyUserSettings(const QJsonObject& settings)
{
    if (settings.contains("enabled"))
        setEnabledByUser(settings.value("enabled").toBool());
    const QJsonValue approval = settings.value("approval");
    if (approval.isObject()) {
        const QJsonObject kinds = approval.toObject();
        for (int i = 0; i < ScriptKindCount; ++i) {
            const QString key = QString::fromLatin1(kKindKeys[i]);
            if (kinds.contains(key))
                setApprovalRequiredByUser(static_cast<ScriptKind>(i), kinds.value(key).toBool());
        }
    } else if (approval.isString()) {
        // Stored when there were three modes instead of kinds.
        const QString mode = approval.toString();
        const Approval preset = mode == QLatin1String("always") ? Approval::Always
                                : mode == QLatin1String("changing") ? Approval::ChangingScripts
                                                                    : Approval::Never;
        for (int i = 0; i < ScriptKindCount; ++i)
            setApprovalRequiredByUser(static_cast<ScriptKind>(i), presetRequires(preset, static_cast<ScriptKind>(i)));
    }
    const QJsonObject methods = settings.value("methods").toObject();
    for (auto it = methods.begin(); it != methods.end(); ++it)
        setMethodEnabledByUser(it.key(), it.value().toBool());
}

// ------------------------------------------------------------ requested runs

void ScriptEngine::requestRun(const Request& request, RunCallback done, std::function<bool()> abandoned)
{
    auto finish = [done](const QJsonObject& result) {
        if (done)
            done(result);
    };
    if (!m_enabled) {
        finish(scriptError(QStringLiteral("unavailable"), QStringLiteral("running scripts is switched off")));
        return;
    }

    QString code = request.code;
    QString name;
    bool saved = false;
    if (code.trimmed().isEmpty() && !request.name.trimmed().isEmpty()) {
        const ScriptLibrary::Script script = m_library ? m_library->script(request.name.trimmed())
                                                       : ScriptLibrary::Script();
        if (!script.isValid()) {
            finish(scriptError(QStringLiteral("check"),
                               QStringLiteral("there is no saved script '%1'").arg(request.name.trimmed())));
            return;
        }
        code = script.code;
        name = script.name;
        // What the requester saved itself is no more trusted than what it
        // hands over with the request.
        saved = script.origin == QLatin1String("user") || script.origin == QLatin1String("application");
    }
    if (code.trimmed().isEmpty()) {
        finish(scriptError(QStringLiteral("check"), QStringLiteral("there is no script to run")));
        return;
    }
    const QString unavailable = unavailableReason(false);
    if (!unavailable.isEmpty()) {
        finish(scriptError(QStringLiteral("unavailable"), unavailable));
        return;
    }

    const Check checked = check(code);
    if (!checked.ok) {
        finish(checked.toJson());
        return;
    }

    const bool dryRun = request.dryRun;
    const QJsonObject args = request.args;
    const bool changes = checked.dynamic || !checked.changingMethods.isEmpty();
    const QByteArray hash = QCryptographicHash::hash(code.toUtf8(), QCryptographicHash::Sha1);
    const ScriptKind kind = dryRun ? ScriptKind::DryRun
                            : saved ? (changes ? ScriptKind::SavedChanging : ScriptKind::SavedReading)
                                    : (changes ? ScriptKind::NewChanging : ScriptKind::NewReading);
    const bool needsApproval = !m_approvedThisSession.contains(hash) && isApprovalRequired(kind);
    // Unasked as a reading script means: judged harmless from its source.
    // Held to that while it runs, where a changing one would have been asked for.
    const bool heldToReading = !dryRun && !changes
        && isApprovalRequired(saved ? ScriptKind::SavedChanging : ScriptKind::NewChanging);

    const QPointer<ScriptEngine> self(this);
    auto start = [self, code, name, args, dryRun, heldToReading, finish](bool approved) {
        if (!self)
            return;
        RunOptions options;
        options.name = name;
        options.args = args;
        options.dryRun = dryRun;
        options.external = true;
        options.readOnlyOnly = heldToReading && !approved;
        self->run(code, options, finish);
    };

    if (!needsApproval) {
        start(m_approvedThisSession.contains(hash));
        return;
    }

    ApprovalRequest approval;
    approval.kind = kind;
    approval.name = name;
    approval.code = code;
    approval.args = args;
    approval.dryRun = dryRun;
    approval.methods = checked.methods;
    approval.changingMethods = checked.changingMethods;
    approval.dynamic = checked.dynamic;
    if (!m_approvalHandler) {
        finish(scriptError(QStringLiteral("approval"),
                           QStringLiteral("the script needs the user's approval, and the application "
                                          "set no approval handler to ask for it")));
        return;
    }
    m_approvalHandler(approval, [self, hash, start, finish, abandoned](const ApprovalDecision& decision) {
        if (!self || (abandoned && abandoned()))
            return;     // nobody is waiting for this run any more
        if (!decision.approved) {
            QJsonObject declined = scriptError(QStringLiteral("declined"),
                                               QStringLiteral("the user declined the script"));
            if (!decision.reason.isEmpty())
                declined["reason"] = decision.reason;
            finish(declined);
            return;
        }
        if (decision.rememberForSession)
            self->m_approvedThisSession.insert(hash);
        start(true);
    });
}

} // namespace ScriptHost
