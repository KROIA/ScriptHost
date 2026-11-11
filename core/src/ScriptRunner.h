#pragma once
// One run of a script: a worker thread with its own JavaScript engine.
// Private to the library.

#include "ScriptExtension.h"
#include <QElapsedTimer>
#include <QHash>
#include <QJSValue>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QMutex>
#include <QPair>
#include <QSet>
#include <QSharedPointer>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QWaitCondition>
#include <functional>

class QJSEngine;

namespace ScriptHost
{

// What the runner needs to know about a method. The method itself lives on
// the GUI thread.
struct ScriptMethodInfo
{
    QString name;
    bool    readOnly = false;
    bool    local = false;      // log, progress, sleep, pause, the timers: handled by the runner
    int     maxCalls = 0;       // of this method per run; 0 = no limit of its own
    QString exampleJson;        // what a dry run answers; empty = {"dry_run":true}
};

// Is the event `name` among those allowed for requested runs? A name allowed
// covers its family too: "ui" the events "ui:...".
inline bool isEventAllowed(const QSet<QString>& allowed, const QString& name)
{
    return allowed.contains(name) || allowed.contains(name.section(QLatin1Char(':'), 0, 0));
}

struct ScriptRunConfig
{
    QString                 code;
    QString                 argsJson = QStringLiteral("{}");
    QList<ScriptMethodInfo> methods;
    int  maxCalls = 5000;       // 0 = no limit
    int  maxLogLines = 200;
    bool compileOnly = false;   // syntax check: nothing is installed, nothing is called
    bool dryRun = false;        // methods that change something are recorded, not called
    bool debug = false;         // stops at calls on request, reports every call
    bool statements = false;    // debug: stops before statements too, reads variables
    bool readOnlyOnly = false;  // a method that changes something fails the script
    bool requested = false;     // started through requestRun(): on() only for requestedEvents
    QSet<QString> requestedEvents;
    quint64 runId = 0;          // the engine's RunId, for the extensions
    QList<QSharedPointer<ScriptExtension>> extensions;  // created at the start, in this order
    QHash<QString, QStringList> hiddenMembers;          // by extension: the members switched off
};

// State both threads touch, under `mutex`.
struct ScriptRunShared
{
    QMutex         mutex;
    QWaitCondition wake;
    bool           stop = false;
    QString        stopReason;      // "cancelled", "timeout", "limit", "approval"
    // The call the script is waiting for.
    quint64        pendingId = 0;
    bool           resultReady = false;
    QString        result;          // {"v":...} or {"e":"message"}
    // Debugger.
    enum { RunOn = 0, ToCall = 1, ToStatement = 2 };
    int            stepMode = RunOn;    // where to stop next, breakpoints aside
    QSet<int>      breakpoints;         // lines
    // While paused: 0 wait, else go on - 1 to the next call, 2 to the next
    // breakpoint, 3 to the next statement.
    int            resume = 0;
    // Events: the names the script has handlers for, and those posted to it
    // and not yet delivered, as (name, data as JSON text).
    QSet<QString>                 listening;
    QList<QPair<QString, QString>> events;
    int                           maxEvents = 0;  // queued at most, the oldest dropped; 0 = no limit
    qint64                        dropped = 0;    // events dropped so far
    // Extensions (ScriptRunContext): callbacks for the worker thread by key
    // ("" = not coalesced), the keys that keep the run alive, and whether
    // the script code is over - nothing is scheduled then.
    QList<QPair<QString, std::function<void()>>> scheduled;
    QSet<QString>                 keepAlive;
    bool                          ended = false;
};

class ScriptRunner : public QThread
{
    Q_OBJECT
public:
    ScriptRunner(const ScriptRunConfig& config, const QSharedPointer<ScriptRunShared>& shared,
                 QObject* parent = nullptr);
    ~ScriptRunner() override;

    // Ends the run as soon as possible. Any thread. The first reason counts.
    void requestStop(const QString& reason);

    // The result, once the thread has finished.
    QJsonObject result() const { return m_result; }

    // Called by the script (worker thread) for every method call.
    QString handleCall(const QString& name, const QString& argsJson, const QString& stack);
    // Called by the script (worker thread) before a statement, when it runs
    // with statement stepping. `read` gives the variables visible there.
    void handleStep(int line, const QJSValue& read);
    // setTimeout / setInterval (repeat) and their clear functions (worker
    // thread). `fn` takes no arguments; the prelude binds them.
    double addTimer(const QJSValue& fn, double ms, bool repeat);
    void clearTimer(double id);
    // on() / off(): the script has handlers for the event `name` (on) or no
    // more (worker thread). False when the run may not receive it.
    bool listen(const QString& name, bool on);

