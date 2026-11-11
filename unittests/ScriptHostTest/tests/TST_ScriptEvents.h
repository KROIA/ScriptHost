#pragma once

// Events: postEvent() to every subscribed run or to one, on() / off(), the
// order of delivery, exceptions in handlers, how long a run with handlers
// lives, stopping it while it waits, and requested runs, which get only the
// events the application allows them.

#include "UnitTest.h"
#include "TestSupport.h"
#include <QElapsedTimer>
#include <QMap>

class TST_ScriptEvents : public UnitTest::Test
{
    TEST_CLASS(TST_ScriptEvents)
public:
    TST_ScriptEvents()
        : Test("TST_ScriptEvents")
    {
        ADD_TEST(TST_ScriptEvents::testBroadcastToSubscribedRuns);
        ADD_TEST(TST_ScriptEvents::testTargetedDelivery);
        ADD_TEST(TST_ScriptEvents::testOffBothWaysAndOrder);
        ADD_TEST(TST_ScriptEvents::testManyEventsInOrder);
        ADD_TEST(TST_ScriptEvents::testExceptionInHandler);
        ADD_TEST(TST_ScriptEvents::testEventsAreNotMethodCalls);
        ADD_TEST(TST_ScriptEvents::testHandlersKeepTheRunAlive);
        ADD_TEST(TST_ScriptEvents::testStopWhileWaitingForEvents);
        ADD_TEST(TST_ScriptEvents::testRequestedRunsNeedTheOptIn);
        ADD_TEST(TST_ScriptEvents::testEventsInCheckAndDefinition);
    }

private:
    using Engine = ScriptHost::ScriptEngine;
    using Options = TestSupport::Options;

    struct Outcome
    {
        bool        done = false;
        QJsonObject result;
    };

    // The log lines of every run, by id.
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

    static void finish(Engine& engine)
    {
        engine.stopAll();
        TestSupport::spin([&]() { return !engine.isRunning(); });
    }

    TEST_FUNCTION(testBroadcastToSubscribedRuns)
    {
        TEST_START;

        Engine engine;
        Logs logs(engine);
        const QString tick = "on('tick', d => { log('tick ' + d.n + ' ' + d.nested.s + ' ' + typeof d); off('tick'); });\n"
                             "log('ready');\nreturn 1;";
        Outcome a, b, c;
        const Engine::RunId idA = start(engine, tick, a);
        const Engine::RunId idB = start(engine, tick, b);
        const Engine::RunId idC = start(engine, "on('other', d => { log('other'); off('other'); });\n"
                                                "log('ready');\nreturn 1;", c);
        TEST_ASSERT(TestSupport::spin([&]() { return logs.ready(idA) && logs.ready(idB) && logs.ready(idC); }));

        engine.postEvent("tick", QJsonObject{{"n", 1}, {"nested", QJsonObject{{"s", "x"}}}});
        TEST_ASSERT(TestSupport::spin([&]() { return a.done && b.done; }));
        TEST_COMPARE(a.result["status"].toString(), QString("ok"));
        TEST_COMPARE(logs.lines[idA], (QStringList{"ready", "tick 1 x object"}));
        TEST_COMPARE(logs.lines[idB], (QStringList{"ready", "tick 1 x object"}));

        // C has no handler for "tick": it got nothing and still waits.
        TEST_ASSERT(engine.isRunning(idC));
        TEST_COMPARE(logs.lines[idC], QStringList{"ready"});
        engine.postEvent("other", QJsonObject());
        TEST_ASSERT(TestSupport::spin([&]() { return c.done; }));
        TEST_COMPARE(logs.lines[idC], (QStringList{"ready", "other"}));
    }

    TEST_FUNCTION(testTargetedDelivery)
    {
        TEST_START;

        Engine engine;
        Logs logs(engine);
        const QString tick = "on('tick', d => { log('tick ' + d.n); off('tick'); });\nlog('ready');\nreturn 1;";
        Outcome a, b;
        const Engine::RunId idA = start(engine, tick, a);
        const Engine::RunId idB = start(engine, tick, b);
        TEST_ASSERT(TestSupport::spin([&]() { return logs.ready(idA) && logs.ready(idB); }));

        engine.postEvent(idB, "tick", QJsonObject{{"n", 2}});
        TEST_ASSERT(TestSupport::spin([&]() { return b.done; }));
        TEST_COMPARE(logs.lines[idB], (QStringList{"ready", "tick 2"}));
        TestSupport::spin([]() { return false; }, 100);
        TEST_ASSERT(!a.done);
        TEST_COMPARE(logs.lines[idA], QStringList{"ready"});

        // An id that is not active: nothing happens.
        engine.postEvent(idB, "tick", QJsonObject{{"n", 3}});
        engine.postEvent(idA, "tick", QJsonObject{{"n", 4}});
        TEST_ASSERT(TestSupport::spin([&]() { return a.done; }));
        TEST_COMPARE(logs.lines[idA], (QStringList{"ready", "tick 4"}));
    }

