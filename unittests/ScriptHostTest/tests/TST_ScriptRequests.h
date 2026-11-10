#pragma once

// Runs somebody else asks for (ScriptEngine::requestRun): the master switch,
// saved scripts by name, approval by a handler and by the built-in dialog,
// and a requester that stops waiting.

#include "UnitTest.h"
#include "TestSupport.h"
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextLayout>
#include <QTimer>

class TST_ScriptRequests : public UnitTest::Test
{
    TEST_CLASS(TST_ScriptRequests)
public:
    TST_ScriptRequests()
        : Test("TST_ScriptRequests")
    {
        ADD_TEST(TST_ScriptRequests::testSwitchedOff);
        ADD_TEST(TST_ScriptRequests::testRequestedRunIsExternal);
        ADD_TEST(TST_ScriptRequests::testSavedScriptByName);
        ADD_TEST(TST_ScriptRequests::testApprovalAlways);
        ADD_TEST(TST_ScriptRequests::testApprovalForChangingScripts);
        ADD_TEST(TST_ScriptRequests::testApprovalPerKind);
        ADD_TEST(TST_ScriptRequests::testAbandonedRequest);
        ADD_TEST(TST_ScriptRequests::testApprovalDialog);
    }

private:
    using Engine = ScriptHost::ScriptEngine;
    using Db = TestSupport::Db;

    // Calls `act` with the approval dialog once it is open. Polls, because the
    // dialog opens inside requestRun(), in a nested event loop.
    static void onApprovalDialog(std::function<void(QDialog*)> act, int attempts = 100)
    {
        QTimer::singleShot(50, qApp, [act, attempts]() {
            for (QWidget* widget : QApplication::topLevelWidgets()) {
                if (widget->objectName() == "scripthost_scriptApprovalDialog" && widget->isVisible()) {
                    act(qobject_cast<QDialog*>(widget));
                    return;
                }
            }
            if (attempts > 0)
                onApprovalDialog(act, attempts - 1);
        });
    }

    // "Count old": a saved script with a parameter.
    static void saveCountOld(ScriptHost::ScriptLibrary& library)
    {
        ScriptHost::ScriptLibrary::Script script;
        script.name = "Count old";
        script.description = "Counts objects older than minAge.";
        script.parameters = {{"minAge", "number", "Age"}};
        script.code = "return db.list().filter(id => db.read(id).age >= args.minAge).length;";
        script.origin = "user";
        library.save(script);
    }

    // Off: requests are refused, the application's own runs are not.
    TEST_FUNCTION(testSwitchedOff)
    {
        TEST_START;

        Engine engine;
        Db db;
        TestSupport::registerDb(engine, db);
        int switched = 0;
        QObject::connect(&engine, &Engine::enabledChanged, [&]() { ++switched; });
        TEST_ASSERT(engine.isEnabled());

        engine.setEnabled(false);
        TEST_COMPARE(switched, 1);
        bool finished = false;
        QJsonObject result = TestSupport::request(engine, TestSupport::codeRequest("return 1 + 1;"),
                                                  nullptr, &finished);
        TEST_ASSERT(finished);
        TEST_COMPARE(result["status"].toString(), QString("error"));
        TEST_COMPARE(result["phase"].toString(), QString("unavailable"));
        TEST_COMPARE(result["message"].toString(), QString("running scripts is switched off"));
        TEST_ASSERT(engine.runs().isEmpty());
        TEST_COMPARE(TestSupport::runScript(engine, "return 1 + 1;")["result"].toInt(), 2);

        engine.setEnabled(true);
        result = TestSupport::request(engine, TestSupport::codeRequest("return 1 + 1;"));
        TEST_COMPARE(result["result"].toInt(), 2);

        // Nothing to run, and a script with a mistake: nothing runs.
        result = TestSupport::request(engine, TestSupport::codeRequest("   "));
        TEST_COMPARE(result["phase"].toString(), QString("check"));
        TEST_COMPARE(result["message"].toString(), QString("there is no script to run"));
        result = TestSupport::request(engine, TestSupport::codeRequest("return db.nothing();"));
        TEST_COMPARE(result["phase"].toString(), QString("check"));
        result = TestSupport::request(engine, TestSupport::codeRequest("return (;"));
        TEST_COMPARE(result["phase"].toString(), QString("syntax"));
        TEST_COMPARE(engine.runs().size(), 2);
    }

