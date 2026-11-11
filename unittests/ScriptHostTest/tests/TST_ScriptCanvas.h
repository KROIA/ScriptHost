#pragma once

// attachCanvas() and `gfx`: each primitive and the batch forms reach the
// screen (checked on target->grab()), present() shows the newest frame and
// keeps it, a run's layer goes with the run, runs paint in start order, only
// the frame's area is repainted, resizes reach the scripts, requested runs
// need the opt-in, the canvas stays on top and detaches when deleted, and
// wrong arguments throw.

#include "UnitTest.h"
#include "TestSupport.h"
#include <QImage>
#include <QMap>
#include <QPointer>
#include <QThread>
#include <QWidget>

class TST_ScriptCanvas : public UnitTest::Test
{
    TEST_CLASS(TST_ScriptCanvas)
public:
    TST_ScriptCanvas()
        : Test("TST_ScriptCanvas")
    {
        ADD_TEST(TST_ScriptCanvas::testPrimitives);
        ADD_TEST(TST_ScriptCanvas::testBatchForms);
        ADD_TEST(TST_ScriptCanvas::testPresentKeepsTheLatestFrame);
        ADD_TEST(TST_ScriptCanvas::testLayerGoesWithTheRun);
        ADD_TEST(TST_ScriptCanvas::testTwoRunsLayers);
        ADD_TEST(TST_ScriptCanvas::testDirtyRegion);
        ADD_TEST(TST_ScriptCanvas::testResize);
        ADD_TEST(TST_ScriptCanvas::testRequestedRuns);
        ADD_TEST(TST_ScriptCanvas::testDetach);
        ADD_TEST(TST_ScriptCanvas::testCheckAndDefinition);
        ADD_TEST(TST_ScriptCanvas::testStaysOnTop);
        ADD_TEST(TST_ScriptCanvas::testArgumentErrors);
    }

private:
    using Engine = ScriptHost::ScriptEngine;
    using Options = TestSupport::Options;

    struct Outcome
    {
        bool        done = false;
        QJsonObject result;
    };

    // An engine and a white widget with a canvas, not on screen.
    struct Field
    {
        Engine                              engine;
        QWidget                             target;
        ScriptHost::ScriptCanvas*           canvas = nullptr;
        QMap<Engine::RunId, QStringList>    logs;
        QMap<Engine::RunId, QList<QRect>>   presented;

        explicit Field(int width = 240, int height = 160)
        {
            target.setAttribute(Qt::WA_DontShowOnScreen);
            QPalette palette = target.palette();
            palette.setColor(QPalette::Window, Qt::white);
            target.setPalette(palette);
            target.setAutoFillBackground(true);
            target.resize(width, height);
            target.show();
            canvas = ScriptHost::attachCanvas(&engine, &target);
            QObject::connect(&engine, &Engine::runLog, [this](Engine::RunId id, const QString& text) {
                logs[id] << text;
            });
            QObject::connect(canvas, &ScriptHost::ScriptCanvas::presented,
                             [this](Engine::RunId id, const QRect& dirty) { presented[id] << dirty; });
        }
        ~Field()
        {
            engine.stopAll();
            TestSupport::spin([this]() { return !engine.isRunning(); });
        }

