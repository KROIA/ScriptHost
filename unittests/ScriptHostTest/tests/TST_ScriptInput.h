#pragma once

// forwardInput(): key and mouse events of a widget reach the scripts with
// the right fields, moves do not, input.mouse() reads the pointer (requested
// runs only with the opt-in), and forwarding ends with the forwarder or the
// widget.

#include "UnitTest.h"
#include "TestSupport.h"
#include <QCoreApplication>
#include <QCursor>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QMap>
#include <QMouseEvent>
#include <QPointer>
#include <QWheelEvent>
#include <QWidget>

class TST_ScriptInput : public UnitTest::Test
{
    TEST_CLASS(TST_ScriptInput)
public:
    TST_ScriptInput()
        : Test("TST_ScriptInput")
    {
        ADD_TEST(TST_ScriptInput::testEventsArriveWithTheirFields);
        ADD_TEST(TST_ScriptInput::testMousePosition);
        ADD_TEST(TST_ScriptInput::testForwardingStops);
    }

private:
    using Engine = ScriptHost::ScriptEngine;

    struct Run
    {
        Engine::RunId id = 0;
        bool          done = false;
        QJsonObject   result;
        QStringList   log;
    };

    // A script that logs each event it gets until the test posts "end".
    static void listen(Engine& engine, Run& run, const QString& handler)
    {
        QObject::connect(&engine, &Engine::runLog, [&run](Engine::RunId id, const QString& text) {
            if (id == run.id)
                run.log << text;
        });
        run.id = engine.run("on('key', " + handler + ");\n"
                            "on('mouse', " + handler + ");\n"
                            "on('end', () => { off('key'); off('mouse'); off('end'); });\n"
                            "log('ready');\nreturn 1;",
                            Engine::RunOptions(), [&run](const QJsonObject& r) {
            run.result = r;
            run.done = true;
        });
    }

    static void send(QWidget* widget, QEvent* event) { QCoreApplication::sendEvent(widget, event); }

    static QMouseEvent mouse(QEvent::Type type, QWidget& widget, const QPoint& at, Qt::MouseButton button,
                             Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        return QMouseEvent(type, QPointF(at), QPointF(widget.mapToGlobal(at)), button, buttons, modifiers);
    }

    TEST_FUNCTION(testEventsArriveWithTheirFields)
    {
        TEST_START;

        Engine engine;
        QWidget area;
        area.resize(200, 100);
        ScriptHost::forwardInput(&engine, &area);
        Run run;
        listen(engine, run, "d => log(JSON.stringify(d))");
        TEST_ASSERT(TestSupport::spin([&]() { return run.log.contains("ready"); }));

        QKeyEvent press(QEvent::KeyPress, Qt::Key_A, Qt::ShiftModifier, "A");
        QKeyEvent release(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier, " ", true);
        QMouseEvent down = mouse(QEvent::MouseButtonPress, area, {10, 20}, Qt::LeftButton, Qt::LeftButton,
                                 Qt::ControlModifier);
        QMouseEvent move = mouse(QEvent::MouseMove, area, {15, 25}, Qt::NoButton, Qt::LeftButton);
        QMouseEvent up = mouse(QEvent::MouseButtonRelease, area, {11, 21}, Qt::LeftButton, Qt::NoButton);
        QMouseEvent twice = mouse(QEvent::MouseButtonDblClick, area, {12, 22}, Qt::RightButton, Qt::RightButton);
        QWheelEvent wheel(QPointF(30, 40), QPointF(area.mapToGlobal(QPoint(30, 40))), QPoint(), QPoint(0, -120),
                          Qt::NoButton, Qt::AltModifier, Qt::NoScrollPhase, false);
        for (QEvent* event : std::initializer_list<QEvent*>{&press, &release, &down, &move, &up, &twice, &wheel})
            send(&area, event);
        engine.postEvent("end", QJsonObject());
        TEST_ASSERT(TestSupport::spin([&]() { return run.done; }));
        TEST_COMPARE(run.result["status"].toString(), QString("ok"));

        const QList<QJsonObject> expected{
            {{"type", "down"}, {"key", "A"}, {"code", 65}, {"text", "A"}, {"modifiers", QJsonArray{"shift"}},
             {"repeat", false}},
            {{"type", "up"}, {"key", "Space"}, {"code", 32}, {"text", " "}, {"modifiers", QJsonArray()},
             {"repeat", true}},
            {{"type", "down"}, {"button", "left"}, {"buttons", QJsonArray{"left"}}, {"x", 10}, {"y", 20},
             {"modifiers", QJsonArray{"control"}}},
            {{"type", "up"}, {"button", "left"}, {"buttons", QJsonArray()}, {"x", 11}, {"y", 21},
             {"modifiers", QJsonArray()}},
            {{"type", "dblclick"}, {"button", "right"}, {"buttons", QJsonArray{"right"}}, {"x", 12}, {"y", 22},
             {"modifiers", QJsonArray()}},
            {{"type", "wheel"}, {"button", "none"}, {"buttons", QJsonArray()}, {"x", 30}, {"y", 40},
             {"modifiers", QJsonArray{"alt"}}, {"deltaX", 0}, {"deltaY", -120}}};
        // "ready", then the events - the move is not among them.
        TEST_COMPARE(run.log.size(), expected.size() + 1);
        for (int i = 0; i < expected.size() && i + 1 < run.log.size(); ++i)
            TEST_COMPARE(QJsonDocument::fromJson(run.log[i + 1].toUtf8()).object(), expected[i]);

        // The key name is the Qt::Key name.
        QKeyEvent pageUp(QEvent::KeyPress, Qt::Key_PageUp, Qt::NoModifier);
        Run second;
        listen(engine, second, "d => log(d.key + ' ' + d.code)");
        TEST_ASSERT(TestSupport::spin([&]() { return second.log.contains("ready"); }));
        send(&area, &pageUp);
        engine.postEvent("end", QJsonObject());
        TEST_ASSERT(TestSupport::spin([&]() { return second.done; }));
        TEST_COMPARE(second.log, (QStringList{"ready", QString("PageUp %1").arg(int(Qt::Key_PageUp))}));
    }

