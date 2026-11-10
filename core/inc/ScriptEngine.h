#pragma once
#include "ScriptHost_base.h"
#include "ScriptMethod.h"
#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QSharedPointer>
#include <QString>
#include <QStringList>
#include <functional>

class QTimer;
class QWidget;

namespace ScriptHost
{

class ScriptLibrary;
class ScriptRunner;
struct ScriptRunShared;

// Runs JavaScript inside an application, against an interface the
// application defines.
//
// A script can call exactly the methods registered here - nothing else. The
// JavaScript engine itself has no access to files, the network or processes.
//
//   auto* engine = new ScriptHost::ScriptEngine(this);
//   ScriptHost::ScriptMethod read("db.readObject");
//   read.setDescription("Reads one object.").addParameter("id", "string", "Object id")
//       .setReturns("object", "The object", exampleObject).setReadOnly(true);
//   engine->registerMethod(read, [this](const QJsonObject& args) -> QJsonValue {
//       return m_db.object(args["id"].toString());
//   });
//
//   engine->run("return db.readObject('a1').name;", {}, [](const QJsonObject& result) { ... });
//
// A script runs on a thread of its own; method handlers run on the thread of
// this object (the GUI thread), so they may touch widgets and application
// data, and the application stays responsive while a script works.
//
// Two ways to start a script:
//   run()         the application itself, or the user in the script editor.
//   requestRun()  somebody else asks: an assistant, a plugin, a remote
//                 caller. Such a run can be switched off (setEnabled) and can
//                 need the user's approval first (setApproval).
class SCRIPT_HOST_API ScriptEngine : public QObject
{
    Q_OBJECT
public:
    explicit ScriptEngine(QObject* parent = nullptr);
    ~ScriptEngine() override;

    // ---- Requested runs: the master switch --------------------------------
    // Off: requestRun() refuses. run() is not affected. Default on.
    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabled; }
    // May the user switch it (ScriptsWidget)? false = the application alone
    // decides. Default true.
    void setEnabledUserConfigurable(bool configurable);
    bool isEnabledUserConfigurable() const { return m_enabledUserConfigurable; }
    // What a settings window calls. Returns false, changing nothing, when locked.
    bool setEnabledByUser(bool enabled);
    // Who it is that requests runs, as the user is told: "the assistant",
    // "the plugin". Default "another program".
    void setRequesterName(const QString& name);
    QString requesterName() const { return m_requesterName; }

    // ---- The methods a script can call ------------------------------------
    // Registering a name again replaces the method.
    void registerMethod(const ScriptMethod& method, ScriptMethodHandler handler);
    void registerAsyncMethod(const ScriptMethod& method, AsyncScriptMethodHandler handler);
    void unregisterMethod(const QString& name);

    // Every method there is - registered and built in - enabled or not,
    // sorted by name.
    QList<ScriptMethod> methods() const;
    bool hasMethod(const QString& name) const;

    // Per method (or per ScriptMethod::group()), by name and durable: may be
    // set before the method exists and survives registering it again. A
    // method's own setting wins over its group's.
    //   enabled            scripts can call it. A disabled method is not in the
    //                      definition, and a script using it is rejected
    //                      before it runs.
    //   user configurable  the user may switch it in a settings window.
    void setMethodEnabled(const QString& name, bool enabled);
    void setMethodGroupEnabled(const QString& group, bool enabled);
    bool isMethodEnabled(const QString& name) const;
    void setMethodUserConfigurable(const QString& name, bool configurable);
    void setMethodGroupUserConfigurable(const QString& group, bool configurable);
    bool isMethodUserConfigurable(const QString& name) const;
    bool setMethodEnabledByUser(const QString& name, bool enabled);

    // ---- The definition ---------------------------------------------------
    // The enabled methods as TypeScript declarations: for whoever writes
    // scripts - a person (the editor's reference shows it) or a program.
    // With a query (a method name, a namespace, keywords): the matching ones.
    // Without, and when everything does not fit setApiBudgetChars(): an index
    // with one line per method.
    QString apiDefinition(const QString& query = QString()) const;
    void setApiBudgetChars(int chars);              // default 6000; 0 = no limit
    int  apiBudgetChars() const { return m_apiBudget; }

