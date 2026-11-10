#include "ScriptEngine.h"
#include "ScriptHighlighter.h"
#include "ScriptLibrary.h"
#include "ScriptRunner.h"
#include "ScriptSchema.h"
#include "ScriptSource.h"
#include <QCheckBox>
#include <QCryptographicHash>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QTimer>
#include <QVBoxLayout>
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

ScriptEngine::ScriptEngine(QObject* parent)
    : QObject(parent)
    , m_requesterName(QStringLiteral("another program"))
{
    m_timeout = new QTimer(this);
    m_timeout->setSingleShot(true);
    connect(m_timeout, &QTimer::timeout, this, [this]() {
        if (m_runner)
            m_runner->requestStop(QStringLiteral("timeout"));
    });
    registerBuiltins();
}

ScriptEngine::~ScriptEngine()
{
    if (m_runner) {
        m_runner->requestStop(QStringLiteral("cancelled"));
        m_runner->wait();
        delete m_runner;
        m_runner = nullptr;
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

void ScriptEngine::setDialogParent(QWidget* parent)
{
    m_dialogParent = parent;
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
    return m_runner != nullptr;
}

bool ScriptEngine::isExternalRun() const
{
    return m_runner != nullptr && m_runRecord.external;
}

void ScriptEngine::stop()
{
    if (m_runner)
        m_runner->requestStop(QStringLiteral("cancelled"));
}

void ScriptEngine::stepStatement()
{
    if (!m_shared)
        return;
    QMutexLocker lock(&m_shared->mutex);
    m_shared->resume = 3;
    m_shared->wake.wakeAll();
}

void ScriptEngine::stepRun()
{
    if (!m_shared)
        return;
    QMutexLocker lock(&m_shared->mutex);
    m_shared->resume = 1;
    m_shared->wake.wakeAll();
}

void ScriptEngine::continueRun()
{
    if (!m_shared)
        return;
    QMutexLocker lock(&m_shared->mutex);
    m_shared->resume = 2;
    m_shared->wake.wakeAll();
}

void ScriptEngine::setBreakpoints(const QSet<int>& lines)
{
    if (!m_shared)
        return;
    QMutexLocker lock(&m_shared->mutex);
    m_shared->breakpoints = lines;
}

void ScriptEngine::clearRuns()
{
    m_runs.clear();
    emit runsChanged();
}

bool ScriptEngine::run(const QString& code, const RunOptions& options, RunCallback done)
{
    auto refuse = [&done](const QString& message) {
        if (done)
            done(scriptError(QStringLiteral("unavailable"), message));
        return false;
    };
    if (isRunning())
        return refuse(QStringLiteral("another script is running; wait for it to finish"));

    ScriptRunConfig config;
    config.code = code;
    config.argsJson = compactJson(options.args);
    config.maxCalls = m_limits.maxCalls;
    config.maxLogLines = m_limits.maxLogLines;
    config.dryRun = options.dryRun;
    config.debug = options.debug;
    config.statements = options.debug && options.statements;
    config.readOnlyOnly = options.readOnlyOnly;

    // The methods as they are now; what changes during the run does not reach it.
    m_runEntries.clear();
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
        m_runEntries.insert(info.name, entry);
    }

    m_shared = QSharedPointer<ScriptRunShared>::create();
    m_shared->stepMode = !(options.debug && options.stepFromStart) ? ScriptRunShared::RunOn
                         : options.statements ? ScriptRunShared::ToStatement
                                              : ScriptRunShared::ToCall;
    m_shared->breakpoints = options.breakpoints;
    m_runCallback = std::move(done);
    m_runRecord = RunRecord();
    m_runRecord.started = QDateTime::currentDateTime();
    m_runRecord.name = options.name;
    m_runRecord.code = code;
    m_runRecord.args = options.args;
    m_runRecord.dryRun = options.dryRun;
    m_runRecord.external = options.external;

    ScriptRunner* runner = new ScriptRunner(config, m_shared, this);
    m_runner = runner;
    // The runner object lives on this thread and signals from its own, so
    // these arrive queued. A late signal of an earlier run is ignored.
    connect(runner, &ScriptRunner::callRequested, this,
            [this, runner](quint64 id, const QString& name, const QString& argsJson) {
        if (runner == m_runner)
            onCallRequested(id, name, argsJson);
    });
    connect(runner, &ScriptRunner::paused, this,
            [this, runner](int line, const QString& name, const QString& argsJson, const QString& variablesJson) {
        if (runner == m_runner)
            emit runPaused(line, name, QJsonDocument::fromJson(argsJson.toUtf8()).array(),
                           QJsonDocument::fromJson(variablesJson.toUtf8()).object());
    });
    connect(runner, &ScriptRunner::callReturned, this,
            [this, runner](int line, const QString& name, const QString& argsJson, const QString& payloadJson) {
        if (runner != m_runner)
            return;
        const QJsonObject payload = QJsonDocument::fromJson(payloadJson.toUtf8()).object();
        emit runCallFinished(line, name, QJsonDocument::fromJson(argsJson.toUtf8()).array(),
                             payload.value("v"), payload.value("e").toString());
    });
    connect(runner, &ScriptRunner::logged, this, [this, runner](const QString& text) {
        if (runner == m_runner)
            emit runLog(text);
    });
    connect(runner, &ScriptRunner::progressed, this,
            [this, runner](double doneCount, double total, const QString& text) {
        if (runner != m_runner)
            return;
        emit runProgress(doneCount, total, text);
    });
    connect(runner, &ScriptRunner::completed, this, [this, runner](const QJsonObject& result) {
        if (runner == m_runner)
            onRunCompleted(result);
    });

    // A paused script is waiting for the user, not running away.
    if (!options.debug && m_limits.timeoutMs > 0)
        m_timeout->start(m_limits.timeoutMs);
    emit runStarted(options.name, options.external);
    runner->start();
    return true;
}

void ScriptEngine::onRunCompleted(const QJsonObject& result)
{
    ScriptRunner* runner = m_runner;
    m_runner = nullptr;
    m_timeout->stop();
    runner->wait();
    runner->deleteLater();
    m_shared.reset();
    m_runEntries.clear();

    m_runRecord.result = result;
    m_runs.append(m_runRecord);
    while (m_runs.size() > kMaxRuns)
        m_runs.removeFirst();

    // The callback may start the next script.
    const RunCallback callback = std::move(m_runCallback);
    m_runCallback = nullptr;
    emit runsChanged();
    emit runFinished(result);
    if (callback)
        callback(result);
}

void ScriptEngine::onCallRequested(quint64 id, const QString& name, const QString& argsJson)
{
    // Hands the answer to the waiting script. Holds the shared state, not
    // this object: an asynchronous method may answer from any thread, and
    // after this object is gone.
    const QSharedPointer<ScriptRunShared> shared = m_shared;
    auto deliver = [shared, id](const QString& payload) {
        QMutexLocker lock(&shared->mutex);
        if (shared->pendingId != id || shared->resultReady)
            return;     // the script is not waiting for this any more
        shared->result = payload;
        shared->resultReady = true;
        shared->wake.wakeAll();
    };

    const auto it = m_runEntries.constFind(name);
    if (it == m_runEntries.cend()) {
        deliver(errorPayload(QStringLiteral("no method '%1'").arg(name)));
        return;
    }
    const Entry entry = it.value();

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

// ------------------------------------------------------------ approval

void ScriptEngine::askApproval(const ApprovalRequest& request,
                            std::function<void(const ApprovalDecision&)> decide)
{
    if (m_approvalHandler) {
        m_approvalHandler(request, std::move(decide));
        return;
    }

    QDialog dialog(m_dialogParent);
    dialog.setObjectName(QStringLiteral("scripthost_scriptApprovalDialog"));
    dialog.setWindowTitle(QStringLiteral("Run script?"));
    dialog.resize(640, 520);
    auto* layout = new QVBoxLayout(&dialog);

    QString headline = request.name.isEmpty()
        ? QStringLiteral("Requested by %1: run this script?").arg(m_requesterName)
        : QStringLiteral("Requested by %1: run the saved script \"%2\"?").arg(m_requesterName, request.name);
    if (request.dryRun)
        headline += QStringLiteral(" It is a dry run: nothing will be changed.");
    auto* headlineLabel = new QLabel(headline, &dialog);
    headlineLabel->setObjectName(QStringLiteral("scripthost_scriptApprovalHeadline"));
    headlineLabel->setWordWrap(true);
    layout->addWidget(headlineLabel);

    auto* code = new QPlainTextEdit(&dialog);
    code->setObjectName(QStringLiteral("scripthost_scriptApprovalCode"));
    code->setReadOnly(true);
    code->setLineWrapMode(QPlainTextEdit::NoWrap);
    code->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    new JsHighlighter(code->document());
    code->setPlainText(request.code);
    layout->addWidget(code, 1);

    QStringList uses;
    for (const QString& method : request.methods) {
        uses << (request.changingMethods.contains(method) ? method + QStringLiteral(" (changes)")
                                                          : method + QStringLiteral(" (reads)"));
    }
    QString usesText = uses.isEmpty() ? QStringLiteral("It calls no methods of the application.")
                                      : QStringLiteral("It uses: ") + uses.join(QStringLiteral(", "));
    if (request.dynamic)
        usesText += QStringLiteral("\nIt picks methods by computed names, so it may call others as well.");
    if (!request.args.isEmpty())
        usesText += QStringLiteral("\nArguments: ")
                    + QString::fromUtf8(QJsonDocument(request.args).toJson(QJsonDocument::Compact));
    auto* usesLabel = new QLabel(usesText, &dialog);
    usesLabel->setObjectName(QStringLiteral("scripthost_scriptApprovalUses"));
    usesLabel->setWordWrap(true);
    layout->addWidget(usesLabel);

    auto* output = new QPlainTextEdit(&dialog);
    output->setObjectName(QStringLiteral("scripthost_scriptApprovalOutput"));
    output->setReadOnly(true);
    output->setMaximumHeight(110);
    output->hide();
    layout->addWidget(output);

    auto* remember = new QCheckBox(QStringLiteral("Do not ask again for this script in this session"), &dialog);
    remember->setObjectName(QStringLiteral("scripthost_scriptApprovalRemember"));
    layout->addWidget(remember);

    auto* reason = new QLineEdit(&dialog);
    reason->setObjectName(QStringLiteral("scripthost_scriptApprovalReason"));
    reason->setPlaceholderText(QStringLiteral("When declining: the reason to pass on (optional)"));
    layout->addWidget(reason);

    auto* buttons = new QDialogButtonBox(&dialog);
    QPushButton* runButton = buttons->addButton(QStringLiteral("Run"), QDialogButtonBox::AcceptRole);
    runButton->setObjectName(QStringLiteral("scripthost_scriptApprovalRun"));
    QPushButton* declineButton = buttons->addButton(QStringLiteral("Decline"), QDialogButtonBox::RejectRole);
    declineButton->setObjectName(QStringLiteral("scripthost_scriptApprovalDecline"));
    QPushButton* testButton = buttons->addButton(QStringLiteral("Test run"), QDialogButtonBox::ActionRole);
    testButton->setObjectName(QStringLiteral("scripthost_scriptApprovalTest"));
    testButton->setToolTip(QStringLiteral("Runs the script without changing anything and shows what it would do"));
    testButton->setVisible(!request.dryRun);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(testButton, &QPushButton::clicked, &dialog, [this, request, output, testButton, runButton]() {
        RunOptions options;
        options.name = request.name;
        options.args = request.args;
        options.dryRun = true;
        testButton->setEnabled(false);
        runButton->setEnabled(false);
        QPointer<QPlainTextEdit> out(output);
        QPointer<QPushButton> test(testButton), runAgain(runButton);
        run(request.code, options, [out, test, runAgain](const QJsonObject& result) {
            if (!out)
                return;
            out->setPlainText(QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Indented)));
            out->show();
            test->setEnabled(true);
            runAgain->setEnabled(true);
        });
    });

    ApprovalDecision decision;
    decision.approved = dialog.exec() == QDialog::Accepted;
    decision.reason = reason->text().trimmed();
    decision.rememberForSession = decision.approved && remember->isChecked();
    // A test run still going when the dialog closes must not block the real
    // one: it is stopped, and the decision takes effect once it has ended.
    if (isRunning()) {
        stop();
        auto* once = new QObject(this);
        connect(this, &ScriptEngine::runFinished, once, [once, decide, decision]() {
            once->deleteLater();
            decide(decision);
        });
        return;
    }
    decide(decision);
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
    if (isRunning()) {
        finish(scriptError(QStringLiteral("unavailable"),
                           QStringLiteral("another script is running; wait for it to finish")));
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
    askApproval(approval, [self, hash, start, finish, abandoned](const ApprovalDecision& decision) {
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
