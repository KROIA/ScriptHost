#pragma once

// Several scripts at once: run ids, stopping one or all, the cap on runs at
// the same time, per-run limits, the script timers, prompt stops, the run*
// signals' ids, the log window, print() / console.log() with the output
// device, and the single debug run.

#include "UnitTest.h"
#include "TestSupport.h"
#include <QBuffer>
#include <QElapsedTimer>
#include <QList>
#include <QMap>
#include <QPair>
#include <vector>

class TST_ScriptConcurrency : public UnitTest::Test
{
    TEST_CLASS(TST_ScriptConcurrency)
public:
    TST_ScriptConcurrency()
        : Test("TST_ScriptConcurrency")
    {
        ADD_TEST(TST_ScriptConcurrency::testTwoRunsInParallel);
        ADD_TEST(TST_ScriptConcurrency::testStopOneLeavesTheOther);
        ADD_TEST(TST_ScriptConcurrency::testStopAll);
        ADD_TEST(TST_ScriptConcurrency::testConcurrencyCap);
        ADD_TEST(TST_ScriptConcurrency::testPerRunTimeLimit);
        ADD_TEST(TST_ScriptConcurrency::testPerRunCallLimit);
        ADD_TEST(TST_ScriptConcurrency::testRequestedRunKeepsEngineLimits);
        ADD_TEST(TST_ScriptConcurrency::testTimeoutOrderAndArguments);
        ADD_TEST(TST_ScriptConcurrency::testIntervalClearedFromItsCallback);
        ADD_TEST(TST_ScriptConcurrency::testClearedTimeoutNeverFires);
        ADD_TEST(TST_ScriptConcurrency::testResultAfterTimersDrain);
        ADD_TEST(TST_ScriptConcurrency::testTimersAreNotMethodCalls);
        ADD_TEST(TST_ScriptConcurrency::testExceptionInTimerCallback);
        ADD_TEST(TST_ScriptConcurrency::testTimersInCheckAndDefinition);
        ADD_TEST(TST_ScriptConcurrency::testStopDuringSleep);
        ADD_TEST(TST_ScriptConcurrency::testStopDuringTimerWait);
        ADD_TEST(TST_ScriptConcurrency::testStopDuringAsyncCall);
        ADD_TEST(TST_ScriptConcurrency::testStopDuringPureLoop);
        ADD_TEST(TST_ScriptConcurrency::testStopInsideARepeatedCallback);
        ADD_TEST(TST_ScriptConcurrency::testSignalsCarryTheRunId);
        ADD_TEST(TST_ScriptConcurrency::testLogKeepsTheLatestLines);
        ADD_TEST(TST_ScriptConcurrency::testPrintAndConsoleLogAreLog);
        ADD_TEST(TST_ScriptConcurrency::testOutputTagsEachRun);
        ADD_TEST(TST_ScriptConcurrency::testOutputOffAndDeletedDevice);
        ADD_TEST(TST_ScriptConcurrency::testOneDebugRunAtATime);
    }

private:
    using Engine = ScriptHost::ScriptEngine;
    using Options = TestSupport::Options;
    using Db = TestSupport::Db;

    // What a run's callback got.
    struct Outcome
    {
        bool        done = false;
        QJsonObject result;
    };

    static Engine::RunId start(Engine& engine, const QString& code, Outcome& out, const Options& options = Options())
    {
        return engine.run(code, options, [&out](const QJsonObject& r) {
            out.result = r;
            out.done = true;
        });
    }

    // gate.wait(key): an asynchronous method that answers only when the test
    // opens the gate - a run that lives exactly as long as the test needs.
    struct Gate
    {
        QMap<QString, ScriptHost::ScriptCompletion> waiting;

        void install(Engine& engine)
        {
            ScriptHost::ScriptMethod wait("gate.wait");
            wait.setDescription("Waits until the test opens the gate.").setReadOnly(true)
                .addParameter("key", "string", "Which gate");
            engine.registerAsyncMethod(wait, [this](const QJsonObject& args, ScriptHost::ScriptCompletion done) {
                waiting.insert(args["key"].toString(), done);
            });
        }
        bool isWaiting(const QString& key) const { return waiting.contains(key); }
        void open(const QString& key, const QJsonValue& value) { waiting.take(key).finish(value); }
    };

