#pragma once

// The engine: the methods an application registers, what a script may call,
// the feedback a broken script gets, limits, dry run, the user's settings and
// the debugger's call stepping.

#include "UnitTest.h"
#include "TestSupport.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QTimer>

class TST_ScriptEngine : public UnitTest::Test
{
    TEST_CLASS(TST_ScriptEngine)
public:
    TST_ScriptEngine()
        : Test("TST_ScriptEngine")
    {
        ADD_TEST(TST_ScriptEngine::testLoopOverObjects);
        ADD_TEST(TST_ScriptEngine::testSyntaxErrorHasTheLine);
        ADD_TEST(TST_ScriptEngine::testUnknownMethodIsRejectedBeforeRunning);
        ADD_TEST(TST_ScriptEngine::testScannerIgnoresTextAndOwnNames);
        ADD_TEST(TST_ScriptEngine::testDisabledMethodsDoNotExist);
        ADD_TEST(TST_ScriptEngine::testArgumentsAreChecked);
        ADD_TEST(TST_ScriptEngine::testMethodFromSchema);
        ADD_TEST(TST_ScriptEngine::testFailingMethodThrows);
        ADD_TEST(TST_ScriptEngine::testReturnValueLogAndThrow);
        ADD_TEST(TST_ScriptEngine::testCallLimit);
        ADD_TEST(TST_ScriptEngine::testCallLimitPerMethod);
        ADD_TEST(TST_ScriptEngine::testEndlessLoopHitsTheTimeLimit);
        ADD_TEST(TST_ScriptEngine::testStop);
        ADD_TEST(TST_ScriptEngine::testAsynchronousMethod);
        ADD_TEST(TST_ScriptEngine::testDryRun);
        ADD_TEST(TST_ScriptEngine::testApiDefinition);
        ADD_TEST(TST_ScriptEngine::testUserSettingsAndLocks);
        ADD_TEST(TST_ScriptEngine::testCallStepping);
        ADD_TEST(TST_ScriptEngine::testStatementStepping);
    }

private:
    using Engine = ScriptHost::ScriptEngine;
    using Options = TestSupport::Options;
    using Db = TestSupport::Db;

    // What it is for: one script, many calls, a small result.
    TEST_FUNCTION(testLoopOverObjects)
    {
        TEST_START;

        Engine engine;
        Db db;
        TestSupport::registerDb(engine, db);

        Options options;
        options.args = QJsonObject{{"minAge", 40}};
        const QJsonObject result = TestSupport::runScript(engine,
            "const changed = [];\n"
            "for (const id of db.list()) {\n"
            "    const o = db.read(id);\n"
            "    if (o.age >= args.minAge) {\n"
            "        db.setStatus(id, 'archived');\n"
            "        changed.push(o.name);\n"
            "    }\n"
            "}\n"
            "return {count: changed.length, first: changed[0]};\n", options);

        TEST_COMPARE(result["status"].toString(), QString("ok"));
        TEST_COMPARE(result["result"].toObject()["count"].toInt(), 3);
        TEST_COMPARE(result["result"].toObject()["first"].toString(), QString("Object 4"));
        TEST_COMPARE(db.archived(), 3);
        const QJsonObject calls = result["calls"].toObject();
        TEST_COMPARE(calls["db.list"].toInt(), 1);
        TEST_COMPARE(calls["db.read"].toInt(), 6);
        TEST_COMPARE(calls["db.setStatus"].toInt(), 3);
        TEST_ASSERT(result.contains("duration_ms"));

        // It is on record - as the application's own run.
        TEST_COMPARE(engine.runs().size(), 1);
        TEST_COMPARE(engine.runs().first().result["status"].toString(), QString("ok"));
        TEST_ASSERT(!engine.runs().first().external);
        TEST_COMPARE(engine.runs().first().args["minAge"].toInt(), 40);
        TEST_ASSERT(!engine.isRunning());
        TEST_ASSERT(engine.activeRuns().isEmpty());
    }

    TEST_FUNCTION(testSyntaxErrorHasTheLine)
    {
        TEST_START;

        Engine engine;
        Db db;
        TestSupport::registerDb(engine, db);

        const QString broken = "const ids = db.list();\nfor (const id of ids {\n    db.read(id);\n}\n";
        const Engine::Check check = engine.check(broken);
        TEST_ASSERT(!check.ok);
        TEST_COMPARE(check.phase, QString("syntax"));
        TEST_COMPARE(check.line, 2);
        TEST_ASSERT(!check.message.isEmpty());
        TEST_COMPARE(check.toJson()["status"].toString(), QString("error"));
        TEST_COMPARE(check.toJson()["line"].toInt(), 2);

        // Running it reports the same, and nothing was called.
        const QJsonObject result = TestSupport::runScript(engine, broken);
        TEST_COMPARE(result["phase"].toString(), QString("syntax"));
        TEST_COMPARE(result["line"].toInt(), 2);
        TEST_COMPARE(db.reads, 0);

        // Code that closes the function early to run while it is "read" finds
        // nothing to call, and an endless loop there is stopped.
        QElapsedTimer timer;
        timer.start();
        const Engine::Check escape = engine.check("}); while (true) {} (function(){");
        TEST_ASSERT(!escape.ok);
        TEST_COMPARE(escape.phase, QString("syntax"));
        TEST_ASSERT(timer.elapsed() < 8000);

        TEST_ASSERT(engine.check("return db.list().length;").ok);
        TEST_COMPARE(engine.check("return 1;").toJson()["status"].toString(), QString("ok"));
    }