    // For ScriptRunContext; worker thread only.
    void writeLog(const QString& text);
    void throwError(const QString& message);
    // Calls a script function; an exception it throws becomes the run's
    // failure, returned once the current step of the run is done.
    bool callScript(const QJSValue& fn, const QJSValueList& args);

signals:
    // The script calls a method of the application; answer through the shared state.
    void callRequested(quint64 id, const QString& name, const QString& argsJson);
    // Debugger: stopped before this call, or before a statement (no name);
    // continue through the shared state. The variables as {"name": "text"}.
    void paused(int line, const QString& name, const QString& argsJson, const QString& variablesJson);
    // Debugger: a call came back with this payload ({"v":...} / {"e":...}).
    void callReturned(int line, const QString& name, const QString& argsJson, const QString& payloadJson);
    void logged(const QString& text);
    void progressed(double done, double total, const QString& text);
    void completed(const QJsonObject& result);

protected:
    void run() override;

private:
    QJsonObject execute(QJSEngine& engine);
    // The error result of a call through the prelude's run() or apply();
    // empty when the function returned, `payload` then holding what it returned.
    QJsonObject failure(const QJSValue& outcome, QJsonObject& payload) const;
    bool stopRequested(QString* reason = nullptr) const;
    bool waitWhilePaused(int line, const QString& name, const QString& argsJson);
    QString variablesJson(int line);
    QJsonObject stoppedResult(const QString& reason) const;
    // Creates the extensions' per-run objects and installs them as globals.
    // An error result when one of them cannot be installed, else empty.
    QJsonObject installExtensions(QJSEngine& engine);
    // The one log line about dropped events, once there are any.
    void noteDroppedEvents();

    ScriptRunConfig                  m_config;
    QSharedPointer<ScriptRunShared>  m_shared;
    QHash<QString, ScriptMethodInfo> m_methods;
    QMutex                           m_engineMutex;
    QJSEngine*                       m_engine = nullptr;   // under m_engineMutex
    QJsonObject                      m_result;

    // Worker thread only.
    QElapsedTimer               m_clock;        // since the run began
    QHash<QString, int>         m_calls;        // method -> number of calls
    QHash<QString, QStringList> m_wouldCall;    // dry run: first argument lists per method
    QStringList                 m_log;          // the latest maxLogLines lines
    qint64                      m_logLines = 0;
    int                         m_callCount = 0;
    bool                        m_droppedNoted = false;
    quint64                     m_nextId = 0;
    QString                     m_limitMessage;
    // Extensions.
    QList<QObject*>             m_extensionObjects; // the per-run objects, owned
    QJSValue                    m_apply;            // prelude: calls a function for callScript()
    QJSValue                    m_done;             // prelude: marks a return of run() and apply()
    QJsonObject                 m_failure;          // what callScript() met; ends the run
    // Statement stepping.
    bool                        m_instrumented = false;
    QJSValue                    m_formatVariables;  // prelude: a reader's values as JSON text
    QJSValue                    m_stepScope;        // the reader of the statement last begun
    int                         m_stepLine = 0;
    bool                        m_stepPaused = false;   // stopped before that statement
    // Timers, by id. Ids grow, so of timers due at once the first set fires first.
    struct Timer
    {
        QJSValue fn;
        qint64   due = 0;           // on m_clock
        qint64   interval = 0;      // 0 = once
    };
    QMap<qint64, Timer>         m_timers;
    qint64                      m_lastTimerId = 0;
};

// What a ScriptRunContext stands for: one extension in one run.
struct ScriptRunContext::Data
{
    quint64                         runId = 0;
    bool                            requested = false;
    bool                            dryRun = false;
    QString                         keyPrefix;  // "<extension name>\n": its keys apart from others'
    QSharedPointer<ScriptRunShared> shared;
    ScriptRunner*                   runner = nullptr;   // used on its own thread only, where it exists
};

// The object the script's method wrappers talk to.
class ScriptBridge : public QObject
{
    Q_OBJECT
public:
    explicit ScriptBridge(ScriptRunner* runner) : m_runner(runner) {}
    Q_INVOKABLE QString call(const QString& name, const QString& argsJson, const QString& stack)
    {
        return m_runner->handleCall(name, argsJson, stack);
    }
    Q_INVOKABLE void step(int line, const QJSValue& read) { m_runner->handleStep(line, read); }
    Q_INVOKABLE double timer(const QJSValue& fn, double ms, bool repeat) { return m_runner->addTimer(fn, ms, repeat); }
    Q_INVOKABLE void clearTimer(double id) { m_runner->clearTimer(id); }
    Q_INVOKABLE bool listen(const QString& name, bool on) { return m_runner->listen(name, on); }

private:
    ScriptRunner* m_runner;
};

} // namespace ScriptHost
