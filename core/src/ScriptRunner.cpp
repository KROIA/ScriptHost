#include "ScriptRunner.h"
#include "ScriptSource.h"

#include <QDeadlineTimer>
#include <QElapsedTimer>
#include <QJSEngine>
#include <QJSValue>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

namespace ScriptHost
{

namespace
{

// Under this name the script is known to the engine; it shows up in stack
// traces as "file:script:<line>".
const char* const kScriptFile = "script";

// make(name, trace): the function a script calls for a method. Arguments and
// result cross to the application as JSON text, which works the same in every
// Qt version and needs no shared objects between the threads.
// run(fn, argsJson): calls the script and reports what happened as JSON text -
// a `throw "text"` is otherwise indistinguishable from a returned string.
const char* const kPrelude = R"JS((function(native){
    return {
        make: function(name, trace){
            return function(){
                var r = JSON.parse(native.call(name,
                    JSON.stringify(Array.prototype.slice.call(arguments)),
                    trace ? new Error().stack : ''));
                if (r.e !== undefined) throw new Error(r.e);
                return r.v;
            };
        },
        step: function(line, read){ native.step(line, read); },
        // timer(repeat): setTimeout / setInterval. The runner calls the
        // function it gets with the further arguments bound.
        timer: function(repeat){
            return function(fn, ms){
                if (typeof fn !== 'function') throw new TypeError('the first argument must be a function');
                var a = Array.prototype.slice.call(arguments, 2);
                return native.timer(function(){ return fn.apply(undefined, a); }, Number(ms) || 0, repeat);
            };
        },
        clear: function(id){ native.clearTimer(Number(id) || 0); },
        // The variables a statement's reader gives, as {"name": "value as text"}.
        vars: function(read){
            var text = function(v){
                if (v === undefined) return 'undefined';
                if (typeof v === 'function') return 'function';
                if (typeof v === 'number') return String(v);
                var s;
                try { s = JSON.stringify(v); } catch (e) { s = undefined; }
                if (s === undefined) s = String(v);
                return s.length > 4000 ? s.slice(0, 4000) + '...' : s;
            };
            var values = read(), out = {};
            for (var name in values) out[name] = text(values[name]);
            return JSON.stringify(out);
        },
        run: function(fn, argsJson){
            try {
                var v = fn(JSON.parse(argsJson));
                return JSON.stringify({v: v === undefined ? null : v, u: v === undefined});
            } catch (e) {
                var o = e !== null && typeof e === 'object';
                return JSON.stringify({
                    e: o && e.message !== undefined ? String(e.message) : String(e),
                    n: o && e.name ? String(e.name) : '',
                    s: o && e.stack ? String(e.stack) : ''});
            }
        }
    };
}))JS";

QString compactJson(const QJsonValue& value)
{
    const QByteArray json = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(json.mid(1, json.size() - 2));
}

QString errorPayload(const QString& message)
{
    return QString::fromUtf8(QJsonDocument(QJsonObject{{"e", message}}).toJson(QJsonDocument::Compact));
}

const QString kNullPayload = QStringLiteral("{\"v\":null}");

// The lines of the script on a stack trace, innermost first.
QList<int> scriptLines(const QString& stack)
{
    static const QRegularExpression frame(QStringLiteral("file:script:(\\d+)"));
    QList<int> lines;
    auto it = frame.globalMatch(stack);
    while (it.hasNext())
        lines << it.next().captured(1).toInt();
    return lines;
}

// log("a", 1, {x: 2}) -> "a 1 {"x":2}"
QString logText(const QString& argsJson)
{
    QStringList parts;
    for (const QJsonValue& v : QJsonDocument::fromJson(argsJson.toUtf8()).array())
        parts << (v.isString() ? v.toString() : compactJson(v));
    return parts.join(QLatin1Char(' '));
}

} // namespace

ScriptRunner::ScriptRunner(const ScriptRunConfig& config, const QSharedPointer<ScriptRunShared>& shared,
                           QObject* parent)
    : QThread(parent)
    , m_config(config)
    , m_shared(shared)
{
    for (const ScriptMethodInfo& method : config.methods)
        m_methods.insert(method.name, method);
}

