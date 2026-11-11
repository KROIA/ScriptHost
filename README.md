# ScriptHost

A Qt library that makes a C++ application scriptable in JavaScript. The application registers the methods a script may call; the user, the application itself or another program writes a script against them; ScriptHost checks it, runs it on a thread of its own and hands back the result. Several scripts run at the same time, and a script with timers can run until it is stopped.

A script can call exactly the registered methods. The JavaScript engine (`QJSEngine`) has no file, network or process access of its own.

Two libraries: **ScriptHost**, the engine (Qt Core + Qml, no GUI), and **ScriptHostWidgets** on top of it (the Scripts window, the editor with its debugger, the approval dialog, a canvas scripts draw on, and the `ui` methods for the application's own widgets).

Version 2.0 breaks the 1.x API (run ids, the widgets split off); see [Migrating from 1.x to 2.0](documentation/Guide.md#migrating-from-1x-to-20). Version 2.1 adds events: keys, mouse clicks and the application's own ([New in 2.1](documentation/Guide.md#new-in-21)). Version 2.2 adds a canvas for scripts and extensions of the application's own ([New in 2.2](documentation/Guide.md#new-in-22)). Version 2.3 lets scripts read, change and extend the application's widgets ([New in 2.3](documentation/Guide.md#new-in-23)).

## Quick start

```cpp
#include "ScriptHost.h"          // the engine; "ScriptHostWidgets.h" adds the widgets

auto* engine = new ScriptHost::ScriptEngine(this);

ScriptHost::ScriptMethod read("db.readObject");
read.setDescription("Reads one object.")
    .addParameter("id", "string", "Object id")
    .setReturns("object", "The object", QJsonObject{{"id", "a1"}, {"name", "Pump"}})
    .setReadOnly(true);
engine->registerMethod(read, [this](const QJsonObject& args) -> QJsonValue {
    const QString id = args["id"].toString();
    if (!m_db.contains(id))
        throw ScriptHost::ScriptError("no object '" + id + "'");
    return m_db.object(id);
});

const ScriptHost::ScriptEngine::RunId id =
    engine->run("return db.readObject('a1').name;", {}, [](const QJsonObject& result) {
        // {"status":"ok","result":"Pump","calls":{"db.readObject":1},...}
    });
// engine->stop(id) ends it early.
```

`examples/LibraryExample` is a small inventory application that uses everything below, commented in the order an application sets it up, including a stock monitor that runs on a `setInterval` timer until it is stopped, an input logger that reacts to keys and clicks, a script that highlights a widget, a small paddle game on the canvas, and scripts that print, rearrange and extend the window through `ui`. `examples/ConsoleExample` uses the engine alone, in a `QCoreApplication`.

## Features

- **Methods defined once**: name, description, typed parameters (ranges, enums, defaults), return example; or taken from an existing JSON schema (`ScriptMethod::fromSchema`). Every call is checked against the definition before the handler runs.
- **Synchronous and asynchronous handlers**, run on the GUI thread; the script waits on its own thread, the application stays responsive.
- **Checked before running**: syntax errors and calls of methods that do not exist come back with the line and a "did you mean", before anything is executed.
- **Concurrent runs**: each `run()` returns a `RunId`; `stop(id)`, `stopAll()`, `activeRuns()`. Up to `Limits::maxConcurrentRuns` at once (default 8).
- **Long-running scripts**: `setTimeout`, `setInterval`, `clearTimeout`, `clearInterval`. A run lives until its timers are done, it is stopped or it reaches its limit; the application can lift the limits for its own runs (`RunOptions::timeoutMs = 0`). `stop()` ends a run at once, also while it sleeps, waits or loops.
- **Events**: a script subscribes with `on(name, fn)`; the application posts with `postEvent(name, data)` - a tick, a sensor value, anything. `forwardInput(engine, widget)` (ScriptHostWidgets) turns a widget's keys and mouse clicks into `"key"` and `"mouse"` events, and `input.mouse()` reads the pointer. Handlers run after the script's body has returned, between its timers. Requested runs get no events unless the application allows them per name.
- **Drawing** (ScriptHostWidgets): `attachCanvas(engine, widget)` gives scripts `gfx`, a transparent canvas over the widget - shapes, text, paths, gradients, transforms, images of the application, shapes that follow a named widget, a frame loop (`gfx.onFrame`) and helpers for games. Recorded on the script's thread and painted in one go: thousands of moving shapes per frame. The application adds drawing functions of its own (`addDrawFunction`). See [the canvas](documentation/Guide.md#drawing-over-the-window-the-canvas).
- **Extensions**: `ScriptExtension` adds an object of the application to every run, called directly on the script's thread - for what is called too often for a method, or keeps state per run. See [Extensions](documentation/Guide.md#extensions-objects-of-your-own).
- **The application's widgets** (ScriptHostWidgets): `exposeWidgets(engine, mainWindow)` gives scripts `ui`. They read the widget tree (`ui.tree`, `ui.find`, `ui.info`), change widgets and layouts and undo it with `ui.restore()`, build a dialog in one call (`ui.create`, `ui.exec`), and get widget signals as events (`on(ui.connect(button, 'clicked'), fn)`). Password text stays unreadable, the application hides what scripts must not see, and every tool can be switched off. See [Reading and changing the UI](documentation/Guide.md#reading-and-changing-the-ui).
- **Output**: `log()`, `print()` and `console.log()` lines arrive as `runLog` and, with `setOutput(device)`, on any `QIODevice` (stdout, a file, a socket).
- **Limits**: run time, total method calls, calls per method (`setMaxCallsPerRun`), log lines, runs at once.
- **Dry run**: read-only methods run, changing ones are only recorded.
- **Requested runs** (`requestRun`) for other programs: a master switch, a requester name the user sees, approval by the user per kind of script (new or saved, reading or changing, dry run), through the application's own handler or the dialog of ScriptHostWidgets (`useApprovalDialog`: run, test run, decline with a reason).
- **Saved scripts** in `.js` files with a small header; shipped read-only folders next to the user's folder.
- **Switches** per method, method group, extension and extension member, and locks: what scripts may use, and what the user may change. See [Switching functions off](documentation/Guide.md#switching-functions-off). The user's choices as JSON for the application to store.
- **Widgets** (ScriptHostWidgets): `ScriptsWidget` (switches, saved scripts, methods, the active runs with a Stop button each, runs of the session) and `ScriptEditorDialog` (editor with method reference, check, run, dry run, and a debugger that steps statement by statement or from method call to method call, with breakpoints and the variables at each stop).
- **Definition** of the methods as TypeScript declarations (`apiDefinition`), for the editor's reference and for programs that write scripts.

Events, in a script:

```js
on('key', event => {
    if (event.type === 'down' && event.key === 'Space') {
        const pointer = input.mouse();
        print('pointer at', pointer.x, pointer.y);
    }
});
setInterval(() => { /* per-frame work */ }, 16);
return 'running';       // handlers run once the body has returned
```

and in the application:

```cpp
ScriptHost::forwardInput(engine, gameView);                       // keys and clicks of gameView
engine->postEvent("sensor", QJsonObject{{"temperature", 21.5}}); // an event of its own
```

A dialog, with the widgets exposed (`ScriptHost::exposeWidgets(engine, mainWindow)`):

```js
const d = ui.create({class: 'QDialog', title: 'Your name', layout: {type: 'vbox', items: [
    {class: 'QLineEdit', name: 'name'},
    {class: 'QDialogButtonBox', standardButtons: 'Ok|Cancel'}]}});
if (ui.exec(d.id).accepted)                              // the script waits, the window does not
    log('Hello ' + ui.property(d.ids.name, 'text'));
```

Drawing, with a canvas attached (`ScriptHost::attachCanvas(engine, mainWindow)`):

```js
const name = gfx.widget('customerName');                 // a widget, by objectName
gfx.roundedRect(name, 6, {stroke: 'red', width: 3, padding: 4});   // follows the widget
gfx.arrow(400, 40, name, {stroke: 'red', width: 3});
gfx.present();
setTimeout(() => {}, 5000);                              // shown while the run lasts
```

The full description is in [documentation/Guide.md](documentation/Guide.md).

## Building

Requirements:

- Qt 5.15 (primary) or Qt 6, modules **Core, Qml** for the engine, plus **Gui, Widgets** for ScriptHostWidgets
- CMake with presets, a C++20 compiler (developed with MSVC / Visual Studio 2022 and Ninja)

With presets:

```
cmake --preset x64-Debug
cmake --build --preset x64-Debug
```

Presets: `x64-Debug`, `x64-Release`, `x64-Debug-Profile`, `x64-Release-Profile`. The Qt installation is found automatically; `QT_MAJOR_VERSION` (default 5) and `QT_VERSION` select another.

On Windows, `build.bat` does the same from a command prompt:

```
.\build.bat                  configure and build x64-Debug and x64-Release
.\build.bat x64-Debug        one preset
.\build.bat list             the presets
.\build.bat clean            remove build outputs
.\build.bat help
```

The libraries, the examples and the tests are installed into `installation/`, with the Qt libraries they need. The tests are `installation/bin/ScriptHostTest.exe`; the examples are `installation/bin/LibraryExample.exe` and `installation/bin/ConsoleExample.exe`.

Targets: `ScriptHost_shared` / `ScriptHost_static` (the engine) and `ScriptHostWidgets_shared` / `ScriptHostWidgets_static` (the widgets, which link the engine).

## Using it in another project

ScriptHost follows the [QT_cmake_library_template](https://github.com/KROIA/QT_cmake_library_template). In a project built from the same template, copy `ScriptHost.cmake` from this repository into that project's `dependencies/` folder. On the next configure the library is fetched from GitHub and both libraries are linked; `SH_LIBRARY_AVAILABLE` is defined. `#include "ScriptHost.h"` gives the engine, `#include "ScriptHostWidgets.h"` the engine and the widgets.

A program without windows (a service, a command-line tool) links the engine alone, without Qt Gui and Widgets:

```cmake
set(SCRIPTHOST_ENGINE_ONLY ON)    # before ScriptHost.cmake is included
```

## License

MIT, see [LICENSE](LICENSE). Qt itself is used under its own license (LGPL or commercial).