    // ---- Limits -----------------------------------------------------------
    struct Limits
    {
        int timeoutMs   = 300000;   // whole run; not applied in the debugger
        int maxCalls    = 5000;     // method calls per run
        int maxLogLines = 200;      // kept of the script log
    };
    void setLimits(const Limits& limits);
    Limits limits() const { return m_limits; }

    // ---- Approval of requested runs ----------------------------------------
    // Whether the user sees a requested script, and agrees, before it runs.
    //   Never            runs at once (default)
    //   ChangingScripts  asked for scripts that call a method that is not
    //                    read-only. A script that runs unasked cannot call one.
    //   Always           asked for every script
    enum class Approval { Never, ChangingScripts, Always };
    void setApproval(Approval approval);
    Approval approval() const { return m_approval; }
    void setApprovalUserConfigurable(bool configurable);
    bool isApprovalUserConfigurable() const { return m_approvalUserConfigurable; }
    bool setApprovalByUser(Approval approval);

    struct ApprovalRequest
    {
        QString     name;               // of a saved script, else empty
        QString     code;
        QJsonObject args;
        bool        dryRun = false;
        QStringList methods;            // the methods the script uses
        QStringList changingMethods;    // those of them that are not read-only
        bool        dynamic = false;    // may call more than the source shows
    };
    struct ApprovalDecision
    {
        bool    approved = false;
        QString reason;                 // told to the requester when declined
        bool    rememberForSession = false;   // do not ask again for this code
    };
    using ApprovalHandler = std::function<void(const ApprovalRequest& request,
                                               std::function<void(const ApprovalDecision&)> decide)>;
    // Replaces the built-in dialog; nullptr brings it back. The handler may
    // decide later, from its own window.
    void setApprovalHandler(ApprovalHandler handler);
    // Parent of the built-in dialog.
    void setDialogParent(QWidget* parent);

    // ---- The user's choices, for the application to store ------------------
    // {"enabled": bool, "approval": "never"|"changing"|"always",
    //  "methods": {"<name>": bool}} - only what the user may change.
    QJsonObject userSettings() const;
    // Applies what is not locked; the rest is ignored.
    void applyUserSettings(const QJsonObject& settings);

    // ---- Checking and running ---------------------------------------------
    struct SCRIPT_HOST_API Check
    {
        bool        ok = false;
        QString     phase;              // "syntax" or "check" when not ok
        int         line = 0;
        QString     message;
        QString     didYouMean;
        QStringList methods;            // the methods the script uses
        QStringList changingMethods;
        bool        dynamic = false;
        // The error as a result object; {"status":"ok"} when ok.
        QJsonObject toJson() const;
    };
    // Does the script compile, and does it only use methods there are?
    // Nothing of the script is executed.
    Check check(const QString& code) const;

    struct RunOptions
    {
        QString     name;               // for the record: which saved script
        QJsonObject args;               // `args` in the script
        bool        dryRun = false;     // methods that change something are recorded, not called
        bool        debug = false;      // debugger: see stepRun() and the run* signals
        bool        stepFromStart = false;   // debugger: stop at the first call
        QSet<int>   breakpoints;        // debugger: stop at calls on these lines
        bool        external = false;   // for the record: started through requestRun()
        bool        readOnlyOnly = false;    // a changing method fails the script
    };
    using RunCallback = std::function<void(const QJsonObject& result)>;
    // Starts a script. One runs at a time: returns false, with the callback
    // called with the error, when one is running. The result:
    //   {"status":"ok","result":...,"calls":{...},"duration_ms":N,"log":[...]}
    //   {"status":"error","phase":"syntax"|"runtime"|"limit"|"cancelled",
    //    "line":N,"message":"..."}
    bool run(const QString& code, const RunOptions& options = RunOptions(), RunCallback done = nullptr);
    bool isRunning() const;
    // True while the script running was started through requestRun().
    bool isExternalRun() const;
    void stop();