ScriptRunner::~ScriptRunner()
{
    requestStop(QStringLiteral("cancelled"));
    wait();
}

void ScriptRunner::requestStop(const QString& reason)
{
    {
        QMutexLocker lock(&m_shared->mutex);
        if (!m_shared->stop) {
            m_shared->stop = true;
            m_shared->stopReason = reason;
        }
        m_shared->wake.wakeAll();
    }
    QMutexLocker lock(&m_engineMutex);
    if (m_engine)
        m_engine->setInterrupted(true);
}

bool ScriptRunner::stopRequested(QString* reason) const
{
    QMutexLocker lock(&m_shared->mutex);
    if (reason)
        *reason = m_shared->stopReason;
    return m_shared->stop;
}

void ScriptRunner::run()
{
    m_clock.start();
    QJsonObject result;
    {
        QJSEngine engine;
        {
            QMutexLocker lock(&m_engineMutex);
            m_engine = &engine;
        }
        // A stop that came before the engine existed had nothing to interrupt.
        if (stopRequested())
            engine.setInterrupted(true);
        result = execute(engine);
        m_stepScope = QJSValue();       // these belong to the engine that ends here
        m_formatVariables = QJSValue();
        m_timers.clear();
        QMutexLocker lock(&m_engineMutex);
        m_engine = nullptr;
    }

    if (!m_config.compileOnly) {
        if (!m_calls.isEmpty()) {
            QJsonObject calls;
            for (auto it = m_calls.constBegin(); it != m_calls.constEnd(); ++it)
                calls.insert(it.key(), it.value());
            result["calls"] = calls;
        }
        if (m_config.dryRun) {
            result["dry_run"] = true;
            QJsonObject would;
            for (auto it = m_wouldCall.constBegin(); it != m_wouldCall.constEnd(); ++it) {
                QJsonArray first;
                for (const QString& args : it.value())
                    first.append(QJsonDocument::fromJson(args.toUtf8()).array());
                would.insert(it.key(), QJsonObject{{"count", m_calls.value(it.key())},
                                                   {"first_arguments", first}});
            }
            result["would_call"] = would;
        }
        if (!m_log.isEmpty()) {
            // The result carries the end of the log; all of it was signalled.
            QJsonArray tail;
            for (int i = qMax(0, m_log.size() - 20); i < m_log.size(); ++i)
                tail.append(m_log[i].left(300));
            result["log"] = tail;
            if (m_logLines > tail.size())
                result["log_lines"] = m_logLines;
        }
        result["duration_ms"] = m_clock.elapsed();
    }
    m_result = result;
    emit completed(result);
}

QJsonObject ScriptRunner::stoppedResult(const QString& reason) const
{
    if (reason == QLatin1String("limit"))
        return {{"status", "error"}, {"phase", "limit"}, {"message", m_limitMessage}};
    if (reason == QLatin1String("timeout"))
        return {{"status", "error"}, {"phase", "limit"},
                {"message", "the script ran into its time limit and was stopped"}};
    if (reason == QLatin1String("approval"))
        return {{"status", "error"}, {"phase", "approval"}, {"message", m_limitMessage}};
    return {{"status", "error"}, {"phase", "cancelled"}, {"message", "the script was stopped"}};
}