        // The script, then it stays alive until the event "done".
        Engine::RunId start(const QString& code, Outcome& out, const Options& options = Options())
        {
            return engine.run(code + "\non('done', () => off('done'));\nlog('ready');", options,
                              [&out](const QJsonObject& r) {
                out.result = r;
                out.done = true;
            });
        }
        // Started and its first frame shown.
        bool shown(Engine::RunId id)
        {
            return TestSupport::spin([&]() { return !presented.value(id).isEmpty(); });
        }
        QImage image() { return target.grab().toImage(); }
        QColor pixel(int x, int y)
        {
            const QImage picture = image();
            const qreal ratio = picture.devicePixelRatio();
            return picture.pixelColor(qRound(x * ratio), qRound(y * ratio));
        }
        // How many pixels of the area are dark (or red, with `red`).
        int count(const QRect& area, bool red = false)
        {
            const QImage picture = image();
            const qreal ratio = picture.devicePixelRatio();
            int n = 0;
            for (int y = area.top(); y <= area.bottom(); ++y) {
                for (int x = area.left(); x <= area.right(); ++x) {
                    const QColor c = picture.pixelColor(qRound(x * ratio), qRound(y * ratio));
                    n += red ? (c.red() > 150 && c.green() < 110 ? 1 : 0) : (c.lightness() < 110 ? 1 : 0);
                }
            }
            return n;
        }
    };

    static bool near(const QColor& a, const QColor& b)
    {
        return qAbs(a.red() - b.red()) <= 40 && qAbs(a.green() - b.green()) <= 40 && qAbs(a.blue() - b.blue()) <= 40;
    }
    static QColor white() { return QColor(255, 255, 255); }

    TEST_FUNCTION(testPrimitives)
    {
        TEST_START;

        Field field(320, 200);
        Outcome out;
        const Engine::RunId id = field.start(
            "gfx.rect(10, 10, 40, 30, {fill: 'red', stroke: 'none'});\n"
            "gfx.rect(60, 10, 40, 30, {stroke: 'blue', width: 4});\n"
            "gfx.roundedRect(110, 10, 40, 30, 10, {fill: '#00ff00', stroke: 'none'});\n"
            "gfx.circle(30, 80, 15, {fill: 'blue', stroke: 'none'});\n"
            "gfx.ellipse(80, 80, 20, 10, {fill: 'red', stroke: 'none'});\n"
            "gfx.line(10, 110, 90, 110, {width: 4});\n"
            "gfx.line(10, 130, 190, 130, {width: 4, dash: 'dash'});\n"
            "gfx.arrow(110, 80, 190, 80, {stroke: 'red', width: 2});\n"
            "gfx.polyline([110, 100, 150, 100, 150, 120], {stroke: 'blue', width: 4});\n"
            "gfx.polygon([160, 100, 190, 100, 175, 125], {fill: '#ff00ff', stroke: 'none'});\n"
            "gfx.text(210, 10, 'MMMM', {fill: 'black', font: {size: 20, bold: true}});\n"
            "gfx.rect(210, 60, 40, 30, {fill: 'blue', stroke: 'none', opacity: 0.5});\n"
            "gfx.rect(260, 60, 40, 30, {fill: '#800000ff', stroke: 'none'});\n"
            "gfx.text(260, 150, 'MM', {stroke: 'red', align: 'right', valign: 'bottom', font: {size: 16}});\n"
            "gfx.present();",
            out);
        TEST_ASSERT(field.shown(id));
        TEST_COMPARE(field.canvas->layers(), (QList<Engine::RunId>{id}));

        TEST_ASSERT(near(field.pixel(30, 25), Qt::red));                // filled rect
        TEST_ASSERT(near(field.pixel(60, 25), Qt::blue));               // outline only
        TEST_ASSERT(near(field.pixel(80, 25), white()));
        TEST_ASSERT(near(field.pixel(130, 25), Qt::green));             // rounded: the corner stays
        TEST_ASSERT(near(field.pixel(111, 11), white()));
        TEST_ASSERT(near(field.pixel(30, 80), Qt::blue));               // circle
        TEST_ASSERT(near(field.pixel(17, 67), white()));
        TEST_ASSERT(near(field.pixel(80, 87), Qt::red));                // ellipse
        TEST_ASSERT(near(field.pixel(98, 88), white()));
        TEST_ASSERT(near(field.pixel(50, 110), Qt::black));             // line, default stroke black
        const int dashes = field.count(QRect(14, 130, 170, 1));         // dashed: gaps
        TEST_ASSERT(dashes > 30 && dashes < 160);
        TEST_ASSERT(near(field.pixel(150, 80), Qt::red));               // arrow shaft
        TEST_ASSERT(near(field.pixel(150, 84), white()));
        TEST_ASSERT(near(field.pixel(184, 81), Qt::red));               // ... its head is wider
        TEST_ASSERT(near(field.pixel(130, 100), Qt::blue));             // polyline, not filled
        TEST_ASSERT(near(field.pixel(150, 110), Qt::blue));
        TEST_ASSERT(near(field.pixel(130, 115), white()));
        TEST_ASSERT(near(field.pixel(175, 108), Qt::magenta));          // polygon
        TEST_ASSERT(field.count(QRect(210, 10, 80, 25)) > 40);          // text from its top left
        TEST_COMPARE(field.count(QRect(210, 40, 80, 10)), 0);
        TEST_ASSERT(near(field.pixel(230, 75), QColor(127, 127, 255))); // opacity
        TEST_ASSERT(near(field.pixel(280, 75), QColor(127, 127, 255))); // #aarrggbb
        TEST_ASSERT(field.count(QRect(200, 128, 60, 22), true) > 10);   // text right, bottom; stroke colour
        TEST_COMPARE(field.count(QRect(263, 120, 40, 40), true), 0);
        TEST_COMPARE(field.count(QRect(200, 151, 60, 20), true), 0);
    }

