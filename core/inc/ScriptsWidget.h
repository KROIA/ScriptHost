#pragma once
#include "ScriptHost_base.h"
// Complete, not forward declared: Qt 6 moc needs the slots' pointer return type.
#include "ScriptEditorDialog.h"
#include <QDateTime>
#include <QMap>
#include <QPointer>
#include <QWidget>

class QCheckBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;
class QTimer;
class QVBoxLayout;

namespace ScriptHost
{

class ScriptEngine;
class ScriptLibrary;

// Where the user of an application sees and steers scripting: may the
// program run scripts, must it ask first, which methods may scripts call,
// the saved scripts (with the editor), the runs going on now - each with a
// Stop button, which acts at once - and the runs of this session. The
// "Scripts" tab of a settings dialog can be one of these; an application with a
// settings window of its own embeds another.
//
// The switches (enabled, approval, methods) are
// pending until apply() unless setApplyImmediately(true); what the
// application locked is shown disabled with a hint, locked methods greyed
// or - with setLockedVisible(false) - not at all. Saving and deleting
// scripts happens at once.
//
// Object names, for tests and tools that address widgets:
//   scripthost_scriptsEnabled, scripthost_scriptsEnabledHint,
//   scripthost_scriptsAsk_<kind> (ScriptEngine::scriptKindKey),
//   scripthost_scriptsApprovalHint, scripthost_scriptsTabs, scripthost_scriptsTable,
//   scripthost_scriptsNoLibrary, scripthost_scriptsNew, scripthost_scriptsEdit,
//   scripthost_scriptsDuplicate, scripthost_scriptsDelete, scripthost_scriptsOpenFolder,
//   scripthost_scriptsMethod_<method name>, scripthost_scriptsActiveRuns,
//   scripthost_scriptsActiveRunStop (one per active run), scripthost_scriptsRuns,
//   scripthost_scriptsRunDetails, scripthost_scriptsRunsClear
class SCRIPT_HOST_API ScriptsWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ScriptsWidget(QWidget* parent = nullptr);
    ~ScriptsWidget() override;

    void setScripting(ScriptEngine* scripting);
    ScriptEngine* scripting() const;

    // Locked methods: listed greyed out (default) or not at all.
    void setLockedVisible(bool visible);
    bool lockedVisible() const { return m_lockedVisible; }

    // true: a click changes the ScriptEngine object at once. false (default):
    // the choices are written by apply() - for a dialog with OK / Cancel.
    void setApplyImmediately(bool immediately);
    bool applyImmediately() const { return m_immediate; }

    // Writes the pending choices (ScriptEngine::set...ByUser).
    void apply();
    // Drops the pending choices and shows the ScriptEngine object's state again.
    void revert();
    // Shows the ScriptEngine object's state; pending choices are kept. Called
    // automatically when it changes.
    void refresh();

public slots:
    // The editor windows open without blocking and delete themselves when
    // closed; nullptr when there is nothing to open or editing is not allowed.
    ScriptEditorDialog* newScript();
    // Read-only for shipped scripts and when the user may not edit scripts.
    ScriptEditorDialog* editScript(const QString& name);
    // A copy in the user's directory, opened unsaved.
    ScriptEditorDialog* duplicateScript(const QString& name);
    bool deleteScript(const QString& name, bool confirm = true);
    void openFolder();

signals:
    // The user switched something that is pending (not yet applied unless
    // applyImmediately()).
    void changed();

private:
    void refreshSwitches();
    void refreshScripts();
    void refreshMethods();
    void refreshRuns();
    void refreshActiveRuns();
    void updateElapsed();
    void updateScriptButtons();
    void showRunDetails();
    QString selectedScript() const;
    ScriptEditorDialog* openEditor();
    void userChanged();

    QPointer<ScriptEngine>     m_scripting;
    QPointer<ScriptLibrary> m_library;
    QLabel*         m_noScripting = nullptr;
    QWidget*        m_content = nullptr;
    QCheckBox*      m_enabled = nullptr;
    QLabel*         m_enabledHint = nullptr;
    QList<QCheckBox*> m_approval;       // by ScriptEngine::ScriptKind
    QLabel*         m_approvalHint = nullptr;
    QTableWidget*   m_scripts = nullptr;
    QLabel*         m_noLibrary = nullptr;
    QPushButton*    m_new = nullptr;
    QPushButton*    m_edit = nullptr;
    QPushButton*    m_duplicate = nullptr;
    QPushButton*    m_delete = nullptr;
    QPushButton*    m_openFolder = nullptr;
    QWidget*        m_methodsPage = nullptr;
    QVBoxLayout*    m_methodsLayout = nullptr;
    QTableWidget*   m_activeRuns = nullptr;
    QTimer*         m_tick = nullptr;               // updates the elapsed times
    QTableWidget*   m_runs = nullptr;
    QPlainTextEdit* m_runDetails = nullptr;

    int                 m_pendingEnabled = -1;      // -1 none, else 0 / 1
    QMap<int, bool>     m_pendingApproval;          // by ScriptEngine::ScriptKind
    QMap<QString, bool> m_pendingMethods;
    bool                m_lockedVisible = true;
    bool                m_immediate = false;
    bool                m_applying = false;
    QDateTime           m_selectedRun;              // RunRecord::started of the shown run
};

} // namespace ScriptHost
