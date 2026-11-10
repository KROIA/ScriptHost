#pragma once

// The Scripts widget (ScriptsWidget) and the script editor (ScriptEditorDialog).
//
// Widgets are found by object name:
//   widget: scripthost_scriptsEnabled, scripthost_scriptsEnabledHint, scripthost_scriptsApproval,
//           scripthost_scriptsApprovalHint, scripthost_scriptsTable, scripthost_scriptsNew,
//           scripthost_scriptsDelete, scripthost_scriptsMethod_<name>, scripthost_scriptsRuns,
//           scripthost_scriptsRunDetails, scripthost_scriptsRunsClear
//   editor: scripthost_scriptEditorName, scripthost_scriptEditorCode, scripthost_scriptEditorArgs,
//           scripthost_scriptEditorStatus, scripthost_scriptEditorRun, scripthost_scriptEditorStep,
//           scripthost_scriptEditorContinue, scripthost_scriptEditorStop, scripthost_scriptEditorSave,
//           scripthost_scriptEditorCalls, scripthost_scriptEditorOutput,
//           scripthost_scriptEditorReference, scripthost_scriptEditorReferenceSearch

#include "UnitTest.h"
#include "TestSupport.h"
#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTextCursor>
#include <QTreeWidget>

class TST_ScriptsWidget : public UnitTest::Test
{
    TEST_CLASS(TST_ScriptsWidget)
public:
    TST_ScriptsWidget()
        : Test("TST_ScriptsWidget")
    {
        ADD_TEST(TST_ScriptsWidget::testSwitchesArePending);
        ADD_TEST(TST_ScriptsWidget::testLockedSwitches);
        ADD_TEST(TST_ScriptsWidget::testMethodChecks);
        ADD_TEST(TST_ScriptsWidget::testScriptList);
        ADD_TEST(TST_ScriptsWidget::testEditorCheckAndRun);
        ADD_TEST(TST_ScriptsWidget::testEditorDebug);
        ADD_TEST(TST_ScriptsWidget::testEditorReference);
        ADD_TEST(TST_ScriptsWidget::testRuns);
    }

private:
    using Engine = ScriptHost::ScriptEngine;
    using Db = TestSupport::Db;

