#pragma once
#include "ScriptHost_base.h"
#include "ScriptLibrary.h"
#include <QDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <QSet>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;
class QTabWidget;
class QTreeWidget;

namespace ScriptHost
{

class ScriptEngine;
class ScriptCodeEdit;

// The window to write, check, run and step through one script: name,
// description, parameters, the code with line numbers and breakpoints, test
// arguments, the output, and a reference of the methods a script may call.
// The Scripts tab (ScriptsWidget) opens it; an application may as well.
//
// It reacts only to the run it started itself: other runs (another window's,
// a requested one, the application's) go on beside it, and its Stop button
// stops only its own. Only one run can be in the debugger at a time; Debug
// while another one is there says so in the status line. Closing the window
// stops its run.
//
// Object names, for tests and tools that address widgets:
//   scripthost_scriptEditorDialog, scripthost_scriptEditorName, scripthost_scriptEditorDescription,
//   scripthost_scriptEditorParameters, scripthost_scriptEditorParameterAdd, scripthost_scriptEditorParameterRemove,
//   scripthost_scriptEditorCode, scripthost_scriptEditorArgs, scripthost_scriptEditorStatus,
//   scripthost_scriptEditorCheck, scripthost_scriptEditorRun, scripthost_scriptEditorDryRun,
//   scripthost_scriptEditorDebug, scripthost_scriptEditorStep, scripthost_scriptEditorStepCall,
//   scripthost_scriptEditorContinue, scripthost_scriptEditorVariables,
//   scripthost_scriptEditorStop, scripthost_scriptEditorSave, scripthost_scriptEditorClose,
//   scripthost_scriptEditorOutputTabs, scripthost_scriptEditorOutput, scripthost_scriptEditorCalls,
//   scripthost_scriptEditorReferenceSearch, scripthost_scriptEditorReference, scripthost_scriptEditorReferenceText
class SCRIPT_HOST_API ScriptEditorDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ScriptEditorDialog(ScriptEngine* scripting, QWidget* parent = nullptr);
    ~ScriptEditorDialog() override;

    void setScript(const ScriptLibrary::Script& script);
    // What the fields hold now; origin and file path as given to setScript().
    ScriptLibrary::Script script() const;

    // Read-only: can be read and run, not changed or saved.
    void setReadOnly(bool readOnly);
    bool isReadOnly() const { return m_readOnly; }

    // Debugger breakpoints, 1-based lines. A click in the line-number gutter
    // toggles one too.
    QSet<int> breakpoints() const;
    void toggleBreakpoint(int line);
    // The line the debugger stopped at, 0 when not paused.
    int pausedLine() const { return m_pausedLine; }
    // The line marked as broken by Check or a failed run, 0 when none.
    int errorLine() const;
    bool isOwnRunActive() const { return m_ownRun != 0; }

public slots:
    // Shows the result in the status line and marks the error line.
    bool checkScript();
    // Start a run of the code in the editor; false when none could start
    // (this window's run still active, arguments not a JSON object, the
    // engine refused it - e.g. another run in the debugger).
    bool runScript(bool dryRun = false);
    bool debugScript();
    // While paused: to the next statement, to the next call, to the next breakpoint.
    void step();
    void stepToCall();
    void continueRun();
    void stopRun();
    // Saves into the library. A script that fails the check is saved only
    // after the user confirms (askOnCheckFailure) - they may be mid-edit.
    bool save(bool askOnCheckFailure = true);

signals:
    void saved(const QString& name);

public:
    void done(int result) override;

private:
    bool startRun(bool dryRun, bool debug);
    bool parseArgs(QJsonObject& args);
    void onPaused(quint64 run, int line, const QString& method, const QJsonArray& arguments,
                  const QJsonObject& variables);
    void resume(int how);
    void showVariables(const QJsonObject& variables);
    void onCallFinished(quint64 run, int line, const QString& method, const QJsonArray& arguments,
                        const QJsonValue& value, const QString& error);
    void onOwnRunFinished(const QJsonObject& result);
    void settlePendingCall();
    void updateButtons();
    void fillReference();
    void showStatus(const QString& text, bool error);

    QPointer<ScriptEngine> m_scripting;
    QLineEdit*      m_name = nullptr;
    QLineEdit*      m_description = nullptr;
    QTableWidget*   m_parameters = nullptr;
    QPushButton*    m_parameterAdd = nullptr;
    QPushButton*    m_parameterRemove = nullptr;
    ScriptCodeEdit* m_code = nullptr;
    QLineEdit*      m_args = nullptr;
    QLabel*         m_status = nullptr;
    QPushButton*    m_check = nullptr;
    QPushButton*    m_run = nullptr;
    QPushButton*    m_dryRun = nullptr;
    QPushButton*    m_debug = nullptr;
    QPushButton*    m_step = nullptr;
    QPushButton*    m_stepCall = nullptr;
    QPushButton*    m_continue = nullptr;
    QPushButton*    m_stop = nullptr;
    QPushButton*    m_save = nullptr;
    QTabWidget*     m_outputTabs = nullptr;
    QPlainTextEdit* m_output = nullptr;
    QTableWidget*   m_calls = nullptr;
    QTableWidget*   m_variables = nullptr;
    QLineEdit*      m_search = nullptr;
    QTreeWidget*    m_reference = nullptr;
    QPlainTextEdit* m_referenceText = nullptr;

    QString m_origin;
    QString m_filePath;
    QString m_savedName;        // the name it has in the library, empty for a new one
    bool    m_readOnly = false;
    quint64 m_ownRun = 0;       // ScriptEngine::RunId of the run started here, 0 = none
    bool    m_ownDebug = false; // m_ownRun is the debug run
    bool    m_paused = false;
    int     m_pausedLine = 0;
    int     m_pendingRow = -1;  // calls row of the call the debugger stopped before
};

} // namespace ScriptHost