    TEST_FUNCTION(testBatchForms)
    {
        TEST_START;

        Field field;
        Outcome out;
        const Engine::RunId id = field.start(
            "gfx.rects([10, 10, 20, 20, 40, 10, 20, 20], {fill: 'red', stroke: 'none'});\n"
            "gfx.circles([20, 60, 8, 50, 60, 8], {fill: 'blue', stroke: 'none'});\n"
            "gfx.lines([10, 90, 60, 90, 10, 100, 60, 100], {stroke: 'green', width: 3});\n"
            "gfx.rects(new Float64Array([70, 10, 20, 20]), {fill: 'red', stroke: 'none'});\n"
            "const many = [];\n"
            "for (let i = 0; i < 1000; ++i)\n"
            "    many.push(100 + (i % 40) * 2, 40 + Math.floor(i / 40) * 2, 1, 1);\n"
            "gfx.rects(many, {fill: '#000000', stroke: 'none'});\n"
            "gfx.present();",
            out);
        TEST_ASSERT(field.shown(id));
        TEST_ASSERT(near(field.pixel(20, 20), Qt::red));
        TEST_ASSERT(near(field.pixel(50, 20), Qt::red));
        TEST_ASSERT(near(field.pixel(35, 20), white()));
        TEST_ASSERT(near(field.pixel(80, 20), Qt::red));            // typed array
        TEST_ASSERT(near(field.pixel(20, 60), Qt::blue));
        TEST_ASSERT(near(field.pixel(50, 60), Qt::blue));
        TEST_ASSERT(near(field.pixel(35, 60), white()));
        TEST_ASSERT(near(field.pixel(35, 90), QColor(0, 128, 0)));
        TEST_ASSERT(near(field.pixel(35, 100), QColor(0, 128, 0)));
        TEST_ASSERT(near(field.pixel(35, 95), white()));
        // 1000 one-pixel squares on every other pixel of a 80 x 50 block.
        TEST_COMPARE(field.count(QRect(100, 40, 80, 50)), 1000);
    }

