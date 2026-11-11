#pragma once
#include "ScriptHost_base.h"
#include "ScriptMethod.h"
#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QSharedPointer>
#include <QString>
#include <QStringList>
#include <functional>

class QIODevice;

namespace ScriptHost
{

class ScriptExtension;
class ScriptLibrary;

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
// data, and the application stays responsive while a script works. Several
// scripts run at the same time, each told apart by its RunId.
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

    // ---- Switching off what scripts can use ------------------------------
    // Every function a script can reach can be switched off by the
    // application, one by one:
    //   a method              setMethodEnabled(), or its group with
    //                         setMethodGroupEnabled(); the user may switch it
    //                         too, unless locked (setMethodUserConfigurable).
    //   an extension          setExtensionEnabled(): the whole object.
    //   an extension's member setExtensionMemberEnabled(): "text" of gfx, or a
    //                         namespace of it ("draw", "ease") with everything
    //                         under it, members added later included.
    // All of them are durable by name: they may be set before the method or
    // extension exists and survive it being registered again. What is
    // switched off is left out of apiDefinition(), check() rejects a script
    // that uses it ("the method 'db.write' is disabled", "'gfx.text' is
    // disabled"), and a run started later does not have it. Runs already
    // active keep what they started with.
    // Precedence, for extensions: a member is there only when its extension,
    // the member itself and every namespace above it are enabled. Enabling a
    // member never brings it back under a disabled extension or namespace;
    // each switch keeps its own setting for when the one above comes back.

    // ---- The methods a script can call ------------------------------------
    // Registering a name again replaces the method. A name that is not valid,
    // or that is an extension's or lies in its name space ("gfx" or
    // "gfx.line" while the extension gfx is added), is refused with a
    // warning; a name space no extension uses ("ui.tree") is fine.
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

    // ---- Extensions: script objects beside the methods ----------------------
    // See ScriptExtension. Adding a name again replaces the extension; a name
    // that is not an identifier, or that a method is or starts with ("db" of
    // "db.read"), is refused with a warning. New runs get the extension; runs
    // already active keep what they started with. Each emits methodsChanged().
    void addExtension(const QSharedPointer<ScriptExtension>& extension);
    void removeExtension(const QString& name);
    // The names of the extensions added, enabled or not, sorted.
    QStringList extensions() const;
    // Durable, by name, may be set before the extension is added. Disabled:
    // absent from new runs and apiDefinition(); check() rejects a use of it.
    // Default enabled.
    void setExtensionEnabled(const QString& name, bool enabled);
    bool isExtensionEnabled(const QString& name) const;
    // One member of an extension, by its path as in ScriptExtension::members()
    // ("text", "draw.star", or a namespace: "draw", "ease"). Durable, by
    // name, may be set before the extension is added or the member exists.
    // Disabled: check() rejects a use of it ("'gfx.text' is disabled"),
    // apiDefinition() leaves its declaration out, and in new runs it is
    // absent - `typeof gfx.text` is "undefined", so a call fails as a call of
    // undefined does. See the precedence above. Default enabled.
    void setExtensionMemberEnabled(const QString& extension, const QString& member, bool enabled);
    // False also when the extension, or a namespace above the member, is
    // disabled.
    bool isExtensionMemberEnabled(const QString& extension, const QString& member) const;
    // The members switched off with setExtensionMemberEnabled(), sorted.
    QStringList disabledExtensionMembers(const QString& extension) const;
    // A requested run (requestRun(), or RunOptions::external) gets no
    // extension that is not allowed here, and check(code, true) rejects a use
    // of it. Nothing is allowed by default.
    void allowExtensionForRequestedRuns(const QString& name, bool allow = true);
    bool isExtensionAllowedForRequestedRuns(const QString& name) const;

    // ---- The definition ---------------------------------------------------
    // The enabled methods as TypeScript declarations: for whoever writes
    // scripts - a person (the editor's reference shows it) or a program.
    // With a query (a method name, a namespace, keywords): the matching ones.
    // Without, and when everything does not fit setApiBudgetChars(): an index
    // with one line per method. The enabled extensions' declarations follow
    // the methods' (with a query: those whose name or declaration matches).
    QString apiDefinition(const QString& query = QString()) const;
    void setApiBudgetChars(int chars);              // default 6000; 0 = no limit
    int  apiBudgetChars() const { return m_apiBudget; }