    TEST_FUNCTION(testOffBothWaysAndOrder)
    {
        TEST_START;

        Engine engine;
        Logs logs(engine);
        Outcome out;
        const Engine::RunId id = start(engine,
            "const a = d => { log('a' + d.n); off('t', a); };\n"
            "const b = d => log('b' + d.n);\n"
            "on('t', a);\n"
            "on('t', b);\n"
            "on('end', () => { off('t'); off('end'); });\n"
            "log('ready');\n"
            "return 5;\n", out);
        TEST_ASSERT(TestSupport::spin([&]() { return logs.ready(id); }));

        engine.postEvent("t", QJsonObject{{"n", 1}});
        engine.postEvent("t", QJsonObject{{"n", 2}});
        engine.postEvent("end", QJsonObject());
        engine.postEvent("t", QJsonObject{{"n", 3}});
        TEST_ASSERT(TestSupport::spin([&]() { return out.done; }));
        TEST_COMPARE(out.result["status"].toString(), QString("ok"));
        TEST_COMPARE(out.result["result"].toInt(), 5);
        // off(name, fn) removed a after its first event; off(name) the rest.
        TEST_COMPARE(logs.lines[id], (QStringList{"ready", "a1", "b1", "b2"}));
    }

    TEST_FUNCTION(testManyEventsInOrder)
    {
        TEST_START;

        Engine engine;
        Logs logs(engine);
        Outcome out;
        const Engine::RunId id = start(engine,
            "const seen = [];\n"
            "on('n', d => { seen.push(d.i); if (seen.length === 200) { off('n'); log(seen.join(',')); } });\n"
            "log('ready');\n"
            "return 0;\n", out);
        TEST_ASSERT(TestSupport::spin([&]() { return logs.ready(id); }));

        QStringList expected;
        for (int i = 0; i < 200; ++i) {
            engine.postEvent("n", QJsonObject{{"i", i}});
            expected << QString::number(i);
        }
        TEST_ASSERT(TestSupport::spin([&]() { return out.done; }));
        TEST_COMPARE(logs.lines[id].value(1), expected.join(','));
    }

    TEST_FUNCTION(testExceptionInHandler)
    {
        TEST_START;

        Engine engine;
        Logs logs(engine);
        Outcome out;
        const Engine::RunId id = start(engine,
            "on('e', d => {\n"                          // 1
            "    throw new Error('in the handler');\n"  // 2
            "});\n"                                     // 3
            "setTimeout(() => log('late'), 60000);\n"   // 4
            "log('ready');\n"                           // 5
            "return 1;\n", out);                        // 6
        TEST_ASSERT(TestSupport::spin([&]() { return logs.ready(id); }));

        engine.postEvent("e", QJsonObject());
        TEST_ASSERT(TestSupport::spin([&]() { return out.done; }, 5000));
        TEST_COMPARE(out.result["status"].toString(), QString("error"));
        TEST_COMPARE(out.result["phase"].toString(), QString("runtime"));
        TEST_COMPARE(out.result["message"].toString(), QString("in the handler"));
        TEST_COMPARE(out.result["line"].toInt(), 2);
        TEST_ASSERT(!engine.isRunning());
    }

    TEST_FUNCTION(testEventsAreNotMethodCalls)
    {
        TEST_START;

        Engine engine;
        Logs logs(engine);
        Options options;
        options.maxCalls = 2;
        Outcome out;
        const Engine::RunId id = start(engine,
            "let n = 0;\n"
            "on('n', d => { if (++n === 10) off('n'); });\n"
            "log('ready');\n"
            "return 0;\n", out, options);
        TEST_ASSERT(TestSupport::spin([&]() { return logs.ready(id); }));
        for (int i = 0; i < 10; ++i)
            engine.postEvent("n", QJsonObject());
        TEST_ASSERT(TestSupport::spin([&]() { return out.done; }));
        TEST_COMPARE(out.result["status"].toString(), QString("ok"));
        TEST_ASSERT(!out.result.contains("calls"));
    }

    TEST_FUNCTION(testHandlersKeepTheRunAlive)
    {
        TEST_START;

        Engine engine;
        Logs logs(engine);
        Outcome out;
        const Engine::RunId id = start(engine, "on('t', d => off('t'));\nlog('ready');\nreturn 7;", out);
        TEST_ASSERT(TestSupport::spin([&]() { return logs.ready(id); }));
        TestSupport::spin([]() { return false; }, 300);
        TEST_ASSERT(!out.done);
        TEST_ASSERT(engine.isRunning(id));

        // Events of other names do not end it.
        engine.postEvent("other", QJsonObject());
        TestSupport::spin([]() { return false; }, 100);
        TEST_ASSERT(!out.done);

        engine.postEvent("t", QJsonObject());
        TEST_ASSERT(TestSupport::spin([&]() { return out.done; }));
        TEST_COMPARE(out.result["status"].toString(), QString("ok"));
        TEST_COMPARE(out.result["result"].toInt(), 7);

        // A script that never subscribed ends with its body; off() of nothing is fine.
        const QJsonObject plain = TestSupport::runScript(engine, "off('t');\noff('t', () => 1);\nreturn 1;");
        TEST_COMPARE(plain["status"].toString(), QString("ok"));
    }