    TEST_FUNCTION(testMousePosition)
    {
        TEST_START;

        Engine engine;
        QWidget area;
        area.setGeometry(100, 100, 200, 100);
        TEST_ASSERT(!engine.hasMethod("input.mouse"));
        ScriptHost::forwardInput(&engine, &area);
        TEST_ASSERT(engine.hasMethod("input.mouse"));

        // The test does not move the real mouse (the user works meanwhile):
        // wherever the pointer is, the answer has to agree with itself.
        const QJsonObject result = TestSupport::runScript(engine, "return input.mouse();");
        TEST_COMPARE(result["status"].toString(), QString("ok"));
        const QJsonObject position = result["result"].toObject();
        TEST_ASSERT(position["x"].isDouble() && position["y"].isDouble());
        TEST_COMPARE(position["inside"].toBool(),
                     area.rect().contains(QPoint(position["x"].toInt(), position["y"].toInt())));
        TEST_ASSERT(position["buttons"].isArray());
        // It is a method call.
        TEST_COMPARE(result["calls"].toObject()["input.mouse"].toInt(), 1);

        // Requested runs: only with the "mouse" opt-in.
        const QJsonObject blocked = TestSupport::request(engine, TestSupport::codeRequest("return input.mouse();"));
        TEST_COMPARE(blocked["phase"].toString(), QString("runtime"));
        TEST_COMPARE(blocked["message"].toString(),
                     QString("input.mouse: the application does not let requested scripts read the mouse"));
        engine.allowEventForRequestedRuns("mouse");
        const QJsonObject allowed = TestSupport::request(engine, TestSupport::codeRequest("return input.mouse();"));
        TEST_COMPARE(allowed["status"].toString(), QString("ok"));
        TEST_ASSERT(allowed["result"].toObject().contains("inside"));
    }

    TEST_FUNCTION(testForwardingStops)
    {
        TEST_START;

        Engine engine;
        Run run;
        listen(engine, run, "d => { if (d.type === 'down') log(d.key || d.button); }");
        TEST_ASSERT(TestSupport::spin([&]() { return run.log.contains("ready"); }));

        auto key = [](QWidget* widget, Qt::Key k) {
            QKeyEvent press(QEvent::KeyPress, k, Qt::NoModifier);
            send(widget, &press);
        };
        auto* area = new QWidget;
        QPointer<QObject> forwarder = ScriptHost::forwardInput(&engine, area);
        key(area, Qt::Key_A);
        delete forwarder;
        TEST_ASSERT(!engine.hasMethod("input.mouse"));
        key(area, Qt::Key_B);       // not forwarded

        // An older forwarder going leaves the newer one's input.mouse.
        QWidget other;
        QPointer<QObject> older = ScriptHost::forwardInput(&engine, &other);
        QPointer<QObject> newer = ScriptHost::forwardInput(&engine, area);
        delete older;
        TEST_ASSERT(engine.hasMethod("input.mouse"));
        key(area, Qt::Key_C);
        key(&other, Qt::Key_D);     // not forwarded

        // The widget goes: its forwarder and input.mouse with it.
        delete area;
        TEST_ASSERT(newer.isNull());
        TEST_ASSERT(!engine.hasMethod("input.mouse"));

        engine.postEvent("end", QJsonObject());
        TEST_ASSERT(TestSupport::spin([&]() { return run.done; }));
        TEST_COMPARE(run.log, (QStringList{"ready", "A", "C"}));

        // The engine goes first: the widget still works.
        auto* gone = new Engine;
        QWidget last;
        ScriptHost::forwardInput(gone, &last);
        delete gone;
        key(&last, Qt::Key_E);
    }
};

TEST_INSTANTIATE(TST_ScriptInput);