    // ---- Limits -----------------------------------------------------------
    struct Limits
    {
        int timeoutMs         = 300000; // whole run, timers included; not applied
                                        // in the debugger; 0 = no limit
        int maxCalls          = 5000;   // method calls per run; 0 = no limit
        int maxLogLines       = 200;    // the latest lines kept of the script log
        int maxConcurrentRuns = 8;      // runs at the same time; 0 = no limit
        int maxQueuedEvents   = 1000;   // events waiting per run (postEvent); when
                                        // full, the oldest is dropped; 0 = no limit
    };
    void setLimits(const Limits& limits);
    Limits limits() const { return m_limits; }

    // ---- Approval of requested runs ----------------------------------------
    // Whether the user sees a requested script, and agrees, before it runs -
    // decided per kind of script. Every requested run is of exactly one kind:
    enum class ScriptKind
    {
        NewReading,     // code that came with the request and only calls
                        // read-only methods
        NewChanging,    // code that came with the request and calls a method
                        // that is not read-only, or picks methods by computed
                        // names
        SavedReading,   // a script of the library, run by name, that the user
        SavedChanging,  // saved or the application ships (origin "user" or
                        // "application"). One the requester saved is new.
        DryRun          // any script as a dry run: nothing is changed
    };
    enum { ScriptKindCount = 5 };
    // The kind's name in userSettings(): "new_reading", "new_changing",
    // "saved_reading", "saved_changing", "dry_run".
    static QString scriptKindKey(ScriptKind kind);

    // Default: no kind needs approval. A script that runs unasked as a
    // reading one cannot call a changing method while changing scripts of
    // the same origin need approval.
    void setApprovalRequired(ScriptKind kind, bool required);
    bool isApprovalRequired(ScriptKind kind) const;
    void setApprovalUserConfigurable(ScriptKind kind, bool configurable);
    void setApprovalUserConfigurable(bool configurable);    // every kind
    bool isApprovalUserConfigurable(ScriptKind kind) const;
    bool setApprovalRequiredByUser(ScriptKind kind, bool required);

    // Every kind at once:
    //   Never            nothing needs approval
    //   ChangingScripts  NewChanging and SavedChanging do
    //   Always           every kind does
    enum class Approval { Never, ChangingScripts, Always };
    void setApproval(Approval approval);

    struct ApprovalRequest
    {
        ScriptKind  kind = ScriptKind::NewChanging;
        QString     name;               // of a saved script, else empty
        QString     code;
        QJsonObject args;
        bool        dryRun = false;
        QStringList methods;            // the methods the script uses
        QStringList changingMethods;    // those of them that are not read-only
        QStringList extensions;         // the extensions it uses, as in Check
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
    // Decides the requested runs that need approval; it may decide later,
    // from its own window. Without one (the default, or nullptr) such a run
    // fails with {"phase":"approval"}. ScriptHostWidgets has a dialog for it:
    // useApprovalDialog().
    void setApprovalHandler(ApprovalHandler handler);

    // ---- The user's choices, for the application to store ------------------
    // {"enabled": bool, "approval": {"<kind>": bool}, "methods": {"<name>": bool}}
    // - only what the user may change. Kinds as in scriptKindKey().
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
        // The extensions it uses, each followed by the members it uses:
        // "gfx", "gfx.rect", "gfx.draw.star", "ext", ... - a member as
        // ScriptExtension::members() names it, or the way to one ("gfx.draw").
        // An extension used as a whole (`const g = gfx;`) has its name only.
        QStringList extensions;
        bool        dynamic = false;
        // The error as a result object; {"status":"ok"} when ok.
        QJsonObject toJson() const;
    };
    // Does the script compile, and does it only use methods there are, and
    // members of the enabled extensions? Nothing of the script is executed.
    // `requested`: as a requested run sees it - an extension it may not use
    // is rejected. requestRun() checks so.
    Check check(const QString& code) const;
    Check check(const QString& code, bool requested) const;