QJsonObject ScriptRunner::execute(QJSEngine& engine)
{
    // The script is the body of a function. Wrapper and first line share a
    // line, so line numbers are the script's own.
    const QString head = QStringLiteral("(function(args){\"use strict\";");
    const QString tail = QStringLiteral("\n})");
    // Compiled before any method exists: code that breaks out of the wrapper
    // runs here, where there is nothing for it to call.
    QJSValue function = engine.evaluate(head + m_config.code + tail, QLatin1String(kScriptFile));
    QString reason;
    if (stopRequested(&reason))
        return stoppedResult(reason);
    if (function.isError()) {
        return {{"status", "error"}, {"phase", "syntax"},
                {"line", function.property(QStringLiteral("lineNumber")).toInt()},
                {"message", function.property(QStringLiteral("message")).toString()}};
    }
    if (!function.isCallable())
        return {{"status", "error"}, {"phase", "syntax"}, {"line", 1},
                {"message", "the script is not a function body (unbalanced braces?)"}};
    if (m_config.compileOnly)
        return {{"status", "ok"}};

    if (m_config.debug && m_config.statements) {
        // ponytail: a script the rewriting breaks is debugged call by call,
        // without a word about it. A parser instead of the text scan would
        // end that.
        const QJSValue stepping = engine.evaluate(
            head + ScriptSource::instrumentForStepping(m_config.code) + tail, QLatin1String(kScriptFile));
        if (!stepping.isError() && stepping.isCallable()) {
            function = stepping;
            m_instrumented = true;
        }
    }

    // The engine owns the bridge (an object without a parent handed to
    // newQObject) and deletes it with itself.
    const QJSValue native = engine.newQObject(new ScriptBridge(this));
    const QJSValue prelude = engine.evaluate(QLatin1String(kPrelude), QStringLiteral("prelude"))
                                 .call({native});
    QJSValue make = prelude.property(QStringLiteral("make"));
    QJSValue timer = prelude.property(QStringLiteral("timer"));
    const QJSValue clear = prelude.property(QStringLiteral("clear"));
    QJSValue runFunction = prelude.property(QStringLiteral("run"));
    QJSValue global = engine.globalObject();
    if (m_instrumented) {
        global.setProperty(QStringLiteral("__sh_step"), prelude.property(QStringLiteral("step")));
        m_formatVariables = prelude.property(QStringLiteral("vars"));
    }
    for (const ScriptMethodInfo& method : m_config.methods) {
        // The timers take a function, so they are JavaScript of their own.
        // A method the application registered under such a name is its own.
        const bool repeat = method.local && method.name == QLatin1String("setInterval");
        QJSValue callable;
        if (repeat || (method.local && method.name == QLatin1String("setTimeout")))
            callable = timer.call({repeat});
        else if (method.local && (method.name == QLatin1String("clearTimeout")
                                  || method.name == QLatin1String("clearInterval")))
            callable = clear;
        else
            callable = make.call({method.name, m_config.debug});
        const int dot = method.name.indexOf(QLatin1Char('.'));
        if (dot < 0) {
            global.setProperty(method.name, callable);
            continue;
        }
        const QString nameSpace = method.name.left(dot);
        QJSValue object = global.property(nameSpace);
        if (!object.isObject()) {
            object = engine.newObject();
            global.setProperty(nameSpace, object);
        }
        object.setProperty(method.name.mid(dot + 1), callable);
    }

    QJsonObject payload;
    const QJsonObject failed = failure(runFunction.call({function, m_config.argsJson}), payload);
    if (!failed.isEmpty())
        return failed;
    QJsonObject result{{"status", "ok"}, {"result", payload.value(QStringLiteral("v"))}};
    if (payload.value(QStringLiteral("u")).toBool() && !m_config.dryRun)
        result["note"] = "the script returned nothing; use `return` to hand back a value";

    // The timers keep the run alive. Waited for like sleep(), so that a stop
    // wakes the wait at once.
    // ponytail: the next timer is found by a scan of all of them; a heap
    // when scripts keep thousands of timers.
    while (!m_timers.isEmpty()) {
        auto next = m_timers.begin();
        for (auto it = m_timers.begin(); it != m_timers.end(); ++it) {
            if (it.value().due < next.value().due)
                next = it;
        }
        {
            QDeadlineTimer deadline(qMax(qint64(0), next.value().due - m_clock.elapsed()));
            QMutexLocker lock(&m_shared->mutex);
            while (!m_shared->stop && !deadline.hasExpired())
                m_shared->wake.wait(&m_shared->mutex, deadline);
        }
        if (stopRequested(&reason))
            return stoppedResult(reason);
        // Due again before it runs, so that the function may clear its own timer.
        const QJSValue fn = next.value().fn;
        if (next.value().interval > 0)
            next.value().due = m_clock.elapsed() + next.value().interval;
        else
            m_timers.erase(next);
        const QJsonObject failedTimer = failure(runFunction.call({fn, QStringLiteral("null")}), payload);
        if (!failedTimer.isEmpty())
            return failedTimer;
    }
    return result;
}