    TEST_FUNCTION(testUnknownMethodIsRejectedBeforeRunning)
    {
        TEST_START;

        Engine engine;
        Db db;
        TestSupport::registerDb(engine, db);

        const QString code = "const ids = db.list();\nfor (const id of ids)\n    db.reed(id);\nreturn ids.length;\n";
        const Engine::Check check = engine.check(code);
        TEST_ASSERT(!check.ok);
        TEST_COMPARE(check.phase, QString("check"));
        TEST_COMPARE(check.line, 3);
        TEST_ASSERT(check.message.contains("db.reed"));
        TEST_COMPARE(check.didYouMean, QString("db.read"));
        TEST_COMPARE(check.toJson()["did_you_mean"].toString(), QString("db.read"));

        // Nothing close: the namespace's methods are named.
        const Engine::Check noMatch = engine.check("db.archiveEverything();");
        TEST_ASSERT(!noMatch.ok);
        TEST_ASSERT(noMatch.message.contains("db.setStatus"));

        // What a script uses, and whether it changes anything.
        const Engine::Check reading = engine.check("return db.read(db.list()[0]).name;");
        TEST_ASSERT(reading.ok);
        TEST_COMPARE(reading.methods.size(), 2);
        TEST_ASSERT(reading.changingMethods.isEmpty());
        TEST_ASSERT(!reading.dynamic);
        const Engine::Check changing = engine.check("db.setStatus('o1', 'archived');");
        TEST_COMPARE(changing.changingMethods, QStringList{"db.setStatus"});
        // By a computed name: the source cannot tell.
        TEST_ASSERT(engine.check("const m = 'set' + 'Status'; db[m]('o1', 'archived');").dynamic);
        TEST_ASSERT(engine.check("return eval('1 + 1');").dynamic);

        // An object the application does not have fails when the script gets there.
        const QJsonObject result = TestSupport::runScript(engine, "const a = 1;\nreturn database.list();\n");
        TEST_COMPARE(result["phase"].toString(), QString("runtime"));
        TEST_COMPARE(result["line"].toInt(), 2);
        TEST_ASSERT(result["message"].toString().contains("database"));
        TEST_COMPARE(result["hint"].toString(), QString("only the methods of the application's script interface exist"));
    }

    // Method names inside strings and comments are text, not calls; a name
    // the script declares itself is the script's.
    TEST_FUNCTION(testScannerIgnoresTextAndOwnNames)
    {
        TEST_START;

        Engine engine;
        Db db;
        TestSupport::registerDb(engine, db);

        TEST_ASSERT(engine.check("// db.nothing() would be wrong\nreturn 'db.nothing()';").ok);
        TEST_ASSERT(engine.check("/* db.nothing()\n db.other() */ return \"it's db.gone()\";").ok);
        TEST_ASSERT(engine.check("return `plain db.nothing() text`;").ok);
        TEST_ASSERT(engine.check("return /db.nothing\\(/.test('x');").ok);
        // ...but code inside a template is code.
        const Engine::Check inTemplate = engine.check("return `name: ${db.nothing('o1')}`;");
        TEST_ASSERT(!inTemplate.ok);
        TEST_COMPARE(inTemplate.phase, QString("check"));
        TEST_ASSERT(engine.check("return `name: ${db.read('o1').name}`;").ok);
        TEST_COMPARE(TestSupport::runScript(engine, "return `name: ${db.read('o1').name}`;")["result"].toString(),
                     QString("name: Object 1"));
        // A division is not the start of a regular expression.
        TEST_ASSERT(!engine.check("const a = 4 / 2; db.nothing(); const b = a / 2;").ok);

        // The script's own `db`.
        TEST_ASSERT(engine.check("const db = {size: () => 3};\nreturn db.size();").ok);
        TEST_COMPARE(TestSupport::runScript(engine, "const db = {size: () => 3};\nreturn db.size();")["result"].toInt(), 3);
        // A property called like a namespace is not the namespace.
        TEST_ASSERT(engine.check("const o = {db: {x: 1}};\nreturn o.db.x;").ok);
    }

    TEST_FUNCTION(testDisabledMethodsDoNotExist)
    {
        TEST_START;

        Engine engine;
        Db db;
        TestSupport::registerDb(engine, db);
        int changes = 0;
        QObject::connect(&engine, &Engine::methodsChanged, [&]() { ++changes; });

        TEST_ASSERT(engine.hasMethod("db.setStatus"));
        TEST_ASSERT(engine.isMethodEnabled("db.setStatus"));
        TEST_ASSERT(!engine.isMethodEnabled("db.unknown"));

        engine.setMethodEnabled("db.setStatus", false);
        TEST_COMPARE(changes, 1);
        TEST_ASSERT(!engine.isMethodEnabled("db.setStatus"));
        TEST_ASSERT(engine.hasMethod("db.setStatus"));               // still registered
        TEST_ASSERT(!engine.apiDefinition().contains("setStatus"));
        TEST_ASSERT(engine.apiDefinition().contains("function read("));

        const Engine::Check check = engine.check("db.setStatus('o1', 'archived');");
        TEST_ASSERT(!check.ok);
        TEST_COMPARE(check.phase, QString("check"));
        TEST_ASSERT(check.message.contains("disabled"));
        // Reached by a computed name it is simply not there.
        const QJsonObject result = TestSupport::runScript(engine, "db['set' + 'Status']('o1', 'archived');");
        TEST_COMPARE(result["status"].toString(), QString("error"));
        TEST_COMPARE(db.writes, 0);

        // A whole group; a method's own switch wins over its group's.
        engine.setMethodGroupEnabled("Database", false);
        TEST_ASSERT(!engine.isMethodEnabled("db.read"));
        engine.setMethodEnabled("db.read", true);
        TEST_ASSERT(engine.isMethodEnabled("db.read"));
        TEST_ASSERT(!engine.isMethodEnabled("db.list"));
        TEST_COMPARE(TestSupport::runScript(engine, "return db.read('o2').name;")["result"].toString(), QString("Object 2"));

        // The policy is by name: it holds for a method registered later.
        engine.setMethodEnabled("db.later", false);
        engine.registerMethod(ScriptHost::ScriptMethod("db.later"), [](const QJsonObject&) -> QJsonValue { return 1; });
        TEST_ASSERT(!engine.isMethodEnabled("db.later"));

        // The built-in ones are methods like any other.
        TEST_ASSERT(engine.isMethodEnabled("log"));
        engine.setMethodEnabled("log", false);
        TEST_ASSERT(!engine.check("log('x');").ok);

        engine.unregisterMethod("db.read");
        TEST_ASSERT(!engine.hasMethod("db.read"));

        // A name that is no identifier is not registered.
        engine.registerMethod(ScriptHost::ScriptMethod("bad-name"), [](const QJsonObject&) -> QJsonValue { return 1; });
        TEST_ASSERT(!engine.hasMethod("bad-name"));
    }