    struct RunOptions
    {
        QString     name;               // for the record: which saved script
        QJsonObject args;               // `args` in the script
        bool        dryRun = false;     // methods that change something are recorded, not called
        bool        debug = false;      // debugger: see stepRun() and the run* signals
        bool        statements = false; // debugger: stop before statements too, and
                                        // report the variables (stepStatement())
        bool        stepFromStart = false;   // debugger: stop at the first call - with
                                             // `statements`, at the first statement
        QSet<int>   breakpoints;        // debugger: stop at these lines - before a
                                        // call there, with `statements` before the
                                        // statement
        bool        external = false;   // started through requestRun(): for the record, and
                                        // it gets only allowEventForRequestedRuns() events
                                        // and allowExtensionForRequestedRuns() extensions
        bool        readOnlyOnly = false;    // a changing method fails the script
        int         timeoutMs = -1;     // -1 = Limits::timeoutMs, 0 = no limit; not
                                        // applied in the debugger
        int         maxCalls = -1;      // -1 = Limits::maxCalls, 0 = no limit
    };
    // Names one run of this object; 0 is none. Counts up from 1 and is never
    // given to a second run.
    using RunId = quint64;
    using RunCallback = std::function<void(const QJsonObject& result)>;
    // Starts a script. Several run at the same time, up to
    // Limits::maxConcurrentRuns, and at most one of them in the debugger
    // (RunOptions::debug). Returns the new run's id; 0 when it cannot start,
    // with the callback called with {"phase":"unavailable"}. The result:
    //   {"status":"ok","result":...,"calls":{...},"duration_ms":N,"log":[...]}
    //   {"status":"error","phase":"syntax"|"runtime"|"limit"|"cancelled"|
    //    "approval"|"unavailable","line":N,"message":"..."}
    // A script that set timers (setTimeout, setInterval) goes on running
    // after its body returned, until no timer is left, it is stopped or it
    // reaches its time limit. `result` is what the body returned,
    // `duration_ms` the whole time; an exception thrown in a timer's function
    // ends the run as a "runtime" error.
    RunId run(const QString& code, const RunOptions& options = RunOptions(), RunCallback done = nullptr);
    // Is any run active?
    bool isRunning() const;
    bool isRunning(RunId id) const;
    // The active runs, oldest first.
    QList<RunId> activeRuns() const;
    // True while the run is active and was started through requestRun().
    bool isExternalRun(RunId id) const;
    // Ends the run soon - also while it sleeps, waits for a timer or for a
    // method, or loops in JavaScript. Its result is {"phase":"cancelled"}.
    // Nothing happens for an id that is not active.
    void stop(RunId id);
    void stopAll();

    // ---- Events -----------------------------------------------------------
    // A script subscribes with on(name, fn) and unsubscribes with
    // off(name, fn) or off(name) (every handler of the name). postEvent()
    // - on this object's thread - hands `data` to every active run with a
    // handler for `name`, or to the one run `id`; a run without one does not
    // get it, not even later. Each run calls its handlers on its own thread,
    // with `data` as a plain object, in the order the events were posted -
    // once its body has returned, between its timers: ahead of due timers,
    // but a due timer waits for at most 64 events. An event posted
    // while the body still runs (a loop, sleep()) waits in the run's queue
    // until the body has returned. The queue holds Limits::maxQueuedEvents;
    // beyond that the oldest event is dropped, the run's log says so once
    // ("events dropped: queue full (N)") and its result carries
    // "events_dropped": <count>. An event is not
    // a method call and does not count toward maxCalls. A run with a
    // handler goes on running after its body returned, like one with timers,
    // until off() removed the last handler and no timer is left, it is
    // stopped or it reaches its time limit; an exception thrown in a handler
    // ends the run as a "runtime" error. ScriptHostWidgets posts "key" and
    // "mouse": forwardInput().
    void postEvent(const QString& name, const QJsonObject& data);
    void postEvent(RunId id, const QString& name, const QJsonObject& data);
    // A requested run (requestRun(), or RunOptions::external) gets no event
    // whose name is not allowed here: its on() for that name throws. A name
    // allowed also allows the names that begin with it and a colon: "ui"
    // allows "ui:7:clicked" (the widget signals, ScriptUi). Nothing is
    // allowed by default. A run keeps what was allowed when it started.
    void allowEventForRequestedRuns(const QString& name, bool allow = true);
    bool isEventAllowedForRequestedRuns(const QString& name) const;
    // While a method's handler runs: the run that called it, else 0. An
    // asynchronous handler sees it only until it returns. With
    // isExternalRun(), a handler can treat requested runs apart.
    RunId callingRun() const { return m_callingRun; }

    struct Request
    {
        QString     code;
        QString     name;               // instead of code: a saved script (library())
        QJsonObject args;
        bool        dryRun = false;
    };
    // A run somebody else asks for. The script is checked first (a mistake
    // comes back as {"status":"error","phase":"syntax"|"check",...} and the
    // user is not bothered), approved by the user where its kind needs that
    // ({"phase":"declined","reason":...} when not; {"phase":"approval"} when
    // no approval handler is set), then run. `done`
    // always gets a result. `abandoned`, if given, is asked once the user has
    // decided: true means nobody is waiting for this run any more, and it is
    // dropped without a result. The run has the engine's Limits: a request
    // cannot choose others, as it comes from outside the application.
    void requestRun(const Request& request, RunCallback done,
                    std::function<bool()> abandoned = nullptr);