    TEST_FUNCTION(testPresentKeepsTheLatestFrame)
    {
        TEST_START;

        Field field;
        Outcome out;
        const Engine::RunId id = field.start(
            "for (let i = 0; i < 50; ++i) {\n"
            "    gfx.clear();\n"
            "    gfx.rect(10 + i * 4, 10, 4, 20, {fill: 'red', stroke: 'none'});\n"
            "    gfx.present();\n"
            "}\n"
            "gfx.rect(10, 50, 30, 30, {fill: 'blue', stroke: 'none'});   // never presented",
            out);
        // The GUI thread busy meanwhile: the frames pile up, only the newest is taken.
        QThread::msleep(300);
        TEST_ASSERT(TestSupport::spin([&]() { return field.logs.value(id).contains("ready"); }));
        TEST_ASSERT(TestSupport::spin([&]() { return near(field.pixel(208, 20), Qt::red); }));
        TEST_ASSERT(field.presented.value(id).size() < 50);
        TEST_ASSERT(near(field.pixel(12, 20), white()));
        TEST_ASSERT(near(field.pixel(25, 65), white()));
        // It stays until the next present().
        TestSupport::spin([]() { return false; }, 200);
        TEST_ASSERT(near(field.pixel(208, 20), Qt::red));
        TEST_COMPARE(field.canvas->layers(), (QList<Engine::RunId>{id}));
    }

    TEST_FUNCTION(testLayerGoesWithTheRun)
    {
        TEST_START;

        Field field;
        const QString draw = "gfx.rect(20, 20, 40, 40, {fill: 'red', stroke: 'none'});\ngfx.present();";

        // Finished.
        Outcome finished;
        const Engine::RunId a = field.start(draw, finished);
        TEST_ASSERT(field.shown(a));
        TEST_ASSERT(near(field.pixel(40, 40), Qt::red));
        field.engine.postEvent("done", QJsonObject());
        TEST_ASSERT(TestSupport::spin([&]() { return finished.done; }));
        TEST_COMPARE(finished.result["status"].toString(), QString("ok"));
        TEST_ASSERT(field.canvas->layers().isEmpty());
        TEST_ASSERT(near(field.pixel(40, 40), white()));

        // Stopped.
        Outcome stopped;
        const Engine::RunId b = field.start(draw, stopped);
        TEST_ASSERT(field.shown(b));
        field.engine.stop(b);
        TEST_ASSERT(TestSupport::spin([&]() { return stopped.done; }));
        TEST_COMPARE(stopped.result["phase"].toString(), QString("cancelled"));
        TEST_ASSERT(field.canvas->layers().isEmpty());
        TEST_ASSERT(near(field.pixel(40, 40), white()));

        // Failed in a handler.
        Outcome failed;
        const Engine::RunId c = field.start(draw + "\non('boom', () => { throw new Error('boom'); });", failed);
        TEST_ASSERT(field.shown(c));
        field.engine.postEvent("boom", QJsonObject());
        TEST_ASSERT(TestSupport::spin([&]() { return failed.done; }));
        TEST_COMPARE(failed.result["phase"].toString(), QString("runtime"));
        TEST_ASSERT(field.canvas->layers().isEmpty());
        TEST_ASSERT(near(field.pixel(40, 40), white()));
    }

    TEST_FUNCTION(testTwoRunsLayers)
    {
        TEST_START;

        Field field;
        // The first run draws after the second: still under it.
        Outcome first;
        const Engine::RunId a = field.start(
            "on('go', () => { gfx.rect(20, 20, 60, 60, {fill: 'red', stroke: 'none'}); gfx.present(); });", first);
        Outcome second;
        const Engine::RunId b = field.start("gfx.rect(50, 50, 60, 60, {fill: 'blue', stroke: 'none'});\ngfx.present();",
                                            second);
        TEST_ASSERT(field.shown(b));
        TEST_ASSERT(TestSupport::spin([&]() { return field.logs.value(a).contains("ready"); }));
        field.engine.postEvent(a, "go", QJsonObject());
        TEST_ASSERT(field.shown(a));
        TEST_COMPARE(field.canvas->layers(), (QList<Engine::RunId>{a, b}));
        TEST_ASSERT(near(field.pixel(30, 30), Qt::red));
        TEST_ASSERT(near(field.pixel(65, 65), Qt::blue));
        TEST_ASSERT(near(field.pixel(100, 100), Qt::blue));

        field.engine.stop(b);
        TEST_ASSERT(TestSupport::spin([&]() { return second.done; }));
        TEST_COMPARE(field.canvas->layers(), (QList<Engine::RunId>{a}));
        TEST_ASSERT(near(field.pixel(65, 65), Qt::red));
        TEST_ASSERT(near(field.pixel(100, 100), white()));
    }