    struct Request
    {
        QString     code;
        QString     name;               // instead of code: a saved script (library())
        QJsonObject args;
        bool        dryRun = false;
    };
    // A run somebody else asks for. The script is checked first (a mistake
    // comes back as {"status":"error","phase":"syntax"|"check",...} and the
    // user is not bothered), approved by the user where the approval mode
    // says so ({"phase":"declined","reason":...} when not), then run. `done`
    // always gets a result. `abandoned`, if given, is asked once the user has
    // decided: true means nobody is waiting for this run any more, and it is
    // dropped without a result.
    void requestRun(const Request& request, RunCallback done,
                    std::function<bool()> abandoned = nullptr);

    // Debugger, while a run is paused (runPaused): one call further, or on to
    // the next breakpoint.
    void stepRun();
    void continueRun();
    void setBreakpoints(const QSet<int>& lines);

    // The runs of this session, oldest first.
    struct RunRecord
    {
        QDateTime   started;
        QString     name;
        QString     code;
        QJsonObject args;
        QJsonObject result;
        bool        dryRun = false;
        bool        external = false;
    };
    QList<RunRecord> runs() const { return m_runs; }
    void clearRuns();

    // ---- Saved scripts ----------------------------------------------------
    // Where requestRun() finds scripts by name and where ScriptsWidget lists
    // and edits them. Not owned.
    void setLibrary(ScriptLibrary* library);
    ScriptLibrary* library() const;
    // For ScriptsWidget: may the user create, change and delete scripts
    // there, and use the runner / debugger of the editor? Default true.
    void setUserMayEditScripts(bool allowed);
    bool userMayEditScripts() const { return m_userMayEdit; }
    void setDebuggerEnabled(bool enabled);
    bool isDebuggerEnabled() const { return m_debuggerEnabled; }

signals:
    void enabledChanged(bool enabled);
    // A method was added, removed, switched or (un)locked.
    void methodsChanged();
    // Approval mode, the library or one of the locks changed.
    void settingsChanged();

    void runStarted(const QString& name, bool external);
    // Debugger: stopped before `method` is called from `line`.
    void runPaused(int line, const QString& method, const QJsonArray& arguments);
    // Debugger: the call returned `value`, or failed with `error`.
    void runCallFinished(int line, const QString& method, const QJsonArray& arguments,
                         const QJsonValue& value, const QString& error);
    void runLog(const QString& text);
    void runProgress(double done, double total, const QString& text);
    void runFinished(const QJsonObject& result);
    void runsChanged();

private:
    struct Entry
    {
        ScriptMethod             method;
        ScriptMethodHandler      handler;
        AsyncScriptMethodHandler asyncHandler;
        bool                     local = false; // handled inside the runner
    };

    void registerBuiltins();
    bool policy(const QHash<QString, bool>& byName, const QHash<QString, bool>& byGroup,
                const ScriptMethod& method, bool fallback) const;
    const Entry* findEntry(const QString& name) const;
    void askApproval(const ApprovalRequest& request, std::function<void(const ApprovalDecision&)> decide);
    void onCallRequested(quint64 id, const QString& name, const QString& argsJson);
    void onRunCompleted(const QJsonObject& result);

    QList<Entry>         m_registered;
    QHash<QString, bool> m_methodEnabled, m_groupEnabled;
    QHash<QString, bool> m_methodConfigurable, m_groupConfigurable;

    bool        m_enabled = true;
    bool        m_enabledUserConfigurable = true;
    QString     m_requesterName;
    Approval    m_approval = Approval::Never;
    bool        m_approvalUserConfigurable = true;
    ApprovalHandler   m_approvalHandler;
    QPointer<QWidget> m_dialogParent;
    QSet<QByteArray>  m_approvedThisSession;    // hashes of approved code
    int         m_apiBudget = 6000;
    Limits      m_limits;
    bool        m_userMayEdit = true;
    bool        m_debuggerEnabled = true;
    QPointer<ScriptLibrary> m_library;

    // The run in progress.
    ScriptRunner*                   m_runner = nullptr;
    QSharedPointer<ScriptRunShared> m_shared;
    QHash<QString, Entry>           m_runEntries;
    RunCallback                     m_runCallback;
    RunRecord                       m_runRecord;
    QTimer*                         m_timeout = nullptr;
    QList<RunRecord>                m_runs;
};

} // namespace ScriptHost