    // Starts the script, waits until `ready`, stops it and returns how long
    // the stop took in ms; -1 when the run did not start or never ended.
    static qint64 stopWhenReady(Engine& engine, const QString& code, const std::function<bool()>& ready,
                                QJsonObject& result)
    {
        Outcome out;
        const Engine::RunId id = start(engine, code, out);
        if (id == 0 || !TestSupport::spin(ready, 5000)) {
            // `out` goes now: no callback may reach it later.
            engine.stopAll();
            TestSupport::spin([&]() { return !engine.isRunning(); }, 10000);
            return -1;
        }
        // A little longer, so that the run is well inside its wait.
        TestSupport::spin([]() { return false; }, 100);
        QElapsedTimer timer;
        timer.start();
        engine.stop(id);
        if (!TestSupport::spin([&]() { return out.done; }, 10000)) {
            engine.stopAll();
            TestSupport::spin([&]() { return !engine.isRunning(); }, 10000);
            return -1;
        }
        result = out.result;
        return timer.elapsed();
    }

    // ---- several runs ------------------------------------------------------

    TEST_FUNCTION(testTwoRunsInParallel)
    {
        TEST_START;

        Gate gate;
        Engine engine;
        Db db;
        TestSupport::registerDb(engine, db);
        gate.install(engine);

        Outcome a, b;
        const Engine::RunId idA = start(engine, "const v = gate.wait('a');\nreturn db.read('o2').age + v;", a);
        const Engine::RunId idB = start(engine, "const v = gate.wait('b');\nreturn db.list().length + v;", b);
        TEST_ASSERT(idA != 0 && idB != 0 && idA != idB);
        TEST_COMPARE(engine.activeRuns(), (QList<Engine::RunId>{idA, idB}));
        TEST_ASSERT(engine.isRunning(idA) && engine.isRunning(idB));
        TEST_COMPARE(engine.activeRun(idA).id, idA);
        TEST_COMPARE(engine.activeRun(idB).code, QString("const v = gate.wait('b');\nreturn db.list().length + v;"));

        // Both wait at the same time; the second one finishes first.
        TEST_ASSERT(TestSupport::spin([&]() { return gate.isWaiting("a") && gate.isWaiting("b"); }));
        gate.open("b", 100);
        TEST_ASSERT(TestSupport::spin([&]() { return b.done; }));
        TEST_COMPARE(b.result["status"].toString(), QString("ok"));
        TEST_COMPARE(b.result["result"].toInt(), 106);
        TEST_ASSERT(!a.done);
        TEST_COMPARE(engine.activeRuns(), QList<Engine::RunId>{idA});
        TEST_ASSERT(!engine.isRunning(idB));
        TEST_COMPARE(engine.activeRun(idB).id, Engine::RunId(0));

        gate.open("a", 1);
        TEST_ASSERT(TestSupport::spin([&]() { return a.done; }));
        TEST_COMPARE(a.result["status"].toString(), QString("ok"));
        TEST_COMPARE(a.result["result"].toInt(), 21);
        TEST_ASSERT(engine.activeRuns().isEmpty());
        TEST_ASSERT(!engine.isRunning());

        // Both on record, with their ids.
        TEST_COMPARE(engine.runs().size(), 2);
        if (engine.runs().size() == 2) {
            TEST_COMPARE(engine.runs()[0].id, idB);
            TEST_COMPARE(engine.runs()[1].id, idA);
        }
    }

    TEST_FUNCTION(testStopOneLeavesTheOther)
    {
        TEST_START;

        Gate gate;
        Engine engine;
        gate.install(engine);

        Outcome a, b;
        const Engine::RunId idA = start(engine, "sleep(60000);\nreturn 'a';", a);
        const Engine::RunId idB = start(engine, "return gate.wait('b') + '!';", b);
        TEST_ASSERT(TestSupport::spin([&]() { return gate.isWaiting("b"); }));

        engine.stop(idA);
        TEST_ASSERT(TestSupport::spin([&]() { return a.done; }, 5000));
        TEST_COMPARE(a.result["status"].toString(), QString("error"));
        TEST_COMPARE(a.result["phase"].toString(), QString("cancelled"));
        TEST_ASSERT(!b.done);
        TEST_ASSERT(engine.isRunning(idB));
        TEST_ASSERT(!engine.isRunning(idA));

        // Stopping a run that is over, or an id there never was, does nothing.
        engine.stop(idA);
        engine.stop(Engine::RunId(9999));
        TEST_ASSERT(engine.isRunning(idB));

        gate.open("b", "b");
        TEST_ASSERT(TestSupport::spin([&]() { return b.done; }));
        TEST_COMPARE(b.result["status"].toString(), QString("ok"));
        TEST_COMPARE(b.result["result"].toString(), QString("b!"));
    }

