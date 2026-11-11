#pragma once

// Extensions (ScriptExtension): the per-run object in scripts, check() and
// apiDefinition(), did-you-mean on members, no maxCalls cost, the run-end
// hook on every end, coalesced scheduling from the GUI thread, the
// keep-alive and stop(), requested runs, the enable switch, removing an
// extension while it runs, and dry runs.

#include "UnitTest.h"
#include "TestSupport.h"
#include "TestExtension.h"
#include <QElapsedTimer>
#include <QMap>

class TST_ScriptExtensions : public UnitTest::Test
{
    TEST_CLASS(TST_ScriptExtensions)
public:
    TST_ScriptExtensions()
        : Test("TST_ScriptExtensions")
    {
        ADD_TEST(TST_ScriptExtensions::testExtensionInScriptsCheckAndDefinition);
        ADD_TEST(TST_ScriptExtensions::testGlue);
        ADD_TEST(TST_ScriptExtensions::testDidYouMeanOnMembers);
        ADD_TEST(TST_ScriptExtensions::testCallsAreNotMethodCalls);
        ADD_TEST(TST_ScriptExtensions::testThrowErrorAndLog);
        ADD_TEST(TST_ScriptExtensions::testRunEndedOnEveryEnd);
        ADD_TEST(TST_ScriptExtensions::testCoalescedScheduling);
        ADD_TEST(TST_ScriptExtensions::testExceptionInScheduledCall);
        ADD_TEST(TST_ScriptExtensions::testKeepAliveAndStop);
        ADD_TEST(TST_ScriptExtensions::testRequestedRunsNeedTheOptIn);
        ADD_TEST(TST_ScriptExtensions::testEnableSwitch);
        ADD_TEST(TST_ScriptExtensions::testMemberSwitch);
        ADD_TEST(TST_ScriptExtensions::testMemberSwitchInGlue);
        ADD_TEST(TST_ScriptExtensions::testMethodNameClash);
        ADD_TEST(TST_ScriptExtensions::testCheckReportsExtensions);
        ADD_TEST(TST_ScriptExtensions::testRemoveWhileRunning);
        ADD_TEST(TST_ScriptExtensions::testDryRunAndCompileOnly);
    }

private:
    using Engine = ScriptHost::ScriptEngine;
    using Options = TestSupport::Options;

    struct Outcome
    {
        bool        done = false;
        QJsonObject result;
    };

    struct Logs
    {
        QMap<Engine::RunId, QStringList> lines;

        explicit Logs(Engine& engine)
        {
            QObject::connect(&engine, &Engine::runLog, [this](Engine::RunId id, const QString& text) {
                lines[id] << text;
            });
        }
        bool ready(Engine::RunId id) const { return lines.value(id).contains("ready"); }
    };

    static Engine::RunId start(Engine& engine, const QString& code, Outcome& out, const Options& options = Options())
    {
        return engine.run(code, options, [&out](const QJsonObject& r) {
            out.result = r;
            out.done = true;
        });
    }

    static QSharedPointer<TestExtension> addExtension(Engine& engine)
    {
        auto extension = QSharedPointer<TestExtension>::create();
        engine.addExtension(extension);
        return extension;
    }

    static void finish(Engine& engine)
    {
        engine.stopAll();
        TestSupport::spin([&]() { return !engine.isRunning(); });
    }

