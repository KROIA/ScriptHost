#pragma once
#include "ScriptHost_base.h"
#include "ScriptEngine.h"
#include <QJSValue>
#include <QObject>
#include <QSharedPointer>
#include <QString>
#include <QStringList>
#include <functional>

namespace ScriptHost
{

// What a run tells an extension's per-run object, and the way back into the
// run. Handed to ScriptExtension::createRunObject(). A cheap handle: copy it,
// keep it in the per-run object, and hand it to the GUI side for schedule().
// A copy stays safe to hold after the run has ended; schedule() then refuses.
//
// Threads: runId(), isRequested(), isDryRun(), isActive(), schedule() and
// setKeepAlive() work on any thread. throwError(), log() and call() work only
// on the run's worker thread (they warn and do nothing elsewhere).
class SCRIPT_HOST_API ScriptRunContext
{
public:
    ScriptRunContext() = default;   // an empty one: belongs to no run

    ScriptEngine::RunId runId() const;
    // Started through requestRun() (or RunOptions::external).
    bool isRequested() const;
    // A dry run (RunOptions::dryRun): the per-run object must not change
    // the application's data. See ScriptExtension.
    bool isDryRun() const;
    // False once the run's script code is over (any end), and for an empty one.
    bool isActive() const;

    // Worker thread, while the script calls into the per-run object: throws
    // a JavaScript Error with `message` once the call returns. The script may
    // catch it; uncaught, the run ends as a "runtime" error.
    void throwError(const QString& message) const;
    // Worker thread: a line in the run's log, as log() writes it.
    void log(const QString& text) const;
    // Worker thread, in a callback of schedule(): calls the script function
    // `fn`. An exception it throws ends the run as a "runtime" error with the
    // script's line, like one thrown in a timer, once the callback returns.
    // False when it threw (or `fn` is not a function).
    bool call(const QJSValue& fn, const QJSValueList& args = QJSValueList()) const;

    // Any thread: runs `callback` on the run's worker thread, once the
    // script's body has returned, between its timers and events - like an
    // event. With a key, at most one callback is pending per key: a newer one
    // replaces the pending one (in its place). An empty key queues each. Keys
    // are the extension's own. False, dropping `callback`, when the run has
    // ended or is stopping.
    // A pending callback keeps the run alive until it ran. One still pending
    // when the run ends is dropped on the worker thread, before the per-run
    // objects are deleted - so a callback may capture a raw pointer to the
    // per-run object. A replaced callback is destroyed on the thread of the
    // schedule() call: do not capture QJSValues.
    bool schedule(const QString& key, std::function<void()> callback) const;
    // Any thread: while any key is held, the run goes on after its body
    // returned, like a run with timers or on() handlers - until every key is
    // released, it is stopped (stop() ends it at once) or reaches its time
    // limit. Keys are the extension's own.
    void setKeepAlive(const QString& key, bool keep) const;