    TEST_FUNCTION(testStopAll)
    {
        TEST_START;

        Gate gate;
        Engine engine;
        gate.install(engine);

        Outcome a, b, c;
        start(engine, "sleep(60000);", a);
        start(engine, "gate.wait('b');", b);
        start(engine, "setTimeout(() => {}, 60000);\nreturn 1;", c);
        TEST_COMPARE(engine.activeRuns().size(), 3);
        TEST_ASSERT(TestSupport::spin([&]() { return gate.isWaiting("b"); }));

        engine.stopAll();
        TEST_ASSERT(TestSupport::spin([&]() { return a.done && b.done && c.done; }, 5000));
        TEST_COMPARE(a.result["phase"].toString(), QString("cancelled"));
        TEST_COMPARE(b.result["phase"].toString(), QString("cancelled"));
        TEST_COMPARE(c.result["phase"].toString(), QString("cancelled"));
        TEST_ASSERT(engine.activeRuns().isEmpty());
        TEST_ASSERT(!engine.isRunning());
        TEST_COMPARE(engine.runs().size(), 3);
    }

    TEST_FUNCTION(testConcurrencyCap)
    {
        TEST_START;

        Gate gate;
        Engine engine;
        gate.install(engine);
        Engine::Limits limits;
        limits.maxConcurrentRuns = 2;
        engine.setLimits(limits);
        TEST_COMPARE(engine.limits().maxConcurrentRuns, 2);
        int started = 0;
        QObject::connect(&engine, &Engine::runStarted, [&]() { ++started; });

        Outcome a, b, third;
        TEST_ASSERT(start(engine, "return gate.wait('a');", a) != 0);
        TEST_ASSERT(start(engine, "return gate.wait('b');", b) != 0);
        // The third: refused at once, with the reason.
        TEST_COMPARE(start(engine, "return 3;", third), Engine::RunId(0));
        TEST_ASSERT(third.done);
        TEST_COMPARE(third.result["status"].toString(), QString("error"));
        TEST_COMPARE(third.result["phase"].toString(), QString("unavailable"));
        TEST_ASSERT(third.result["message"].toString().contains("the limit of 2"));
        TEST_COMPARE(started, 2);
        TEST_COMPARE(engine.activeRuns().size(), 2);
        // A requested run as well.
        const QJsonObject requested = TestSupport::request(engine, TestSupport::codeRequest("return 4;"));
        TEST_COMPARE(requested["phase"].toString(), QString("unavailable"));

        // One ends: the next one starts.
        TEST_ASSERT(TestSupport::spin([&]() { return gate.isWaiting("a"); }));
        gate.open("a", 1);
        TEST_ASSERT(TestSupport::spin([&]() { return a.done; }));
        Outcome fourth;
        TEST_ASSERT(start(engine, "return 4;", fourth) != 0);
        TEST_ASSERT(TestSupport::spin([&]() { return fourth.done; }));
        TEST_COMPARE(fourth.result["result"].toInt(), 4);

        // 0 = no limit.
        limits.maxConcurrentRuns = 0;
        engine.setLimits(limits);
        std::vector<Outcome> many(10);
        int accepted = 0;
        for (Outcome& out : many)
            accepted += start(engine, "sleep(60000);", out) != 0 ? 1 : 0;
        TEST_COMPARE(accepted, 10);
        engine.stopAll();
        TEST_ASSERT(TestSupport::spin([&]() { return !engine.isRunning(); }, 5000));
    }

    // ---- per-run limits ----------------------------------------------------

    TEST_FUNCTION(testPerRunTimeLimit)
    {
        TEST_START;

        Engine engine;
        Engine::Limits limits;
        limits.timeoutMs = 200;
        engine.setLimits(limits);

        // No limit for this run: outlives the engine's.
        Options unlimited;
        unlimited.timeoutMs = 0;
        QJsonObject result = TestSupport::runScript(engine, "sleep(600);\nreturn 'done';", unlimited);
        TEST_COMPARE(result["status"].toString(), QString("ok"));
        TEST_COMPARE(result["result"].toString(), QString("done"));
        // ...with timers, too.
        result = TestSupport::runScript(engine, "setTimeout(() => log('late'), 600);\nreturn 1;", unlimited);
        TEST_COMPARE(result["status"].toString(), QString("ok"));
        TEST_COMPARE(result["log"].toArray().last().toString(), QString("late"));

        // The engine's limit (-1, the default) still holds for the others.
        result = TestSupport::runScript(engine, "sleep(60000);\nreturn 1;");
        TEST_COMPARE(result["phase"].toString(), QString("limit"));

        // A small limit of its own, under a large engine limit.
        limits.timeoutMs = 300000;
        engine.setLimits(limits);
        Options small;
        small.timeoutMs = 200;
        QElapsedTimer timer;
        timer.start();
        result = TestSupport::runScript(engine, "setTimeout(() => log('never'), 60000);\nreturn 1;", small);
        TEST_COMPARE(result["status"].toString(), QString("error"));
        TEST_COMPARE(result["phase"].toString(), QString("limit"));
        TEST_ASSERT(result["message"].toString().contains("time limit"));
        TEST_ASSERT(timer.elapsed() < 5000);
    }