    TEST_FUNCTION(testArgumentsAreChecked)
    {
        TEST_START;

        Engine engine;
        Db db;
        TestSupport::registerDb(engine, db);

        // A value outside the enum: the script stops there, with the signature.
        QJsonObject result = TestSupport::runScript(engine, "const id = 'o1';\n\ndb.setStatus(id, 'deleted');\n");
        TEST_COMPARE(result["status"].toString(), QString("error"));
        TEST_COMPARE(result["phase"].toString(), QString("runtime"));
        TEST_COMPARE(result["line"].toInt(), 3);
        TEST_ASSERT(result["message"].toString().contains("db.setStatus: 'status' must be one of: active, archived"));
        TEST_ASSERT(result["message"].toString().contains("Signature: db.setStatus(id: string, status: \"active\" | \"archived\")"));
        TEST_COMPARE(db.writes, 0);

        // Missing and surplus arguments.
        result = TestSupport::runScript(engine, "db.setStatus('o1');");
        TEST_COMPARE(result["phase"].toString(), QString("runtime"));
        TEST_ASSERT(result["message"].toString().contains("'status' is missing"));
        result = TestSupport::runScript(engine, "db.read('o1', 'o2');");
        TEST_ASSERT(result["message"].toString().contains("takes 1 argument(s), got 2"));
        result = TestSupport::runScript(engine, "db.list(1);");
        TEST_ASSERT(result["message"].toString().contains("takes no arguments"));
        // A named argument nobody declared, with a suggestion.
        result = TestSupport::runScript(engine, "db.setStatus({id: 'o1', stat: 'archived'});");
        TEST_ASSERT(result["message"].toString().contains("unknown parameter 'stat'; there are: id, status"));
        TEST_ASSERT(result["message"].toString().contains("did you mean 'status'?"));

        // Named arguments work as well, and the enum's spelling is repaired.
        result = TestSupport::runScript(engine, "return db.setStatus({id: 'o1', status: 'ARCHIVED'}).changed;");
        TEST_COMPARE(result["status"].toString(), QString("ok"));
        TEST_ASSERT(result["result"].toBool());
        TEST_COMPARE(db.objects["o1"]["status"].toString(), QString("archived"));

        // The definition's own check.
        QJsonObject named;
        QString error;
        ScriptHost::ScriptMethod method("x.y");
        method.addParameter("count", "integer", "How many").addParameter("label", "string", "Text", false)
              .setRange("count", 1, 10);
        TEST_ASSERT(method.bindArguments(QJsonArray{"3"}, named, &error));     // "3" -> 3
        TEST_COMPARE(named["count"].toInt(), 3);
        TEST_ASSERT(!named.contains("label"));
        TEST_ASSERT(!method.bindArguments(QJsonArray{}, named, &error));
        TEST_COMPARE(error, QString("'count' is missing"));
        TEST_ASSERT(!method.bindArguments(QJsonArray{11}, named, &error));
        TEST_COMPARE(error, QString("'count' must be at most 10"));
        TEST_ASSERT(!method.bindArguments(QJsonArray{2.5}, named, &error));
        TEST_COMPARE(error, QString("'count' must be of type integer"));
        TEST_ASSERT(ScriptHost::ScriptMethod::isValidName("db.readObject"));
        TEST_ASSERT(ScriptHost::ScriptMethod::isValidName("log"));
        TEST_ASSERT(!ScriptHost::ScriptMethod::isValidName("a.b.c"));
        TEST_ASSERT(!ScriptHost::ScriptMethod::isValidName("read-object"));
        TEST_ASSERT(!ScriptHost::ScriptMethod::isValidName(""));
    }

    // A method described by a JSON schema takes one object.
    TEST_FUNCTION(testMethodFromSchema)
    {
        TEST_START;

        Engine engine;
        const QJsonObject schema{
            {"type", "object"},
            {"properties", QJsonObject{{"a", QJsonObject{{"type", "number"}, {"description", "First"}}},
                                       {"b", QJsonObject{{"type", "string"}}}}},
            {"required", QJsonArray{"a"}}};
        ScriptHost::ScriptMethod add = ScriptHost::ScriptMethod::fromSchema("calc.add", schema);
        add.setDescription("Adds the length of b to a.").setReadOnly(true).setReturns("number");
        TEST_ASSERT(add.takesObject());
        TEST_COMPARE(add.parameterNames(), (QStringList{"a", "b"}));
        TEST_COMPARE(add.signature(), QString("calc.add(args: { a: number, b?: string }): number"));
        TEST_ASSERT(add.declaration().contains("@param args.a First"));

        QJsonObject received;
        engine.registerMethod(add, [&received](const QJsonObject& args) -> QJsonValue {
            received = args;
            return args["a"].toDouble() + double(args["b"].toString().size());
        });
        TEST_ASSERT(engine.apiDefinition().contains("  function add(args: { a: number, b?: string }): number;\n"));

        QJsonObject result = TestSupport::runScript(engine, "return calc.add({a: 2, b: 'xyz'});");
        TEST_COMPARE(result["status"].toString(), QString("ok"));
        TEST_COMPARE(result["result"].toInt(), 5);
        TEST_COMPARE(received["b"].toString(), QString("xyz"));
        // Repaired like positional arguments.
        TEST_COMPARE(TestSupport::runScript(engine, "return calc.add({a: '5'});")["result"].toInt(), 5);

        // What does not fit the schema.
        result = TestSupport::runScript(engine, "return calc.add({b: 'x'});");
        TEST_COMPARE(result["phase"].toString(), QString("runtime"));
        TEST_ASSERT(result["message"].toString().contains("calc.add: 'a' is missing"));
        TEST_ASSERT(result["message"].toString().contains("Signature: calc.add(args: { a: number, b?: string }): number"));
        result = TestSupport::runScript(engine, "return calc.add({a: 1, c: 2});");
        TEST_ASSERT(result["message"].toString().contains("unknown parameter 'c'; there are: a, b"));
        result = TestSupport::runScript(engine, "return calc.add({a: 'many'});");
        TEST_ASSERT(result["message"].toString().contains("'a' must be of type number"));
        result = TestSupport::runScript(engine, "return calc.add(1, 2);");
        TEST_ASSERT(result["message"].toString().contains("takes one object with the named arguments"));
        result = TestSupport::runScript(engine, "return calc.add();");
        TEST_ASSERT(result["message"].toString().contains("'a' is missing"));
    }