    TEST_FUNCTION(testExtensionInScriptsCheckAndDefinition)
    {
        TEST_START;

        Engine engine;
        int changes = 0;
        QObject::connect(&engine, &Engine::methodsChanged, [&changes]() { ++changes; });
        auto extension = addExtension(engine);
        TEST_COMPARE(changes, 1);
        TEST_COMPARE(engine.extensions(), QStringList{"ext"});

        Outcome out;
        const Engine::RunId id = start(engine, "return [ext.add(2, 3), ext.runId(), typeof ext.add];", out);
        TEST_ASSERT(TestSupport::spin([&]() { return out.done; }));
        TEST_COMPARE(out.result["status"].toString(), QString("ok"));
        const QJsonArray values = out.result["result"].toArray();
        TEST_COMPARE(values.at(0).toInt(), 5);
        TEST_COMPARE(Engine::RunId(values.at(1).toDouble()), id);
        TEST_COMPARE(values.at(2).toString(), QString("function"));
        TEST_COMPARE(extension->created, 1);
        TEST_ASSERT(extension->createdOnWorker);

        // check() knows it.
        const Engine::Check check = engine.check("const n = ext.add(1, 2);\nreturn n;");
        TEST_ASSERT(check.ok);
        TEST_ASSERT(!check.dynamic);
        TEST_ASSERT(check.methods.isEmpty());

        // apiDefinition() has its declaration, in full and for a query.
        engine.setApiBudgetChars(0);
        TEST_ASSERT(engine.apiDefinition().contains("declare namespace ext {"));
        TEST_ASSERT(engine.apiDefinition("ext").contains("function add(a: number, b: number): number;"));
        TEST_ASSERT(engine.apiDefinition("ext.add").contains("declare namespace ext {"));
        TEST_ASSERT(!engine.apiDefinition("db").contains("declare namespace ext {"));
        // In the index, when everything does not fit.
        engine.setApiBudgetChars(50);
        TEST_ASSERT(engine.apiDefinition().contains("ext - an object of the application"));

        // A change of members or declaration is a change of methods.
        emit extension->changed();
        TEST_COMPARE(changes, 2);

        // A name that is a method's, or no identifier, is refused.
        TestSupport::Db db;
        TestSupport::registerDb(engine, db);
        engine.addExtension(QSharedPointer<TestExtension>::create("db"));
        engine.addExtension(QSharedPointer<TestExtension>::create("log"));
        engine.addExtension(QSharedPointer<TestExtension>::create("a.b"));
        TEST_COMPARE(engine.extensions(), QStringList{"ext"});
    }

    TEST_FUNCTION(testGlue)
    {
        TEST_START;

        Engine engine;
        engine.addExtension(QSharedPointer<TestExtension>::create("glued",
            "(function(native){ return { twice: function(x){ return native.add(x, x); }, info: { version: 2 } }; })"));
        const QJsonObject result = TestSupport::runScript(engine,
            "return glued.twice(4) + glued.info.version + (typeof glued.add);");
        TEST_COMPARE(result["status"].toString(), QString("ok"));
        TEST_COMPARE(result["result"].toString(), QString("10undefined"));

        // Glue that is not a function ends the run before the body.
        Engine broken;
        broken.addExtension(QSharedPointer<TestExtension>::create("bad", "1 +"));
        const QJsonObject failed = TestSupport::runScript(broken, "log('body');\nreturn 1;");
        TEST_COMPARE(failed["phase"].toString(), QString("runtime"));
        TEST_ASSERT(failed["message"].toString().startsWith("the extension 'bad' could not start"));
        TEST_ASSERT(!failed.contains("log"));
    }

    TEST_FUNCTION(testDidYouMeanOnMembers)
    {
        TEST_START;

        Engine engine;
        auto extension = addExtension(engine);

        const Engine::Check typo = engine.check("const a = 1;\nreturn ext.ad(1, 2);");
        TEST_ASSERT(!typo.ok);
        TEST_COMPARE(typo.phase, QString("check"));
        TEST_COMPARE(typo.line, 2);
        TEST_COMPARE(typo.message, QString("'ext' has no member 'ad'"));
        TEST_COMPARE(typo.didYouMean, QString("ext.add"));
        TEST_COMPARE(typo.toJson()["did_you_mean"].toString(), QString("ext.add"));

        // Nested members, the way to them, and what a member has itself.
        TEST_ASSERT(engine.check("return ext.info.version + ext.info.build.date;").ok);
        TEST_ASSERT(engine.check("const i = ext.info;\nreturn i.version;").ok);
        TEST_ASSERT(engine.check("return ext.add.call(null, 1, 2) + ext . add(1, 2);").ok);
        TEST_ASSERT(engine.check("const e = ext;\nreturn e.add(1, 2);").ok);
        TEST_ASSERT(engine.check("return 'ext.nothing';").ok);

        const Engine::Check nested = engine.check("return ext.info.versoin;");
        TEST_ASSERT(!nested.ok);
        TEST_COMPARE(nested.message, QString("'ext' has no member 'info.versoin'"));
        TEST_COMPARE(nested.didYouMean, QString("ext.info.version"));

        const Engine::Check deep = engine.check("return ext.info.build.dat;");
        TEST_COMPARE(deep.didYouMean, QString("ext.info.build.date"));

        const Engine::Check unlike = engine.check("ext.zzzzzzzz();");
        TEST_ASSERT(!unlike.ok);
        TEST_ASSERT(unlike.didYouMean.isEmpty());

        // A script's own `ext` is its own.
        TEST_ASSERT(engine.check("const ext = {}; ext.whatever = 1;").ok);
    }