    // The method names in the editor's reference, without the namespace rows.
    static QStringList referenceMethods(QTreeWidget* tree)
    {
        QStringList names;
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            for (int j = 0; j < tree->topLevelItem(i)->childCount(); ++j)
                names << tree->topLevelItem(i)->child(j)->text(0);
        }
        return names;
    }

    TEST_FUNCTION(testSwitchesArePending)
    {
        TEST_START;

        Engine engine;

        // Nothing bound: nothing to switch.
        ScriptHost::ScriptsWidget widget;
        auto* enabled = widget.findChild<QCheckBox*>("scripthost_scriptsEnabled");
        auto* approval = widget.findChild<QCheckBox*>("scripthost_scriptsAsk_new_changing");
        TEST_ASSERT(enabled && approval);
        if (!enabled || !approval)
            return;
        TEST_ASSERT(!enabled->isEnabled());

        widget.setScripting(&engine);
        TEST_ASSERT(widget.scripting() == &engine);
        TEST_ASSERT(enabled->isEnabled());
        TEST_ASSERT(enabled->isChecked());
        TEST_ASSERT(!approval->isChecked());
        // Who asks for runs is the application's to say.
        TEST_COMPARE(enabled->text(), QString("Allow another program to run scripts"));
        engine.setRequesterName("the assistant");
        TEST_COMPARE(enabled->text(), QString("Allow the assistant to run scripts"));
        int changes = 0;
        QObject::connect(&widget, &ScriptHost::ScriptsWidget::changed, [&]() { ++changes; });

        // Pending until apply().
        enabled->click();
        approval->click();
        TEST_COMPARE(changes, 2);
        TEST_ASSERT(engine.isEnabled());
        TEST_ASSERT(!engine.isApprovalRequired(Engine::ScriptKind::NewChanging));
        widget.refresh();                                   // pending choices survive a refresh
        TEST_ASSERT(!enabled->isChecked());
        widget.apply();
        TEST_ASSERT(!engine.isEnabled());
        TEST_ASSERT(engine.isApprovalRequired(Engine::ScriptKind::NewChanging));

        // Dropped by revert().
        enabled->click();
        TEST_ASSERT(enabled->isChecked());
        widget.revert();
        TEST_ASSERT(!enabled->isChecked());
        TEST_ASSERT(!engine.isEnabled());

        // The application changes it: the widget follows.
        engine.setEnabled(true);
        TEST_ASSERT(enabled->isChecked());
        engine.setApproval(Engine::Approval::Never);
        TEST_ASSERT(!approval->isChecked());

        // At once, for a settings page without an Apply button.
        widget.setApplyImmediately(true);
        TEST_ASSERT(widget.applyImmediately());
        enabled->click();
        TEST_ASSERT(!engine.isEnabled());
    }

    TEST_FUNCTION(testLockedSwitches)
    {
        TEST_START;

        Engine engine;
        Db db;
        TestSupport::registerDb(engine, db);
        engine.setEnabledUserConfigurable(false);
        engine.setApproval(Engine::Approval::Always);
        engine.setApprovalUserConfigurable(false);
        engine.setMethodEnabled("db.setStatus", false);
        engine.setMethodUserConfigurable("db.setStatus", false);

        ScriptHost::ScriptsWidget widget;
        widget.setScripting(&engine);
        auto* enabled = widget.findChild<QCheckBox*>("scripthost_scriptsEnabled");
        auto* approval = widget.findChild<QCheckBox*>("scripthost_scriptsAsk_new_changing");
        TEST_ASSERT(!enabled->isEnabled());
        TEST_ASSERT(!widget.findChild<QLabel*>("scripthost_scriptsEnabledHint")->isHidden());
        TEST_ASSERT(!approval->isEnabled());
        TEST_ASSERT(!widget.findChild<QLabel*>("scripthost_scriptsApprovalHint")->isHidden());
        TEST_ASSERT(approval->isChecked());
        auto* locked = widget.findChild<QCheckBox*>("scripthost_scriptsMethod_db.setStatus");
        TEST_ASSERT(locked && !locked->isEnabled() && !locked->isChecked());
        if (!locked)
            return;

        // Even switched behind the disabled controls' back, nothing changes.
        enabled->setChecked(false);
        approval->setChecked(false);
        locked->setChecked(true);
        widget.apply();
        TEST_ASSERT(engine.isEnabled());
        TEST_ASSERT(engine.isApprovalRequired(Engine::ScriptKind::NewChanging));
        TEST_ASSERT(!engine.isMethodEnabled("db.setStatus"));
        TEST_ASSERT(enabled->isChecked());                  // shows the real state again

        // Unlocked: the hint goes.
        engine.setEnabledUserConfigurable(true);
        TEST_ASSERT(enabled->isEnabled());
        TEST_ASSERT(widget.findChild<QLabel*>("scripthost_scriptsEnabledHint")->isHidden());

        // Locked methods can be left out.
        widget.setLockedVisible(false);
        TEST_ASSERT(!widget.lockedVisible());
        TEST_ASSERT(widget.findChild<QCheckBox*>("scripthost_scriptsMethod_db.setStatus") == nullptr);
        TEST_ASSERT(widget.findChild<QCheckBox*>("scripthost_scriptsMethod_db.read") != nullptr);
    }

    TEST_FUNCTION(testMethodChecks)
    {
        TEST_START;

        Engine engine;
        Db db;
        TestSupport::registerDb(engine, db);

        ScriptHost::ScriptsWidget widget;
        widget.setScripting(&engine);
        auto method = [&](const QString& name) {
            return widget.findChild<QCheckBox*>("scripthost_scriptsMethod_" + name);
        };
        TEST_ASSERT(method("db.read") && method("db.read")->isChecked() && method("db.read")->isEnabled());
        if (!method("db.read"))
            return;
        TEST_ASSERT(method("log") != nullptr);                          // built in
        TEST_ASSERT(method("db.read")->toolTip().contains("db.read(id: string)"));
        TEST_ASSERT(method("db.read")->toolTip().contains("Reads one object."));

        method("db.read")->click();
        TEST_ASSERT(engine.isMethodEnabled("db.read"));
        // A method arriving meanwhile rebuilds the list; the pending choice stays.
        ScriptHost::ScriptMethod extra("db.count");
        extra.setGroup("Database").setReadOnly(true).setReturns("number");
        engine.registerMethod(extra, [](const QJsonObject&) -> QJsonValue { return 6; });
        TEST_ASSERT(method("db.count") != nullptr);
        TEST_ASSERT(!method("db.read")->isChecked());
        widget.apply();
        TEST_ASSERT(!engine.isMethodEnabled("db.read"));
        TEST_ASSERT(engine.isMethodEnabled("db.list"));
    }

    TEST_FUNCTION(testScriptList)
    {
        TEST_START;

        QTemporaryDir userDir, shippedDir;
        TEST_ASSERT(userDir.isValid() && shippedDir.isValid());
        TestSupport::writeFile(shippedDir.path() + "/count.js",
                               "// @name Count objects\n// @description Counts the objects.\n\nreturn db.list().length;\n");
        ScriptHost::ScriptLibrary library;
        library.setDirectory(userDir.path() + "/scripts");
        library.addReadOnlyDirectory(shippedDir.path());
        ScriptHost::ScriptLibrary::Script mine;
        mine.name = "Oldest";
        mine.description = "Finds the oldest object.";
        mine.code = "return 1;";
        mine.origin = "user";
        TEST_ASSERT(library.save(mine));

        Engine engine;
        Db db;
        TestSupport::registerDb(engine, db);

        ScriptHost::ScriptsWidget widget;
        widget.setScripting(&engine);
        auto* table = widget.findChild<QTableWidget*>("scripthost_scriptsTable");
        TEST_ASSERT(table);
        if (!table)
            return;
        TEST_ASSERT(table->isHidden());                     // no library yet
        TEST_ASSERT(!widget.findChild<QLabel*>("scripthost_scriptsNoLibrary")->isHidden());

        engine.setLibrary(&library);
        TEST_ASSERT(!table->isHidden());
        TEST_COMPARE(table->rowCount(), 2);
        if (table->rowCount() == 2) {
            TEST_COMPARE(table->item(0, 0)->text(), QString("Count objects"));
            TEST_ASSERT(table->item(0, 2)->text().contains("read-only"));
            TEST_COMPARE(table->item(1, 0)->text(), QString("Oldest"));
            TEST_COMPARE(table->item(1, 2)->text(), QString("user"));
        }

        auto* deleteButton = widget.findChild<QPushButton*>("scripthost_scriptsDelete");
        table->selectRow(0);
        TEST_ASSERT(!deleteButton->isEnabled());            // shipped scripts stay
        table->selectRow(1);
        TEST_ASSERT(deleteButton->isEnabled());

        widget.resize(720, 420);
        TestSupport::grab(&widget, "scripts_tab.png");

        // A shipped script opens read-only.
        ScriptHost::ScriptEditorDialog* viewer = widget.editScript("Count objects");
        TEST_ASSERT(viewer && viewer->isReadOnly());
        if (viewer) {
            TEST_ASSERT(!viewer->findChild<QPushButton*>("scripthost_scriptEditorSave")->isEnabled());
            TEST_COMPARE(viewer->script().code, QString("return db.list().length;"));
            delete viewer;
        }

        // New -> editor -> Save: the file is there and listed.
        ScriptHost::ScriptEditorDialog* editor = widget.newScript();
        TEST_ASSERT(editor);
        if (editor) {
            TEST_ASSERT(!editor->save(false));                  // no name yet
            editor->findChild<QLineEdit*>("scripthost_scriptEditorName")->setText("Archive all");
            editor->findChild<QPlainTextEdit*>("scripthost_scriptEditorCode")
                ->setPlainText("for (const id of db.list())\n    db.setStatus(id, 'archived');\nreturn 1;");
            TEST_ASSERT(editor->save(false));
            delete editor;
        }
        TEST_ASSERT(QFile::exists(userDir.path() + "/scripts/archive-all.js"));
        TEST_COMPARE(library.script("Archive all").origin, QString("user"));
        TEST_COMPARE(table->rowCount(), 3);

        // Duplicating a shipped script makes an editable copy.
        editor = widget.duplicateScript("Count objects");
        TEST_ASSERT(editor && !editor->isReadOnly());
        if (editor) {
            TEST_ASSERT(editor->save(false));
            delete editor;
        }
        TEST_ASSERT(library.contains("Count objects (copy)"));
        TEST_ASSERT(!library.script("Count objects (copy)").readOnly);

        // Delete removes the file; shipped ones cannot be deleted.
        TEST_ASSERT(widget.deleteScript("Archive all", false));
        TEST_ASSERT(!QFile::exists(userDir.path() + "/scripts/archive-all.js"));
        TEST_ASSERT(!widget.deleteScript("Count objects", false));
        TEST_COMPARE(table->rowCount(), 3);

        // The application does not let the user edit scripts.
        engine.setUserMayEditScripts(false);
        TEST_ASSERT(!engine.userMayEditScripts());
        TEST_ASSERT(!widget.findChild<QPushButton*>("scripthost_scriptsNew")->isEnabled());
        TEST_ASSERT(widget.newScript() == nullptr);
        TEST_ASSERT(widget.duplicateScript("Oldest") == nullptr);
        TEST_ASSERT(!widget.deleteScript("Oldest", false));
        TEST_ASSERT(library.contains("Oldest"));
        viewer = widget.editScript("Oldest");
        TEST_ASSERT(viewer && viewer->isReadOnly());
        if (viewer) {
            TEST_ASSERT(!viewer->save(false));
            delete viewer;
        }
    }

    TEST_FUNCTION(testEditorCheckAndRun)
    {
        TEST_START;

        Engine engine;
        Db db;
        TestSupport::registerDb(engine, db);

        ScriptHost::ScriptEditorDialog editor(&engine);
        auto* code = editor.findChild<QPlainTextEdit*>("scripthost_scriptEditorCode");
        auto* status = editor.findChild<QLabel*>("scripthost_scriptEditorStatus");
        auto* output = editor.findChild<QPlainTextEdit*>("scripthost_scriptEditorOutput");
        auto* runButton = editor.findChild<QPushButton*>("scripthost_scriptEditorRun");
        TEST_ASSERT(code && status && output && runButton);
        if (!code || !status || !output || !runButton)
            return;

        // Check: the error and its line.
        code->setPlainText("const ids = db.list();\nreturn db.nothing(ids);");
        TEST_ASSERT(!editor.checkScript());
        TEST_COMPARE(editor.errorLine(), 2);
        TEST_ASSERT(status->text().contains("db.nothing"));
        code->setPlainText("log('hello');\nreturn db.list().length;");
        TEST_ASSERT(editor.checkScript());
        TEST_COMPARE(editor.errorLine(), 0);
        TEST_ASSERT(status->text().contains("db.list"));

        // Arguments that are not a JSON object are not sent.
        auto* args = editor.findChild<QLineEdit*>("scripthost_scriptEditorArgs");
        args->setText("{days: 3");
        TEST_ASSERT(!editor.runScript());
        TEST_ASSERT(status->text().contains("JSON"));
        args->setText("{\"days\": 3}");

        // Run: log and result in the output.
        TEST_ASSERT(editor.runScript());
        TEST_ASSERT(TestSupport::spin([&]() { return !editor.isOwnRunActive(); }));
        TEST_ASSERT(output->toPlainText().contains("hello"));
        TEST_ASSERT(output->toPlainText().contains("\"result\": 6"));
        TEST_ASSERT(runButton->isEnabled());
        TEST_ASSERT(!engine.runs().last().external);

        // A runtime error marks its line.
        code->setPlainText("let x = 1;\nlet y = 2;\nthrow new Error('broken');");
        TEST_ASSERT(editor.runScript());
        TEST_ASSERT(TestSupport::spin([&]() { return !editor.isOwnRunActive(); }));
        TEST_COMPARE(editor.errorLine(), 3);

        // Another run (a requested one, or the application's) keeps the
        // buttons disabled and does not reach this window.
        TestSupport::Options foreign;
        foreign.debug = true;
        foreign.stepFromStart = true;
        bool foreignDone = false;
        bool paused = false;
        auto pausedConnection = QObject::connect(&engine, &Engine::runPaused, [&]() { paused = true; });
        engine.run("return db.list().length;", foreign, [&](const QJsonObject&) { foreignDone = true; });
        TEST_ASSERT(TestSupport::spin([&]() { return paused; }));
        TEST_ASSERT(!runButton->isEnabled());
        TEST_ASSERT(!editor.runScript());
        TEST_COMPARE(editor.findChild<QTableWidget*>("scripthost_scriptEditorCalls")->rowCount(), 0);
        TEST_COMPARE(editor.pausedLine(), 0);
        engine.stop();
        TEST_ASSERT(TestSupport::spin([&]() { return foreignDone; }));
        QObject::disconnect(pausedConnection);
        TEST_ASSERT(runButton->isEnabled());
    }

    TEST_FUNCTION(testEditorDebug)
    {
        TEST_START;

        Engine engine;
        Db db;
        TestSupport::registerDb(engine, db);

        ScriptHost::ScriptEditorDialog editor(&engine);
        ScriptHost::ScriptLibrary::Script script;
        script.name = "Sum of ages";
        script.code =
            "const ids = db.list();\n"              // 1
            "const first = db.read(ids[0]);\n"      // 2
            "let n = 0;\n"                          // 3
            "for (const id of ids)\n"               // 4
            "    n += db.read(id).age;\n"           // 5
            "return n;\n";                          // 6
        editor.setScript(script);
        auto* calls = editor.findChild<QTableWidget*>("scripthost_scriptEditorCalls");
        auto* step = editor.findChild<QPushButton*>("scripthost_scriptEditorStep");
        auto* stop = editor.findChild<QPushButton*>("scripthost_scriptEditorStop");
        auto* runButton = editor.findChild<QPushButton*>("scripthost_scriptEditorRun");
        TEST_ASSERT(calls && step && stop && runButton);
        if (!calls || !step || !stop || !runButton)
            return;
        TEST_ASSERT(!step->isEnabled());

        // Stops before the first call.
        TEST_ASSERT(editor.debugScript());
        TEST_ASSERT(TestSupport::spin([&]() { return editor.pausedLine() == 1; }));
        TEST_ASSERT(step->isEnabled());
        TEST_ASSERT(stop->isEnabled());
        TEST_ASSERT(!runButton->isEnabled());
        TEST_COMPARE(calls->rowCount(), 1);
        if (calls->rowCount() >= 1) {
            TEST_ASSERT(calls->item(0, 1)->text().contains("about to call db.list()"));
        }

        // One call further.
        editor.step();
        TEST_ASSERT(TestSupport::spin([&]() { return editor.pausedLine() == 2; }));
        TEST_COMPARE(calls->rowCount(), 2);
        if (calls->rowCount() == 2) {
            TEST_COMPARE(calls->item(0, 1)->text(), QString("db.list()"));
            TEST_ASSERT(calls->item(0, 2)->text().startsWith("[\"o1\""));
            TEST_ASSERT(calls->item(1, 1)->text().contains("db.read(\"o1\")"));
        }

        editor.resize(1000, 700);
        TestSupport::grab(&editor, "script_editor.png");

        // A breakpoint set while running takes effect.
        editor.toggleBreakpoint(5);
        TEST_ASSERT(editor.breakpoints().contains(5));
        editor.continueRun();
        TEST_ASSERT(TestSupport::spin([&]() { return editor.pausedLine() == 5; }));
        editor.toggleBreakpoint(5);
        TEST_ASSERT(editor.breakpoints().isEmpty());
        editor.continueRun();
        TEST_ASSERT(TestSupport::spin([&]() { return !editor.isOwnRunActive(); }));
        TEST_COMPARE(calls->rowCount(), 8);                 // list, read, six reads
        TEST_ASSERT(editor.findChild<QPlainTextEdit*>("scripthost_scriptEditorOutput")->toPlainText().contains("210"));
        TEST_ASSERT(!step->isEnabled());

        // Closing the window stops its run.
        TEST_ASSERT(editor.debugScript());
        TEST_ASSERT(TestSupport::spin([&]() { return editor.pausedLine() == 1; }));
        editor.reject();
        TEST_ASSERT(TestSupport::spin([&]() { return !engine.isRunning(); }));
        TEST_COMPARE(engine.runs().last().result["phase"].toString(), QString("cancelled"));

        // Without the debugger there are no debug buttons.
        engine.setDebuggerEnabled(false);
        TEST_ASSERT(!engine.isDebuggerEnabled());
        TEST_ASSERT(step->isHidden());
        TEST_ASSERT(editor.findChild<QPushButton*>("scripthost_scriptEditorDebug")->isHidden());
        TEST_ASSERT(!editor.debugScript());
    }

    TEST_FUNCTION(testEditorReference)
    {
        TEST_START;

        Engine engine;
        Db db;
        TestSupport::registerDb(engine, db);
        engine.setMethodEnabled("db.setStatus", false);

        ScriptHost::ScriptEditorDialog editor(&engine);
        auto* tree = editor.findChild<QTreeWidget*>("scripthost_scriptEditorReference");
        auto* search = editor.findChild<QLineEdit*>("scripthost_scriptEditorReferenceSearch");
        TEST_ASSERT(tree && search);
        if (!tree || !search)
            return;
        QStringList names = referenceMethods(tree);
        TEST_ASSERT(names.contains("db.read"));
        TEST_ASSERT(names.contains("log"));
        TEST_ASSERT(!names.contains("db.setStatus"));       // disabled: not offered

        engine.setMethodEnabled("db.setStatus", true);
        TEST_ASSERT(referenceMethods(tree).contains("db.setStatus"));

        search->setText("read");
        names = referenceMethods(tree);
        TEST_COMPARE(int(names.size()), 1);
        if (names.isEmpty() || tree->topLevelItemCount() == 0)
            return;
        TEST_COMPARE(names.first(), QString("db.read"));

        // Selecting shows the declaration; a double click inserts a call.
        QTreeWidgetItem* item = tree->topLevelItem(0)->child(0);
        tree->setCurrentItem(item);
        TEST_ASSERT(editor.findChild<QPlainTextEdit*>("scripthost_scriptEditorReferenceText")
                        ->toPlainText().contains("read(id: string)"));
        auto* code = editor.findChild<QPlainTextEdit*>("scripthost_scriptEditorCode");
        code->setPlainText("const o = ");
        code->moveCursor(QTextCursor::End);
        emit tree->itemDoubleClicked(item, 0);
        TEST_COMPARE(code->toPlainText(), QString("const o = db.read(id)"));
    }

    TEST_FUNCTION(testRuns)
    {
        TEST_START;

        Engine engine;
        Db db;
        TestSupport::registerDb(engine, db);
        engine.setRequesterName("the assistant");

        ScriptHost::ScriptsWidget widget;
        widget.setScripting(&engine);
        auto* runs = widget.findChild<QTableWidget*>("scripthost_scriptsRuns");
        auto* details = widget.findChild<QPlainTextEdit*>("scripthost_scriptsRunDetails");
        TEST_ASSERT(runs && details);
        if (!runs || !details)
            return;
        TEST_COMPARE(runs->rowCount(), 0);

        TestSupport::runScript(engine, "log('first');\nreturn 1;");
        TestSupport::Options options;
        options.name = "Second";
        options.dryRun = true;
        options.args = QJsonObject{{"days", 3}};
        TestSupport::runScript(engine, "return db.nothing();", options);
        TestSupport::request(engine, TestSupport::codeRequest("return 3;"));

        // Newest first.
        TEST_COMPARE(runs->rowCount(), 3);
        if (runs->rowCount() != 3)
            return;
        TEST_COMPARE(runs->item(0, 1)->text(), QString("(unsaved)"));
        TEST_COMPARE(runs->item(0, 2)->text(), QString("the assistant"));
        TEST_COMPARE(runs->item(1, 1)->text(), QString("Second"));
        TEST_COMPARE(runs->item(1, 3)->text(), QString("yes"));
        TEST_ASSERT(runs->item(1, 4)->text().startsWith("error"));
        TEST_COMPARE(runs->item(2, 1)->text(), QString("(unsaved)"));
        TEST_COMPARE(runs->item(2, 2)->text(), QString("the user"));
        TEST_COMPARE(runs->item(2, 4)->text(), QString("ok"));

        runs->selectRow(2);
        TEST_ASSERT(details->toPlainText().contains("log('first');"));
        TEST_ASSERT(details->toPlainText().contains("first"));
        TEST_ASSERT(details->toPlainText().contains(" by the user"));
        runs->selectRow(1);
        TEST_ASSERT(details->toPlainText().contains("\"days\": 3"));
        TEST_ASSERT(details->toPlainText().contains("db.nothing"));
        runs->selectRow(0);
        TEST_ASSERT(details->toPlainText().contains(" by the assistant"));
        TEST_ASSERT_M(!details->toPlainText().contains("the the assistant"),
                      "Run details read 'by the the assistant' (ScriptsWidget.cpp, showRunDetails)");

        widget.findChild<QPushButton*>("scripthost_scriptsRunsClear")->click();
        TEST_COMPARE(runs->rowCount(), 0);
        TEST_ASSERT(details->toPlainText().isEmpty());
    }
};

TEST_INSTANTIATE(TST_ScriptsWidget);
