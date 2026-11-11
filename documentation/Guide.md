# ScriptHost guide

ScriptHost lets the user of an application, the application itself, or another program run small JavaScript programs inside the application, against methods the application defines.

## What it is for

"Do this for every object." Without scripts, every object needs a command of its own: one click, one remote call, one request. With a script the loop is written once:

```js
const changed = [];
for (const id of db.listObjects()) {
    const object = db.readObject(id);
    if (object.year < args.before) {
        db.setProperty(id, "status", "archived");
        changed.push(object.name);
    }
}
return { archived: changed.length, first: changed.slice(0, 5) };
```

The calls happen inside the application. Whoever started the script gets back only what it returns.

## Setting it up

```cpp
#include "ScriptHost.h"

auto* engine = new ScriptHost::ScriptEngine(this);

ScriptHost::ScriptMethod read("db.readObject");
read.setDescription("Reads one object of the project.")
    .addParameter("id", "string", "Object id")
    .setReturns("object", "The object", QJsonObject{{"id", "a1"}, {"name", "Pump"}, {"year", 2019}})
    .setReadOnly(true)
    .setGroup("Database");
engine->registerMethod(read, [this](const QJsonObject& args) -> QJsonValue {
    const QString id = args["id"].toString();
    if (!m_db.contains(id))
        throw ScriptHost::ScriptError("no object '" + id + "'");   // the script gets an exception
    return m_db.object(id);
});

engine->run("return db.readObject('a1').name;", {}, [](const QJsonObject& result) { ... });
```

`examples/LibraryExample` is a complete application: an inventory table with its methods, saved scripts, the Scripts window, the editor, a requested run, a stock monitor that runs until it is stopped, an input logger that reacts to keys and clicks, and a canvas with a script that highlights a widget and a small paddle game, in the order an application sets them up. `examples/ConsoleExample` is the engine alone in a `QCoreApplication`, its script's lines printed to stdout.

### Two libraries

| Library | Qt modules | Include | Contains |
|---|---|---|---|
| `ScriptHost` | Core, Qml | `ScriptHost.h` | `ScriptEngine`, `ScriptMethod`, `ScriptLibrary`, `ScriptExtension`: everything to run scripts |
| `ScriptHostWidgets` | Core, Qml, Gui, Widgets | `ScriptHostWidgets.h` (includes `ScriptHost.h`) | `ScriptsWidget`, `ScriptEditorDialog` with its debugger, the approval dialog (`useApprovalDialog`), keys and mouse for scripts (`forwardInput`), drawing for scripts (`attachCanvas`, `ScriptCanvas`) |

A program without windows - a service, a command-line tool - links the engine alone and needs neither Qt Gui nor Qt Widgets. In a project built from the same CMake template, `ScriptHost.cmake` in the `dependencies/` folder links both libraries; for the engine alone, set this before it is included:

```cmake
set(SCRIPTHOST_ENGINE_ONLY ON)
```

The targets are `ScriptHost_shared` / `ScriptHost_static` and `ScriptHostWidgets_shared` / `ScriptHostWidgets_static`.

## The methods a script can call

A script can call exactly what the application registers, and the built-in methods below. The JavaScript engine has no file, network or process access of its own.

### Defining a method

| Call | Meaning |
|---|---|
| `ScriptMethod("db.readObject")` | `space.name` is a function of the object `space`; a plain `name` is a global function |
| `setDescription(text)` | What it does |
| `addParameter(name, type, description, required = true, enumValues = {})` | Types: `string`, `integer`, `number`, `boolean`, `array`, `object`. Optional parameters last |
| `addRawParameter(name, schema, required = true)` | The parameter's JSON schema, as given |
| `setRange(name, min, max)`, `setDefault(name, value)` | Checked and applied before the handler runs |
| `setReturns(type, description, example)` | The example gives the return type its shape, and is what a dry run answers |
| `setReadOnly(true)` | The method only reads: it is really called in a dry run, and a script that calls nothing else counts as a reading script when it comes to approval |
| `setGroup(name)` | For the Methods list and for switching whole groups |
| `setMaxCallsPerRun(n)` | At most `n` calls of this method in one run. For methods that are expensive. 0 (default): only the engine's limit applies |

A parameter is required unless `required` is passed as `false`: `addParameter` and `addRawParameter` default to `required = true`. Other libraries (QtLLM, for one) default the other way; pass the flag explicitly when porting definitions.

In a script, arguments are positional in the order the parameters were added: `db.readObject("a1")`. A single object is taken as named arguments: `db.setProperty({id: "a1", name: "status", value: "archived"})`.

Every call is checked against the definition first. `"5"` for a number is repaired, an enum's spelling is normalised, defaults are applied. A call that does not fit never reaches the handler; the script gets an exception that names the problem and the signature:

```
db.setProperty: 'value' is missing. Signature: db.setProperty(id: string, name: string, value: string): object
```

### Methods defined by a JSON schema

An interface that is described by a JSON schema already (a command set, a plugin API) does not need to be written again with `addParameter`:

```cpp
const QJsonObject schema = QJsonDocument::fromJson(R"({
    "type": "object",
    "properties": {
        "a": {"type": "number", "description": "First"},
        "b": {"type": "string"}
    },
    "required": ["a"]
})").object();

auto add = ScriptHost::ScriptMethod::fromSchema("calc.add", schema);
add.setDescription("Adds.").setReturns("number");
engine->registerMethod(add, [](const QJsonObject& args) -> QJsonValue { ... });
```

Such a method takes one object: `calc.add({a: 1, b: "x"})`. Its signature reads `calc.add(args: { a: number, b?: string }): number`. The object is checked like the arguments of any other method: types, enums, ranges, required properties. A property the schema does not name is an error (`unknown parameter 'c'; there are: a, b`, with a "did you mean" where one is close). The properties are listed in alphabetical order.

### Handlers

```cpp
engine->registerMethod(method, [](const QJsonObject& args) -> QJsonValue { ... });
engine->registerAsyncMethod(method, [](const QJsonObject& args, ScriptHost::ScriptCompletion done) {
    startSomething(args, [done](const QJsonValue& result) { done.finish(result); });   // or done.fail("why")
});
```

Handlers run on the thread of the engine (the GUI thread) and may touch widgets and application data. The script runs on a thread of its own and waits; the application stays responsive. To fail a call, throw `ScriptHost::ScriptError` (or call `done.fail`). An asynchronous handler returns at once; `ScriptCompletion` can be copied and called from any thread, and only its first call counts.

Registering a name again replaces the method; `unregisterMethod(name)` removes it.

### Built-in methods