    TEST_FUNCTION(testDirtyRegion)
    {
        TEST_START;

        Field field;
        Outcome out;
        const Engine::RunId id = field.start(
            "gfx.rect(10, 10, 20, 20, {fill: 'red', stroke: 'none'});\n"
            "gfx.present();\n"
            "on('move', () => { gfx.rect(100, 50, 20, 20, {fill: 'red', stroke: 'none'}); gfx.present(); });\n"
            "on('thick', () => { gfx.rect(100, 50, 20, 20, {width: 6}); gfx.present(); });\n"
            "on('empty', () => gfx.present());",
            out);
        TEST_ASSERT(field.shown(id));
        TEST_ASSERT(TestSupport::spin([&]() { return field.logs.value(id).contains("ready"); }));
        const QRect first = field.presented.value(id).value(0);
        TEST_ASSERT(first.contains(QRect(10, 10, 20, 20)));
        TEST_ASSERT(QRect(8, 8, 24, 24).contains(first));

        // Moved: the old place and the new one.
        field.engine.postEvent("move", QJsonObject());
        TEST_ASSERT(TestSupport::spin([&]() { return field.presented.value(id).size() == 2; }));
        const QRect moved = field.presented.value(id).value(1);
        TEST_ASSERT(moved.contains(QRect(10, 10, 20, 20)) && moved.contains(QRect(100, 50, 20, 20)));
        TEST_ASSERT(QRect(8, 8, 114, 64).contains(moved));
        TEST_ASSERT(near(field.pixel(20, 20), white()));
        TEST_ASSERT(near(field.pixel(110, 60), Qt::red));

        // A wide outline: its outer half is in.
        field.engine.postEvent("thick", QJsonObject());
        TEST_ASSERT(TestSupport::spin([&]() { return field.presented.value(id).size() == 3; }));
        const QRect thick = field.presented.value(id).value(2);
        TEST_ASSERT(thick.contains(QRect(97, 47, 26, 26)));
        TEST_ASSERT(QRect(90, 40, 40, 40).contains(thick));

        // Nothing drawn: only the old frame's place.
        field.engine.postEvent("empty", QJsonObject());
        TEST_ASSERT(TestSupport::spin([&]() { return field.presented.value(id).size() == 4; }));
        TEST_COMPARE(field.presented.value(id).value(3), thick);
        TEST_ASSERT(near(field.pixel(100, 60), white()));
        TEST_COMPARE(field.canvas->layers(), (QList<Engine::RunId>{id}));
    }

    TEST_FUNCTION(testResize)
    {
        TEST_START;

        Field field;
        TEST_COMPARE(field.canvas->geometry(), QRect(0, 0, 240, 160));
        Outcome out;
        const Engine::RunId id = field.start(
            "log('size ' + gfx.width + 'x' + gfx.height);\n"
            "on('resize', e => log('resize ' + e.width + 'x' + e.height + ' ' + gfx.width + 'x' + gfx.height));",
            out);
        TEST_ASSERT(TestSupport::spin([&]() { return field.logs.value(id).contains("ready"); }));
        TEST_ASSERT(field.logs.value(id).contains("size 240x160"));

        field.target.resize(300, 200);
        TEST_ASSERT(TestSupport::spin([&]() { return field.canvas->geometry() == QRect(0, 0, 300, 200); }));
        TEST_ASSERT(TestSupport::spin([&]() { return field.logs.value(id).contains("resize 300x200 300x200"); }));
    }