    TEST_FUNCTION(testCallsAreNotMethodCalls)
    {
        TEST_START;

        Engine engine;
        addExtension(engine);
        Options options;
        options.maxCalls = 2;
        const QJsonObject result = TestSupport::runScript(engine,
            "let n = 0;\nfor (let i = 0; i < 1000; ++i) n = ext.add(n, 1);\nreturn n;", options);
        TEST_COMPARE(result["status"].toString(), QString("ok"));
        TEST_COMPARE(result["result"].toInt(), 1000);
        TEST_ASSERT(!result.contains("calls"));
    }

    TEST_FUNCTION(testThrowErrorAndLog)
    {
        TEST_START;

        Engine engine;
        addExtension(engine);
        Logs logs(engine);

        Outcome caught;
        const Engine::RunId id = start(engine,
            "try { ext.fail('no good'); } catch (e) { ext.note('caught ' + e.message); }\nreturn 1;", caught);
        TEST_ASSERT(TestSupport::spin([&]() { return caught.done; }));
        TEST_COMPARE(caught.result["status"].toString(), QString("ok"));
        TEST_COMPARE(logs.lines[id], QStringList{"caught no good"});

        const QJsonObject failed = TestSupport::runScript(engine, "const a = 1;\next.fail('bad value');\nreturn a;");
        TEST_COMPARE(failed["status"].toString(), QString("error"));
        TEST_COMPARE(failed["phase"].toString(), QString("runtime"));
        TEST_COMPARE(failed["message"].toString(), QString("bad value"));
        TEST_COMPARE(failed["line"].toInt(), 2);
    }

    TEST_FUNCTION(testRunEndedOnEveryEnd)
    {
        TEST_START;

        Engine engine;
        auto extension = addExtension(engine);
        TestSupport::Db db;
        TestSupport::registerDb(engine, db);
        Logs logs(engine);

        // Finished.
        Outcome finished;
        const Engine::RunId finishedId = start(engine, "return ext.add(1, 1);", finished);
        TEST_ASSERT(TestSupport::spin([&]() { return finished.done; }));
        TEST_ASSERT(extension->hasEnded(finishedId));

        // Stopped.
        Outcome stopped;
        const Engine::RunId stoppedId = start(engine, "ext.hold(true);\nlog('ready');\nreturn 1;", stopped);
        TEST_ASSERT(TestSupport::spin([&]() { return logs.ready(stoppedId); }));
        TEST_ASSERT(!extension->hasEnded(stoppedId));
        engine.stop(stoppedId);
        TEST_ASSERT(TestSupport::spin([&]() { return stopped.done; }));
        TEST_COMPARE(stopped.result["phase"].toString(), QString("cancelled"));
        TEST_ASSERT(extension->hasEnded(stoppedId));

        // Error.
        Outcome error;
        const Engine::RunId errorId = start(engine, "throw new Error('x');", error);
        TEST_ASSERT(TestSupport::spin([&]() { return error.done; }));
        TEST_COMPARE(error.result["phase"].toString(), QString("runtime"));
        TEST_ASSERT(extension->hasEnded(errorId));

        // Syntax error: the extension was never created, the hook fires still.
        const int created = extension->created;
        Outcome syntax;
        const Engine::RunId syntaxId = start(engine, "return (;", syntax);
        TEST_ASSERT(TestSupport::spin([&]() { return syntax.done; }));
        TEST_COMPARE(syntax.result["phase"].toString(), QString("syntax"));
        TEST_COMPARE(extension->created, created);
        TEST_ASSERT(extension->hasEnded(syntaxId));

        // Time limit.
        Options timeout;
        timeout.timeoutMs = 200;
        Outcome timedOut;
        const Engine::RunId timeoutId = start(engine, "ext.hold(true);\nreturn 1;", timedOut, timeout);
        TEST_ASSERT(TestSupport::spin([&]() { return timedOut.done; }));
        TEST_COMPARE(timedOut.result["phase"].toString(), QString("limit"));
        TEST_ASSERT(extension->hasEnded(timeoutId));

        // Call limit.
        Options limit;
        limit.maxCalls = 3;
        Outcome limited;
        const Engine::RunId limitId = start(engine, "for (;;) db.list();", limited, limit);
        TEST_ASSERT(TestSupport::spin([&]() { return limited.done; }));
        TEST_COMPARE(limited.result["phase"].toString(), QString("limit"));
        TEST_ASSERT(extension->hasEnded(limitId));

        // Once each, on the GUI thread, after the per-run object was deleted.
        TEST_COMPARE(extension->ended.size(), 6);
        TEST_ASSERT(extension->endedOnGui);
        TEST_ASSERT(extension->probeGoneAtEnd);

        // The engine deleted while a run is active.
        auto lasting = QSharedPointer<TestExtension>::create();
        Engine::RunId lastingId = 0;
        {
            Engine doomed;
            doomed.addExtension(lasting);
            Logs doomedLogs(doomed);
            lastingId = doomed.run("ext.hold(true);\nlog('ready');\nreturn 1;");
            TEST_ASSERT(TestSupport::spin([&]() { return doomedLogs.ready(lastingId); }));
        }
        TEST_COMPARE(lasting->ended, QList<Engine::RunId>{lastingId});
    }