    TEST_FUNCTION(testPerRunCallLimit)
    {
        TEST_START;

        Engine engine;
        Db db;
        TestSupport::registerDb(engine, db);
        Engine::Limits limits;
        limits.maxCalls = 5;
        engine.setLimits(limits);
        const QString code = "for (let i = 0; i < 20; i++)\n    db.read('o1');\nreturn 'all';";

        Options unlimited;
        unlimited.maxCalls = 0;
        QJsonObject result = TestSupport::runScript(engine, code, unlimited);
        TEST_COMPARE(result["status"].toString(), QString("ok"));
        TEST_COMPARE(result["calls"].toObject()["db.read"].toInt(), 20);

        result = TestSupport::runScript(engine, code);
        TEST_COMPARE(result["phase"].toString(), QString("limit"));
        TEST_COMPARE(result["message"].toString(), QString("stopped after 5 method calls (the limit)"));

        Options own;
        own.maxCalls = 3;
        result = TestSupport::runScript(engine, code, own);
        TEST_COMPARE(result["phase"].toString(), QString("limit"));
        TEST_COMPARE(result["message"].toString(), QString("stopped after 3 method calls (the limit)"));
    }

    // A request comes from outside the application: it cannot pick limits,
    // it has the engine's.
    TEST_FUNCTION(testRequestedRunKeepsEngineLimits)
    {
        TEST_START;

        Engine engine;
        Db db;
        TestSupport::registerDb(engine, db);
        Engine::Limits limits;
        limits.timeoutMs = 300;
        limits.maxCalls = 3;
        engine.setLimits(limits);

        QElapsedTimer timer;
        timer.start();
        QJsonObject result = TestSupport::request(engine,
            TestSupport::codeRequest("setTimeout(() => log('late'), 60000);\nreturn 1;"));
        TEST_COMPARE(result["status"].toString(), QString("error"));
        TEST_COMPARE(result["phase"].toString(), QString("limit"));
        TEST_ASSERT(result["message"].toString().contains("time limit"));
        TEST_ASSERT(timer.elapsed() < 5000);

        result = TestSupport::request(engine,
            TestSupport::codeRequest("for (let i = 0; i < 10; i++)\n    db.read('o1');\nreturn 1;"));
        TEST_COMPARE(result["phase"].toString(), QString("limit"));
        TEST_COMPARE(result["message"].toString(), QString("stopped after 3 method calls (the limit)"));
    }

    // ---- timers --------------------------------------------------------------

    TEST_FUNCTION(testTimeoutOrderAndArguments)
    {
        TEST_START;

        Engine engine;
        const QJsonObject result = TestSupport::runScript(engine,
            "setTimeout((a, b) => log('third', a, b), 200, 'x', 2);\n"
            "setTimeout(() => log('first'), 0);\n"
            "setTimeout(t => log('second', t), 100, 'y');\n"
            "log('body');\n"
            "return 0;\n");
        TEST_COMPARE(result["status"].toString(), QString("ok"));
        TEST_COMPARE(result["result"].toInt(), 0);
        QStringList log;
        for (const QJsonValue& line : result["log"].toArray())
            log << line.toString();
        TEST_COMPARE(log, (QStringList{"body", "first", "second y", "third x 2"}));
        TEST_ASSERT(result["duration_ms"].toInt() >= 200);
    }

    TEST_FUNCTION(testIntervalClearedFromItsCallback)
    {
        TEST_START;

        Engine engine;
        const QJsonObject result = TestSupport::runScript(engine,
            "let n = 0;\n"
            "const t = setInterval(step => {\n"
            "    n += step;\n"
            "    log('tick ' + n);\n"
            "    if (n >= 3)\n"
            "        clearInterval(t);\n"
            "}, 5, 1);\n"
            "return 'started';\n");
        TEST_COMPARE(result["status"].toString(), QString("ok"));
        TEST_COMPARE(result["result"].toString(), QString("started"));
        QStringList log;
        for (const QJsonValue& line : result["log"].toArray())
            log << line.toString();
        TEST_COMPARE(log, (QStringList{"tick 1", "tick 2", "tick 3"}));
    }