    TEST_FUNCTION(testRequestedRuns)
    {
        TEST_START;

        Field field;
        const QString code = "const n = 1;\nreturn typeof gfx;";
        Options external;
        external.external = true;

        TEST_ASSERT(!field.canvas->isAllowedForRequestedRuns());
        const Engine::Check refused = field.engine.check(code, true);
        TEST_ASSERT(!refused.ok);
        TEST_COMPARE(refused.message, QString("the application does not let requested scripts use 'gfx'"));
        TEST_COMPARE(TestSupport::runScript(field.engine, code, external)["result"].toString(), QString("undefined"));
        TEST_COMPARE(TestSupport::runScript(field.engine, code)["result"].toString(), QString("object"));

        field.canvas->setAllowedForRequestedRuns(true);
        TEST_ASSERT(field.canvas->isAllowedForRequestedRuns());
        TEST_ASSERT(field.engine.isEventAllowedForRequestedRuns("resize"));
        TEST_ASSERT(field.engine.check(code, true).ok);
        const QJsonObject allowed = TestSupport::runScript(
            field.engine, "on('resize', () => {});\noff('resize');\nreturn typeof gfx;", external);
        TEST_COMPARE(allowed["result"].toString(), QString("object"));

        field.canvas->setAllowedForRequestedRuns(false);
        TEST_ASSERT(!field.engine.check(code, true).ok);
        TEST_ASSERT(!field.engine.isEventAllowedForRequestedRuns("resize"));
    }

    TEST_FUNCTION(testDetach)
    {
        TEST_START;

        Field field;
        TEST_ASSERT(field.engine.extensions().contains("gfx"));
        Outcome out;
        const Engine::RunId id = field.start(
            "on('draw', () => { gfx.rect(0, 0, 10, 10); gfx.present(); log('drew ' + gfx.width); off('draw'); });", out);
        TEST_ASSERT(TestSupport::spin([&]() { return field.logs.value(id).contains("ready"); }));

        // Deleted while a run has gfx: it draws into nothing.
        delete field.canvas;
        TEST_ASSERT(!field.engine.extensions().contains("gfx"));
        TEST_ASSERT(!field.engine.apiDefinition().contains("declare namespace gfx"));
        field.engine.postEvent("draw", QJsonObject());
        field.engine.postEvent("done", QJsonObject());
        TEST_ASSERT(TestSupport::spin([&]() { return out.done; }));
        TEST_COMPARE(out.result["status"].toString(), QString("ok"));
        TEST_ASSERT(field.logs.value(id).contains("drew 240"));
        TEST_COMPARE(TestSupport::runScript(field.engine, "return typeof gfx;")["result"].toString(),
                     QString("undefined"));

        // Goes with its target.
        auto* other = new QWidget;
        QPointer<ScriptHost::ScriptCanvas> canvas = ScriptHost::attachCanvas(&field.engine, other);
        TEST_ASSERT(canvas && canvas->target() == other);
        TEST_ASSERT(field.engine.extensions().contains("gfx"));
        delete other;
        TEST_ASSERT(canvas.isNull());
        TEST_ASSERT(!field.engine.extensions().contains("gfx"));

        // A second canvas takes gfx over; the first one's going leaves it.
        QWidget one;
        QWidget two;
        ScriptHost::ScriptCanvas* first = ScriptHost::attachCanvas(&field.engine, &one);
        ScriptHost::attachCanvas(&field.engine, &two);
        delete first;
        TEST_ASSERT(field.engine.extensions().contains("gfx"));
        TEST_ASSERT(ScriptHost::attachCanvas(nullptr, &one) == nullptr);
    }