    TEST_FUNCTION(testCoalescedScheduling)
    {
        TEST_START;

        Engine engine;
        auto extension = addExtension(engine);
        Logs logs(engine);
        Outcome out;
        // The callbacks wait while the body sleeps; then only the newest
        // "tick" runs, in the place of the first, and both plain ones.
        const Engine::RunId id = start(engine,
            "ext.onTick(n => { log('tick ' + n); ext.onTick(null); });\n"
            "log('ready');\n"
            "sleep(300);\n"
            "return 1;\n", out);
        TEST_ASSERT(TestSupport::spin([&]() { return logs.ready(id); }));
        const ScriptHost::ScriptRunContext context = extension->context(id);
        ExtensionProbe* probe = extension->probe(id);
        TEST_ASSERT(context.isActive());
        TEST_COMPARE(context.runId(), id);
        TEST_ASSERT(!context.isRequested());
        for (int n = 1; n <= 3; ++n) {
            TEST_ASSERT(context.schedule("tick", [probe, n]() { probe->tick(n); }));
            if (n == 1) {
                TEST_ASSERT(context.schedule(QString(), [context]() { context.log("plain"); }));
                TEST_ASSERT(context.schedule(QString(), [context]() { context.log("plain"); }));
            }
        }
        TEST_ASSERT(TestSupport::spin([&]() { return out.done; }));
        TEST_COMPARE(out.result["status"].toString(), QString("ok"));
        TEST_COMPARE(logs.lines[id], (QStringList{"ready", "tick 3", "plain", "plain"}));

        // Over: nothing is scheduled any more.
        TEST_ASSERT(!context.isActive());
        TEST_ASSERT(!context.schedule("tick", []() {}));
        // Worker-thread functions do nothing elsewhere.
        context.log("from the GUI");
        TEST_COMPARE(logs.lines[id].size(), 4);
        TEST_ASSERT(!ScriptHost::ScriptRunContext().schedule("x", []() {}));

        // A frame loop: ticks keep coming while the handler is slow; the
        // run sees fewer of them than were scheduled, the newest last.
        Outcome loop;
        const Engine::RunId loopId = start(engine,
            "let seen = 0, last = 0;\n"
            "ext.onTick(n => {\n"
            "    ++seen; last = n;\n"
            "    const begin = Date.now();\n"
            "    while (Date.now() - begin < 20) {}\n"
            "    if (n === 100) { ext.onTick(null); log(seen + ' ' + last); }\n"
            "});\n"
            "log('ready');\n"
            "return 1;\n", loop);
        TEST_ASSERT(TestSupport::spin([&]() { return logs.ready(loopId); }));
        const ScriptHost::ScriptRunContext loopContext = extension->context(loopId);
        ExtensionProbe* loopProbe = extension->probe(loopId);
        for (int n = 1; n <= 100; ++n) {
            loopContext.schedule("tick", [loopProbe, n]() { loopProbe->tick(n); });
            TestSupport::spin([]() { return false; }, 1);
        }
        TEST_ASSERT(TestSupport::spin([&]() { return loop.done; }));
        TEST_COMPARE(loop.result["status"].toString(), QString("ok"));
        const QStringList seen = logs.lines[loopId].value(1).split(' ');
        TEST_COMPARE(seen.value(1), QString("100"));
        TEST_ASSERT(seen.value(0).toInt() < 100);
    }

    TEST_FUNCTION(testExceptionInScheduledCall)
    {
        TEST_START;

        Engine engine;
        auto extension = addExtension(engine);
        Logs logs(engine);
        Outcome out;
        const Engine::RunId id = start(engine,
            "ext.onTick(n => {\n"                   // 1
            "    throw new Error('tick ' + n);\n"   // 2
            "});\n"                                 // 3
            "log('ready');\n"                       // 4
            "return 1;\n", out);
        TEST_ASSERT(TestSupport::spin([&]() { return logs.ready(id); }));
        ExtensionProbe* probe = extension->probe(id);
        TEST_ASSERT(extension->context(id).schedule("tick", [probe]() { probe->tick(7); }));
        TEST_ASSERT(TestSupport::spin([&]() { return out.done; }));
        TEST_COMPARE(out.result["status"].toString(), QString("error"));
        TEST_COMPARE(out.result["phase"].toString(), QString("runtime"));
        TEST_COMPARE(out.result["message"].toString(), QString("tick 7"));
        TEST_COMPARE(out.result["line"].toInt(), 2);
    }

