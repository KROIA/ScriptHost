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

`examples/LibraryExample` is a complete application: an inventory table with its methods, saved scripts, the Scripts window, the editor and a requested run, in the order an application sets them up.

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
| `log(...values)` | A line in the script log: shown in the editor and the Runs list, signal `runLog`; the last lines go back with the result |
| `progress(done, total, text)` | Signal `runProgress`, for a status line of the application |
| `sleep(ms)` | Waits; the application stays responsive |
| `pause(label)` | Stops in the debugger; does nothing otherwise |
| `args` | The arguments of the run |

### Switching methods on and off

```cpp
engine->setMethodEnabled("db.deleteObject", false);
engine->setMethodGroupEnabled("Database", false);              // a method's own setting wins
engine->setMethodUserConfigurable("db.deleteObject", false);   // the user cannot switch it either
```

These settings are by name and durable: they may be set before the method is registered and survive registering it again. A disabled method is not in the definition, and a script that uses it is rejected before it runs. The built-in methods are switched the same way (`"log"`, `"sleep"`).

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

The user sees it in the reference pane of the script editor. A program that writes scripts (a remote client, an assistant) can be given the same text. Without a query it is everything, or an index with one line per method when the text is longer than `setApiBudgetChars()` (default 6000; 0 means no limit). With a query (a method name, a namespace, keywords) it is the matching declarations.

## Running a script

```cpp
ScriptHost::ScriptEngine::RunOptions options;
options.name = "Archive outdated objects";   // for the record
options.args = QJsonObject{{"before", 2015}};
options.dryRun = true;
engine->run(code, options, [](const QJsonObject& result) { ... });

const ScriptHost::ScriptEngine::Check check = engine->check(code);   // compiles? uses only what exists?
```