    // Debugger, for the run in the debugger (RunOptions::debug) while it is
    // paused (runPaused): one statement further (a run with
    // RunOptions::statements; a script whose statements cannot be told apart
    // is stepped call by call), one call further, or on to the next
    // breakpoint. Nothing happens without a run in the debugger.
    void stepStatement();
    void stepRun();
    void continueRun();
    void setBreakpoints(const QSet<int>& lines);

    // The finished runs of this session, oldest first; the latest 100.
    struct RunRecord
    {
        RunId       id = 0;
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
    // The record of an active run so far, without a result yet; an empty one
    // (id 0) when the run is not active.
    RunRecord activeRun(RunId id) const;
    // Where the log lines of all runs go besides runLog: a QFile opened on
    // stdout, a QBuffer, a socket, ... Not owned; nullptr (default) = none,
    // and a device that is deleted is dropped. See runLog for the format.
    void setOutput(QIODevice* device);
    QIODevice* output() const;

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
    // What needs approval, the library or one of the locks changed.
    void settingsChanged();

    // The run* signals name the run they are about first.
    void runStarted(RunId id, const QString& name, bool external);
    // Debugger: stopped in `line` - before `method` is called, or before the
    // statement there (`method` empty). `variables` are those visible at that
    // statement, each as the text a debugger shows: {"n": "3", "id": "\"o1\""}.
    // Empty in a run without RunOptions::statements.
    void runPaused(RunId id, int line, const QString& method, const QJsonArray& arguments,
                   const QJsonObject& variables);
    // Debugger: the call returned `value`, or failed with `error`.
    void runCallFinished(RunId id, int line, const QString& method, const QJsonArray& arguments,
                         const QJsonValue& value, const QString& error);
    // A line the script wrote with log(), print() or console.log() - the
    // values joined by spaces. Also written to output(), if set, on this
    // thread as "[<script name, or #<id> when unnamed>] <text>\n" in UTF-8,
    // flushed after each line when it is a QFileDevice.
    void runLog(RunId id, const QString& text);
    void runProgress(RunId id, double done, double total, const QString& text);
    void runFinished(RunId id, const QJsonObject& result);
    // runs() changed.
    void runsChanged();

private:
    struct Entry
    {
        ScriptMethod             method;
        ScriptMethodHandler      handler;
        AsyncScriptMethodHandler asyncHandler;
        bool                     local = false; // handled inside the runner
    };
    struct Run;     // one run in progress

    void registerBuiltins();
    // A valid method name outside the extensions' name spaces; warns if not.
    bool acceptsMethodName(const QString& name) const;
    // Is `path` of the extension switched off itself, or by a namespace above it?
    bool isMemberDisabled(const QString& extension, const QString& path) const;
    bool policy(const QHash<QString, bool>& byName, const QHash<QString, bool>& byGroup,
                const ScriptMethod& method, bool fallback) const;
    const Entry* findEntry(const QString& name) const;
    // Why no further run can start now; empty when one can.
    QString unavailableReason(bool debug) const;
    Run* debugRun() const;
    void onCallRequested(RunId id, quint64 call, const QString& name, const QString& argsJson);
    void onRunCompleted(RunId id, const QJsonObject& result);

    QList<Entry>         m_registered;
    QHash<QString, bool> m_methodEnabled, m_groupEnabled;
    QHash<QString, bool> m_methodConfigurable, m_groupConfigurable;

    bool        m_enabled = true;
    bool        m_enabledUserConfigurable = true;
    QString     m_requesterName;
    bool        m_approvalRequired[ScriptKindCount] = {};
    bool        m_approvalConfigurable[ScriptKindCount] = {true, true, true, true, true};
    ApprovalHandler   m_approvalHandler;
    QSet<QByteArray>  m_approvedThisSession;    // hashes of approved code
    int         m_apiBudget = 6000;
    Limits      m_limits;
    bool        m_userMayEdit = true;
    bool        m_debuggerEnabled = true;
    QPointer<ScriptLibrary> m_library;
    QPointer<QIODevice> m_output;
    QSet<QString>       m_requestedEvents;  // allowEventForRequestedRuns()
    QList<QSharedPointer<ScriptExtension>> m_extensions;
    QSet<QString>       m_disabledExtensions;   // setExtensionEnabled()
    QHash<QString, QSet<QString>> m_disabledMembers;    // setExtensionMemberEnabled(), by extension
    QSet<QString>       m_requestedExtensions;  // allowExtensionForRequestedRuns()

    QMap<RunId, Run*>   m_active;           // the runs in progress, owned
    RunId               m_lastRunId = 0;
    RunId               m_callingRun = 0;   // callingRun()
    QList<RunRecord>    m_runs;
};

} // namespace ScriptHost