    TEST_FUNCTION(testKeepAliveAndStop)
    {
        TEST_START;

        Engine engine;
        auto extension = addExtension(engine);
        Logs logs(engine);

        // Held: it runs on after its body; released (from the GUI thread): it ends.
        Outcome held;
        const Engine::RunId id = start(engine, "ext.hold(true);\nlog('ready');\nreturn 7;", held);
        TEST_ASSERT(TestSupport::spin([&]() { return logs.ready(id); }));
        TestSupport::spin([]() { return false; }, 300);
        TEST_ASSERT(!held.done);
        TEST_ASSERT(engine.isRunning(id));
        extension->context(id).setKeepAlive("other", false);   // not held: changes nothing
        TestSupport::spin([]() { return false; }, 100);
        TEST_ASSERT(!held.done);
        extension->context(id).setKeepAlive("hold", false);
        TEST_ASSERT(TestSupport::spin([&]() { return held.done; }));
        TEST_COMPARE(held.result["status"].toString(), QString("ok"));
        TEST_COMPARE(held.result["result"].toInt(), 7);

        // Released by the script itself.
        const QJsonObject own = TestSupport::runScript(engine,
            "ext.hold(true);\nsetTimeout(() => ext.hold(false), 50);\nreturn 1;");
        TEST_COMPARE(own["status"].toString(), QString("ok"));

        // stop() ends a held run at once.
        Outcome stopped;
        const Engine::RunId stoppedId = start(engine, "ext.hold(true);\nlog('ready');\nreturn 1;", stopped);
        TEST_ASSERT(TestSupport::spin([&]() { return logs.ready(stoppedId); }));
        TestSupport::spin([]() { return false; }, 100);
        QElapsedTimer timer;
        timer.start();
        engine.stop(stoppedId);
        TEST_ASSERT(TestSupport::spin([&]() { return stopped.done; }));
        TEST_ASSERT(timer.elapsed() < 2000);
        TEST_COMPARE(stopped.result["phase"].toString(), QString("cancelled"));
    }

    TEST_FUNCTION(testRequestedRunsNeedTheOptIn)
    {
        TEST_START;

        Engine engine;
        auto extension = addExtension(engine);
        TEST_ASSERT(!engine.isExtensionAllowedForRequestedRuns("ext"));
        const QString code = "const a = 1;\nreturn [ext.add(1, 2), ext.isRequested()];";

        // Not allowed: check() as a requested run sees it says so; the
        // application's own view is unchanged.
        const Engine::Check requested = engine.check(code, true);
        TEST_ASSERT(!requested.ok);
        TEST_COMPARE(requested.phase, QString("check"));
        TEST_COMPARE(requested.line, 2);
        TEST_COMPARE(requested.message, QString("the application does not let requested scripts use 'ext'"));
        TEST_ASSERT(engine.check(code).ok);
        const QJsonObject blocked = TestSupport::request(engine, TestSupport::codeRequest(code));
        TEST_COMPARE(blocked["phase"].toString(), QString("check"));
        TEST_COMPARE(blocked["message"].toString(), requested.message);

        // A run marked external without the check: the extension is absent.
        Options external;
        external.external = true;
        const QJsonObject absent = TestSupport::runScript(engine, "return typeof ext;", external);
        TEST_COMPARE(absent["result"].toString(), QString("undefined"));
        const int created = extension->created;

        // Allowed.
        engine.allowExtensionForRequestedRuns("ext");
        TEST_ASSERT(engine.isExtensionAllowedForRequestedRuns("ext"));
        TEST_ASSERT(engine.check(code, true).ok);
        const QJsonObject allowed = TestSupport::request(engine, TestSupport::codeRequest(code));
        TEST_COMPARE(allowed["status"].toString(), QString("ok"));
        TEST_COMPARE(allowed["result"].toArray(), (QJsonArray{3, true}));
        TEST_COMPARE(extension->created, created + 1);

        // Taken back.
        engine.allowExtensionForRequestedRuns("ext", false);
        TEST_COMPARE(TestSupport::request(engine, TestSupport::codeRequest(code))["phase"].toString(),
                     QString("check"));
    }

