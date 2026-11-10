# ScriptHost

A Qt library that makes a C++ application scriptable in JavaScript. The application registers the methods a script may call; the user, the application itself or another program writes a script against them; ScriptHost checks it, runs it on a thread of its own and hands back the result.

A script can call exactly the registered methods. The JavaScript engine (`QJSEngine`) has no file, network or process access of its own.

## Quick start

```cpp
#include "ScriptHost.h"

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

engine->run("return db.readObject('a1').name;", {}, [](const QJsonObject& result) {
    // {"status":"ok","result":"Pump","calls":{"db.readObject":1},...}
});
```

`examples/LibraryExample` is a small inventory application that uses everything below, commented in the order an application sets it up.

## Features

- **Methods defined once**: name, description, typed parameters (ranges, enums, defaults), return example; or taken from an existing JSON schema (`ScriptMethod::fromSchema`). Every call is checked against the definition before the handler runs.
- **Synchronous and asynchronous handlers**, run on the GUI thread; the script waits on its own thread, the application stays responsive.
- **Checked before running**: syntax errors and calls of methods that do not exist come back with the line and a "did you mean", before anything is executed.
- **Limits**: run time, total method calls, calls per method (`setMaxCallsPerRun`), log lines.
- **Dry run**: read-only methods run, changing ones are only recorded.
- **Requested runs** (`requestRun`) for other programs: a master switch, a requester name the user sees, approval by the user per kind of script (new or saved, reading or changing, dry run), with a built-in dialog (run, test run, decline with a reason) or the application's own handler.
- **Saved scripts** in `.js` files with a small header; shipped read-only folders next to the user's folder.
- **Per-method switches** and locks: what scripts may call, and what the user may change. The user's choices as JSON for the application to store.
- **Widgets**: `ScriptsWidget` (switches, saved scripts, methods, runs of the session) and `ScriptEditorDialog` (editor with method reference, check, run, dry run, and a debugger that steps statement by statement or from method call to method call, with breakpoints and the variables at each stop).
- **Definition** of the methods as TypeScript declarations (`apiDefinition`), for the editor's reference and for programs that write scripts.

The full description is in [documentation/Guide.md](documentation/Guide.md).

## Building

Requirements:

- Qt 5.15 (primary) or Qt 6, modules **Core, Gui, Widgets, Qml**
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

The library, the example and the tests are installed into `installation/`, with the Qt libraries they need. The tests are `installation/bin/ScriptHostTest.exe`; the example is `installation/bin/LibraryExample.exe`.

## Using it in another project

ScriptHost follows the [QT_cmake_library_template](https://github.com/KROIA/QT_cmake_library_template). In a project built from the same template, copy `ScriptHost.cmake` from this repository into that project's `dependencies/` folder. On the next configure the library is fetched from GitHub and linked; `SH_LIBRARY_AVAILABLE` is defined, and `#include "ScriptHost.h"` gives all classes.

## License

MIT, see [LICENSE](LICENSE). Qt itself is used under its own license (LGPL or commercial).