    TEST_FUNCTION(testClearedTimeoutNeverFires)
    {
        TEST_START;

        Engine engine;
        const QJsonObject result = TestSupport::runScript(engine,
            "const t = setTimeout(() => log('never'), 50);\n"
            "setTimeout(() => log('other'), 100);\n"
            "clearTimeout(t);\n"
            "clearTimeout(12345);\n"         // no such timer: nothing happens
            "return 1;\n");
        TEST_COMPARE(result["status"].toString(), QString("ok"));
        TEST_COMPARE(result["log"].toArray().size(), 1);
        TEST_COMPARE(result["log"].toArray().first().toString(), QString("other"));
    }

    // The run goes on after its body returned, until no timer is left; the
    // result is what the body returned.
    TEST_FUNCTION(testResultAfterTimersDrain)
    {
        TEST_START;

        Engine engine;
        QStringList logged;
        QObject::connect(&engine, &Engine::runLog, [&](Engine::RunId, const QString& text) { logged << text; });
        QStringList loggedAtTheEnd;
        bool done = false;
        QJsonObject result;
        engine.run("setTimeout(() => { log('late'); return 'ignored'; }, 150);\n"
                   "log('body done');\n"
                   "return 'body';\n",
                   Options(), [&](const QJsonObject& r) {
            result = r;
            loggedAtTheEnd = logged;
            done = true;
        });
        TEST_ASSERT(TestSupport::spin([&]() { return logged.contains("body done"); }));
        TEST_ASSERT(TestSupport::spin([&]() { return done; }));
        TEST_COMPARE(loggedAtTheEnd, (QStringList{"body done", "late"}));
        TEST_COMPARE(result["status"].toString(), QString("ok"));
        TEST_COMPARE(result["result"].toString(), QString("body"));
        TEST_ASSERT(!result.contains("note"));
        TEST_ASSERT(result["duration_ms"].toInt() >= 150);
    }

    TEST_FUNCTION(testTimersAreNotMethodCalls)
    {
        TEST_START;

        Engine engine;
        Db db;
        TestSupport::registerDb(engine, db);
        Engine::Limits limits;
        limits.maxCalls = 3;
        engine.setLimits(limits);

        const QJsonObject result = TestSupport::runScript(engine,
            "let ticks = 0;\n"
            "const t = setInterval(() => {\n"
            "    if (++ticks >= 10)\n"
            "        clearInterval(t);\n"
            "}, 1);\n"
            "for (let i = 0; i < 10; i++)\n"
            "    clearTimeout(setTimeout(() => {}, 1));\n"
            "setTimeout(() => log(db.read('o1').name), 1);\n"
            "return db.list().length;\n");
        TEST_COMPARE(result["status"].toString(), QString("ok"));
        TEST_COMPARE(result["result"].toInt(), 6);
        const QJsonObject calls = result["calls"].toObject();
        TEST_COMPARE(calls.keys(), (QStringList{"db.list", "db.read"}));
        // A method called from a timer's function is a call like any other.
        TEST_COMPARE(calls["db.read"].toInt(), 1);
        TEST_COMPARE(result["log"].toArray().first().toString(), QString("Object 1"));
    }

    TEST_FUNCTION(testExceptionInTimerCallback)
    {
        TEST_START;

        Engine engine;
        const QJsonObject result = TestSupport::runScript(engine,
            "let n = 0;\n"                              // 1
            "setTimeout(() => {\n"                      // 2
            "    n++;\n"                                // 3
            "    throw new Error('in the timer');\n"    // 4
            "}, 10);\n"                                 // 5
            "setTimeout(() => log('after'), 500);\n"    // 6
            "return 1;\n");                             // 7
        TEST_COMPARE(result["status"].toString(), QString("error"));
        TEST_COMPARE(result["phase"].toString(), QString("runtime"));
        TEST_COMPARE(result["message"].toString(), QString("in the timer"));
        TEST_COMPARE(result["line"].toInt(), 4);
        // The run ended there: the later timer did not fire.
        TEST_ASSERT(!result.contains("log"));
        TEST_ASSERT(!engine.isRunning());
    }

