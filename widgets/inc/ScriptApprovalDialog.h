#pragma once
#include "ScriptHost_base.h"

class QWidget;

namespace ScriptHost
{

class ScriptEngine;

// The built-in approval dialog for requested runs: it shows who asks, the
// code, the arguments, the methods the script uses and the objects of the
// application it uses (extensions such as gfx, with their members), and lets
// the user run it, test-run it (a dry run, its result shown in the dialog) or decline with
// a reason. Installed as the engine's approval handler (setApprovalHandler),
// so it replaces one set before; `parent` is the window it opens over and may
// go away before the engine does.
//
// Object names: scripthost_scriptApprovalDialog, scripthost_scriptApprovalHeadline,
//   scripthost_scriptApprovalCode, scripthost_scriptApprovalUses,
//   scripthost_scriptApprovalOutput, scripthost_scriptApprovalRemember,
//   scripthost_scriptApprovalReason, scripthost_scriptApprovalRun,
//   scripthost_scriptApprovalDecline, scripthost_scriptApprovalTest
SCRIPT_HOST_WIDGETS_API void useApprovalDialog(ScriptEngine* engine, QWidget* parent = nullptr);

} // namespace ScriptHost
