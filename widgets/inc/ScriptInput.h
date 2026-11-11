#pragma once
#include "ScriptHost_base.h"

class QObject;
class QWidget;

namespace ScriptHost
{

class ScriptEngine;

// Hands the keys and mouse clicks `source` receives to the engine's scripts,
// as events for on() (ScriptEngine::postEvent):
//   "key"    {type: "down"|"up", key: "Space"|"A"|"Left"|..., code: <Qt::Key>,
//             text: "a", modifiers: ["shift","control","alt","meta"], repeat: bool}
//   "mouse"  {type: "down"|"up"|"dblclick"|"wheel",
//             button: "left"|"right"|"middle"|"back"|"forward"|"other"|"none",
//             buttons: [...held...], x, y (in source), modifiers: [...],
//             deltaX, deltaY (wheel only: angleDelta, 120 per notch)}
// `key` is the name of the Qt::Key value without "Key_" ("Escape",
// "PageUp", "F1", "1"); empty for a key Qt has no name for. Mouse moves are
// not posted; a script reads the pointer when it needs it:
//   input.mouse()  {x, y (in source), inside: bool, buttons: [...]}
// a read-only method this registers (group "Input"; it counts as a call).
// Key events reach `source` only while it, or a child that ignores them,
// has the focus. Nothing is consumed: `source` sees every event as before.
//
// Requested runs get neither events nor input.mouse() unless the
// application allows them: allowEventForRequestedRuns("key"), and
// allowEventForRequestedRuns("mouse") for clicks and the position.
//
// Returns the forwarder, a child of `source`: delete it to stop. It goes
// with `source` too. input.mouse() reads the source of the engine's latest
// forwarder and is unregistered when that forwarder goes.
SCRIPT_HOST_WIDGETS_API QObject* forwardInput(ScriptEngine* engine, QWidget* source);

} // namespace ScriptHost
