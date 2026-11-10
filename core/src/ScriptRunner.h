#pragma once
// One run of a script: a worker thread with its own JavaScript engine.
// Private to the library.

#include <QHash>
#include <QJSValue>
#include <QJsonObject>
#include <QList>
#include <QMutex>
#include <QSet>
#include <QSharedPointer>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QWaitCondition>

class QJSEngine;

namespace ScriptHost
{

// What the runner needs to know about a method. The method itself lives on
// the GUI thread.
struct ScriptMethodInfo
{
    QString name;
    bool    readOnly = false;
    bool    local = false;      // log, progress, sleep, pause: handled by the runner
    int     maxCalls = 0;       // of this method per run; 0 = no limit of its own
    QString exampleJson;        // what a dry run answers; empty = {"dry_run":true}
};

struct ScriptRunConfig
{
    QString                 code;
    QString                 argsJson = QStringLiteral("{}");
    QList<ScriptMethodInfo> methods;
    int  maxCalls = 5000;
    int  maxLogLines = 200;
    bool compileOnly = false;   // syntax check: nothing is installed, nothing is called
    bool dryRun = false;        // methods that change something are recorded, not called
    bool debug = false;         // stops at calls on request, reports every call
    bool statements = false;    // debug: stops before statements too, reads variables
    bool readOnlyOnly = false;  // a method that changes something fails the script
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
    bool stopRequested(QString* reason = nullptr) const;
    bool waitWhilePaused(int line, const QString& name, const QString& argsJson);
    QString variablesJson(int line);
    QJsonObject stoppedResult(const QString& reason) const;

    ScriptRunConfig                  m_config;
    QSharedPointer<ScriptRunShared>  m_shared;
    QHash<QString, ScriptMethodInfo> m_methods;
    QMutex                           m_engineMutex;
    QJSEngine*                       m_engine = nullptr;   // under m_engineMutex
    QJsonObject                      m_result;

    // Worker thread only.
    QHash<QString, int>         m_calls;        // method -> number of calls
    QHash<QString, QStringList> m_wouldCall;    // dry run: first argument lists per method
    QStringList                 m_log;
    int                         m_logLines = 0;
    int                         m_callCount = 0;
    quint64                     m_nextId = 0;
    QString                     m_limitMessage;
    // Statement stepping.
    bool                        m_instrumented = false;
    QJSValue                    m_formatVariables;  // prelude: a reader's values as JSON text
    QJSValue                    m_stepScope;        // the reader of the statement last begun
    int                         m_stepLine = 0;
    bool                        m_stepPaused = false;   // stopped before that statement
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

private:
    ScriptRunner* m_runner;
};

} // namespace ScriptHost