    TEST_FUNCTION(testRequestedRunIsExternal)
    {
        TEST_START;

        Engine engine;
        Db db;
        TestSupport::registerDb(engine, db);
        bool startedExternal = false;
        bool externalWhileRunning = false;
        QObject::connect(&engine, &Engine::runStarted, [&](const QString&, bool external) {
            startedExternal = external;
            externalWhileRunning = engine.isExternalRun();
        });

        QJsonObject result = TestSupport::request(engine,
            TestSupport::codeRequest("return args.a * 2;", QJsonObject{{"a", 21}}));
        TEST_COMPARE(result["status"].toString(), QString("ok"));
        TEST_COMPARE(result["result"].toInt(), 42);
        TEST_ASSERT(startedExternal);
        TEST_ASSERT(externalWhileRunning);
        TEST_ASSERT(!engine.isExternalRun());
        TEST_COMPARE(engine.runs().size(), 1);
        TEST_ASSERT(engine.runs().last().external);
        TEST_COMPARE(engine.runs().last().args["a"].toInt(), 21);

        // The application's own run is not.
        TestSupport::runScript(engine, "return 1;");
        TEST_ASSERT(!startedExternal);
        TEST_ASSERT(!externalWhileRunning);
        TEST_ASSERT(!engine.runs().last().external);

        // A dry run on request.
        result = TestSupport::request(engine,
            TestSupport::codeRequest("db.setStatus('o1', 'archived');\nreturn 1;", QJsonObject(), true));
        TEST_COMPARE(result["status"].toString(), QString("ok"));
        TEST_ASSERT(result["dry_run"].toBool());
        TEST_COMPARE(db.writes, 0);
        TEST_ASSERT(engine.runs().last().dryRun);

        // One at a time: a request while the application's script runs.
        bool done = false;
        engine.run("sleep(60000);", TestSupport::Options(), [&](const QJsonObject&) { done = true; });
        result = TestSupport::request(engine, TestSupport::codeRequest("return 1;"));
        TEST_COMPARE(result["phase"].toString(), QString("unavailable"));
        TEST_ASSERT(result["message"].toString().contains("another script is running"));
        engine.stop();
        TEST_ASSERT(TestSupport::spin([&]() { return done; }, 5000));
    }

    TEST_FUNCTION(testSavedScriptByName)
    {
        TEST_START;

        QTemporaryDir dir;
        TEST_ASSERT(dir.isValid());
        ScriptHost::ScriptLibrary library;
        library.setDirectory(dir.path());
        saveCountOld(library);

        Engine engine;
        Db db;
        TestSupport::registerDb(engine, db);

        // Without a library there is nothing to find.
        ScriptHost::ScriptEngine::Request byName;
        byName.name = "Count old";
        byName.args = QJsonObject{{"minAge", 50}};
        QJsonObject result = TestSupport::request(engine, byName);
        TEST_COMPARE(result["phase"].toString(), QString("check"));
        TEST_COMPARE(result["message"].toString(), QString("there is no saved script 'Count old'"));

        engine.setLibrary(&library);
        TEST_ASSERT(engine.library() == &library);
        QString startedName;
        QObject::connect(&engine, &Engine::runStarted, [&](const QString& name, bool) { startedName = name; });
        byName.name = "count OLD";                                  // any case
        result = TestSupport::request(engine, byName);
        TEST_COMPARE(result["status"].toString(), QString("ok"));
        TEST_COMPARE(result["result"].toInt(), 2);
        TEST_COMPARE(startedName, QString("Count old"));            // the library's spelling
        TEST_COMPARE(engine.runs().last().name, QString("Count old"));
        TEST_ASSERT(engine.runs().last().external);

        byName.name = "No such";
        result = TestSupport::request(engine, byName);
        TEST_COMPARE(result["status"].toString(), QString("error"));
        TEST_COMPARE(result["phase"].toString(), QString("check"));
        TEST_COMPARE(result["message"].toString(), QString("there is no saved script 'No such'"));

        // The approval request names the saved script.
        engine.setApproval(Engine::Approval::Always);
        QString askedFor;
        engine.setApprovalHandler([&](const Engine::ApprovalRequest& approvalRequest,
                                      std::function<void(const Engine::ApprovalDecision&)> decide) {
            askedFor = approvalRequest.name;
            decide(Engine::ApprovalDecision());
        });
        byName.name = "Count old";
        result = TestSupport::request(engine, byName);
        TEST_COMPARE(askedFor, QString("Count old"));
        TEST_COMPARE(result["phase"].toString(), QString("declined"));
    }