QJsonObject ScriptRunner::failure(const QJSValue& outcome, QJsonObject& payload) const
{
    QString reason;
    if (stopRequested(&reason) && (outcome.isError() || !outcome.isString()))
        return stoppedResult(reason);
    if (outcome.isError() || !outcome.isString()) {
        return {{"status", "error"}, {"phase", "runtime"},
                {"message", outcome.toString()}};
    }

    payload = QJsonDocument::fromJson(outcome.toString().toUtf8()).object();
    if (!payload.contains(QStringLiteral("e")))
        return QJsonObject();
    // A stop makes the pending call fail; that failure is not the news.
    if (stopRequested(&reason))
        return stoppedResult(reason);
    QString message = payload.value(QStringLiteral("e")).toString();
    const QString name = payload.value(QStringLiteral("n")).toString();
    if (!name.isEmpty() && name != QLatin1String("Error"))
        message = name + QStringLiteral(": ") + message;
    QJsonObject result{{"status", "error"}, {"phase", "runtime"}, {"message", message}};
    const QList<int> lines = scriptLines(payload.value(QStringLiteral("s")).toString());
    if (!lines.isEmpty())
        result["line"] = lines.first();
    if (name == QLatin1String("ReferenceError"))
        result["hint"] = "only the methods of the application's script interface exist";
    return result;
}

double ScriptRunner::addTimer(const QJSValue& fn, double ms, bool repeat)
{
    // An interval of at least a millisecond, so that it cannot spin.
    const qint64 wait = qint64(qBound(repeat ? 1.0 : 0.0, ms, 2147483647.0));
    Timer timer;
    timer.fn = fn;
    timer.due = m_clock.elapsed() + wait;
    timer.interval = repeat ? wait : 0;
    m_timers.insert(++m_lastTimerId, timer);
    return double(m_lastTimerId);
}

void ScriptRunner::clearTimer(double id)
{
    // Whatever number the script passes; only an id it was given counts.
    if (id >= 1 && id <= double(m_lastTimerId))
        m_timers.remove(qint64(id));
}

bool ScriptRunner::waitWhilePaused(int line, const QString& name, const QString& argsJson)
{
    {
        QMutexLocker lock(&m_shared->mutex);
        m_shared->resume = 0;
    }
    emit paused(line, name, argsJson, variablesJson(line));
    QMutexLocker lock(&m_shared->mutex);
    while (m_shared->resume == 0 && !m_shared->stop)
        m_shared->wake.wait(&m_shared->mutex);
    if (m_shared->stop)
        return false;
    m_shared->stepMode = m_shared->resume == 1 ? ScriptRunShared::ToCall
                         : m_shared->resume == 3 ? ScriptRunShared::ToStatement
                                                 : ScriptRunShared::RunOn;
    return true;
}

QString ScriptRunner::variablesJson(int line)
{
    // ponytail: only for a stop on the line of the statement last begun. A
    // call further down a statement that spans lines, or one made after a
    // function of the script returned, would be shown another scope.
    if (!m_instrumented || line != m_stepLine || !m_stepScope.isCallable())
        return QStringLiteral("{}");
    const QJSValue text = m_formatVariables.call({m_stepScope});
    return text.isString() ? text.toString() : QStringLiteral("{}");
}

void ScriptRunner::handleStep(int line, const QJSValue& read)
{
    m_stepLine = line;
    m_stepScope = read;
    m_stepPaused = false;
    {
        QMutexLocker lock(&m_shared->mutex);
        if (m_shared->stop)
            return;     // the engine is interrupted; the script ends by itself
        if (m_shared->stepMode != ScriptRunShared::ToStatement && !m_shared->breakpoints.contains(line))
            return;
    }
    m_stepPaused = true;
    waitWhilePaused(line, QString(), QStringLiteral("[]"));
}