| Method | |
|---|---|
| `log(...values)` | A line in the script log, the values joined by spaces: shown in the editor and the Runs list, signal `runLog`, the output device (see [Output](#output)); the last lines go back with the result |
| `print(...values)`, `console.log(...values)` | The same as `log` |
| `progress(done, total, text)` | Signal `runProgress`, for a status line of the application |
| `sleep(ms)` | Waits; the application stays responsive. `stop()` ends the wait |
| `pause(label)` | Stops in the debugger; does nothing otherwise |
| `setTimeout(fn, ms, ...args)` | Calls `fn(...args)` once, after `ms` milliseconds. Returns the timer's number |
| `setInterval(fn, ms, ...args)` | Calls `fn(...args)` every `ms` milliseconds (at least 1). Returns the timer's number |
| `clearTimeout(id)`, `clearInterval(id)` | Cancels a timer; a timer's function may cancel its own |
| `on(name, fn)` | Calls `fn(data)` for each event `name` the application posts, once the body has returned. See [Events](#events-keys-mouse-and-your-own) |
| `off(name, fn)`, `off(name)` | Removes `fn` as a handler of `name`; without `fn`, every handler of `name` |
| `args` | The arguments of the run |

The built-ins are not method calls: they do not count toward `maxCalls` and are not listed in `calls`. A timer or an event handler keeps the run going after the script's end; see [Timers](#timers-scripts-that-run-on) and [Events](#events-keys-mouse-and-your-own).

`input.mouse()` - where the pointer is - is no built-in: `forwardInput()` registers it as a read-only method (group `Input`), and like any method it counts as a call.

### Switching methods on and off

```cpp
engine->setMethodEnabled("db.deleteObject", false);
engine->setMethodGroupEnabled("Database", false);              // a method's own setting wins
engine->setMethodUserConfigurable("db.deleteObject", false);   // the user cannot switch it either
```

These settings are by name and durable: they may be set before the method is registered and survive registering it again. A disabled method is not in the definition, and a script that uses it is rejected before it runs. The built-in methods are switched the same way (`"log"`, `"sleep"`, `"setInterval"`).

## The definition

`apiDefinition(query)` renders the enabled methods as TypeScript declarations:

```ts
// Methods a script can call. Calls are synchronous; a failing call throws.
declare namespace db {
  /** Reads one object of the project. Read-only.
   *  @param id Object id
   *  @returns The object */
  function readObject(id: string): { id: string, name: string, year: number };
}
```

The user sees it in the reference pane of the script editor. A program that writes scripts (a remote client, an assistant) can be given the same text. Without a query it is everything, or an index with one line per method when the text is longer than `setApiBudgetChars()` (default 6000; 0 means no limit). With a query (a method name, a namespace, keywords) it is the matching declarations. The declarations of [extensions](#extensions-objects-of-your-own) follow the methods'; with a query, an extension comes whole when its name or a word of its declaration matches - `apiDefinition("gfx")` is the whole canvas API, which is longer than the default budget on its own.

## Running a script

```cpp
ScriptHost::ScriptEngine::RunOptions options;
options.name = "Archive outdated objects";   // for the record
options.args = QJsonObject{{"before", 2015}};
options.dryRun = true;
const ScriptHost::ScriptEngine::RunId id =
    engine->run(code, options, [](const QJsonObject& result) { ... });

const ScriptHost::ScriptEngine::Check check = engine->check(code);   // compiles? uses only what exists?
```

`run()` is the application's own decision: it needs no approval and works while requested runs are switched off. It returns the new run's `RunId`, or 0 when the run cannot start - then the callback has been called already, with an error of phase `unavailable` (see [Several runs at once](#several-runs-at-once)).

A run passes three stages. The first two execute nothing.

| Stage | Checks | Error |
|---|---|---|
| Syntax | The code compiles | `{"status":"error","phase":"syntax","line":7,"message":"Expected token `)'"}` |
| Check | Every `space.method` in the source exists and is enabled | `{"status":"error","phase":"check","line":3,"message":"no method 'db.readObjekt'","did_you_mean":"db.readObject"}` |
| Run | Arguments per call; whatever the script throws | `{"status":"error","phase":"runtime","line":12,"message":"db.setProperty: ... Signature: db.setProperty(...)","calls":{...}}` |

Other outcomes:

```json
{"status":"ok","result":{"archived":312},"calls":{"db.readObject":1000,"db.setProperty":312},"duration_ms":4210,"log":["..."]}
{"status":"error","phase":"limit","message":"stopped after 5000 method calls (the limit)","calls":{...}}
{"status":"error","phase":"limit","message":"stopped after 3 calls of net.sendMail (its limit)","calls":{...}}
{"status":"error","phase":"cancelled","message":"the script was stopped"}
```

A script that fails half way leaves its changes; `calls` says how many calls of which method were made. There is no rollback.

### Several runs at once

Every run has a thread, limits and counts of its own; up to `Limits::maxConcurrentRuns` of them go on at the same time. A `RunId` names one run of the engine: a number counting up from 1, never reused; 0 means none.

| `ScriptEngine` | |
|---|---|
| `run(code, options, done)` | Starts a run; its id, or 0 when it could not start |
| `isRunning(id)`, `isRunning()` | That run is active; any run is |
| `activeRuns()` | The ids of the active runs, oldest first |
| `activeRun(id)` | The `RunRecord` of an active run so far (`id`, `started`, `name`, `code`, `args`, ...) without a result. One with `id` 0 when the run is not active |
| `isExternalRun(id)` | The run is active and was started through `requestRun()` |
| `stop(id)`, `stopAll()` | See [Stopping](#stopping) |

Every `run*` signal names its run first (see [Signals](#signals)), so a window that shows one run picks its own by id.

A run beyond the cap does not start and is not queued:

```json
{"status":"error","phase":"unavailable","message":"the limit of 8 scripts running at once is reached; wait for one to finish"}
```

Only one run at a time can be in the debugger (`RunOptions::debug`). A second debug run fails the same way, with `another script runs in the debugger; stop it first`. Runs that are not debugged go on beside it.

The method handlers of all runs are called on the engine's thread, one at a time.

### Timers: scripts that run on

```js
let last = null;
setInterval(() => {
    const low = inventory.list().filter(entry => entry.quantity < 50).map(entry => entry.item);
    const now = low.join(", ");
    if (now !== last)
        print("low:", now || "nothing");
    last = now;
}, 5000);
return "monitoring";
```

The script's body runs first; what it returns is the run's `result`. Then the run waits for its timers and calls each one when it is due, on the script's thread, one after the other. Event handlers (`on`) are called the same way, between the timers. The run ends

- when no timer and no event handler is left (every `setTimeout` fired or cleared, every `setInterval` cleared, every handler removed with `off`) and no [extension](#extensions-objects-of-your-own) keeps it (`gfx.onFrame` does, until `gfx.offFrame`): with the result of the body, `duration_ms` counting the whole time;
- when it is stopped (phase `cancelled`) or reaches its time limit (phase `limit`): what the body returned is lost;
- when a timer's function or an event handler throws: phase `runtime`, with the line.

`check()` knows the timer functions and `apiDefinition()` declares them. A timer's function calls methods like the body does, and those calls count toward the limits.

### Running until stopped

The engine's `Limits` end every run after 5 minutes or 5000 method calls by default - a monitor with an interval timer would be cut off. For a run of its own the application chooses other limits:

```cpp
ScriptHost::ScriptEngine::RunOptions options;
options.name = "Stock monitor";
options.timeoutMs = 0;   // -1 (default): Limits::timeoutMs; 0: no limit; > 0: this many ms
options.maxCalls  = 0;   // -1 (default): Limits::maxCalls;  0: no limit; > 0: this many calls
const ScriptHost::ScriptEngine::RunId id = engine->run(code, options, done);
```

`requestRun()` always applies the engine's `Limits`: a request comes from outside the application and cannot lift them. The script editor uses them too.

A run that never ends does not grow: it keeps only the latest `maxLogLines` lines of its log (see [Limits](#limits)).

### Stopping

`stop(id)` ends one run, `stopAll()` every run. The run ends soon - also while it sleeps (`sleep`), waits for a timer or for an asynchronous method, or loops in JavaScript without calling anything (`while (true) {}`). `try` / `catch` in the script cannot hold it. Its result:

```json
{"status":"error","phase":"cancelled","message":"the script was stopped"}
```

Other runs go on. What the script changed before stays changed.

In the widgets: the Runs tab of the Scripts window lists the active runs, each with a Stop button - a selected one shows its run id, arguments, code and its log as it is written; the editor's Stop button stops only the run that editor started, and closing the editor stops it.

### The language

The script runs in Qt's `QJSEngine`: on Qt 5.15 that is ECMAScript 2016 with `let` / `const`, arrow functions, `for...of`, template strings, destructuring, `Map` / `Set`. Not available there: `async` / `await`, optional chaining (`?.`), object spread. The script is the body of a function: `return` hands back the result. It runs in strict mode.

### Dry run

With `dryRun`, read-only methods run for real and every other call is recorded and answered with the method's return example:

```json
{"status":"ok","dry_run":true,"result":4,
 "would_call":{"db.setProperty":{"count":312,"first_arguments":[["a1","status","archived"], ...]}}}
```

## Limits

```cpp
ScriptHost::ScriptEngine::Limits limits;
limits.timeoutMs         = 300000;   // whole run, timers included; not applied in the debugger; 0 = none
limits.maxCalls          = 5000;     // method calls per run; 0 = none
limits.maxLogLines       = 200;      // the latest lines a run keeps of its log
limits.maxConcurrentRuns = 8;        // runs at the same time; 0 = no limit
limits.maxQueuedEvents   = 1000;     // events waiting per run; the oldest dropped when full; 0 = no limit
engine->setLimits(limits);
```

`timeoutMs` and `maxCalls` apply to every run that does not choose its own (`RunOptions::timeoutMs`, `maxCalls`; see [Running until stopped](#running-until-stopped)). `ScriptMethod::setMaxCallsPerRun(n)` limits one method on top of that. Every run counts for itself, from zero. `maxLogLines` and `maxQueuedEvents` hold for every run, requested ones included.

A script that waits, sleeps or loops without calling anything is stopped by the time limit; `try` / `catch` in the script cannot swallow that.

Every log line reaches `runLog` and the output device as it is written. A run keeps the latest `maxLogLines` of them, so one that runs for days does not grow; its result's `log` holds the last of those (at most 20, each cut to 300 characters), and `log_lines` the number written in all when that is more.

## Output

`log(...)`, `print(...)` and `console.log(...)` write the same kind of line. Besides the signal `runLog(id, text)`, the lines of all runs can go to any `QIODevice` - stdout, a file, a socket:

```cpp
QFile out;
out.open(stdout, QIODevice::WriteOnly);
engine->setOutput(&out);
```

```
[Stock monitor] low: Nuts, Hinges
[#7] a line of a run without a name
```

Each line is `[<RunOptions::name, or #<RunId> when the run has no name>] <text>` and a newline, in UTF-8, written on the engine's thread; a `QFileDevice` is flushed after each line. The engine does not own the device: the application opens it, keeps it alive and closes it. A device that is deleted is dropped; `setOutput(nullptr)` (the default) writes nowhere. `output()` returns the device.

## Events: keys, mouse and your own

A script can react to what happens in the application while it runs: keys, mouse clicks, or events the application posts itself - a tick, a sensor value, a message that came in.

```js
// the script
on('key', event => {
    if (event.type === 'down' && event.key === 'Space')
        print('jump');
});
on('sensor', data => print('temperature', data.temperature));
return 'listening';
```

```cpp
// the application
engine->postEvent("sensor", QJsonObject{{"temperature", 21.5}});
```

`on(name, fn)` subscribes, `off(name, fn)` removes one handler, `off(name)` every handler of the name. `postEvent(name, data)` hands `data` to every active run that has a handler for `name`; `postEvent(id, name, data)` to the one run `id`. A run without a handler for the name does not get the event - not later either, when it subscribes after it was posted. The handler gets `data` as a plain object. `postEvent()` is called on the engine's thread, like the rest of the engine.

### When events arrive

**Events are delivered only after the script's body has returned** - between timers, the way timer functions run. They do not arrive while the body is still running: not during `sleep()`, not inside a loop. An event posted then (after `on()`) waits in the run's queue until the body has returned.

The right pattern: register the handlers, do periodic work with `setInterval`, and return.

```js
let x = 0, speed = 0;
on('key', event => {
    if (event.key === 'Left')  speed = event.type === 'down' ? -1 : 0;
    if (event.key === 'Right') speed = event.type === 'down' ? 1 : 0;
});
setInterval(() => {             // the per-frame work
    x += speed;
    game.moveTo(x);
}, 16);
return 'running';               // now the events come
```

The wrong pattern - the handler never runs, as the body never returns:

```js
on('key', event => { speed = 1; });
while (true) {                  // events wait until the body returns: never
    game.moveTo(x += speed);
    sleep(16);
}
```

Events are delivered in the order they were posted, ahead of timers that are due - but a due timer waits for at most 64 events, then the due timers run before the next event. The callbacks an [extension](#extensions-objects-of-your-own) schedules (the frames of `gfx.onFrame`, for one) wait in the same way and count toward those 64; while events and such callbacks both wait, they take turns, so a busy frame loop cannot hold back keys and clicks. Each run calls its handlers on its own thread, one at a time. An event is not a method call: it does not count toward `maxCalls` and is not listed in `calls` - but what a handler calls is, and so is `input.mouse()`. An exception thrown in a handler ends the run with phase `runtime`.

Each run's queue holds at most `Limits::maxQueuedEvents` events (default 1000; 0 = no limit, see [Limits](#limits)). When an application posts faster than the script handles, the oldest waiting event is dropped to make room for the new one. The first drop writes one line to the run's log, `events dropped: queue full (1000)`, and the result carries the count: `"events_dropped": 12`. Mouse moves are not posted (below), so that ordinary input does not fill the queue.

### How long a run with handlers lives

A run with a handler goes on after its body returned, like a run with timers: until `off()` removed its last handler and no timer is left, until it is stopped, or until it reaches its time limit. With the engine's `Limits` that is 5 minutes - an input script meant to run as long as the application does needs the opt-in of [Running until stopped](#running-until-stopped):

```cpp
ScriptHost::ScriptEngine::RunOptions options;
options.name = "Input logger";
options.timeoutMs = 0;          // no time limit
options.maxCalls  = 0;          // no call limit
engine->run(code, options, done);
```

A script ends itself by removing its handlers:

```js
on('key', event => {
    if (event.key === 'Escape') {
        off('key');
        off('mouse');           // no handler and no timer left: the run ends
    }
});
```

### Keys and the mouse: forwardInput

ScriptHostWidgets turns a widget's keys and mouse clicks into events:

```cpp
#include "ScriptHostWidgets.h"

gameView->setFocusPolicy(Qt::StrongFocus);                     // to get keys at all
QObject* forwarder = ScriptHost::forwardInput(engine, gameView);
...
delete forwarder;                                              // to stop forwarding
```

It posts `"key"` and `"mouse"` events and registers the method `input.mouse()`. The forwarder is a child of the widget and goes with it; deleting it stops forwarding, and `input.mouse()` goes with the latest forwarder. The widget sees every event as before: nothing is consumed.

**Focus:** key events arrive only while the forwarded widget - or a child of it that does not handle the key - has the keyboard focus. A plain `QWidget` or `QLabel` takes no focus until `setFocusPolicy(Qt::StrongFocus)`; then a click gives it. In the same way, clicks arrive when they hit the widget itself or a child that does not handle them. A scroll area (item views, text edits, `QGraphicsView`) receives its clicks on its `viewport()`, and an editable view uses the keys itself: a plain widget of its own for the input works best.

`"key"`:

| Field | |
|---|---|
| `type` | `"down"` or `"up"` |
| `key` | The name of the `Qt::Key` without `Key_`: `"A"`, `"1"`, `"Space"`, `"Escape"`, `"Left"`, `"PageUp"`, `"F1"`. Empty for a key Qt has no name for |
| `code` | The `Qt::Key` value, a number |
| `text` | The text the key types: `"a"`, `"A"` with shift; empty for keys that type nothing |
| `modifiers` | The modifiers held, of `"shift"`, `"control"`, `"alt"`, `"meta"` |
| `repeat` | `true` for the repeats of a held key |

`"mouse"`:

| Field | |
|---|---|
| `type` | `"down"`, `"up"`, `"dblclick"` or `"wheel"`. Moves are not posted |
| `button` | The button of this event: `"left"`, `"right"`, `"middle"`, `"back"`, `"forward"`, `"other"`; `"none"` for the wheel |
| `buttons` | The buttons held, by the same names |
| `x`, `y` | Where, in the forwarded widget's coordinates |
| `modifiers` | As for keys |
| `deltaX`, `deltaY` | Wheel only: the angle turned, 120 per notch; `deltaY > 0` away from the user |

A double click comes as `down`, `up`, `dblclick`, `up`.

`input.mouse()` returns where the pointer is now, for scripts that need the position between clicks:

| Field | |
|---|---|
| `x`, `y` | In the forwarded widget's coordinates (of the latest `forwardInput()` call) |
| `inside` | The pointer is over the widget |
| `buttons` | The buttons held |

A run sees the methods there were when it started, so `forwardInput()` comes before the run; while there is no forwarder, `check()` reports `no method 'input.mouse'`.

### Requested runs get no events

A requested run - from `requestRun()`, or any run with `RunOptions::external` - comes from another program. If it could subscribe to `"key"`, it could log everything the user types, passwords included. So **requested runs get no events by default**: their `on(name, ...)` throws, and the run ends with phase `runtime`:

```
the application does not let requested scripts receive 'key' events
```

The application allows event names one by one:

```cpp
engine->allowEventForRequestedRuns("sensor");   // its own, harmless events
engine->allowEventForRequestedRuns("mouse");    // clicks, and input.mouse()
// engine->allowEventForRequestedRuns("key");   // only for a requester trusted with the keyboard
```

`"mouse"` also gates `input.mouse()`: without it, the call throws `the application does not let requested scripts read the mouse`. A run keeps what was allowed when it started; `allowEventForRequestedRuns(name, false)` takes it back for later runs. `isEventAllowedForRequestedRuns(name)` asks.

Method handlers can apply the same rule: `callingRun()` is the run whose call is being handled (0 outside a handler; an asynchronous handler sees it only until it returns), and `isExternalRun(callingRun())` tells whether it is a requested one.

### Events of your own, without widgets

An engine-only application posts what it has. A `QCoreApplication` that reads a sensor:

```cpp
QTimer* poll = new QTimer(&app);
QObject::connect(poll, &QTimer::timeout, [&]() {
    engine.postEvent("sensor", QJsonObject{{"temperature", sensor.read()}});
});
poll->start(1000);
```

```js
let high = 0;
on('sensor', data => {
    if (data.temperature > 30 && ++high === 3)
        print('too hot for three readings');
});
return 'watching';
```

## Drawing over the window: the canvas

ScriptHostWidgets gives scripts a drawing surface over a widget: to point the user at a field, mark what a script found, draw a chart, or run a small game. The application attaches it once:

```cpp
#include "ScriptHostWidgets.h"

ScriptHost::ScriptCanvas* canvas = ScriptHost::attachCanvas(engine, mainWindow);
```

From then on, new runs have the global `gfx`:

```js
gfx.rect(10, 10, 80, 40, {stroke: 'red', width: 2});
gfx.text(50, 30, 'Hello', {fill: '#202020', align: 'center', valign: 'middle'});
gfx.present();
setTimeout(() => {}, 5000);     // the drawing stays while the run does
```

The canvas is a transparent child of the target widget. It covers the target, follows its size, stays above the target's other children, and takes no clicks and no focus: the window works as before. It goes with the target; deleting it detaches it - `gfx` is gone from new runs, and runs that have it draw into nothing. Coordinates are pixels of the target, (0, 0) top left; `gfx.width` and `gfx.height` are its size.

### Frames and layers

`gfx` is immediate mode. The drawing functions record into a frame on the script's own thread - no method calls, no JSON, nothing counted toward `maxCalls`. `present()` hands the frame to the canvas without waiting and starts an empty one.

- **A presented frame stays** on screen until the run presents the next. A highlight drawn once costs nothing after that. A frame that was never presented is never shown.
- **The newest frame wins.** A script that presents faster than the screen can show loses the frames in between, not time.
- **Each run has a layer**, painted in the order the runs started (a later run on top). The layer goes when the run ends - finished, stopped, failed or at a limit. To keep a drawing up, the script stays alive: a timer, an `on()` handler or `gfx.onFrame`.
- **The transform and `gfx.style()` stay** from one frame to the next, like a canvas in a browser. `gfx.clear()` empties the frame being drawn and keeps them; `save()` / `restore()` or `resetTransform()` put them back.
- Only what changed is repainted: the area of the run's old and new frame.

On a resize every run with a handler gets the event `"resize"` `{width, height}`.

### The functions

`style` is always optional; what it leaves out comes from `gfx.style()`. The full list with types is in `apiDefinition("gfx")`.

| Function | |
|---|---|
| `clear()` | Empties the frame being drawn; what is on screen stays until `present()` |
| `style(style?)` | The defaults for what follows; without an argument the built-in ones. It replaces the defaults; it does not add to them |
| `save()`, `restore()` | Keeps the transform and `gfx.style()` until `restore()` |
| `translate(x, y)`, `rotate(radians)`, `scale(x, y?)`, `resetTransform()` | The transform of what follows; `rotate` turns clockwise |
| `rect(x, y, w, h, style?)`, `roundedRect(x, y, w, h, radius, style?)` | |
| `circle(cx, cy, r, style?)`, `ellipse(cx, cy, rx, ry, style?)` | |
| `line(x1, y1, x2, y2, style?)` | |
| `arrow(x1, y1, x2, y2, style?)` | The head at (x2, y2), in the stroke colour; `head` sets its length |
| `polyline(points, style?)`, `polygon(points, style?)` | `points`: `[x0, y0, x1, y1, ...]`; a polygon is closed |
| `text(x, y, text, style?)` | One line, at (x, y) placed by `align` / `valign`. Its colour is `fill`, else `stroke` |
| `rects(values, style?)`, `circles(values, style?)`, `lines(values, style?)` | Many of one style: `[x, y, w, h, ...]`, `[cx, cy, r, ...]`, `[x1, y1, x2, y2, ...]` |
| `path()` | A path to build: `moveTo`, `lineTo`, `quadTo`, `cubicTo`, `arc`, `close`, then `draw(style?)` |
| `linearGradient(x0, y0, x1, y1, stops)`, `radialGradient(cx, cy, r, stops, fx?, fy?)` | A gradient for a style's `fill`. `stops`: `[[0, 'red'], [1, 'blue']]` |
| `image(name, x, y, w?, h?, source?, style?)`, `imageSize(name)` | An image of the application; see [Images](#images) |
| `widget(objectName)` | A handle to draw on a widget; see [Shapes on widgets](#shapes-on-widgets) |
| `widgetRect(objectName)` | Where the widget is now: `{x, y, width, height}`, or `null` when it is missing or hidden |
| `onFrame(fn)`, `offFrame()` | `fn(dt)` once per screen refresh; see [The frame loop](#the-frame-loop) |
| `measureText(text, font?)` | `{width, height, ascent, descent}`; without a font, `gfx.style()`'s |
| `pointInRect`, `pointInCircle`, `rectsOverlap`, `circlesOverlap`, `ease`, `tween` | For games; see [Helpers](#helpers) |
| `draw.<name>(...)` | A drawing function of the application; see [Drawing functions of the application](#drawing-functions-of-the-application) |
| `present()` | Shows the frame drawn since the last `present()` and starts an empty one. Does not wait |
| `width`, `height` | The canvas size in pixels |

A wrong argument throws in the script, with the function's name: `gfx.rect: expects numbers`, `gfx.line: the style's dash is one of solid, dash, dot, dashDot`. Uncaught, the run ends with phase `runtime`.

### Styles

| Key | |
|---|---|
| `stroke` | The outline colour: `"#rrggbb"`, `"#aarrggbb"`, a CSS name (`"red"`) or `"none"`. Default black |
| `fill` | A colour as for `stroke`, or a gradient. Default none |
| `width` | Outline width; 0: no outline. Default 1 |
| `dash` | `"solid"` (default), `"dash"`, `"dot"`, `"dashDot"` |
| `opacity` | 0..1 |
| `head` | Arrow head length; default 4 x `width`, at least 8 |
| `padding` | Space around a widget the shape is drawn on |
| `font` | `{family, size, bold, italic}`; `size` in pixels, default 14 |
| `align` | Where text's x is: `"left"` (default), `"center"`, `"right"` |
| `valign` | Where text's y is: `"top"` (default), `"middle"`, `"bottom"`, `"baseline"` |

An unknown key throws (`gfx.rect: unknown style 'colour'`).

### Transforms and paths

```js
gfx.save();
gfx.translate(100, 50);
gfx.rotate(Math.PI / 4);                        // radians, clockwise
gfx.rect(-20, -10, 40, 20, {fill: 'teal', stroke: 'none'});
gfx.restore();

gfx.path()
   .moveTo(10, 100).lineTo(60, 100).quadTo(80, 60, 100, 100)
   .arc(140, 100, 20, Math.PI, 0)               // start and end angle, clockwise from 3 o'clock
   .close()
   .draw({fill: '#ffe0a0', stroke: '#a06000'});
```

`arc(cx, cy, r, start, end, counterclockwise?)` works as in a browser canvas: a line joins it to the current point. A path can be drawn again.

### Gradients

```js
const sky = gfx.linearGradient(0, 0, 0, gfx.height, [[0, '#a0c8ff'], [1, 'white']]);
gfx.rect(0, 0, gfx.width, gfx.height, {fill: sky, stroke: 'none'});
gfx.circle(80, 80, 30, {fill: gfx.radialGradient(80, 80, 30, [[0, 'yellow'], [1, 'orange']])});
```

A gradient is frozen when it is made. Make it once and use it in many frames; a new one each frame is read again each frame.

### Images

Scripts read no files. The application adds the images they may draw, by name:

```cpp
canvas->addImage("ship", QImage(":/sprites/ship.png"));
canvas->addImage("tiles", QImage(":/sprites/tiles.png"));
```

```js
gfx.image('ship', x, y);                                      // at its size
gfx.image('ship', x, y, 64, 32);                              // scaled
gfx.image('tiles', x, y, 16, 16, {sx: 32, sy: 0, sw: 16, sh: 16});   // a sprite of a sheet
const size = gfx.imageSize('ship');                           // {width, height}, or null
```

An unknown name throws (`gfx.image: no image 'x'`). The style's `opacity` applies. `removeImage(name)` takes one back; `images()` lists them. Replacing or removing one repaints the canvas.

### Shapes on widgets

A shape can be drawn on a widget of the target instead of at numbers. It then follows the widget:

```js
const field = gfx.widget('customerName');                     // a widget's objectName
gfx.roundedRect(field, 6, {stroke: 'red', width: 3, padding: 4});
gfx.arrow(400, 40, field, {stroke: 'red', width: 3});
gfx.text(field, 'required', {fill: 'red', align: 'right', valign: 'bottom'});
gfx.present();
```

`rect`, `roundedRect`, `ellipse` and `text` take a widget in place of the numbers; `arrow` takes one for either end or both (`arrow(x1, y1, w)`, `arrow(w, x2, y2)`, `arrow(w1, w2)`), and ends on the widget's edge. Text is placed inside the widget by `align` / `valign`. `padding` widens the widget's rectangle.

The rules:

- The widget is the first descendant of the target with that `objectName`. It is looked up on the GUI thread, when the frame is shown.
- Where it is, is worked out on the GUI thread, from where the widget is now: the shape follows moves, resizes, splitters and scrolling without a new `present()`. Only the layers that have a shape on that widget are repainted.
- While the widget is hidden or missing, the shape is not drawn; it comes back when the widget does. A widget that first appears after the frame was presented shows at the run's next `present()`.
- Shapes on widgets ignore the transform.
- `gfx.widgetRect(name)` returns the rectangle at the moment of the call. It asks the GUI thread and waits for the answer: call it once, or on `"resize"`, not for every frame.

### The frame loop

```js
let x = 0;
gfx.onFrame(dt => {                 // dt: milliseconds since the last frame
    x = (x + 0.2 * dt) % gfx.width;
    gfx.clear();
    gfx.circle(x, 50, 10, {fill: 'red', stroke: 'none'});
    gfx.present();
});
```

`fn(dt)` is called once per screen refresh (`QScreen::refreshRate`, 60 when unknown), from one timer of the canvas for every run, which runs only while some run has a frame handler. A handler slower than a refresh skips frames - none pile up - so movement goes by `dt`, not by counting frames. Frames run between events and timers like an event handler, and take turns with key and mouse events, so input still arrives.

A frame handler keeps the run alive until `gfx.offFrame()`; then, with no timer and no handler left, the run ends. An exception in the handler ends the run (phase `runtime`). Mind the time limit: with the engine's `Limits` a run ends after 5 minutes; a game the application starts sets `RunOptions::timeoutMs = 0` (see [Running until stopped](#running-until-stopped)).

### Helpers

For games, all in the script, without a round trip to the application:

| | |
|---|---|
| `pointInRect(px, py, x, y, w, h)`, `pointInCircle(px, py, cx, cy, r)` | Hit tests |
| `rectsOverlap(x1, y1, w1, h1, x2, y2, w2, h2)`, `circlesOverlap(x1, y1, r1, x2, y2, r2)` | Collisions |
| `measureText(text, font?)` | `{width, height, ascent, descent}` of one line |
| `ease.linear`, `inQuad`, `outQuad`, `inOutQuad`, `inCubic`, `outCubic`, `inOutCubic`, `outBack`, `outBounce` | 0..1 to 0..1 |
| `tween(from, to, ms, easing?)` | `{value, done, elapsed, step(dt), reset()}`; `easing` a name of `gfx.ease` or a function; `step(dt)` moves it on and returns the value |

```js
const fade = gfx.tween(1, 0, 500, 'outQuad');
gfx.onFrame(dt => {
    const opacity = fade.step(dt);
    ...
    if (fade.done) gfx.offFrame();
});
```

### Drawing functions of the application

The application can add drawing functions of its own, painted with `QPainter`, for what scripts would otherwise build from many shapes:

```cpp
canvas->addDrawFunction("star",
    [](QPainter& painter, const QVariantList& args) {          // at paint time
        painter.drawPath(starPath(args[0].toDouble(), args[1].toDouble(), args[2].toDouble()));
    },
    "/** A star around (x, y). */\nfunction star(x: number, y: number, r: number): void;",
    [](const QVariantList& args) {                             // where it paints
        const double r = args[2].toDouble();
        return QRectF(args[0].toDouble() - r, args[1].toDouble() - r, 2 * r, 2 * r);
    });
```

```js
gfx.style({fill: 'gold', stroke: '#a07000'});
gfx.draw.star(100, 60, 20);
```

- The arguments come as they were passed: numbers, strings, arrays as `QVariantList`, objects as `QVariantMap`.
- The painter has the call's transform, pen, brush and opacity set, from `gfx.style()`. What the function changes on the painter is undone after it.
- The bounds function says where a call paints, in its own coordinates, so only that is repainted. **Without it, every frame with such a call repaints the whole canvas.**
- The declaration appears in `apiDefinition()`, inside `namespace draw`; an empty one declares `function name(...args: any[]): void;`. `check()` knows the function at once (`methodsChanged` is emitted) and catches typos below `gfx.draw.` with a "did you mean".
- **A run has the functions there were when it started**: one added later reaches the next runs.
- `removeDrawFunction(name)` takes one back; `drawFunctions()` lists them.

### Requested runs and dry runs

A requested run gets no `gfx` - and no `"resize"` event - unless the application allows it:

```cpp
canvas->setAllowedForRequestedRuns(true);
```

Until then `check(code, true)` reports `the application does not let requested scripts use 'gfx'` and `gfx` is undefined in such a run. A dry run draws as any other run: drawing changes none of the application's data.

### Speed

- **Use the batch forms** (`rects`, `circles`, `lines`) for many shapes of one style: one call for all of them.
- **Use one style object or `gfx.style()`** for many shapes, not a new object per shape. A style object is read on every call; `gfx.style()` once.
- **Present once per frame.** Each `present()` hands a frame over; the canvas shows only the newest.
- **Reuse gradients**, and give drawing functions a bounds function.
- **Do not call `widgetRect` per frame**: it waits for the GUI thread.

What a frame of moving 4 x 4 rectangles costs. *Record*: the script drawing the frame, on its thread, `present()` included. *Paint*: the canvas painting the frame on the GUI thread (into an image, as Qt's raster backing store does). Recording and painting run side by side, on two threads.

| ms per frame | 2,000 shapes: record | paint | 5,000 shapes: record | paint |
|---|---|---|---|---|
| `rect(x, y, w, h, style)`, a style object per call | 1.6 | 0.2 | 6.7 | 0.7 |
| `rect(x, y, w, h)` after `gfx.style()` | 1.3 | 0.2 | 4.1 | 0.7 |
| `rects([...])`, the array built each frame | 0.4 | 0.4 | 1.4 | 0.7 |
| `draw.dot(x, y)`, a drawing function without bounds | 3.6 | 1.1 | 9.2 | 2.7 |

Release build (x64-Release, MSVC 2022, Qt 5.15.2), Windows 10, Intel Core i9-13900K. 2,000 moving shapes take under 2 ms a frame - far within the 16.7 ms of a 60 Hz screen. A Debug build is 10 to 20 times slower at recording (5,000 shapes: 54 / 43 / 18 / 74 ms) and about 5 times at painting.

The numbers come from the test `TST_ScriptCanvas::testBenchmark` (it prints them and asserts nothing): `installation/bin/ScriptHostTest.exe`.

### Known limits

- **One canvas per engine.** A second canvas attached to the same engine takes `gfx` over for new runs.
- **Text is one line**: no wrapping, no `maxWidth`. `measureText` tells a script how wide a line is.
- **Native child widgets**: the canvas is a sibling widget, and cannot paint over a native child (a `QOpenGLWidget` in native mode, an embedded native window). Over such a widget it stays empty.
- **The definition budget.** The declaration of `gfx` is longer than the default `setApiBudgetChars()` (6000), so `apiDefinition()` without a query shows `gfx` as one index line. A program that writes scripts (an assistant, a remote client) should ask for `apiDefinition("gfx")`, which comes whole, or the application raises the budget.

## Extensions: objects of your own

A method is called with JSON on the GUI thread, counted, recorded and checked against its definition: right for working on the application's data. An extension is an object every run gets under a global name, called directly on the script's thread: for what is called thousands of times a second, or keeps state per run - a drawing surface, a device, a game API. `gfx` is one.

```cpp
#include "ScriptHost.h"

// The object a run gets: lives on the run's thread.
class Counter : public QObject
{
    Q_OBJECT
public:
    explicit Counter(const ScriptHost::ScriptRunContext& context) : m_context(context) {}
    Q_INVOKABLE int next() { return ++m_n; }
    Q_INVOKABLE void reset(int to)
    {
        if (to < 0)
            m_context.throwError("counter.reset: the value must not be negative");
        else
            m_n = to;
    }
private:
    ScriptHost::ScriptRunContext m_context;
    int m_n = 0;
};

class CounterExtension : public ScriptHost::ScriptExtension
{
public:
    CounterExtension() : ScriptExtension("counter") {}
    QStringList members() const override { return {"next", "reset"}; }     // for check()
    QString declaration() const override                                   // for apiDefinition()
    {
        return "declare namespace counter {\n"
               "  /** The next number, from 1. */\n  function next(): number;\n"
               "  function reset(to: number): void;\n}\n";
    }
    QObject* createRunObject(const ScriptHost::ScriptRunContext& context) override
    {
        return new Counter(context);
    }
};

engine->addExtension(QSharedPointer<CounterExtension>::create());
```

```js
log(counter.next(), counter.next());     // 1 2
```

The script calls the object's `Q_INVOKABLE` functions and reads its properties. Unlike a method's, such a call is not counted toward `maxCalls`, not recorded in `calls`, not stopped at by the debugger, and not checked for being read-only.

### Threads and ownership

| | Runs on |
|---|---|
| `members()`, `declaration()` | The GUI thread, each time `check()` or `apiDefinition()` runs. They may change; emit `changed()` when they do, and the engine emits `methodsChanged()` |
| `createRunObject(context)`, `glue()` | The run's thread, when the run starts, before the body. Runs start side by side: both must be safe against each other and against the GUI thread |
| The per-run object | The run's thread. The run owns it and deletes it there when the script is over, while the run's JavaScript engine still exists. Return it without a parent |
| `runEnded(id)` | The GUI thread, once per run that had the extension, on every end, after the per-run object is gone and before `runFinished` |

The engine and every run that has the extension hold a reference to it: `removeExtension(name)` while runs go on is safe; they keep it, new runs go without it.

`glue()` may return a JavaScript function that takes the per-run object and returns what the script sees under the name - for defaults, overloads or plain JavaScript helpers:

```js
(function (native) { return { next: function () { return native.next(); }, twice: function () { return native.next() * 2; } }; })
```

### The run context

The `ScriptRunContext` passed to `createRunObject` is the way back into the run. Keep a copy in the per-run object; a copy stays safe after the run ended.

| `ScriptRunContext` | |
|---|---|
| `runId()`, `isRequested()`, `isDryRun()`, `isActive()` | Any thread. `isActive()` is false once the script is over |
| `throwError(message)` | The run's thread, in a call from the script: throws a JavaScript `Error` once the call returns |
| `log(text)` | The run's thread: a line in the run's log |
| `schedule(key, callback)` | Any thread: runs `callback` on the run's thread, once the body has returned, between timers and events like an event handler. False when the run is over |
| `call(fn, args)` | The run's thread, in a scheduled callback: calls a script function; an exception it throws ends the run as `runtime` |
| `setKeepAlive(key, keep)` | Any thread: while any key is held, the run goes on after its body, like a run with a timer |

**Coalescing.** With a key, at most one callback per key waits: a newer one replaces the waiting one. A tick from the application that the script cannot keep up with is then skipped, not queued - this is how `gfx.onFrame` skips frames. An empty key queues every callback. A callback still waiting when the run ends is dropped on the run's thread, before the per-run object is deleted, so it may hold a plain pointer to that object. Do not capture `QJSValue`s in a callback: keep them in the per-run object.

```cpp
// GUI side: a tick for every run that asked for ticks
for (const Ticked& run : m_runs)
    run.context.schedule("tick", [object = run.object]() { object->tick(); });

// the per-run object, on the run's thread
void Ticker::tick() { m_context.call(m_handler, {QJSValue(m_count++)}); }
```

**Keep-alive.** A script that registers a handler with an extension (`gfx.onFrame(fn)`) expects to go on after its body: the per-run object calls `setKeepAlive("handler", true)`, and `false` when the handler is removed. A pending scheduled callback keeps the run alive by itself until it ran.

### Switches, requested runs and dry runs

```cpp
engine->setExtensionEnabled("counter", false);              // durable, by name; default enabled
engine->allowExtensionForRequestedRuns("counter");          // requested runs get none by default
```

A disabled extension is not in new runs nor in `apiDefinition()`, and `check()` rejects a use of it. A requested run gets only the extensions allowed for it; `check(code, true)` checks as such a run. `extensions()` lists the names added.

`check()` validates every `name.member` against `members()` with a "did you mean"; nested members are given dotted (`"draw.star"`, `"ease.outQuad"`). `check()` and the syntax check never create a per-run object. A dry run does, with `context.isDryRun()` true: the per-run object must then leave the application's data alone - drawing on an overlay, or answering, is fine.

An extension's name is a global of its own: `addExtension()` refuses, with a warning, a name that is not an identifier or that a method has or starts with (`db` while `db.read` is registered). Keep extension names and the name spaces of methods apart. Adding a name again replaces the extension; runs already going keep the one they started with.

## Requested runs

`requestRun()` is for runs that somebody other than the user asks for: a plugin, a remote connection, an assistant.

```cpp
engine->setRequesterName("the remote client");

ScriptHost::ScriptEngine::Request request;
request.code = code;            // or request.name = "Archive outdated objects", a saved script
request.args = QJsonObject{{"before", 2015}};
engine->requestRun(request,
    [](const QJsonObject& result) { sendBack(result); },
    [connection]() { return connection->isClosed(); });   // optional: is anybody still waiting?
```

What happens, in order:

1. Requested runs switched off (`setEnabled(false)`): `{"status":"error","phase":"unavailable","message":"running scripts is switched off"}`.
2. A name and no code: the script is looked up in the library, ignoring case. `there is no saved script '...'` (phase `check`) when there is none; `there is no script to run` when neither code nor name is given.
3. `Limits::maxConcurrentRuns` scripts running already: `the limit of 8 scripts running at once is reached; wait for one to finish` (phase `unavailable`). Requests are not queued.
4. The script is checked (syntax, methods). A mistake comes back at once; the user is not bothered.
5. Approval, where the kind of script needs it (below). Declined: `{"status":"error","phase":"declined","message":"the user declined the script","reason":"..."}`. No approval handler set: phase `approval`.
6. If an `abandoned` function was given, it is asked now. True: nobody waits for the result any more, and the run is dropped without one.
7. The script runs, with the engine's `Limits` - a request cannot choose others - and gets only the events the application allows (see [Requested runs get no events](#requested-runs-get-no-events)). `done` always gets a result, except for an abandoned request.

The run is recorded with `external` set: `isExternalRun(id)` is true while it runs, `runStarted` has `external == true`, and the `RunRecord` in `runs()` has `external` set.

### The requester name

`setRequesterName(name)` says who asks, in the words the user reads: "Allow the remote client to run scripts", "Requested by the remote client: run this script?", "Started: ... by the remote client" in the Runs list. Write it as it reads in the middle of a sentence, with its article. Default: "another program".

### Approval

Whether the user sees a requested script, and agrees, before it runs, is decided per kind of script. Every requested run is of exactly one kind:

| `ScriptKind` | A run is of this kind when |
|---|---|
| `NewReading` | the code came with the request and only calls read-only methods |
| `NewChanging` | the code came with the request and calls a method that is not read-only, or picks methods by computed names |
| `SavedReading`, `SavedChanging` | the same two, for a script of the library run by name that the user saved or the application ships (origin `user` or `application`). A script the requester saved itself counts as new |
| `DryRun` | it is a dry run, of whatever script: nothing is changed |

`setApprovalRequired(kind, bool)` switches one kind; by default none needs approval. `setApproval(...)` sets all of them at once:

| `setApproval(...)` | Needs approval |
|---|---|
| `Approval::Never` | no kind |
| `Approval::ChangingScripts` | `NewChanging` and `SavedChanging` |
| `Approval::Always` | every kind |

A script that runs unasked as a reading one is held to that while changing scripts of the same origin need approval: its first call to a changing method stops it (phase `approval`).

The user decides the same in the Scripts window, with one check box per kind under "Ask before running". `setApprovalUserConfigurable(kind, false)` takes one kind out of the user's hands, `setApprovalUserConfigurable(false)` all of them.

A run that needs approval is decided by the engine's approval handler. There is none by default - the engine has no window of its own - and without one such a request fails at once:

```json
{"status":"error","phase":"approval","message":"the script needs the user's approval, and the application set no approval handler to ask for it"}
```

With ScriptHostWidgets, one call installs a dialog:

```cpp
#include "ScriptHostWidgets.h"

ScriptHost::useApprovalDialog(engine, mainWindow);   // the window it opens over; may be deleted first
```

The dialog shows who asks, the code, the arguments and the methods the script uses, marked as reading or changing. The user can run it, test-run it (a dry run, with the result shown in the dialog) or decline with a reason that goes back to the requester. "Do not ask again for this script in this session" remembers the exact code. `useApprovalDialog` sets the engine's approval handler, so it replaces one set before.

`setApprovalHandler(handler)` sets the application's own handler instead; `nullptr` removes it. The handler gets an `ApprovalRequest` and may call `decide` later, from a window of its own:

| `ApprovalRequest` | |
|---|---|
| `kind` | The `ScriptKind` of the run |
| `name`, `code`, `args` | The saved script's name (empty for code that came with the request), the code, the arguments |
| `dryRun` | It is a dry run |
| `methods`, `changingMethods` | The methods the script uses, and those of them that are not read-only |
| `dynamic` | The script may call more than its source shows |

| `ApprovalDecision` | |
|---|---|
| `approved` | Run it |
| `reason` | Told to the requester when declined |
| `rememberForSession` | Do not ask again for this exact code |

Approval applies to `requestRun()` only. `run()` never asks.

## Saved scripts

```cpp
auto* library = new ScriptHost::ScriptLibrary(this);
library->setDirectory(appData + "/scripts");          // writable: the user's scripts
library->addReadOnlyDirectory(":/scripts");           // shipped with the application
engine->setLibrary(library);
```

One `.js` file per script, with a header of comment lines:

```js
// @name Archive outdated objects
// @description Sets the status of every object older than `before` to archived.
// @param before number Year
// @origin user

for (const id of db.listObjects()) { ... }
```

`script(name)` finds a script by name, ignoring case. Scripts in a read-only directory cannot be changed or deleted; the Scripts window offers Duplicate for them. With a library, `requestRun()` accepts a name instead of code.

## Switches

| What | By code | Locked against the user |
|---|---|---|
| Requested runs | `setEnabled(bool)` | `setEnabledUserConfigurable(false)` |
| What needs approval | `setApprovalRequired(kind, bool)`, `setApproval(preset)` | `setApprovalUserConfigurable(kind, false)`, or `(false)` for every kind |
| Each method | `setMethodEnabled`, `setMethodGroupEnabled` | `setMethodUserConfigurable`, `setMethodGroupUserConfigurable` |
| Editing saved scripts in the Scripts window | `setUserMayEditScripts(bool)` | |
| Runner and debugger in the editor | `setDebuggerEnabled(bool)` | |

A switch that is locked is shown disabled in the Scripts window. A settings window of the application's own changes the switches through `setEnabledByUser(bool)`, `setApprovalRequiredByUser(kind, bool)` and `setMethodEnabledByUser(name, bool)`: they return false, and change nothing, for what is locked.

`userSettings()` returns the user's choices as JSON for the application to store; `applyUserSettings()` puts them back and skips what is locked:

```cpp
engine->applyUserSettings(QJsonDocument::fromJson(settings.value("scripting").toByteArray()).object());
...
settings.setValue("scripting", QJsonDocument(engine->userSettings()).toJson(QJsonDocument::Compact));
```

The stored object holds only what the user may change:

```json
{"enabled": true,
 "approval": {"new_reading": false, "new_changing": true, "saved_reading": false, "saved_changing": true, "dry_run": false},
 "methods": {"db.readObject": true, "db.setProperty": false}}
```

The names of the kinds are those of `ScriptEngine::scriptKindKey(kind)`.

## The Scripts window and the editor

Both are in ScriptHostWidgets (`#include "ScriptHostWidgets.h"`). `ScriptHost::ScriptsWidget` is a plain widget, for a settings dialog or a window of its own. `setScripting(engine)` connects it. Changes wait for `apply()` unless `setApplyImmediately(true)`; `changed()` is emitted when the user changed something, which is the moment to store `userSettings()`.

- **Switches**: "Allow (requester) to run scripts" and, under "Ask before running", one check box per kind of script.
- **Scripts**: the saved scripts. New, Edit, Duplicate, Delete, Open folder.
- **Methods**: every method with a check box; locked ones are greyed out.
- **Runs**: the runs going on now, each with its start, elapsed time and a Stop button - select one to see its run id (`Run #7`), arguments, code and live log; when it ends it stays shown with its result - and the scripts run in this session, with who started them, code, arguments, result and log.

The editor (`ScriptHost::ScriptEditorDialog`) is a window of its own: name, description, parameters, the code, and beside it the reference of the methods a script can call, with search. A double click inserts a call. It can be opened without the Scripts window:

```cpp
auto* editor = new ScriptHost::ScriptEditorDialog(engine, this);
editor->setAttribute(Qt::WA_DeleteOnClose);
editor->setScript(script);   // a ScriptLibrary::Script; a blank one for a new script
editor->show();
```

### Running and stepping

Check, Run and Dry run work on the code as it stands in the editor, saved or not. The editor's run goes on beside other runs - another editor's, the application's, a requested one - and its Stop button stops only its own.

Debug starts the script and stops before its first statement. Only one run can be in the debugger at a time; Debug while another is there says so in the status line. The current line is marked, and the Variables table shows what the script's variables hold there; a value that changed since the stop before is bold.

| Button | |
|---|---|
| Step | Runs the statement and stops before the next one, also inside functions of the script |
| Next call | Runs on to the next method call and shows it with its arguments; the Calls table gets the value it returned |
| Continue | Runs on to the next breakpoint (set by a click in the line-number margin) or to `pause("label")` in the script |

The engine has no debugger interface, so for a debug run the script is rewritten: a hidden call in front of every statement that starts a line. Stepping is therefore by line - a second statement on a line is not stopped at - and a variable appears from the line after its declaration on. A script whose statements the rewriting cannot tell apart is stepped call by call instead. Normal runs are not rewritten.

The same is available from code: `RunOptions::debug`, `statements`, `stepFromStart`, `breakpoints`, then `stepStatement()`, `stepRun()` (to the next call), `continueRun()` and the signals `runPaused(id, line, method, arguments, variables)` and `runCallFinished`. These act on the one run in the debugger; nothing happens without one. `variables` holds each visible variable as the text a debugger shows. Without `statements` a debug run is not rewritten and stops at calls only.

## More of the interface

What the sections above did not need.

| `ScriptEngine` | |
|---|---|
| `methods()`, `hasMethod(name)` | The registered methods |
| `check(code)` returns `Check` | `ok`; when not: `phase` (`syntax` or `check`), `line`, `message`, `didYouMean`. Always: `methods` and `changingMethods` the script uses, `dynamic` (it picks methods by computed names). `toJson()` gives the error as a result object |
| `RunOptions::external` | Marks the run as requested, in the record and in `runStarted`, and it gets only the events `allowEventForRequestedRuns()` allows. `requestRun()` sets it |
| `RunOptions::readOnlyOnly` | A call to a method that is not read-only stops the script (phase `approval`). `requestRun()` sets it for a script that runs unasked as a reading one |
| `RunOptions::timeoutMs`, `maxCalls` | This run's limits: -1 (default) the engine's `Limits`, 0 none. See [Running until stopped](#running-until-stopped) |
| `setBreakpoints(lines)` | Changes the breakpoints of the run in the debugger |
| `runs()`, `clearRuns()` | The finished runs of the session (the latest 100), oldest first, as `RunRecord`: `id`, `started`, `name`, `code`, `args`, `result`, `dryRun`, `external` |
| `setOutput(device)`, `output()` | Where log lines are written besides `runLog`. See [Output](#output) |
| `setApprovalHandler(handler)` | Decides requested runs that need approval. See [Approval](#approval) |
| `postEvent(name, data)`, `postEvent(id, name, data)` | An event for every run with a handler for `name`, or for the one run. See [Events](#events-keys-mouse-and-your-own) |
| `allowEventForRequestedRuns(name, allow)`, `isEventAllowedForRequestedRuns(name)` | Which events requested runs get. See [Requested runs get no events](#requested-runs-get-no-events) |
| `callingRun()` | In a method's handler: the run that called it; 0 elsewhere |
| `addExtension(extension)`, `removeExtension(name)`, `extensions()` | Objects of the application in every new run. See [Extensions](#extensions-objects-of-your-own) |
| `setExtensionEnabled(name, bool)`, `allowExtensionForRequestedRuns(name, allow)` | Which runs get an extension |

| `ScriptMethod` | |
|---|---|
| `signature()` | `db.readObject(id: string): { id: string, name: string }` |
| `declaration(indent, keyword)` | The TypeScript declaration with its comment, as in the definition |
| `nameSpace()`, `functionName()` | `db` and `readObject` of `db.readObject` |
| `parameterSchema()` | The parameters as a JSON schema |
| `static isValidName(name)` | An identifier, or two joined by a dot |
| `bindArguments(arguments, named, &error)` | The arguments of a call as the named input a handler gets, checked against the definition |

| `ScriptLibrary` | |
|---|---|
| `scripts()`, `script(name)`, `contains(name)` | The scripts sorted by name, as `Script`: `name`, `description`, `parameters` (`name`, `type`, `description`), `code`, `origin`, `filePath`, `readOnly` |
| `save(script, &error)`, `remove(name, &error)` | Into and out of the writable directory. False with the reason when that is not possible |
| `changed()` | Signal: a script was saved or removed |
| `static toFileText(script)`, `fromFileText(text, fallbackName)`, `fileBaseName(name)` | The file format, for an application that stores scripts elsewhere |

`origin` says who saved a script: `user`, `application` (from a read-only directory) or whatever a requester's save puts there - a script with another origin counts as new when it comes to approval.

| `ScriptsWidget` | |
|---|---|
| `setLockedVisible(false)` | Leaves locked methods out instead of greying them |
| `apply()`, `revert()`, `refresh()` | Hands the pending switches to the engine, drops them, reads everything again |
| Slots `newScript()`, `editScript(name)`, `duplicateScript(name)`, `deleteScript(name, confirm)`, `openFolder()` | What the buttons do, for an application's own menu |

| `ScriptEditorDialog` | |
|---|---|
| `setScript(script)`, `script()` | What it edits; what the fields hold now |
| `setReadOnly(bool)` | For a shipped script: run it, do not change it |
| Slots `checkScript()`, `runScript(dryRun)`, `debugScript()`, `step()`, `stepToCall()`, `continueRun()`, `stopRun()`, `save(askOnCheckFailure)` | What the buttons do |
| `breakpoints()`, `toggleBreakpoint(line)`, `pausedLine()`, `errorLine()`, `isOwnRunActive()` | The state of the editor and of the run it started |
| `saved(name)` | Signal: the script was saved into the library |

| ScriptHostWidgets functions | |
|---|---|
| `useApprovalDialog(engine, parent)` | Installs the approval dialog as the engine's approval handler. See [Approval](#approval) |
| `forwardInput(engine, widget)` | Posts the widget's keys and clicks as events, registers `input.mouse()`; returns the forwarder, delete it to stop. See [Keys and the mouse](#keys-and-the-mouse-forwardinput) |
| `attachCanvas(engine, target)` | A canvas over `target`: `gfx` for new runs; returns the `ScriptCanvas`, delete it to detach. See [the canvas](#drawing-over-the-window-the-canvas) |

| `ScriptCanvas` | |
|---|---|
| `addImage(name, image)`, `removeImage(name)`, `images()` | Images scripts draw by name. See [Images](#images) |
| `addDrawFunction(name, painter, declaration, bounds)`, `removeDrawFunction(name)`, `drawFunctions()` | `gfx.draw.<name>`. See [Drawing functions of the application](#drawing-functions-of-the-application) |
| `setAllowedForRequestedRuns(bool)`, `isAllowedForRequestedRuns()` | `gfx` and `"resize"` for requested runs |
| `layers()` | The runs with a layer, in paint order |
| `target()`, `engine()` | |
| Signal `presented(id, dirty)` | A run's frame reached the canvas; `dirty` is the area repainted for it |

Every control of the two widgets and of the approval dialog has an object name that starts with `scripthost_`, for tests and for tools that address widgets by name. The lists are in the headers.

## Signals

Every `run*` signal names its run first.

| Signal | |
|---|---|
| `runStarted(id, name, external)` | A run began; `external` for `requestRun()` |
| `runLog(id, text)` | A `log()`, `print()` or `console.log()` line |
| `runProgress(id, done, total, text)` | A `progress()` call |
| `runFinished(id, result)` | The result object; the run is no longer active |
| `runPaused(id, line, method, arguments, variables)`, `runCallFinished(id, line, method, arguments, value, error)` | Debugger |
| `runsChanged()` | `runs()` changed |
| `enabledChanged(bool)`, `methodsChanged()`, `settingsChanged()` | Switches and locks |

## New in 2.2

Nothing breaks; 2.2 adds drawing and extensions:

- [The canvas](#drawing-over-the-window-the-canvas) (ScriptHostWidgets): `attachCanvas(engine, widget)` gives scripts `gfx` - shapes, text, paths, gradients, transforms, images, shapes that follow widgets, a frame loop and helpers for games, and drawing functions of the application.
- [Extensions](#extensions-objects-of-your-own): `ScriptExtension` and `addExtension()` add an object of the application to every run, called on the script's thread.
- Events and the callbacks of an extension take turns, so a frame loop cannot hold back keys and clicks.

## New in 2.1

Nothing breaks; 2.1 adds [events](#events-keys-mouse-and-your-own):

- `on(name, fn)` and `off(name[, fn])` in scripts; `postEvent(name, data)` and `postEvent(id, name, data)` in the application.
- `forwardInput(engine, widget)` (ScriptHostWidgets): `"key"` and `"mouse"` events, and `input.mouse()` for the pointer.
- `allowEventForRequestedRuns(name)` / `isEventAllowedForRequestedRuns(name)`: requested runs get no events unless allowed.
- `callingRun()`: the run whose method call a handler serves.

## Migrating from 1.x to 2.0

Version 2.0 runs several scripts at once and splits the widgets off into a library of their own. Each change below breaks code written for 1.x.

**`run()` returns a `RunId`, not `bool`.** 0 means the run did not start.

```cpp
// 1.x
if (!engine->run(code, options, done)) { ... }
// 2.0
const ScriptHost::ScriptEngine::RunId id = engine->run(code, options, done);
if (id == 0) { ... }
```

**`stop()` and `isExternalRun()` take the run.** `stopAll()` stops every run.

```cpp
// 1.x
engine->stop();
if (engine->isExternalRun()) { ... }
// 2.0
engine->stop(id);            // or engine->stopAll();
if (engine->isExternalRun(id)) { ... }
```

`isRunning()` stays and means "any run"; `isRunning(id)` asks about one.

**Every `run*` signal has the `RunId` first.** Lambdas and slots get one more parameter.

```cpp
// 1.x
connect(engine, &ScriptHost::ScriptEngine::runLog, this, [this](const QString& text) { ... });
connect(engine, &ScriptHost::ScriptEngine::runFinished, this, [this](const QJsonObject& result) { ... });
// 2.0
connect(engine, &ScriptHost::ScriptEngine::runLog, this,
        [this](ScriptHost::ScriptEngine::RunId id, const QString& text) { ... });
connect(engine, &ScriptHost::ScriptEngine::runFinished, this,
        [this](ScriptHost::ScriptEngine::RunId id, const QJsonObject& result) { ... });
```

The same for `runStarted`, `runProgress`, `runPaused` and `runCallFinished`.

**A second run no longer fails.** In 1.x a run while another was going on failed with phase `unavailable`; now it starts, up to `Limits::maxConcurrentRuns` (default 8). Code that relies on one run at a time sets the cap:

```cpp
ScriptHost::ScriptEngine::Limits limits = engine->limits();
limits.maxConcurrentRuns = 1;
engine->setLimits(limits);
```

**The widgets are a library of their own.** `ScriptHost.h` no longer includes `ScriptsWidget.h` and `ScriptEditorDialog.h`.

```cpp
// 1.x
#include "ScriptHost.h"
// 2.0, in an application that uses the widgets
#include "ScriptHostWidgets.h"
```

In CMake, link `ScriptHostWidgets_static` (or `_shared`) besides the engine. `ScriptHost.cmake` does that by default.

**`setDialogParent()` is gone, and there is no default approval dialog.** In 1.x a requested run that needed approval opened a built-in dialog. The engine now has no window; the application installs one:

```cpp
// 1.x
engine->setDialogParent(mainWindow);
// 2.0
#include "ScriptHostWidgets.h"
ScriptHost::useApprovalDialog(engine, mainWindow);
```

Without a handler (`useApprovalDialog` or `setApprovalHandler`) a requested run that needs approval fails with phase `approval`. `setApprovalHandler(nullptr)` no longer brings the dialog back; it removes the handler.

**New, and not breaking:** `setTimeout`, `setInterval`, `clearTimeout`, `clearInterval`, `print` and `console.log` in scripts; `RunOptions::timeoutMs` and `maxCalls`; `Limits::maxConcurrentRuns`; `activeRuns()`, `activeRun(id)`, `RunRecord::id`; `setOutput()`.