    TEST_FUNCTION(testFailingMethodThrows)
    {
        TEST_START;

        Engine engine;
        Db db;
        TestSupport::registerDb(engine, db);

        // Not caught: the script ends there and says where.
        QJsonObject result = TestSupport::runScript(engine,
            "function load(id) {\n"
            "    return db.read(id);\n"
            "}\n"
            "db.setStatus('o1', 'archived');\n"
            "load('missing');\n"
            "db.setStatus('o2', 'archived');\n");
        TEST_COMPARE(result["status"].toString(), QString("error"));
        TEST_COMPARE(result["phase"].toString(), QString("runtime"));
        TEST_COMPARE(result["line"].toInt(), 2);
        TEST_ASSERT(result["message"].toString().contains("db.read: no object 'missing'"));
        // What happened before stays done, and the result says what that was.
        TEST_COMPARE(db.archived(), 1);
        TEST_COMPARE(result["calls"].toObject()["db.setStatus"].toInt(), 1);

        // Caught: the script goes on.
        result = TestSupport::runScript(engine,
            "let failed = 0;\n"
            "for (const id of ['o2', 'nope', 'o3']) {\n"
            "    try { db.setStatus(id, 'archived'); } catch (e) { failed++; log(e.message); }\n"
            "}\n"
            "return failed;\n");
        TEST_COMPARE(result["status"].toString(), QString("ok"));
        TEST_COMPARE(result["result"].toInt(), 1);
        TEST_COMPARE(db.archived(), 3);
        TEST_ASSERT(result["log"].toArray().first().toString().contains("no object 'nope'"));
    }

    TEST_FUNCTION(testReturnValueLogAndThrow)
    {
        TEST_START;

        Engine engine;
        QStringList logged;
        QObject::connect(&engine, &Engine::runLog, [&](Engine::RunId, const QString& text) { logged << text; });
        QList<double> progress;
        QObject::connect(&engine, &Engine::runProgress,
                         [&](Engine::RunId, double done, double total, const QString&) { progress << done << total; });

        QJsonObject result = TestSupport::runScript(engine,
            "log('start', 3, {a: 1});\nprogress(1, 2, 'half');\nreturn [1, 'two', {three: 3}, null];\n");
        TEST_COMPARE(result["status"].toString(), QString("ok"));
        TEST_COMPARE(result["result"].toArray().size(), 4);
        TEST_COMPARE(result["result"].toArray()[2].toObject()["three"].toInt(), 3);
        TEST_COMPARE(result["log"].toArray().size(), 1);
        TEST_COMPARE(result["log"].toArray()[0].toString(), QString("start 3 {\"a\":1}"));
        TEST_COMPARE(logged, QStringList{"start 3 {\"a\":1}"});
        TEST_COMPARE(progress, (QList<double>{1, 2}));
        TEST_ASSERT(!result.contains("note"));

        // Nothing returned: said so, because it usually is a mistake.
        result = TestSupport::runScript(engine, "const a = 1 + 1;");
        TEST_COMPARE(result["status"].toString(), QString("ok"));
        TEST_ASSERT(result["result"].isNull());
        TEST_ASSERT(result["note"].toString().contains("return"));

        // The script's own throw, of an error and of a plain value.
        result = TestSupport::runScript(engine, "const n = 2;\nif (n > 1)\n    throw new Error('too many: ' + n);\n");
        TEST_COMPARE(result["phase"].toString(), QString("runtime"));
        TEST_COMPARE(result["message"].toString(), QString("too many: 2"));
        TEST_COMPARE(result["line"].toInt(), 3);
        result = TestSupport::runScript(engine, "throw 'just text';");
        TEST_COMPARE(result["phase"].toString(), QString("runtime"));
        TEST_COMPARE(result["message"].toString(), QString("just text"));
        result = TestSupport::runScript(engine, "const o = null;\nreturn o.field;\n");
        TEST_ASSERT(result["message"].toString().startsWith("TypeError"));
        TEST_COMPARE(result["line"].toInt(), 2);

        // Only the end of a long log goes back; all of it was signalled.
        logged.clear();
        result = TestSupport::runScript(engine, "for (let i = 0; i < 50; i++) log('line ' + i);\nreturn 0;");
        TEST_COMPARE(logged.size(), 50);
        TEST_COMPARE(result["log"].toArray().size(), 20);
        TEST_COMPARE(result["log"].toArray().last().toString(), QString("line 49"));
        TEST_COMPARE(result["log_lines"].toInt(), 50);

        // Syntax the engine of Qt 5 does not have yet.
        TEST_COMPARE(engine.check("return (async () => 1)();").phase, QString("syntax"));
    }

    TEST_FUNCTION(testCallLimit)
    {
        TEST_START;

        Engine engine;
        Db db;
        TestSupport::registerDb(engine, db);
        Engine::Limits limits;
        limits.maxCalls = 10;
        engine.setLimits(limits);
        TEST_COMPARE(engine.limits().maxCalls, 10);

        // Catching the failure does not get a script past the limit.
        const QJsonObject result = TestSupport::runScript(engine,
            "for (let i = 0; i < 100; i++) {\n    try { db.read('o1'); } catch (e) {}\n}\nreturn 'finished';\n");
        TEST_COMPARE(result["status"].toString(), QString("error"));
        TEST_COMPARE(result["phase"].toString(), QString("limit"));
        TEST_COMPARE(result["message"].toString(), QString("stopped after 10 method calls (the limit)"));
        TEST_COMPARE(db.reads, 10);
        TEST_COMPARE(result["calls"].toObject()["db.read"].toInt(), 10);

        // log and the like are not what the limit is about.
        const QJsonObject chatty = TestSupport::runScript(engine, "for (let i = 0; i < 30; i++) log(i);\nreturn 1;");
        TEST_COMPARE(chatty["status"].toString(), QString("ok"));
    }