`run()` is the application's own decision: it needs no approval and works while requested runs are switched off. One script runs at a time; `run()` returns false, and calls back with an error of phase `unavailable`, while another is running. `stop()` ends the running script; `isRunning()` and `isExternalRun()` say what is going on.

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
limits.timeoutMs   = 300000;   // whole run; not applied in the debugger
limits.maxCalls    = 5000;     // method calls per run
limits.maxLogLines = 200;      // kept of the script log
engine->setLimits(limits);
```

`ScriptMethod::setMaxCallsPerRun(n)` limits one method on top of that. The next run starts with fresh counts.

A script that waits, sleeps or loops without calling anything is stopped by the time limit; `try` / `catch` in the script cannot swallow that.

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
3. Another script running: `another script is running; wait for it to finish` (phase `unavailable`). Requests are not queued.
4. The script is checked (syntax, methods). A mistake comes back at once; the user is not bothered.
5. Approval, where the kind of script needs it (below). Declined: `{"status":"error","phase":"declined","message":"the user declined the script","reason":"..."}`.
6. If an `abandoned` function was given, it is asked now. True: nobody waits for the result any more, and the run is dropped without one.
7. The script runs. `done` always gets a result, except for an abandoned request.

The run is recorded with `external` set: `isExternalRun()` is true while it runs, `runStarted` has `external == true`, and the `RunRecord` in `runs()` has `external` set.

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

The built-in dialog shows who asks, the code, the arguments and the methods the script uses, marked as reading or changing. The user can run it, test-run it (a dry run, with the result shown in the dialog) or decline with a reason that goes back to the requester. "Do not ask again for this script in this session" remembers the exact code. `setDialogParent(widget)` sets the window it opens over.

`setApprovalHandler(handler)` replaces the dialog with the application's own. The handler gets an `ApprovalRequest` and may call `decide` later, from a window of its own:

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

`ScriptHost::ScriptsWidget` is a plain widget, for a settings dialog or a window of its own. `setScripting(engine)` connects it. Changes wait for `apply()` unless `setApplyImmediately(true)`; `changed()` is emitted when the user changed something, which is the moment to store `userSettings()`.

- **Switches**: "Allow (requester) to run scripts" and, under "Ask before running", one check box per kind of script.
- **Scripts**: the saved scripts. New, Edit, Duplicate, Delete, Open folder.
- **Methods**: every method with a check box; locked ones are greyed out.
- **Runs**: the scripts run in this session, with who started them, code, arguments, result and log.

The editor (`ScriptHost::ScriptEditorDialog`) is a window of its own: name, description, parameters, the code, and beside it the reference of the methods a script can call, with search. A double click inserts a call. It can be opened without the Scripts window:

```cpp
auto* editor = new ScriptHost::ScriptEditorDialog(engine, this);
editor->setAttribute(Qt::WA_DeleteOnClose);
editor->setScript(script);   // a ScriptLibrary::Script; a blank one for a new script
editor->show();
```

### Running and stepping

Check, Run and Dry run work on the code as it stands in the editor, saved or not.

Debug starts the script and stops before its first statement. The current line is marked, and the Variables table shows what the script's variables hold there; a value that changed since the stop before is bold.

| Button | |
|---|---|
| Step | Runs the statement and stops before the next one, also inside functions of the script |
| Next call | Runs on to the next method call and shows it with its arguments; the Calls table gets the value it returned |
| Continue | Runs on to the next breakpoint (set by a click in the line-number margin) or to `pause("label")` in the script |

The engine has no debugger interface, so for a debug run the script is rewritten: a hidden call in front of every statement that starts a line. Stepping is therefore by line - a second statement on a line is not stopped at - and a variable appears from the line after its declaration on. A script whose statements the rewriting cannot tell apart is stepped call by call instead. Normal runs are not rewritten.

The same is available from code: `RunOptions::debug`, `statements`, `stepFromStart`, `breakpoints`, then `stepStatement()`, `stepRun()` (to the next call), `continueRun()` and the signals `runPaused(line, method, arguments, variables)` and `runCallFinished`. `variables` holds each visible variable as the text a debugger shows. Without `statements` a debug run is not rewritten and stops at calls only.

## More of the interface

What the sections above did not need.

| `ScriptEngine` | |
|---|---|
| `methods()`, `hasMethod(name)` | The registered methods |
| `check(code)` returns `Check` | `ok`; when not: `phase` (`syntax` or `check`), `line`, `message`, `didYouMean`. Always: `methods` and `changingMethods` the script uses, `dynamic` (it picks methods by computed names). `toJson()` gives the error as a result object |
| `RunOptions::external` | Marks the run as requested, in the record and in `runStarted`. `requestRun()` sets it |
| `RunOptions::readOnlyOnly` | A call to a method that is not read-only stops the script (phase `approval`). `requestRun()` sets it for a script that runs unasked as a reading one |
| `setBreakpoints(lines)` | Changes the breakpoints of the run that is going on |
| `runs()`, `clearRuns()` | The runs of the session, oldest first, as `RunRecord`: `started`, `name`, `code`, `args`, `result`, `dryRun`, `external` |

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
| `breakpoints()`, `toggleBreakpoint(line)`, `pausedLine()`, `errorLine()`, `isOwnRunActive()` | The state of the editor and of its run |
| `saved(name)` | Signal: the script was saved into the library |

Every control of the two widgets and of the approval dialog has an object name that starts with `scripthost_`, for tests and for tools that address widgets by name. The lists are in the headers.

## Signals

| Signal | |
|---|---|
| `runStarted(name, external)` | A run began; `external` for `requestRun()` |
| `runLog(text)` | A `log()` line |
| `runProgress(done, total, text)` | A `progress()` call |
| `runFinished(result)` | The result object |
| `runPaused(line, method, arguments, variables)`, `runCallFinished(line, method, arguments, value, error)` | Debugger |
| `runsChanged()` | `runs()` changed |
| `enabledChanged(bool)`, `methodsChanged()`, `settingsChanged()` | Switches and locks |