    TEST_FUNCTION(testEnableSwitch)
    {
        TEST_START;

        Engine engine;
        auto extension = addExtension(engine);
        engine.setApiBudgetChars(0);
        int changes = 0;
        QObject::connect(&engine, &Engine::methodsChanged, [&changes]() { ++changes; });

        engine.setExtensionEnabled("ext", false);
        TEST_ASSERT(!engine.isExtensionEnabled("ext"));
        TEST_COMPARE(changes, 1);
        TEST_ASSERT(!engine.apiDefinition().contains("declare namespace ext"));
        TEST_ASSERT(!engine.apiDefinition("ext").contains("declare namespace ext"));
        const Engine::Check check = engine.check("return ext.add(1, 2);");
        TEST_ASSERT(!check.ok);
        TEST_COMPARE(check.message, QString("'ext' is disabled"));
        const QJsonObject absent = TestSupport::runScript(engine, "return typeof ext;");
        TEST_COMPARE(absent["result"].toString(), QString("undefined"));
        TEST_COMPARE(extension->created, 0);
        TEST_COMPARE(engine.extensions(), QStringList{"ext"});

        engine.setExtensionEnabled("ext", true);
        TEST_COMPARE(changes, 2);
        TEST_ASSERT(engine.check("return ext.add(1, 2);").ok);
        TEST_COMPARE(TestSupport::runScript(engine, "return ext.add(1, 2);")["result"].toInt(), 3);

        // Durable by name: set before the extension exists.
        Engine early;
        early.setExtensionEnabled("ext", false);
        auto late = addExtension(early);
        TEST_COMPARE(TestSupport::runScript(early, "return typeof ext;")["result"].toString(), QString("undefined"));
        TEST_COMPARE(late->created, 0);
    }

    // One member of a QObject extension (no glue) switched off and on again.
    TEST_FUNCTION(testMemberSwitch)
    {
        TEST_START;

        Engine engine;
        auto extension = addExtension(engine);
        engine.setApiBudgetChars(0);
        int changes = 0;
        QObject::connect(&engine, &Engine::methodsChanged, [&changes]() { ++changes; });

        engine.setExtensionMemberEnabled("ext", "add", false);
        TEST_COMPARE(changes, 1);
        engine.setExtensionMemberEnabled("ext", "add", false);     // no change
        TEST_COMPARE(changes, 1);
        TEST_ASSERT(!engine.isExtensionMemberEnabled("ext", "add"));
        TEST_ASSERT(engine.isExtensionMemberEnabled("ext", "note"));
        TEST_COMPARE(engine.disabledExtensionMembers("ext"), QStringList{"add"});

        // check() rejects it, and does not suggest it.
        const Engine::Check check = engine.check("const a = 1;\nreturn ext.add(1, 2);");
        TEST_ASSERT(!check.ok);
        TEST_COMPARE(check.phase, QString("check"));
        TEST_COMPARE(check.line, 2);
        TEST_COMPARE(check.message, QString("'ext.add' is disabled"));
        TEST_ASSERT(engine.check("return ext.note('x');").ok);
        TEST_COMPARE(engine.check("return ext.ad(1, 2);").didYouMean, QString());

        // Not in the definition; the rest of the extension is.
        TEST_ASSERT(!engine.apiDefinition().contains("function add("));
        TEST_ASSERT(!engine.apiDefinition().contains("Adds two numbers"));
        TEST_ASSERT(engine.apiDefinition().contains("declare namespace ext {"));

        // Absent in a run; the other members work, properties too.
        QJsonObject result = TestSupport::runScript(engine,
            "ext.note('noted');\n"
            "let threw = '';\n"
            "try { ext['add'](1, 2); } catch (e) { threw = e.name; }\n"
            "return [typeof ext.add, 'add' in ext, typeof ext.note, ext.isDryRun(), threw, ext.objectName];");
        TEST_COMPARE(result["status"].toString(), QString("ok"));
        const QJsonArray values = result["result"].toArray();
        TEST_COMPARE(values.at(0).toString(), QString("undefined"));
        TEST_ASSERT(!values.at(1).toBool());
        TEST_COMPARE(values.at(2).toString(), QString("function"));
        TEST_ASSERT(!values.at(3).toBool());
        TEST_COMPARE(values.at(4).toString(), QString("TypeError"));
        TEST_ASSERT(result["log"].toArray().contains(QJsonValue("noted")));

        // On again.
        engine.setExtensionMemberEnabled("ext", "add", true);
        TEST_COMPARE(changes, 2);
        TEST_ASSERT(engine.isExtensionMemberEnabled("ext", "add"));
        TEST_ASSERT(engine.disabledExtensionMembers("ext").isEmpty());
        TEST_ASSERT(engine.check("return ext.add(1, 2);").ok);
        TEST_ASSERT(engine.apiDefinition().contains("function add(a: number, b: number): number;"));
        TEST_COMPARE(TestSupport::runScript(engine, "return ext.add(1, 2);")["result"].toInt(), 3);

        // Precedence: the extension switched off switches every member off;
        // the member keeps its own setting for when it comes back.
        engine.setExtensionMemberEnabled("ext", "note", false);
        engine.setExtensionEnabled("ext", false);
        TEST_ASSERT(!engine.isExtensionMemberEnabled("ext", "add"));
        engine.setExtensionEnabled("ext", true);
        TEST_ASSERT(engine.isExtensionMemberEnabled("ext", "add"));
        TEST_ASSERT(!engine.isExtensionMemberEnabled("ext", "note"));

        // Durable by name: set before the extension exists.
        Engine early;
        early.setExtensionMemberEnabled("ext", "add", false);
        addExtension(early);
        TEST_COMPARE(TestSupport::runScript(early, "return typeof ext.add;")["result"].toString(), QString("undefined"));
    }