    TEST_FUNCTION(testApprovalAlways)
    {
        TEST_START;

        Engine engine;
        Db db;
        TestSupport::registerDb(engine, db);
        engine.setApproval(Engine::Approval::Always);
        TEST_ASSERT(engine.isApprovalRequired(Engine::ScriptKind::DryRun));

        QList<Engine::ApprovalRequest> requests;
        Engine::ApprovalDecision answer;
        engine.setApprovalHandler([&](const Engine::ApprovalRequest& approvalRequest,
                                      std::function<void(const Engine::ApprovalDecision&)> decide) {
            requests << approvalRequest;
            // Like a window of the application would: later, not from inside the call.
            QTimer::singleShot(10, qApp, [decide, a = answer]() { decide(a); });
        });

        const QString code = "db.setStatus('o1', 'archived');\nreturn db.read('o1').status;";

        // Declined, with a word for the requester.
        answer.approved = false;
        answer.reason = "leave o1 alone";
        QJsonObject result = TestSupport::request(engine, TestSupport::codeRequest(code, QJsonObject{{"x", 1}}));
        TEST_COMPARE(requests.size(), 1);
        if (requests.size() == 1) {
            TEST_COMPARE(requests[0].code, code);
            TEST_ASSERT(requests[0].name.isEmpty());
            TEST_COMPARE(requests[0].args["x"].toInt(), 1);
            TEST_COMPARE(requests[0].methods.size(), 2);
            TEST_COMPARE(requests[0].changingMethods, QStringList{"db.setStatus"});
            TEST_ASSERT(!requests[0].dryRun);
        }
        TEST_COMPARE(result["status"].toString(), QString("error"));
        TEST_COMPARE(result["phase"].toString(), QString("declined"));
        TEST_COMPARE(result["message"].toString(), QString("the user declined the script"));
        TEST_COMPARE(result["reason"].toString(), QString("leave o1 alone"));
        TEST_COMPARE(db.writes, 0);
        TEST_ASSERT(engine.runs().isEmpty());

        // Approved, and not to be asked again for this script.
        answer.approved = true;
        answer.reason.clear();
        answer.rememberForSession = true;
        result = TestSupport::request(engine, TestSupport::codeRequest(code));
        TEST_COMPARE(requests.size(), 2);
        TEST_COMPARE(result["result"].toString(), QString("archived"));

        result = TestSupport::request(engine, TestSupport::codeRequest(code));
        TEST_COMPARE(requests.size(), 2);                    // same code: not asked
        TEST_COMPARE(result["status"].toString(), QString("ok"));

        // "Always" means reading scripts too, and dry runs.
        result = TestSupport::request(engine, TestSupport::codeRequest("return db.list().length;", QJsonObject(), true));
        TEST_COMPARE(requests.size(), 3);
        if (requests.size() == 3) {
            TEST_ASSERT(requests[2].dryRun);
            TEST_ASSERT(requests[2].changingMethods.isEmpty());
        }
        TEST_COMPARE(result["result"].toInt(), 6);

        // A script with a mistake is not shown to the user at all.
        result = TestSupport::request(engine, TestSupport::codeRequest("return db.nothing();"));
        TEST_COMPARE(requests.size(), 3);
        TEST_COMPARE(result["phase"].toString(), QString("check"));
    }