    // An expensive method has a limit of its own.
    TEST_FUNCTION(testCallLimitPerMethod)
    {
        TEST_START;

        Engine engine;
        Db db;
        TestSupport::registerDb(engine, db);
        int sent = 0;
        ScriptHost::ScriptMethod mail("net.sendMail");
        mail.setDescription("Sends a mail.").addParameter("to", "string", "Address").setMaxCallsPerRun(3);
        TEST_COMPARE(mail.maxCallsPerRun(), 3);
        engine.registerMethod(mail, [&sent](const QJsonObject&) -> QJsonValue {
            ++sent;
            return true;
        });

        const QJsonObject result = TestSupport::runScript(engine,
            "for (let i = 0; i < 10; i++) {\n"
            "    db.read('o1');\n"
            "    try { net.sendMail('a@b.c'); } catch (e) {}\n"
            "}\n"
            "return 'finished';\n");
        TEST_COMPARE(result["status"].toString(), QString("error"));
        TEST_COMPARE(result["phase"].toString(), QString("limit"));
        TEST_COMPARE(result["message"].toString(), QString("stopped after 3 calls of net.sendMail (its limit)"));
        TEST_COMPARE(sent, 3);
        TEST_COMPARE(result["calls"].toObject()["net.sendMail"].toInt(), 3);
        // The other methods were not held back by it.
        TEST_COMPARE(result["calls"].toObject()["db.read"].toInt(), 4);

        // Per run: the next run may send again.
        TEST_COMPARE(TestSupport::runScript(engine, "net.sendMail('x'); return 1;")["status"].toString(), QString("ok"));
        TEST_COMPARE(sent, 4);
    }

    TEST_FUNCTION(testEndlessLoopHitsTheTimeLimit)
    {
        TEST_START;

        Engine engine;
        Engine::Limits limits;
        limits.timeoutMs = 300;
        engine.setLimits(limits);

        QElapsedTimer timer;
        timer.start();
        QJsonObject result = TestSupport::runScript(engine, "let n = 0;\nwhile (true) { try { n++; } catch (e) {} }\n");
        TEST_COMPARE(result["status"].toString(), QString("error"));
        TEST_COMPARE(result["phase"].toString(), QString("limit"));
        TEST_ASSERT(result["message"].toString().contains("time limit"));
        TEST_ASSERT(timer.elapsed() < 5000);
        TEST_ASSERT(!engine.isRunning());

        // A sleeping script is woken for it.
        timer.restart();
        result = TestSupport::runScript(engine, "sleep(60000);\nreturn 1;");
        TEST_COMPARE(result["phase"].toString(), QString("limit"));
        TEST_ASSERT(timer.elapsed() < 5000);

        // The next script runs normally.
        TEST_COMPARE(TestSupport::runScript(engine, "sleep(20);\nreturn 5;")["result"].toInt(), 5);
    }

    TEST_FUNCTION(testStop)
    {
        TEST_START;

        QJsonObject result;
        bool done = false;
        {
            Engine engine;
            int started = 0;
            bool startedExternal = true;
            QString startedName;
            QObject::connect(&engine, &Engine::runStarted, [&](Engine::RunId, const QString& name, bool external) {
                ++started;
                startedName = name;
                startedExternal = external;
            });

            Options options;
            options.name = "Sleeper";
            Engine::Limits limits;
            limits.maxConcurrentRuns = 1;
            engine.setLimits(limits);
            const Engine::RunId id = engine.run("sleep(60000);\nreturn 1;", options, [&](const QJsonObject& r) {
                result = r;
                done = true;
            });
            TEST_ASSERT(id != 0);
            TEST_COMPARE(started, 1);
            TEST_COMPARE(startedName, QString("Sleeper"));
            TEST_ASSERT(!startedExternal);
            TEST_ASSERT(engine.isRunning());
            TEST_ASSERT(engine.isRunning(id));
            TEST_ASSERT(!engine.isExternalRun(id));

            // No more than the limit allows.
            QJsonObject second;
            TEST_COMPARE(engine.run("return 2;", Options(), [&](const QJsonObject& r) { second = r; }), Engine::RunId(0));
            TEST_COMPARE(second["status"].toString(), QString("error"));
            TEST_ASSERT(second["message"].toString().contains("the limit of 1"));

            engine.stop(id);
            TEST_ASSERT(TestSupport::spin([&]() { return done; }, 5000));
            TEST_COMPARE(result["phase"].toString(), QString("cancelled"));
            TEST_ASSERT(!engine.isRunning());

            // Destroying the object ends a running script without waiting for it.
            engine.run("while (true) {}");
        }
        TEST_ASSERT(true);
    }

    TEST_FUNCTION(testAsynchronousMethod)
    {
        TEST_START;

        Engine engine;

        ScriptHost::ScriptMethod later("net.fetch");
        later.addParameter("what", "string", "What to get").setReadOnly(true);
        engine.registerAsyncMethod(later, [](const QJsonObject& args, ScriptHost::ScriptCompletion completion) {
            const QString what = args["what"].toString();
            QTimer::singleShot(30, qApp, [completion, what]() {
                if (what == "bad")
                    completion.fail("cannot get that");
                else
                    completion.finish(QJsonObject{{"got", what}});
                completion.finish("ignored");     // only the first answer counts
            });
        });

        QElapsedTimer timer;
        timer.start();
        QJsonObject result = TestSupport::runScript(engine, "return net.fetch('a').got + net.fetch('b').got;");
        TEST_COMPARE(result["result"].toString(), QString("ab"));
        TEST_ASSERT(timer.elapsed() >= 50);

        result = TestSupport::runScript(engine, "return net.fetch('bad');");
        TEST_COMPARE(result["phase"].toString(), QString("runtime"));
        TEST_ASSERT(result["message"].toString().contains("net.fetch: cannot get that"));

        // A method that never answers does not keep a stopped script alive.
        ScriptHost::ScriptMethod never("net.never");
        engine.registerAsyncMethod(never, [](const QJsonObject&, ScriptHost::ScriptCompletion) {});
        bool done = false;
        engine.run("net.never();", Options(), [&](const QJsonObject& r) {
            result = r;
            done = true;
        });
        TestSupport::spin([]() { return false; }, 100);
        TEST_ASSERT(engine.isRunning());
        engine.stopAll();
        TEST_ASSERT(TestSupport::spin([&]() { return done; }, 5000));
        TEST_COMPARE(result["phase"].toString(), QString("cancelled"));
    }