    // Glue: nested members and namespaces, getters, a frozen object.
    TEST_FUNCTION(testMemberSwitchInGlue)
    {
        TEST_START;

        Engine engine;
        const QString glue =
            "(function(native){ var n = 0; var g = {"
            " twice: function(x){ return native.add(x, x); },"
            " info: Object.freeze({ version: 2, build: Object.freeze({ date: 'today' }) }) };"
            " Object.defineProperty(g, 'count', { get: function(){ return ++n; } });"
            " return Object.freeze(g); })";
        engine.addExtension(QSharedPointer<TestExtension>::create("ext", glue));

        // A namespace: everything under it goes.
        engine.setExtensionMemberEnabled("ext", "info", false);
        TEST_ASSERT(!engine.isExtensionMemberEnabled("ext", "info.build.date"));
        Engine::Check check = engine.check("return ext.info.build.date;");
        TEST_ASSERT(!check.ok);
        TEST_COMPARE(check.message, QString("'ext.info' is disabled"));
        QJsonObject result = TestSupport::runScript(engine,
            "return [typeof ext.info, ext.twice(3), ext.count, ext.count, Object.isFrozen(ext)];");
        TEST_COMPARE(result["status"].toString(), QString("ok"));
        QJsonArray values = result["result"].toArray();
        TEST_COMPARE(values.at(0).toString(), QString("undefined"));
        TEST_COMPARE(values.at(1).toInt(), 6);
        TEST_COMPARE(values.at(2).toInt(), 1);      // the getter, still called each time
        TEST_COMPARE(values.at(3).toInt(), 2);
        TEST_ASSERT(values.at(4).toBool());

        // A member below a namespace: its siblings stay. Enabling it does not
        // bring it back while the namespace is off.
        engine.setExtensionMemberEnabled("ext", "info", true);
        engine.setExtensionMemberEnabled("ext", "info.build.date", false);
        check = engine.check("return ext.info.build.date;");
        TEST_COMPARE(check.message, QString("'ext.info.build.date' is disabled"));
        TEST_ASSERT(engine.check("return ext.info.version;").ok);
        result = TestSupport::runScript(engine,
            "return [ext.info.version, typeof ext.info.build, typeof ext.info.build.date, Object.isFrozen(ext.info)];");
        values = result["result"].toArray();
        TEST_COMPARE(values.at(0).toInt(), 2);
        TEST_COMPARE(values.at(1).toString(), QString("object"));
        TEST_COMPARE(values.at(2).toString(), QString("undefined"));
        TEST_ASSERT(values.at(3).toBool());
        engine.setExtensionMemberEnabled("ext", "info", false);
        TEST_ASSERT(!engine.isExtensionMemberEnabled("ext", "info.version"));
        engine.setExtensionMemberEnabled("ext", "info", true);
        TEST_ASSERT(engine.isExtensionMemberEnabled("ext", "info.version"));
        TEST_ASSERT(!engine.isExtensionMemberEnabled("ext", "info.build.date"));
    }