    struct Data;
    explicit ScriptRunContext(const QSharedPointer<Data>& data) : d(data) {}

private:
    QSharedPointer<Data> d;
};

// A script-side object an application adds to every run, under a global
// name: a drawing surface, a device, a game API - anything that is more than
// a list of methods. ScriptHostWidgets builds its canvas (`gfx`) on it.
//
// Unlike a ScriptMethod, the per-run object is called directly on the run's
// worker thread: no JSON, no thread hop, no maxCalls cost, no record per
// call, no debugger stop, no read-only check. That is what makes it fast,
// and why it must be written for the worker thread.
//
//   class Counter : public QObject       // the per-run object
//   {
//       Q_OBJECT
//   public:
//       explicit Counter(const ScriptHost::ScriptRunContext& context) : m_context(context) {}
//       Q_INVOKABLE int next() { return ++m_n; }
//       Q_INVOKABLE void reset(int to)
//       {
//           if (to < 0)
//               m_context.throwError("reset: the value must not be negative");
//           else
//               m_n = to;
//       }
//   private:
//       ScriptHost::ScriptRunContext m_context;
//       int m_n = 0;
//   };
//
//   class CounterExtension : public ScriptHost::ScriptExtension
//   {
//   public:
//       CounterExtension() : ScriptExtension("counter") {}
//       QStringList members() const override { return {"next", "reset"}; }
//       QString declaration() const override
//       {
//           return "declare namespace counter {\n"
//                  "  /** The next number, from 1. */\n  function next(): number;\n"
//                  "  function reset(to: number): void;\n}\n";
//       }
//       QObject* createRunObject(const ScriptHost::ScriptRunContext& context) override
//       {
//           return new Counter(context);
//       }
//   };
//
//   engine->addExtension(QSharedPointer<CounterExtension>::create());
//   // in a script: log(counter.next());
//
// Threads and ownership:
//   members(), declaration()   the engine's thread (GUI), each time check()
//                              or apiDefinition() runs: they may change at any
//                              time - emit changed() when they do.
//   createRunObject(), glue()  the run's worker thread, at the run's start,
//                              after the methods are installed and before the
//                              body runs. Runs start side by side: both must
//                              be thread-safe against each other and against
//                              the GUI thread.
//   the per-run object         lives on the worker thread. The run owns it and
//                              deletes it there when the run's script code is
//                              over (every end), while the run's JavaScript
//                              engine still exists - its destructor may let go
//                              of QJSValues. Return an object without a parent.
//                              qjsEngine(this) is the run's engine.
//   runEnded()                 the engine's thread, once per run that had the
//                              extension, on every end (finished, stopped,
//                              error, time or call limit, the engine deleted)
//                              - after the worker thread is done and the
//                              per-run object is gone, before runFinished.
//                              Also for a run that ended before the extension
//                              was created (a syntax error).
// The engine and every run that has the extension hold a reference to it:
// removeExtension() while runs are active is safe - those runs keep it,
// runEnded() included; new runs go without it.
//
// Requested runs (requestRun()) get the extension only when the application
// allows it: ScriptEngine::allowExtensionForRequestedRuns().
//
// The application can switch off the whole extension
// (ScriptEngine::setExtensionEnabled) or single members of it
// (ScriptEngine::setExtensionMemberEnabled); the engine leaves them out of
// what a run sees, so the extension need not know.
//
// Dry runs and checks: check() and the compile-only syntax check never call
// createRunObject(). A dry run (RunOptions::dryRun) does, with
// context.isDryRun() true: the per-run object must then leave the
// application's data alone - drawing on an overlay, or answering, is fine.
class SCRIPT_HOST_API ScriptExtension : public QObject
{
    Q_OBJECT
public:
    // `name`: the script's global, an identifier ("gfx"). Its own name space:
    // no method may be or start with it.
    explicit ScriptExtension(const QString& name);
    ~ScriptExtension() override;

    QString name() const { return m_name; }

    // What a script may use, for check(): "rect", "width", and nested ones as
    // "draw.star". A use of `gfx.x.y` is accepted when "x" or "x.y" is a member,
    // or "x" leads to one ("x.y.z"); otherwise check() fails with did-you-mean.
    virtual QStringList members() const = 0;
    // TypeScript declarations, added to apiDefinition() as they are:
    // "declare namespace gfx { function rect(...): void; }".
    virtual QString declaration() const = 0;
    // Creates the per-run object; see above. nullptr: the run goes without it.
    virtual QObject* createRunObject(const ScriptRunContext& context) = 0;
    // Optional JavaScript evaluated in the run's engine: a function that takes
    // the per-run object (as wrapped by the engine) and returns what the
    // script sees under name(), e.g.
    //   "(function(native){ return { rect: native.rect, twice: function(x){ return native.add(x, x); } }; })"
    // Empty (default): the script sees the per-run object itself. An error
    // ends the run as a "runtime" error before the body runs.
    virtual QString glue() const { return QString(); }
    // The run ended; see above.
    virtual void runEnded(ScriptEngine::RunId) {}

signals:
    // members() or declaration() changed. The engines it is added to emit
    // methodsChanged().
    void changed();

private:
    QString m_name;
};

} // namespace ScriptHost