QString ScriptRunner::handleCall(const QString& name, const QString& argsJson, const QString& stack)
{
    const QString stopped = errorPayload(QStringLiteral("the script was stopped"));
    if (stopRequested())
        return stopped;

    const ScriptMethodInfo method = m_methods.value(name);
    const QList<int> lines = scriptLines(stack);
    const int line = lines.isEmpty() ? 0 : lines.first();

    // ---- handled here, without asking the application
    if (method.local) {
        if (name == QLatin1String("log") || name == QLatin1String("print")
            || name == QLatin1String("console.log")) {
            const QString text = logText(argsJson);
            ++m_logLines;
            m_log << text;
            if (m_log.size() > m_config.maxLogLines)
                m_log.removeFirst();
            emit logged(text);
        } else if (name == QLatin1String("progress")) {
            const QJsonArray args = QJsonDocument::fromJson(argsJson.toUtf8()).array();
            emit progressed(args.at(0).toDouble(), args.at(1).toDouble(), args.at(2).toString());
        } else if (name == QLatin1String("sleep")) {
            const double ms = QJsonDocument::fromJson(argsJson.toUtf8()).array().at(0).toDouble();
            QDeadlineTimer deadline(qint64(qBound(0.0, ms, 3600000.0)));
            QMutexLocker lock(&m_shared->mutex);
            while (!m_shared->stop && !deadline.hasExpired())
                m_shared->wake.wait(&m_shared->mutex, deadline);
            if (m_shared->stop)
                return stopped;
        } else if (name == QLatin1String("pause")) {
            if (m_config.debug && !waitWhilePaused(line, name, argsJson))
                return stopped;
        }
        return kNullPayload;
    }

    // ---- debugger: stop before the call
    if (m_config.debug) {
        // A breakpoint that stopped the script before the statement does not
        // stop it again before that statement's first call.
        const bool stoppedAtStatement = m_stepPaused && m_stepLine == line;
        m_stepPaused = false;
        bool stopHere = false;
        {
            QMutexLocker lock(&m_shared->mutex);
            stopHere = m_shared->stepMode == ScriptRunShared::ToCall
                       || (m_shared->stepMode == ScriptRunShared::ToStatement && !m_instrumented);
            for (int i = 0; i < lines.size() && !stopHere && !stoppedAtStatement; ++i)
                stopHere = m_shared->breakpoints.contains(lines[i]);
        }
        if (stopHere && !waitWhilePaused(line, name, argsJson))
            return stopped;
    }

    // ---- limits
    auto exceed = [this](const QString& reason, const QString& message) {
        m_limitMessage = message;
        requestStop(reason);
        return errorPayload(message);
    };
    if (m_config.readOnlyOnly && !method.readOnly) {
        return exceed(QStringLiteral("approval"),
                      QStringLiteral("'%1' changes something, and the script ran without the approval that needs").arg(name));
    }
    if (m_config.maxCalls > 0 && ++m_callCount > m_config.maxCalls) {
        return exceed(QStringLiteral("limit"),
                      QStringLiteral("stopped after %1 method calls (the limit)").arg(m_config.maxCalls));
    }
    if (method.maxCalls > 0 && m_calls.value(name) >= method.maxCalls) {
        return exceed(QStringLiteral("limit"),
                      QStringLiteral("stopped after %1 calls of %2 (its limit)").arg(method.maxCalls).arg(name));
    }
    ++m_calls[name];

    // ---- dry run: what changes something is written down, not done
    QString payload;
    if (m_config.dryRun && !method.readOnly) {
        QStringList& first = m_wouldCall[name];
        if (first.size() < 3)
            first << argsJson;
        payload = method.exampleJson.isEmpty()
            ? QStringLiteral("{\"v\":{\"dry_run\":true}}")
            : QStringLiteral("{\"v\":") + method.exampleJson + QLatin1Char('}');
    } else {
        // ---- the application does it, on its own thread; wait for the answer
        const quint64 id = ++m_nextId;
        {
            QMutexLocker lock(&m_shared->mutex);
            m_shared->pendingId = id;
            m_shared->resultReady = false;
        }
        emit callRequested(id, name, argsJson);
        QMutexLocker lock(&m_shared->mutex);
        while (!m_shared->resultReady && !m_shared->stop)
            m_shared->wake.wait(&m_shared->mutex);
        if (!m_shared->resultReady)
            return stopped;
        payload = m_shared->result;
        m_shared->pendingId = 0;
    }

    if (m_config.debug)
        emit callReturned(line, name, argsJson, payload);
    return payload;
}

} // namespace ScriptHost