    TEST_FUNCTION(testApprovalForChangingScripts)
    {
        TEST_START;

        Engine engine;
        Db db;
        TestSupport::registerDb(engine, db);
        engine.setApproval(Engine::Approval::ChangingScripts);

        int asked = 0;
        bool dynamicSeen = false;
        engine.setApprovalHandler([&](const Engine::ApprovalRequest& approvalRequest,
                                      std::function<void(const Engine::ApprovalDecision&)> decide) {
            ++asked;
            dynamicSeen = approvalRequest.dynamic;
            Engine::ApprovalDecision decision;
            decision.approved = true;
            decide(decision);
        });

        // Only reads: runs unasked.
        QJsonObject result = TestSupport::request(engine,
            TestSupport::codeRequest("return db.list().map(id => db.read(id).age);"));
        TEST_COMPARE(asked, 0);
        TEST_COMPARE(result["result"].toArray().size(), 6);

        // Changes something: asked.
        result = TestSupport::request(engine, TestSupport::codeRequest("db.setStatus('o1', 'archived');\nreturn 1;"));
        TEST_COMPARE(asked, 1);
        TEST_ASSERT(!dynamicSeen);
        TEST_COMPARE(result["status"].toString(), QString("ok"));
        TEST_COMPARE(db.archived(), 1);

        // By a computed name: asked, and the user is told why.
        result = TestSupport::request(engine, TestSupport::codeRequest("db['set' + 'Status']('o2', 'archived');\nreturn 1;"));
        TEST_COMPARE(asked, 2);
        TEST_ASSERT(dynamicSeen);
        TEST_COMPARE(db.archived(), 2);

        // A dry run changes nothing, so nobody is asked.
        result = TestSupport::request(engine,
            TestSupport::codeRequest("db.setStatus('o3', 'archived');\nreturn 1;", QJsonObject(), true));
        TEST_COMPARE(asked, 2);
        TEST_COMPARE(result["status"].toString(), QString("ok"));
        TEST_COMPARE(db.archived(), 2);

        // A script that hides what it calls from the source gets past the
        // question - and is stopped when it tries to change something.
        const QString sneaky = "const d = [].constructor.constructor('return d' + 'b')();\n"
                               "d['set' + 'Status']('o4', 'archived');\nreturn 'done';";
        TEST_ASSERT(!engine.check(sneaky).dynamic);       // the scanner does not see it
        result = TestSupport::request(engine, TestSupport::codeRequest(sneaky));
        TEST_COMPARE(asked, 2);
        TEST_COMPARE(result["status"].toString(), QString("error"));
        TEST_COMPARE(result["phase"].toString(), QString("approval"));
        TEST_ASSERT(result["message"].toString().contains("db.setStatus"));
        TEST_COMPARE(db.archived(), 2);

        // The application's own run is not held to it.
        TEST_COMPARE(TestSupport::runScript(engine, sneaky)["result"].toString(), QString("done"));
        TEST_COMPARE(db.archived(), 3);
    }