    TEST_FUNCTION(testTimersInCheckAndDefinition)
    {
        TEST_START;

        Engine engine;
        Db db;
        TestSupport::registerDb(engine, db);

        const Engine::Check check = engine.check(
            "const t = setInterval(() => db.read('o1'), 10);\n"
            "clearInterval(t);\n"
            "clearTimeout(setTimeout(n => log(n), 5, 1));\n"
            "return 1;\n");
        TEST_ASSERT(check.ok);
        TEST_ASSERT(check.methods.contains("db.read"));
        TEST_ASSERT(check.changingMethods.isEmpty());
        // What the timer's function calls is checked as well.
        const Engine::Check wrong = engine.check("setTimeout(() => db.nothing(), 5);");
        TEST_ASSERT(!wrong.ok);
        TEST_COMPARE(wrong.phase, QString("check"));

        const QString all = engine.apiDefinition();
        TEST_ASSERT(all.contains("declare function setTimeout("));
        TEST_ASSERT(all.contains("declare function setInterval("));
        TEST_ASSERT(all.contains("declare function clearTimeout("));
        TEST_ASSERT(all.contains("declare function clearInterval("));
        TEST_ASSERT(engine.apiDefinition("setInterval").contains("function setInterval("));
    }

    // ---- stopping promptly -----------------------------------------------------

    TEST_FUNCTION(testStopDuringSleep)
    {
        TEST_START;

        Engine engine;
        bool ready = false;
        QObject::connect(&engine, &Engine::runLog, [&]() { ready = true; });
        QJsonObject result;
        const qint64 ms = stopWhenReady(engine, "log('ready');\nsleep(60000);\nreturn 1;",
                                        [&]() { return ready; }, result);
        TEST_ASSERT(ms >= 0 && ms < 2000);
        TEST_COMPARE(result["phase"].toString(), QString("cancelled"));
    }

    TEST_FUNCTION(testStopDuringTimerWait)
    {
        TEST_START;

        Engine engine;
        bool ready = false;
        QObject::connect(&engine, &Engine::runLog, [&]() { ready = true; });
        QJsonObject result;
        const qint64 ms = stopWhenReady(engine, "setTimeout(() => log('never'), 60000);\nlog('ready');\nreturn 1;",
                                        [&]() { return ready; }, result);
        TEST_ASSERT(ms >= 0 && ms < 2000);
        TEST_COMPARE(result["phase"].toString(), QString("cancelled"));
    }

    TEST_FUNCTION(testStopDuringAsyncCall)
    {
        TEST_START;

        Gate gate;
        Engine engine;
        gate.install(engine);
        QJsonObject result;
        const qint64 ms = stopWhenReady(engine, "gate.wait('x');\nreturn 1;",
                                        [&]() { return gate.isWaiting("x"); }, result);
        TEST_ASSERT(ms >= 0 && ms < 2000);
        TEST_COMPARE(result["phase"].toString(), QString("cancelled"));
        // A late answer goes nowhere.
        gate.open("x", 1);
        TEST_ASSERT(!engine.isRunning());
    }

    TEST_FUNCTION(testStopDuringPureLoop)
    {
        TEST_START;

        Engine engine;
        bool ready = false;
        QObject::connect(&engine, &Engine::runLog, [&]() { ready = true; });
        QJsonObject result;
        const qint64 ms = stopWhenReady(engine, "log('ready');\nlet n = 0;\nwhile (true) { n++; }\n",
                                        [&]() { return ready; }, result);
        TEST_ASSERT(ms >= 0 && ms < 2000);
        TEST_COMPARE(result["phase"].toString(), QString("cancelled"));
    }

    TEST_FUNCTION(testStopInsideARepeatedCallback)
    {
        TEST_START;

        // The stop lands in a timer's function after enough calls that Qt
        // compiled the code that calls it: Qt 5.15 asserted ("hasException")
        // when that code had a try/catch (I-016).
        Engine engine;
        bool ready = false;
        QObject::connect(&engine, &Engine::runLog, [&]() { ready = true; });
        QJsonObject result;
        const qint64 ms = stopWhenReady(engine,
            "let n = 0;\nsetInterval(() => { if (++n === 20) { log('ready'); while (true) {} } }, 1);\n",
            [&]() { return ready; }, result);
        TEST_ASSERT(ms >= 0 && ms < 2000);
        TEST_COMPARE(result["phase"].toString(), QString("cancelled"));
    }

    // ---- signals and the log -----------------------------------------------------