    TEST_FUNCTION(testCheckAndDefinition)
    {
        TEST_START;

        Field field;
        TEST_ASSERT(field.engine.check("gfx.clear();\ngfx.rect(1, 2, 3, 4);\ngfx.present();\nreturn gfx.width;").ok);
        const Engine::Check typo = field.engine.check("gfx.rectt(1, 2, 3, 4);");
        TEST_ASSERT(!typo.ok);
        TEST_COMPARE(typo.didYouMean, QString("gfx.rect"));
        TEST_ASSERT(field.engine.apiDefinition().contains("declare namespace gfx"));
        TEST_ASSERT(field.engine.apiDefinition("gfx").contains("function rects(values: number[]"));
    }

    TEST_FUNCTION(testStaysOnTop)
    {
        TEST_START;

        Field field;
        auto* panel = new QWidget(&field.target);
        QPalette palette = panel->palette();
        palette.setColor(QPalette::Window, Qt::red);
        panel->setPalette(palette);
        panel->setAutoFillBackground(true);
        panel->setGeometry(0, 0, 100, 100);
        panel->show();
        TEST_ASSERT(TestSupport::spin([&]() { return field.target.children().last() == field.canvas; }));

        Outcome out;
        const Engine::RunId id = field.start("gfx.rect(20, 20, 40, 40, {fill: 'blue', stroke: 'none'});\ngfx.present();",
                                             out);
        TEST_ASSERT(field.shown(id));
        TEST_ASSERT(near(field.pixel(40, 40), Qt::blue));
        TEST_ASSERT(near(field.pixel(80, 80), Qt::red));
        TEST_ASSERT(field.canvas->testAttribute(Qt::WA_TransparentForMouseEvents));
        TEST_COMPARE(field.canvas->focusPolicy(), Qt::NoFocus);
    }

    TEST_FUNCTION(testArgumentErrors)
    {
        TEST_START;

        Field field;
        const QJsonObject result = TestSupport::runScript(field.engine,
            "const errors = [];\n"
            "function attempt(f) { try { f(); errors.push('none'); } catch (e) { errors.push(e.message); } }\n"
            "attempt(() => gfx.rect(1, 2, 'x', 4));\n"
            "attempt(() => gfx.rect(1, 2, 3));\n"
            "attempt(() => gfx.rect(0, 0, 1, 1, {fill: 'nocolour'}));\n"
            "attempt(() => gfx.rect(0, 0, 1, 1, {colour: 'red'}));\n"
            "attempt(() => gfx.line(0, 0, 1, 1, {dash: 'wavy'}));\n"
            "attempt(() => gfx.circle(0, 0, 1, {width: -1}));\n"
            "attempt(() => gfx.rects([1, 2, 3]));\n"
            "attempt(() => gfx.polygon([1, 2]));\n"
            "attempt(() => gfx.polyline(5));\n"
            "attempt(() => gfx.rect(0, 0, 1, 1, {fill: 'none', opacity: 0.5, font: {size: 9}}));\n"
            "return errors;");
        TEST_COMPARE(result["status"].toString(), QString("ok"));
        TEST_COMPARE(result["result"].toArray(), (QJsonArray{
            "gfx.rect: expects numbers",
            "gfx.rect: expects numbers",
            "gfx.rect: 'nocolour' is not a colour",
            "gfx.rect: unknown style 'colour'",
            "gfx.line: the style's dash is one of solid, dash, dot, dashDot",
            "gfx.circle: the style's width must be a number, 0 or more",
            "gfx.rects: the array's length must be a multiple of 4",
            "gfx.polygon: expects at least 2 points: [x0, y0, x1, y1, ...]",
            "gfx.polyline: expects an array of numbers",
            "none"}));

        // Not caught: the run fails with it.
        const QJsonObject uncaught = TestSupport::runScript(field.engine, "const a = 1;\ngfx.circle(0, 0, NaN);");
        TEST_COMPARE(uncaught["phase"].toString(), QString("runtime"));
        TEST_ASSERT(uncaught["message"].toString().contains("gfx.circle: expects numbers"));
    }
};

TEST_INSTANTIATE(TST_ScriptCanvas);