    // Each kind of script has its own switch, and its own lock.
    TEST_FUNCTION(testApprovalPerKind)
    {
        TEST_START;
        using Kind = Engine::ScriptKind;

        QTemporaryDir dir;
        TEST_ASSERT(dir.isValid());
        ScriptHost::ScriptLibrary library;
        library.setDirectory(dir.path());
        saveCountOld(library);                                      // the user's, only reads
        ScriptHost::ScriptLibrary::Script script;
        script.name = "Archive one";
        script.code = "db.setStatus('o1', 'archived');\nreturn 1;";
        script.origin = "user";
        library.save(script);
        script.name = "Archive two";                                // saved by the requester
        script.code = "db.setStatus('o2', 'archived');\nreturn 1;";
        script.origin = "assistant";
        library.save(script);

        Engine engine;
        Db db;
        TestSupport::registerDb(engine, db);
        engine.setLibrary(&library);
        QList<Kind> asked;
        engine.setApprovalHandler([&](const Engine::ApprovalRequest& approvalRequest,
                                      std::function<void(const Engine::ApprovalDecision&)> decide) {
            asked << approvalRequest.kind;
            Engine::ApprovalDecision decision;
            decision.approved = true;
            decide(decision);
        });
        auto byName = [&](const QString& name, const QJsonObject& args = QJsonObject()) {
            Engine::Request req;
            req.name = name;
            req.args = args;
            return TestSupport::request(engine, req);
        };
        const QString changing = "db.setStatus('o3', 'archived');\nreturn 1;";
        const QString reading = "return db.list().length;";

        // Only new changing scripts are asked for.
        engine.setApprovalRequired(Kind::NewChanging, true);
        TEST_COMPARE(TestSupport::request(engine, TestSupport::codeRequest(reading))["status"].toString(), QString("ok"));
        TEST_COMPARE(byName("Count old", QJsonObject{{"minAge", 50}})["status"].toString(), QString("ok"));
        TEST_COMPARE(byName("Archive one")["status"].toString(), QString("ok"));
        TEST_COMPARE(TestSupport::request(engine, TestSupport::codeRequest(changing, QJsonObject(), true))["status"].toString(),
                     QString("ok"));
        TEST_ASSERT(asked.isEmpty());
        TEST_COMPARE(db.archived(), 1);                             // "Archive one" ran unasked

        TEST_COMPARE(TestSupport::request(engine, TestSupport::codeRequest(changing))["status"].toString(), QString("ok"));
        TEST_COMPARE(asked, QList<Kind>{Kind::NewChanging});
        // What the requester saved itself is as new as what it hands over.
        TEST_COMPARE(byName("Archive two")["status"].toString(), QString("ok"));
        TEST_COMPARE(asked, (QList<Kind>{Kind::NewChanging, Kind::NewChanging}));
        TEST_COMPARE(db.archived(), 3);

        // The other kinds, one by one.
        asked.clear();
        engine.setApprovalRequired(Kind::NewChanging, false);
        engine.setApprovalRequired(Kind::SavedReading, true);
        byName("Count old", QJsonObject{{"minAge", 50}});
        TestSupport::request(engine, TestSupport::codeRequest(reading));
        TEST_COMPARE(asked, QList<Kind>{Kind::SavedReading});

        engine.setApprovalRequired(Kind::SavedChanging, true);
        byName("Archive one");
        TEST_COMPARE(asked.last(), Kind::SavedChanging);

        engine.setApprovalRequired(Kind::DryRun, true);
        TestSupport::request(engine, TestSupport::codeRequest(changing, QJsonObject(), true));
        TEST_COMPARE(asked.last(), Kind::DryRun);

        engine.setApprovalRequired(Kind::NewReading, true);
        TestSupport::request(engine, TestSupport::codeRequest(reading));
        TEST_COMPARE(asked.last(), Kind::NewReading);
        TEST_COMPARE(asked.size(), 4);

        // One kind locked: the user's choice, and what was stored, leave it alone.
        engine.setApproval(Engine::Approval::Never);
        engine.setApprovalRequired(Kind::NewChanging, true);
        engine.setApprovalUserConfigurable(Kind::NewChanging, false);
        TEST_ASSERT(!engine.isApprovalUserConfigurable(Kind::NewChanging));
        TEST_ASSERT(engine.isApprovalUserConfigurable(Kind::DryRun));
        TEST_ASSERT(!engine.setApprovalRequiredByUser(Kind::NewChanging, false));
        TEST_ASSERT(engine.setApprovalRequiredByUser(Kind::DryRun, true));
        const QJsonObject stored = engine.userSettings()["approval"].toObject();
        TEST_ASSERT(!stored.contains("new_changing"));
        TEST_COMPARE(stored["dry_run"].toBool(), true);
        TEST_COMPARE(stored.size(), 4);
        engine.applyUserSettings(QJsonObject{{"approval", QJsonObject{{"new_changing", false}, {"dry_run", false},
                                                                      {"saved_reading", true}}}});
        TEST_ASSERT(engine.isApprovalRequired(Kind::NewChanging));
        TEST_ASSERT(!engine.isApprovalRequired(Kind::DryRun));
        TEST_ASSERT(engine.isApprovalRequired(Kind::SavedReading));
        // Settings stored when there were three modes still apply.
        engine.applyUserSettings(QJsonObject{{"approval", "changing"}});
        TEST_ASSERT(engine.isApprovalRequired(Kind::SavedChanging));
        TEST_ASSERT(!engine.isApprovalRequired(Kind::SavedReading));
    }

    // The requester gave up while the user was deciding: the run is dropped.
    TEST_FUNCTION(testAbandonedRequest)
    {
        TEST_START;

        Engine engine;
        Db db;
        TestSupport::registerDb(engine, db);
        engine.setApproval(Engine::Approval::Always);
        std::function<void(const Engine::ApprovalDecision&)> pending;
        engine.setApprovalHandler([&](const Engine::ApprovalRequest&,
                                      std::function<void(const Engine::ApprovalDecision&)> decide) {
            pending = decide;
        });

        const QString code = "db.setStatus('o1', 'archived');\nreturn 1;";
        bool gone = false;
        bool called = false;
        engine.requestRun(TestSupport::codeRequest(code), [&](const QJsonObject&) { called = true; },
                          [&]() { return gone; });
        TEST_ASSERT(bool(pending));
        gone = true;
        Engine::ApprovalDecision yes;
        yes.approved = true;
        if (pending)
            pending(yes);
        TestSupport::spin([]() { return false; }, 200);
        TEST_ASSERT(!called);
        TEST_ASSERT(!engine.isRunning());
        TEST_ASSERT(engine.runs().isEmpty());
        TEST_COMPARE(db.writes, 0);

        // Still waiting: the run goes ahead.
        pending = nullptr;
        gone = false;
        QJsonObject result;
        engine.requestRun(TestSupport::codeRequest(code), [&](const QJsonObject& r) {
            result = r;
            called = true;
        }, [&]() { return gone; });
        if (pending)
            pending(yes);
        TEST_ASSERT(TestSupport::spin([&]() { return called; }));
        TEST_COMPARE(result["status"].toString(), QString("ok"));
        TEST_COMPARE(db.writes, 1);
    }