    TEST_FUNCTION(testSignalsCarryTheRunId)
    {
        TEST_START;

        Engine engine;
        QList<Engine::RunId> started;
        QList<QPair<Engine::RunId, QString>> logs;
        QList<QPair<Engine::RunId, QString>> progress;
        QMap<Engine::RunId, QString> finished;
        QObject::connect(&engine, &Engine::runStarted, [&](Engine::RunId id, const QString&, bool) { started << id; });
        QObject::connect(&engine, &Engine::runLog, [&](Engine::RunId id, const QString& text) {
            logs << qMakePair(id, text);
        });
        QObject::connect(&engine, &Engine::runProgress, [&](Engine::RunId id, double, double, const QString& text) {
            progress << qMakePair(id, text);
        });
        QObject::connect(&engine, &Engine::runFinished, [&](Engine::RunId id, const QJsonObject& result) {
            finished.insert(id, result["result"].toString());
        });

        auto code = [](const QString& tag) {
            return QString("for (let i = 0; i < 5; i++) {\n"
                           "    log('%1' + i);\n"
                           "    progress(i, 5, '%1');\n"
                           "    sleep(5);\n"
                           "}\n"
                           "return '%1';\n").arg(tag);
        };
        Outcome a, b;
        const Engine::RunId idA = start(engine, code("A"), a);
        const Engine::RunId idB = start(engine, code("B"), b);
        TEST_ASSERT(TestSupport::spin([&]() { return a.done && b.done; }));
        TEST_COMPARE(started, (QList<Engine::RunId>{idA, idB}));

        TEST_COMPARE(logs.size(), 10);
        int wrong = 0;
        for (const auto& entry : logs)
            wrong += entry.first != (entry.second.startsWith(QLatin1Char('A')) ? idA : idB) ? 1 : 0;
        TEST_COMPARE(wrong, 0);
        TEST_COMPARE(progress.size(), 10);
        wrong = 0;
        for (const auto& entry : progress)
            wrong += entry.first != (entry.second == "A" ? idA : idB) ? 1 : 0;
        TEST_COMPARE(wrong, 0);

        TEST_COMPARE(finished.size(), 2);
        TEST_COMPARE(finished.value(idA), QString("A"));
        TEST_COMPARE(finished.value(idB), QString("B"));
    }

    // For runs that never end: only the latest lines are kept.
    TEST_FUNCTION(testLogKeepsTheLatestLines)
    {
        TEST_START;

        Engine engine;
        Engine::Limits limits;
        limits.maxLogLines = 3;
        engine.setLimits(limits);
        int signalled = 0;
        QObject::connect(&engine, &Engine::runLog, [&]() { ++signalled; });

        const QJsonObject result = TestSupport::runScript(engine,
            "for (let i = 0; i < 10; i++)\n    log('line ' + i);\nreturn 0;");
        TEST_COMPARE(result["status"].toString(), QString("ok"));
        QStringList log;
        for (const QJsonValue& line : result["log"].toArray())
            log << line.toString();
        TEST_COMPARE(log, (QStringList{"line 7", "line 8", "line 9"}));
        TEST_COMPARE(result["log_lines"].toInt(), 10);
        TEST_COMPARE(signalled, 10);
    }

    // ---- print(), console.log() and the output device -------------------------

    TEST_FUNCTION(testPrintAndConsoleLogAreLog)
    {
        TEST_START;

        Engine engine;
        Db db;
        TestSupport::registerDb(engine, db);
        QStringList signalled;
        QObject::connect(&engine, &Engine::runLog, [&](Engine::RunId, const QString& text) { signalled << text; });

        // One method call allowed: the log lines do not count.
        const QString code = "log('a', 1);\nprint('b', {x: 2});\nconsole.log('c', 3, [4]);\n"
                             "print();\nreturn db.list().length;";
        TEST_ASSERT(engine.check(code).ok);
        Options options;
        options.maxCalls = 1;
        const QJsonObject result = TestSupport::runScript(engine, code, options);
        TEST_COMPARE(result["status"].toString(), QString("ok"));
        TEST_COMPARE(result["result"].toInt(), 6);
        const QStringList expected{"a 1", "b {\"x\":2}", "c 3 [4]", ""};
        QStringList log;
        for (const QJsonValue& line : result["log"].toArray())
            log << line.toString();
        TEST_COMPARE(log, expected);
        TEST_COMPARE(signalled, expected);

        // console is checked like any other namespace.
        const Engine::Check wrong = engine.check("console.lg('x');");
        TEST_ASSERT(!wrong.ok);
        TEST_COMPARE(wrong.phase, QString("check"));

        engine.setApiBudgetChars(0);
        const QString all = engine.apiDefinition();
        TEST_ASSERT(all.contains("declare function print("));
        TEST_ASSERT(all.contains("declare namespace console {"));
        TEST_ASSERT(engine.apiDefinition("console.log").contains("function log("));
    }