    // Reading is done, changing is written down.
    TEST_FUNCTION(testDryRun)
    {
        TEST_START;

        Engine engine;
        Db db;
        TestSupport::registerDb(engine, db);

        Options options;
        options.dryRun = true;
        const QJsonObject result = TestSupport::runScript(engine,
            "let n = 0;\n"
            "for (const id of db.list()) {\n"
            "    if (db.read(id).age > 20 && db.setStatus(id, 'archived').changed) n++;\n"
            "}\n"
            "return n;\n", options);

        TEST_COMPARE(result["status"].toString(), QString("ok"));
        TEST_ASSERT(result["dry_run"].toBool());
        TEST_COMPARE(result["result"].toInt(), 4);          // answered with the method's example
        TEST_COMPARE(db.reads, 6);
        TEST_COMPARE(db.writes, 0);
        TEST_COMPARE(db.archived(), 0);
        const QJsonObject would = result["would_call"].toObject();
        TEST_COMPARE(would.keys(), QStringList{"db.setStatus"});
        TEST_COMPARE(would["db.setStatus"].toObject()["count"].toInt(), 4);
        const QJsonArray first = would["db.setStatus"].toObject()["first_arguments"].toArray();
        TEST_COMPARE(first.size(), 3);
        TEST_COMPARE(first[0].toArray()[0].toString(), QString("o3"));
        TEST_COMPARE(first[0].toArray()[1].toString(), QString("archived"));
        TEST_ASSERT(engine.runs().last().dryRun);
    }

    TEST_FUNCTION(testApiDefinition)
    {
        TEST_START;

        Engine engine;
        Db db;
        TestSupport::registerDb(engine, db);

        ScriptHost::ScriptMethod find("db.find");
        find.setDescription("Finds objects by a text in\n  their name.").setReadOnly(true)
            .addParameter("text", "string", "What the name must contain")
            .addParameter("limit", "integer", "How many at most", false)
            .setDefault("limit", 10).setRange("limit", 1, 100)
            .addParameter("fields", "array", "Which fields to return", false)
            .setReturns("array", "The matches", QJsonArray{QJsonObject{{"id", "o1"}, {"name", "Pump"}}});
        engine.registerMethod(find, [](const QJsonObject&) -> QJsonValue { return QJsonArray(); });

        TEST_COMPARE(find.signature(),
                     QString("db.find(text: string, limit?: number, fields?: any[]): { id: string, name: string }[]"));
        TEST_COMPARE(find.nameSpace(), QString("db"));
        TEST_COMPARE(find.functionName(), QString("find"));
        const QString declaration = find.declaration("  ");
        TEST_ASSERT(declaration.startsWith("  /** Finds objects by a text in their name. Read-only.\n"));
        TEST_ASSERT(declaration.contains("   *  @param limit How many at most (default 10, 1 to 100)\n"));
        TEST_ASSERT(declaration.contains("   *  @returns The matches */\n"));
        TEST_ASSERT(declaration.endsWith("  function find(text: string, limit?: number, fields?: any[]): { id: string, name: string }[];\n"));

        const QString all = engine.apiDefinition();
        TEST_ASSERT(all.contains("declare namespace db {\n"));
        TEST_ASSERT(all.contains("  function read(id: string): { age: number, id: string, name: string, status: string };\n"));
        TEST_ASSERT(all.contains("  function setStatus(id: string, status: \"active\" | \"archived\"): { changed: boolean };\n"));
        TEST_ASSERT(all.contains("  function list(): string[];\n"));
        TEST_ASSERT(all.contains("declare function sleep(ms: number): void;\n"));
        TEST_ASSERT(all.contains("declare const args: any;"));

        // A query: the matching methods only.
        const QString some = engine.apiDefinition("setStatus");
        TEST_ASSERT(some.contains("function setStatus("));
        TEST_ASSERT(!some.contains("function read("));
        TEST_ASSERT(!some.contains("declare function log("));
        const QString byNamespace = engine.apiDefinition("db");
        TEST_ASSERT(byNamespace.contains("function read(") && byNamespace.contains("function find("));
        const QString byWord = engine.apiDefinition("contain");       // from a description
        TEST_ASSERT(byWord.contains("function find("));
        TEST_ASSERT(engine.apiDefinition("zebra").contains("Nothing matches"));

        // Too much for the budget: an index, one line per method.
        engine.setApiBudgetChars(300);
        TEST_COMPARE(engine.apiBudgetChars(), 300);
        const QString index = engine.apiDefinition();
        TEST_ASSERT(index.contains("db.find - Finds objects by a text in their name."));
        TEST_ASSERT(index.contains("Ask for the declarations with a query"));
        TEST_ASSERT(!index.contains("declare namespace"));
        TEST_ASSERT(engine.apiDefinition("db.find").contains("function find("));
        engine.setApiBudgetChars(0);                                    // no limit
        TEST_ASSERT(engine.apiDefinition().contains("declare namespace db {\n"));

        // methods(): everything, sorted, built-in ones included.
        QStringList names;
        for (const ScriptHost::ScriptMethod& method : engine.methods())
            names << method.name();
        TEST_COMPARE(names, (QStringList{"clearInterval", "clearTimeout", "db.find", "db.list", "db.read",
                                         "db.setStatus", "log", "pause", "progress", "setInterval",
                                         "setTimeout", "sleep"}));
    }