    // The built-in dialog: shows the script and who asks, and Decline carries the reason.
    TEST_FUNCTION(testApprovalDialog)
    {
        TEST_START;

        Engine engine;
        Db db;
        TestSupport::registerDb(engine, db);
        engine.setApproval(Engine::Approval::Always);
        engine.setRequesterName("the assistant");

        const QString code = "db.setStatus('o1', 'archived');\nreturn 1;";
        QString shownCode, shownUses, headline;
        bool coloured = false;
        onApprovalDialog([&](QDialog* dialog) {
            auto* codeEdit = dialog->findChild<QPlainTextEdit*>("scripthost_scriptApprovalCode");
            shownCode = codeEdit->toPlainText();
            coloured = !codeEdit->document()->firstBlock().layout()->formats().isEmpty();
            shownUses = dialog->findChild<QLabel*>("scripthost_scriptApprovalUses")->text();
            for (QLabel* label : dialog->findChildren<QLabel*>()) {
                if (label->text().startsWith("Requested by"))
                    headline = label->text();
            }
            TestSupport::grab(dialog, "script_approval.png");
            dialog->findChild<QLineEdit*>("scripthost_scriptApprovalReason")->setText("not today");
            dialog->findChild<QPushButton*>("scripthost_scriptApprovalDecline")->click();
        });
        QJsonObject result = TestSupport::request(engine, TestSupport::codeRequest(code, QJsonObject{{"x", 1}}));
        TEST_COMPARE(shownCode, code);
        TEST_ASSERT(coloured);
        TEST_ASSERT(shownUses.contains("db.setStatus (changes)"));
        TEST_ASSERT(shownUses.contains("Arguments: {\"x\":1}"));
        TEST_COMPARE(headline, QString("Requested by the assistant: run this script?"));
        TEST_COMPARE(result["phase"].toString(), QString("declined"));
        TEST_COMPARE(result["reason"].toString(), QString("not today"));
        TEST_COMPARE(db.writes, 0);

        // Test run inside the dialog, then Run.
        QString testOutput;
        onApprovalDialog([&](QDialog* dialog) {
            dialog->findChild<QPushButton*>("scripthost_scriptApprovalTest")->click();
            auto* output = dialog->findChild<QPlainTextEdit*>("scripthost_scriptApprovalOutput");
            QTimer::singleShot(600, dialog, [&testOutput, output, dialog]() {
                testOutput = output->toPlainText();
                dialog->findChild<QPushButton*>("scripthost_scriptApprovalRun")->click();
            });
        });
        result = TestSupport::request(engine, TestSupport::codeRequest(code));
        TEST_ASSERT(testOutput.contains("would_call"));
        TEST_COMPARE(result["status"].toString(), QString("ok"));
        TEST_COMPARE(db.writes, 1);         // once: the test run changed nothing
        TEST_ASSERT(engine.runs().last().external);
        TEST_ASSERT(!engine.runs().first().external);    // the test run is the dialog's own

        // "Do not ask again" remembers the exact code.
        onApprovalDialog([](QDialog* dialog) {
            dialog->findChild<QCheckBox*>("scripthost_scriptApprovalRemember")->setChecked(true);
            dialog->findChild<QPushButton*>("scripthost_scriptApprovalRun")->click();
        });
        const QString again = "db.setStatus('o2', 'archived');\nreturn 2;";
        result = TestSupport::request(engine, TestSupport::codeRequest(again));
        TEST_COMPARE(result["result"].toInt(), 2);
        bool dialogSeen = false;
        onApprovalDialog([&dialogSeen](QDialog* dialog) {
            dialogSeen = true;
            dialog->reject();
        }, 10);
        result = TestSupport::request(engine, TestSupport::codeRequest(again));
        TEST_COMPARE(result["result"].toInt(), 2);
        TestSupport::spin([]() { return false; }, 700);
        TEST_ASSERT(!dialogSeen);
    }
};

TEST_INSTANTIATE(TST_ScriptRequests);