    // I-007: a method in an extension's name space is refused; other name spaces are fine.
    TEST_FUNCTION(testMethodNameClash)
    {
        TEST_START;

        Engine engine;
        addExtension(engine);
        auto handler = [](const QJsonObject&) -> QJsonValue { return 1; };
        engine.registerMethod(ScriptHost::ScriptMethod("ext"), handler);
        engine.registerMethod(ScriptHost::ScriptMethod("ext.add"), handler);
        engine.registerAsyncMethod(ScriptHost::ScriptMethod("ext.later"),
                                   [](const QJsonObject&, ScriptHost::ScriptCompletion done) { done.finish(1); });
        TEST_ASSERT(!engine.hasMethod("ext"));
        TEST_ASSERT(!engine.hasMethod("ext.add"));
        TEST_ASSERT(!engine.hasMethod("ext.later"));

        // A name space no extension uses, or one that only starts like one.
        engine.registerMethod(ScriptHost::ScriptMethod("ui.tree"), handler);
        engine.registerMethod(ScriptHost::ScriptMethod("extra.read"), handler);
        engine.registerMethod(ScriptHost::ScriptMethod("next"), handler);
        TEST_ASSERT(engine.hasMethod("ui.tree"));
        TEST_ASSERT(engine.hasMethod("extra.read"));
        TEST_ASSERT(engine.hasMethod("next"));

        // Once the extension is gone, the name is free.
        engine.removeExtension("ext");
        engine.registerMethod(ScriptHost::ScriptMethod("ext.add"), handler);
        TEST_ASSERT(engine.hasMethod("ext.add"));
    }

    // I-008: check() names the extensions and members a script uses.
    TEST_FUNCTION(testCheckReportsExtensions)
    {
        TEST_START;

        Engine engine;
        addExtension(engine);
        engine.addExtension(QSharedPointer<TestExtension>::create("other"));
        TestSupport::Db db;
        TestSupport::registerDb(engine, db);

        const Engine::Check check = engine.check(
            "ext.add(1, 2);\n"
            "const v = ext.info.version.toFixed(1);\n"
            "other.note('x');\n"
            "ext.add(2, 3);\n"
            "const b = ext.info.build;\n"
            "return db.list();");
        TEST_ASSERT(check.ok);
        TEST_COMPARE(check.extensions, (QStringList{"ext", "ext.add", "ext.info.version", "ext.info.build", "other",
                                                    "other.note"}));
        TEST_COMPARE(check.methods, QStringList{"db.list"});

        // Used as a whole: the name only.
        TEST_COMPARE(engine.check("const e = ext;\nreturn e;").extensions, QStringList{"ext"});
        TEST_ASSERT(engine.check("return 1;").extensions.isEmpty());
    }

    TEST_FUNCTION(testRemoveWhileRunning)
    {
        TEST_START;

        Engine engine;
        auto extension = addExtension(engine);
        Logs logs(engine);
        Outcome out;
        const Engine::RunId id = start(engine,
            "ext.hold(true);\n"
            "setTimeout(() => { log(ext.add(20, 22)); ext.hold(false); }, 300);\n"
            "log('ready');\n"
            "return 1;\n", out);
        TEST_ASSERT(TestSupport::spin([&]() { return logs.ready(id); }));

        int changes = 0;
        QObject::connect(&engine, &Engine::methodsChanged, [&changes]() { ++changes; });
        engine.removeExtension("ext");
        TEST_COMPARE(changes, 1);
        TEST_ASSERT(engine.extensions().isEmpty());
        TEST_ASSERT(!engine.apiDefinition().contains("declare namespace ext"));
        // New runs go without it; the active one keeps it to its end.
        TEST_COMPARE(TestSupport::runScript(engine, "return typeof ext;")["result"].toString(), QString("undefined"));
        TEST_ASSERT(TestSupport::spin([&]() { return out.done; }));
        TEST_COMPARE(out.result["status"].toString(), QString("ok"));
        TEST_COMPARE(logs.lines[id], (QStringList{"ready", "42"}));
        TEST_ASSERT(extension->hasEnded(id));
        TEST_COMPARE(extension->ended.size(), 1);
    }

    TEST_FUNCTION(testDryRunAndCompileOnly)
    {
        TEST_START;

        Engine engine;
        auto extension = addExtension(engine);

        // check() and its syntax check create nothing.
        TEST_ASSERT(engine.check("return ext.add(1, 2);").ok);
        TEST_COMPARE(extension->created, 0);

        // A dry run creates the object and tells it.
        Options dry;
        dry.dryRun = true;
        const QJsonObject result = TestSupport::runScript(engine, "return [ext.isDryRun(), ext.add(1, 2)];", dry);
        TEST_COMPARE(result["status"].toString(), QString("ok"));
        TEST_COMPARE(result["result"].toArray(), (QJsonArray{true, 3}));
        TEST_COMPARE(extension->created, 1);
        const QJsonObject wet = TestSupport::runScript(engine, "return ext.isDryRun();");
        TEST_COMPARE(wet["result"].toBool(), false);
    }
};

TEST_INSTANTIATE(TST_ScriptExtensions);