    TEST_FUNCTION(testOutputTagsEachRun)
    {
        TEST_START;

        Engine engine;
        Gate gate;
        gate.install(engine);
        QBuffer device;
        device.open(QIODevice::WriteOnly);
        engine.setOutput(&device);
        TEST_ASSERT(engine.output() == &device);

        // Both at once, their lines interleaved.
        Options named;
        named.name = "Report";
        Outcome first, second;
        start(engine, "print('one');\ngate.wait('a');\nconsole.log('two', 2);\nreturn 0;", first, named);
        const Engine::RunId unnamed = start(engine, "log('gr\\u00fc\\u00dfe');\ngate.wait('b');\nprint('y');\nreturn 0;",
                                            second);
        TEST_ASSERT(TestSupport::spin([&]() { return gate.isWaiting("a") && gate.isWaiting("b"); }));
        gate.open("b", QJsonValue());
        gate.open("a", QJsonValue());
        TEST_ASSERT(TestSupport::spin([&]() { return first.done && second.done; }));

        const QString tag = QString("[#%1] ").arg(unnamed);
        const QStringList lines = QString::fromUtf8(device.data()).split('\n');
        TEST_COMPARE(lines.size(), 5);
        TEST_COMPARE(lines.last(), QString());
        TEST_COMPARE(lines.filter("[Report] "), (QStringList{"[Report] one", "[Report] two 2"}));
        TEST_COMPARE(lines.filter(tag), (QStringList{tag + QString::fromUtf8("gr\xc3\xbc\xc3\x9f" "e"), tag + "y"}));
    }

    TEST_FUNCTION(testOutputOffAndDeletedDevice)
    {
        TEST_START;

        Engine engine;
        QBuffer device;
        device.open(QIODevice::WriteOnly);
        engine.setOutput(&device);
        engine.setOutput(nullptr);
        TEST_ASSERT(engine.output() == nullptr);
        TEST_COMPARE(TestSupport::runScript(engine, "print('x');\nreturn 0;")["status"].toString(), QString("ok"));
        TEST_ASSERT(device.data().isEmpty());

        // A device deleted while set is dropped, not written to.
        auto* gone = new QBuffer();
        gone->open(QIODevice::WriteOnly);
        engine.setOutput(gone);
        delete gone;
        TEST_ASSERT(engine.output() == nullptr);
        const QJsonObject result = TestSupport::runScript(engine, "print('x');\nreturn 0;");
        TEST_COMPARE(result["status"].toString(), QString("ok"));
        TEST_COMPARE(result["log"].toArray().size(), 1);
    }

    // ---- the debugger ------------------------------------------------------------

    TEST_FUNCTION(testOneDebugRunAtATime)
    {
        TEST_START;

        Engine engine;
        Db db;
        TestSupport::registerDb(engine, db);
        QList<Engine::RunId> pausedRuns;
        QObject::connect(&engine, &Engine::runPaused, [&](Engine::RunId id) { pausedRuns << id; });

        Options debug;
        debug.debug = true;
        debug.stepFromStart = true;
        Outcome first;
        const Engine::RunId idFirst = start(engine, "const n = db.list().length;\nreturn n;", first, debug);
        TEST_ASSERT(idFirst != 0);
        TEST_ASSERT(TestSupport::spin([&]() { return !pausedRuns.isEmpty(); }));
        TEST_COMPARE(pausedRuns.value(0), idFirst);

        // A second one in the debugger is refused...
        Outcome second;
        TEST_COMPARE(start(engine, "return 2;", second, debug), Engine::RunId(0));
        TEST_ASSERT(second.done);
        TEST_COMPARE(second.result["phase"].toString(), QString("unavailable"));
        TEST_ASSERT(second.result["message"].toString().contains("debugger"));

        // ...a normal run goes on beside it.
        const QJsonObject normal = TestSupport::runScript(engine, "return db.read('o3').age;");
        TEST_COMPARE(normal["status"].toString(), QString("ok"));
        TEST_COMPARE(normal["result"].toInt(), 30);
        TEST_ASSERT(engine.isRunning(idFirst));
        TEST_ASSERT(!first.done);
        TEST_COMPARE(pausedRuns.size(), 1);

        // The debugger's controls act on the debug run.
        engine.continueRun();
        TEST_ASSERT(TestSupport::spin([&]() { return first.done; }));
        TEST_COMPARE(first.result["result"].toInt(), 6);

        // Once it is over, the next debug run may start.
        Outcome third;
        TEST_ASSERT(start(engine, "return db.list().length;", third, debug) != 0);
        TEST_ASSERT(TestSupport::spin([&]() { return pausedRuns.size() == 2; }));
        engine.stopAll();
        TEST_ASSERT(TestSupport::spin([&]() { return third.done; }, 5000));
        TEST_COMPARE(third.result["phase"].toString(), QString("cancelled"));
    }
};

TEST_INSTANTIATE(TST_ScriptConcurrency);