    TEST_FUNCTION(testStopWhileWaitingForEvents)
    {
        TEST_START;

        Engine engine;
        Logs logs(engine);
        Outcome out;
        const Engine::RunId id = start(engine, "on('t', d => log(d));\nlog('ready');\nreturn 1;", out);
        TEST_ASSERT(TestSupport::spin([&]() { return logs.ready(id); }));
        TestSupport::spin([]() { return false; }, 100);

        QElapsedTimer timer;
        timer.start();
        engine.stop(id);
        TEST_ASSERT(TestSupport::spin([&]() { return out.done; }));
        TEST_ASSERT(timer.elapsed() < 2000);
        TEST_COMPARE(out.result["phase"].toString(), QString("cancelled"));
    }

    TEST_FUNCTION(testRequestedRunsNeedTheOptIn)
    {
        TEST_START;

        Engine engine;
        Logs logs(engine);
        TEST_ASSERT(!engine.isEventAllowedForRequestedRuns("key"));

        // Not allowed: on() throws.
        const QJsonObject blocked = TestSupport::request(engine,
            TestSupport::codeRequest("on('key', d => log(d.key));\nreturn 1;"));
        TEST_COMPARE(blocked["status"].toString(), QString("error"));
        TEST_COMPARE(blocked["phase"].toString(), QString("runtime"));
        TEST_COMPARE(blocked["message"].toString(),
                     QString("the application does not let requested scripts receive 'key' events"));
        TEST_COMPARE(blocked["line"].toInt(), 1);

        // Allowed: it gets the event - and only that name.
        engine.allowEventForRequestedRuns("key");
        TEST_ASSERT(engine.isEventAllowedForRequestedRuns("key"));
        QJsonObject result;
        bool done = false;
        engine.requestRun(TestSupport::codeRequest(
            "on('key', d => { log('got ' + d.key); off('key'); });\n"
            "try { on('mouse', d => 1); } catch (e) { log(e.message); }\n"
            "log('ready');\nreturn 1;"),
            [&](const QJsonObject& r) { result = r; done = true; });
        Engine::RunId id = 0;
        TEST_ASSERT(TestSupport::spin([&]() {
            for (auto it = logs.lines.cbegin(); it != logs.lines.cend(); ++it) {
                if (it.value().contains("ready"))
                    id = it.key();
            }
            return id != 0;
        }));
        TEST_ASSERT(engine.isExternalRun(id));
        engine.postEvent("key", QJsonObject{{"key", "A"}});
        TEST_ASSERT(TestSupport::spin([&]() { return done; }));
        TEST_COMPARE(result["status"].toString(), QString("ok"));
        TEST_COMPARE(logs.lines[id], (QStringList{
            "the application does not let requested scripts receive 'mouse' events", "ready", "got A"}));

        // Taken back: blocked again.
        engine.allowEventForRequestedRuns("key", false);
        TEST_ASSERT(!engine.isEventAllowedForRequestedRuns("key"));
        const QJsonObject again = TestSupport::request(engine,
            TestSupport::codeRequest("on('key', d => 1);\nreturn 1;"));
        TEST_COMPARE(again["phase"].toString(), QString("runtime"));

        // The application's own runs need no opt-in.
        Outcome own;
        const Engine::RunId ownId = start(engine, "on('mouse', d => { log('m'); off('mouse'); });\nlog('ready');\nreturn 1;", own);
        TEST_ASSERT(TestSupport::spin([&]() { return logs.ready(ownId); }));
        engine.postEvent("mouse", QJsonObject());
        TEST_ASSERT(TestSupport::spin([&]() { return own.done; }));
        TEST_COMPARE(own.result["status"].toString(), QString("ok"));
        finish(engine);
    }

    TEST_FUNCTION(testEventsInCheckAndDefinition)
    {
        TEST_START;

        Engine engine;
        const Engine::Check check = engine.check("const f = d => log(d.key);\non('key', f);\noff('key', f);\noff('key');\nreturn 1;");
        TEST_ASSERT(check.ok);
        const Engine::Check wrong = engine.check("on('key', d => console.lg(d));");
        TEST_ASSERT(!wrong.ok);
        TEST_COMPARE(wrong.phase, QString("check"));

        engine.setApiBudgetChars(0);
        const QString all = engine.apiDefinition();
        TEST_ASSERT(all.contains("declare function on("));
        TEST_ASSERT(all.contains("declare function off("));

        // A wrong handler is a clear error.
        const QJsonObject bad = TestSupport::runScript(engine, "on('key', 5);\nreturn 1;");
        TEST_COMPARE(bad["phase"].toString(), QString("runtime"));
        TEST_COMPARE(bad["message"].toString(), QString("TypeError: the second argument must be a function"));
    }
};

TEST_INSTANTIATE(TST_ScriptEvents);