    TEST_FUNCTION(testUserSettingsAndLocks)
    {
        TEST_START;

        Engine engine;
        Db db;
        TestSupport::registerDb(engine, db);
        int settingsChanges = 0;
        QObject::connect(&engine, &Engine::settingsChanged, [&]() { ++settingsChanges; });

        // The user's choices, out and back in.
        TEST_ASSERT(engine.setEnabledByUser(false));
        TEST_ASSERT(engine.setApprovalRequiredByUser(Engine::ScriptKind::NewChanging, true));
        TEST_ASSERT(engine.setMethodEnabledByUser("db.setStatus", false));
        TEST_ASSERT(!engine.setMethodEnabledByUser("db.unknown", false));
        const QJsonObject saved = engine.userSettings();
        TEST_COMPARE(saved["enabled"].toBool(), false);
        TEST_COMPARE(saved["approval"].toObject()["new_changing"].toBool(), true);
        TEST_COMPARE(saved["approval"].toObject()["saved_changing"].toBool(), false);
        TEST_COMPARE(saved["methods"].toObject()["db.setStatus"].toBool(), false);
        TEST_COMPARE(saved["methods"].toObject()["db.read"].toBool(), true);

        Engine other;
        Db db2;
        TestSupport::registerDb(other, db2);
        other.applyUserSettings(saved);
        TEST_ASSERT(!other.isEnabled());
        TEST_ASSERT(other.isApprovalRequired(Engine::ScriptKind::NewChanging));
        TEST_ASSERT(!other.isApprovalRequired(Engine::ScriptKind::SavedChanging));
        TEST_ASSERT(!other.isMethodEnabled("db.setStatus"));
        TEST_ASSERT(other.isMethodEnabled("db.read"));

        // What the application fixes, the user cannot change - and stored
        // choices from before do not get around it.
        Engine locked;
        Db db3;
        TestSupport::registerDb(locked, db3);
        locked.setEnabledUserConfigurable(false);
        locked.setApproval(Engine::Approval::Always);
        locked.setApprovalUserConfigurable(false);
        locked.setMethodEnabled("db.setStatus", false);
        locked.setMethodUserConfigurable("db.setStatus", false);
        locked.setMethodGroupUserConfigurable("Script", false);
        TEST_ASSERT(!locked.setEnabledByUser(false));
        TEST_ASSERT(!locked.setApprovalRequiredByUser(Engine::ScriptKind::DryRun, false));
        TEST_ASSERT(!locked.setMethodEnabledByUser("db.setStatus", true));
        TEST_ASSERT(!locked.setMethodEnabledByUser("log", false));
        TEST_ASSERT(locked.setMethodEnabledByUser("db.read", false));
        TEST_ASSERT(!locked.isMethodUserConfigurable("db.setStatus"));
        TEST_ASSERT(locked.isMethodUserConfigurable("db.read"));

        locked.applyUserSettings(QJsonObject{{"enabled", false}, {"approval", "never"},
            {"methods", QJsonObject{{"db.setStatus", true}, {"db.list", false}}}});
        TEST_ASSERT(locked.isEnabled());
        TEST_ASSERT(locked.isApprovalRequired(Engine::ScriptKind::NewReading));
        TEST_ASSERT(!locked.isMethodEnabled("db.setStatus"));
        TEST_ASSERT(!locked.isMethodEnabled("db.list"));
        const QJsonObject lockedSettings = locked.userSettings();
        TEST_ASSERT(!lockedSettings.contains("enabled"));
        TEST_ASSERT(!lockedSettings.contains("approval"));
        TEST_ASSERT(!lockedSettings["methods"].toObject().contains("db.setStatus"));
        TEST_ASSERT(!lockedSettings["methods"].toObject().contains("log"));
        TEST_ASSERT(settingsChanges >= 1);

        // The requester's name is a setting the widgets follow.
        TEST_COMPARE(engine.requesterName(), QString("another program"));
        const int before = settingsChanges;
        engine.setRequesterName("the assistant");
        TEST_COMPARE(engine.requesterName(), QString("the assistant"));
        TEST_COMPARE(settingsChanges, before + 1);
    }

    // The debugger: stop before each call, see what goes in and comes out.
    TEST_FUNCTION(testCallStepping)
    {
        TEST_START;

        Engine engine;
        Db db;
        TestSupport::registerDb(engine, db);

        struct Stop { int line; QString method; QJsonArray args; };
        QList<Stop> stops;
        QStringList returned;
        bool autoStep = true;
        QObject::connect(&engine, &Engine::runPaused,
                         [&](Engine::RunId, int line, const QString& method, const QJsonArray& arguments) {
            stops.append({line, method, arguments});
            if (autoStep)
                engine.stepRun();
            else
                engine.continueRun();
        });
        QObject::connect(&engine, &Engine::runCallFinished,
                         [&](Engine::RunId, int line, const QString& method, const QJsonArray&, const QJsonValue& value,
                             const QString& error) {
            returned << QString("%1:%2:%3").arg(line).arg(method,
                error.isEmpty() ? QString::fromUtf8(QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact)).left(24)
                                : error);
        });

        const QString code =
            "const ids = db.list();\n"                       // 1
            "let n = 0;\n"                                   // 2
            "for (const id of ids.slice(0, 2)) {\n"          // 3
            "    const o = db.read(id);\n"                   // 4
            "    if (o.age > 10)\n"                          // 5
            "        db.setStatus(id, 'archived');\n"        // 6
            "    n++;\n"                                     // 7
            "}\n"                                            // 8
            "pause('before the end');\n"                     // 9
            "return n;\n";                                   // 10

        // Step by step from the first call.
        Options options;
        options.debug = true;
        options.stepFromStart = true;
        QJsonObject result = TestSupport::runScript(engine, code, options);
        TEST_COMPARE(result["result"].toInt(), 2);
        TEST_COMPARE(stops.size(), 5);
        if (stops.size() == 5) {
            TEST_COMPARE(stops[0].line, 1);
            TEST_COMPARE(stops[0].method, QString("db.list"));
            TEST_COMPARE(stops[1].line, 4);
            TEST_COMPARE(stops[1].method, QString("db.read"));
            TEST_COMPARE(stops[1].args[0].toString(), QString("o1"));
            TEST_COMPARE(stops[2].line, 4);
            TEST_COMPARE(stops[3].line, 6);
            TEST_COMPARE(stops[3].method, QString("db.setStatus"));
            TEST_COMPARE(stops[3].args[1].toString(), QString("archived"));
            TEST_COMPARE(stops[4].line, 9);
            TEST_COMPARE(stops[4].method, QString("pause"));
        }
        TEST_COMPARE(returned.size(), 4);
        if (returned.size() == 4) {
            TEST_ASSERT(returned[0].startsWith("1:db.list:[[\"o1\""));
            TEST_ASSERT(returned[3].startsWith("6:db.setStatus:"));
        }

        // Breakpoint: runs up to the calls on that line only.
        stops.clear();
        autoStep = false;
        Options breakpoint;
        breakpoint.debug = true;
        breakpoint.breakpoints = QSet<int>{4};
        result = TestSupport::runScript(engine, code, breakpoint);
        TEST_COMPARE(result["result"].toInt(), 2);
        TEST_COMPARE(stops.size(), 3);                      // two reads and pause()
        if (stops.size() == 3) {
            TEST_COMPARE(stops[0].line, 4);
            TEST_COMPARE(stops[1].line, 4);
            TEST_COMPARE(stops[2].method, QString("pause"));
        }

        // Stopping while paused ends the script; no time limit applies to a
        // script that waits for the user.
        Engine::Limits limits;
        limits.timeoutMs = 200;
        engine.setLimits(limits);
        QObject::disconnect(&engine, &Engine::runPaused, nullptr, nullptr);
        bool paused = false;
        QObject::connect(&engine, &Engine::runPaused, [&]() { paused = true; });
        bool done = false;
        engine.run(code, options, [&](const QJsonObject& r) {
            result = r;
            done = true;
        });
        TEST_ASSERT(TestSupport::spin([&]() { return paused; }, 5000));
        TestSupport::spin([]() { return false; }, 500);
        TEST_ASSERT(engine.isRunning());                    // still waiting, not timed out
        engine.stopAll();
        TEST_ASSERT(TestSupport::spin([&]() { return done; }, 5000));
        TEST_COMPARE(result["phase"].toString(), QString("cancelled"));

        // Outside the debugger pause() does nothing.
        QObject::disconnect(&engine, &Engine::runPaused, nullptr, nullptr);
        limits.timeoutMs = 300000;
        engine.setLimits(limits);
        TEST_COMPARE(TestSupport::runScript(engine, "pause('x');\nreturn 7;")["result"].toInt(), 7);
    }

    // The debugger statement by statement, with the variables at each stop.
    TEST_FUNCTION(testStatementStepping)
    {
        TEST_START;

        Engine engine;
        Db db;
        TestSupport::registerDb(engine, db);

        struct Stop { int line; QString method; QJsonObject variables; };
        QList<Stop> stops;
        int mode = 0;       // 0 next statement, 1 next call, 2 next breakpoint
        QObject::connect(&engine, &Engine::runPaused,
                         [&](Engine::RunId, int line, const QString& method, const QJsonArray&, const QJsonObject& variables) {
            stops.append({line, method, variables});
            if (mode == 0)
                engine.stepStatement();
            else if (mode == 1)
                engine.stepRun();
            else
                engine.continueRun();
        });
        auto lines = [&stops]() {
            QStringList text;
            for (const Stop& s : stops)
                text << QString::number(s.line);
            return text.join(' ');
        };

        const QString code =
            "const ids = db.list();\n"                       // 1
            "let n = 0;\n"                                   // 2
            "for (const id of ids.slice(0, 2)) {\n"          // 3
            "    const o = db.read(id);\n"                   // 4
            "    if (o.age > 10)\n"                          // 5
            "        db.setStatus(id, 'archived');\n"        // 6
            "    n++;\n"                                     // 7
            "}\n"                                            // 8
            "return n;\n";                                   // 9

        Options options;
        options.debug = true;
        options.statements = true;
        options.stepFromStart = true;
        QJsonObject result = TestSupport::runScript(engine, code, options);
        TEST_COMPARE(result["result"].toInt(), 2);
        // Line 6 is the single statement of an if without braces, and only
        // reached for the second object.
        TEST_COMPARE(lines(), QString("1 2 3 4 5 7 4 5 6 7 9"));
        if (stops.size() == 11) {
            for (const Stop& s : stops)
                TEST_ASSERT(s.method.isEmpty());
            // Before line 1 nothing is declared yet.
            TEST_COMPARE(stops[0].variables["args"].toString(), QString("{}"));
            TEST_ASSERT(!stops[0].variables.contains("ids"));
            TEST_ASSERT(stops[1].variables["ids"].toString().startsWith("[\"o1\""));
            TEST_ASSERT(!stops[1].variables.contains("n"));
            TEST_COMPARE(stops[2].variables["n"].toString(), QString("0"));
            TEST_COMPARE(stops[4].variables["id"].toString(), QString("\"o1\""));
            TEST_ASSERT(stops[4].variables["o"].toString().contains("\"age\":10"));
            TEST_COMPARE(stops[9].variables["n"].toString(), QString("1"));
            // After the loop its variables are gone.
            TEST_COMPARE(stops[10].variables["n"].toString(), QString("2"));
            TEST_ASSERT(!stops[10].variables.contains("id"));
        }
        TEST_COMPARE(db.archived(), 1);

        // Breakpoints stop before the statement, once per time it is reached.
        stops.clear();
        mode = 2;
        Options breakpoints;
        breakpoints.debug = true;
        breakpoints.statements = true;
        breakpoints.breakpoints = QSet<int>{4, 6};
        result = TestSupport::runScript(engine, code, breakpoints);
        TEST_COMPARE(result["result"].toInt(), 2);
        TEST_COMPARE(lines(), QString("4 4 6"));
        for (const Stop& s : stops)
            TEST_ASSERT(s.method.isEmpty());

        // From a statement on to the next call.
        stops.clear();
        mode = 1;
        result = TestSupport::runScript(engine, code, options);
        TEST_COMPARE(result["result"].toInt(), 2);
        TEST_ASSERT(stops.size() >= 3);
        if (stops.size() >= 3) {
            TEST_ASSERT(stops[0].method.isEmpty());
            TEST_COMPARE(stops[1].method, QString("db.list"));
            TEST_COMPARE(stops[1].line, 1);
            TEST_COMPARE(stops[1].variables["args"].toString(), QString("{}"));
            TEST_COMPARE(stops[2].method, QString("db.read"));
            TEST_COMPARE(stops[2].line, 4);
        }

        // What is not JSON is shown all the same; functions of the script are stepped into.
        stops.clear();
        mode = 0;
        result = TestSupport::runScript(engine,
            "let u;\n"                                       // 1
            "const twice = function(a) {\n"                  // 2
            "    return a * 2;\n"                            // 3
            "};\n"                                           // 4
            "const half = 1 / 2\n"                           // 5  no semicolon
            "return twice(half);\n",                         // 6
            options);
        TEST_COMPARE(result["result"].toInt(), 1);
        TEST_COMPARE(lines(), QString("1 2 5 6 3"));
        if (stops.size() == 5) {
            TEST_COMPARE(stops[3].variables["u"].toString(), QString("undefined"));
            TEST_COMPARE(stops[3].variables["twice"].toString(), QString("function"));
            TEST_COMPARE(stops[3].variables["half"].toString(), QString("0.5"));
            TEST_COMPARE(stops[4].variables["a"].toString(), QString("0.5"));
        }

        // A script the debugger cannot split into statements is stepped call by call.
        stops.clear();
        result = TestSupport::runScript(engine,
            "if (true)\n"
            "    outer: for (;;) break outer;\n"
            "return db.list().length;\n",
            options);
        TEST_COMPARE(result["result"].toInt(), 6);
        TEST_COMPARE(stops.size(), 1);
        if (stops.size() == 1) {
            TEST_COMPARE(stops[0].method, QString("db.list"));
            TEST_COMPARE(stops[0].line, 3);
        }

        // Without `statements` nothing of this happens: see testCallStepping.
    }
};

TEST_INSTANTIATE(TST_ScriptEngine);
