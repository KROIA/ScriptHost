#pragma once

// attachCanvas() and `gfx`: each primitive and the batch forms reach the
// screen (checked on target->grab()), present() shows the newest frame and
// keeps it, a run's layer goes with the run, runs paint in start order, only
// the frame's area is repainted, resizes reach the scripts, requested runs
// need the opt-in, the canvas stays on top and detaches when deleted, and
// wrong arguments throw. The extras: transforms, paths, gradients, images,
// shapes anchored to widgets, the frame loop, the helpers and the
// application's draw functions. Item anchors (T-028): cells and graphics
// items, followed while the view scrolls or zooms, clipped, hidden.

#include "UnitTest.h"
#include "TestSupport.h"
#include "../../../widgets/src/ItemGeometry.h"
#include <QElapsedTimer>
#include <QFontMetricsF>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QHeaderView>
#include <QImage>
#include <QMap>
#include <QPainter>
#include <QPointer>
#include <QScrollBar>
#include <QTableWidget>
#include <QThread>
#include <QTimer>
#include <QWidget>
#include <cstdio>

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
        ADD_TEST(TST_ScriptCanvas::testTransforms);
        ADD_TEST(TST_ScriptCanvas::testPaths);
        ADD_TEST(TST_ScriptCanvas::testGradients);
        ADD_TEST(TST_ScriptCanvas::testImagesAndSprites);
        ADD_TEST(TST_ScriptCanvas::testAnchoredShapes);
        ADD_TEST(TST_ScriptCanvas::testWidgetRect);
        ADD_TEST(TST_ScriptCanvas::testFrameLoop);
        ADD_TEST(TST_ScriptCanvas::testFrameLoopCoalesces);
        ADD_TEST(TST_ScriptCanvas::testStopInsideAFrame);
        ADD_TEST(TST_ScriptCanvas::testHelpers);
        ADD_TEST(TST_ScriptCanvas::testDrawFunctions);
        ADD_TEST(TST_ScriptCanvas::testMemberSwitches);
        ADD_TEST(TST_ScriptCanvas::testBenchmark);
        // T-028
        ADD_TEST(TST_ScriptCanvas::testItemAnchorIsTheCell);
        ADD_TEST(TST_ScriptCanvas::testItemAnchorFollowsScrolling);
        ADD_TEST(TST_ScriptCanvas::testItemAnchorOutOfViewAndClipped);
        ADD_TEST(TST_ScriptCanvas::testItemAnchorFollowsZoom);
        ADD_TEST(TST_ScriptCanvas::testItemAnchorHidden);
        ADD_TEST(TST_ScriptCanvas::testItemAnchorNoRepaintLoop);
        ADD_TEST(TST_ScriptCanvas::testItemAnchorErrors);
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
        TEST_ASSERT(!field.engine.apiDefinition("gfx").contains("declare namespace gfx"));
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
        // Over the default budget: the index names it; asked for, it comes whole.
        TEST_ASSERT(field.engine.apiDefinition().contains("gfx - an object of the application"));
        TEST_ASSERT(field.engine.apiDefinition("gfx").contains("function rects(values: number[]"));
        field.engine.setApiBudgetChars(0);
        TEST_ASSERT(field.engine.apiDefinition().contains("declare namespace gfx"));
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

    TEST_FUNCTION(testTransforms)
    {
        TEST_START;

        Field field;
        Outcome out;
        const Engine::RunId id = field.start(
            "gfx.save();\n"
            "gfx.translate(100, 50);\n"
            "gfx.rotate(Math.PI / 2);\n"
            "gfx.rect(0, 0, 40, 10, {fill: 'red', stroke: 'none'});\n"        // at (90, 50, 10, 40)
            "gfx.restore();\n"
            "gfx.save();\n"
            "gfx.scale(2);\n"
            "gfx.rect(10, 10, 10, 10, {fill: 'blue', stroke: 'none'});\n"     // at (20, 20, 20, 20)
            "gfx.restore();\n"
            "gfx.rect(200, 10, 10, 10, {fill: '#00ff00', stroke: 'none'});\n"
            "gfx.translate(0, 100);\n"
            "gfx.resetTransform();\n"
            "gfx.rect(150, 10, 10, 10, {fill: 'blue', stroke: 'none'});\n"
            "gfx.present();",
            out);
        TEST_ASSERT(field.shown(id));
        TEST_ASSERT(near(field.pixel(95, 70), Qt::red));
        TEST_ASSERT(near(field.pixel(95, 88), Qt::red));
        TEST_ASSERT(near(field.pixel(120, 55), white()));
        TEST_ASSERT(near(field.pixel(105, 52), white()));
        TEST_ASSERT(near(field.pixel(35, 35), Qt::blue));
        TEST_ASSERT(near(field.pixel(17, 17), white()));
        TEST_ASSERT(near(field.pixel(205, 15), Qt::green));               // restored: as it was
        TEST_ASSERT(near(field.pixel(215, 15), white()));
        TEST_ASSERT(near(field.pixel(155, 15), Qt::blue));                // reset
        // The transformed bounds are repainted, not more.
        const QRect dirty = field.presented.value(id).value(0);
        TEST_ASSERT(dirty.contains(QRect(90, 50, 10, 40)) && dirty.contains(QRect(20, 20, 20, 20)));
        TEST_ASSERT(QRect(10, 0, 210, 100).contains(dirty));
    }

    TEST_FUNCTION(testPaths)
    {
        TEST_START;

        Field field;
        Outcome out;
        const Engine::RunId id = field.start(
            "gfx.path().moveTo(10, 10).lineTo(60, 10).lineTo(60, 60).close().draw({fill: 'red', stroke: 'none'});\n"
            "gfx.path().moveTo(10, 100).quadTo(50, 60, 90, 100).draw({width: 3});\n"
            "gfx.path().moveTo(110, 100).cubicTo(110, 70, 170, 70, 170, 100).draw({width: 3});\n"
            "gfx.path().arc(200, 60, 20, 0, Math.PI).close().draw({fill: '#00ff00', stroke: 'none'});\n"
            "gfx.path().arc(40, 130, 15, 0, 2 * Math.PI).draw({fill: 'blue', stroke: 'none'});\n"
            "gfx.present();",
            out);
        TEST_ASSERT(field.shown(id));
        TEST_ASSERT(near(field.pixel(50, 20), Qt::red));                  // filled triangle
        TEST_ASSERT(near(field.pixel(20, 50), white()));
        TEST_ASSERT(near(field.pixel(50, 80), Qt::black));                // quad, its middle
        TEST_ASSERT(near(field.pixel(50, 95), white()));                  // not filled
        TEST_ASSERT(near(field.pixel(140, 77), Qt::black));               // cubic, its middle
        TEST_ASSERT(near(field.pixel(140, 90), white()));
        TEST_ASSERT(near(field.pixel(200, 70), Qt::green));               // clockwise: the lower half
        TEST_ASSERT(near(field.pixel(200, 50), white()));
        TEST_ASSERT(near(field.pixel(40, 120), Qt::blue));                // full circle
        TEST_ASSERT(near(field.pixel(40, 140), Qt::blue));
        TEST_ASSERT(near(field.pixel(40, 148), white()));

        const QJsonObject result = TestSupport::runScript(field.engine,
            "const errors = [];\n"
            "function attempt(f) { try { f(); errors.push('none'); } catch (e) { errors.push(e.message); } }\n"
            "attempt(() => gfx.path().moveTo('a', 1).draw());\n"
            "attempt(() => gfx.path().arc(0, 0, -1, 0, 1).draw());\n"
            "attempt(() => gfx.path().draw());\n"
            "attempt(() => gfx.path().lineTo(1, 1).lineTo(5, 5).draw());\n"
            "return errors;");
        TEST_COMPARE(result["result"].toArray(), (QJsonArray{
            "gfx.path: expects numbers",
            "gfx.path: an arc's radius must not be negative",
            "none",
            "none"}));
    }

    TEST_FUNCTION(testGradients)
    {
        TEST_START;

        Field field;
        Outcome out;
        const Engine::RunId id = field.start(
            "const across = gfx.linearGradient(10, 0, 110, 0, [[0, 'red'], [1, 'blue']]);\n"
            "gfx.rect(10, 120, 100, 20, {fill: across, stroke: 'none'});\n"
            "gfx.circle(180, 130, 20, {fill: gfx.radialGradient(180, 130, 20, [[0, 'white'], [1, 'black']]),\n"
            "                          stroke: 'none'});\n"
            "gfx.style({fill: across});\n"
            "gfx.rect(10, 10, 100, 20, {stroke: 'none'});\n"
            "gfx.present();",
            out);
        TEST_ASSERT(field.shown(id));
        TEST_ASSERT(near(field.pixel(12, 130), Qt::red));
        TEST_ASSERT(near(field.pixel(107, 130), Qt::blue));
        TEST_ASSERT(near(field.pixel(60, 130), QColor(127, 0, 127)));
        TEST_ASSERT(field.pixel(180, 130).lightness() > 200);
        TEST_ASSERT(field.pixel(180, 148).lightness() < 80);
        TEST_ASSERT(near(field.pixel(12, 20), Qt::red));                  // through gfx.style()
        TEST_ASSERT(near(field.pixel(107, 20), Qt::blue));

        const QJsonObject result = TestSupport::runScript(field.engine,
            "const errors = [];\n"
            "function attempt(f) { try { f(); errors.push('none'); } catch (e) { errors.push(e.message); } }\n"
            "attempt(() => gfx.linearGradient(0, 0, 1, 1, 'red'));\n"
            "attempt(() => gfx.rect(0, 0, 1, 1, {fill: gfx.linearGradient(0, 0, 1, 1, [[2, 'red']])}));\n"
            "return errors;");
        TEST_COMPARE(result["result"].toArray(), (QJsonArray{
            "gfx.linearGradient: stops are [[offset, colour], ...]",
            "gfx.rect: a gradient stop's offset is 0..1"}));
    }

    TEST_FUNCTION(testImagesAndSprites)
    {
        TEST_START;

        Field field;
        QImage tiles(20, 10, QImage::Format_ARGB32);
        tiles.fill(Qt::red);
        {
            QPainter painter(&tiles);
            painter.fillRect(10, 0, 10, 10, Qt::blue);
        }
        field.canvas->addImage("tiles", tiles);
        TEST_COMPARE(field.canvas->images(), QStringList{"tiles"});

        Outcome out;
        const Engine::RunId id = field.start(
            "gfx.image('tiles', 10, 10);\n"
            "gfx.image('tiles', 10, 30, 40, 20);\n"
            "gfx.image('tiles', 10, 60, undefined, undefined, {sx: 10, sw: 10, sh: 10});\n"
            "gfx.present();\n"
            "log(JSON.stringify(gfx.imageSize('tiles')) + ' ' + JSON.stringify(gfx.imageSize('none')));\n"
            "try { gfx.image('none', 0, 0); } catch (e) { log(e.message); }",
            out);
        TEST_ASSERT(field.shown(id));
        TEST_ASSERT(TestSupport::spin([&]() { return field.logs.value(id).contains("ready"); }));
        TEST_ASSERT(field.logs.value(id).contains("{\"width\":20,\"height\":10} null"));
        TEST_ASSERT(field.logs.value(id).contains("gfx.image: no image 'none'"));
        TEST_ASSERT(near(field.pixel(14, 15), Qt::red));                  // its own size
        TEST_ASSERT(near(field.pixel(25, 15), Qt::blue));
        TEST_ASSERT(near(field.pixel(32, 15), white()));
        TEST_ASSERT(near(field.pixel(20, 40), Qt::red));                  // scaled
        TEST_ASSERT(near(field.pixel(45, 45), Qt::blue));
        TEST_ASSERT(near(field.pixel(15, 65), Qt::blue));                 // the sprite: its right half
        TEST_ASSERT(near(field.pixel(25, 65), white()));

        field.canvas->removeImage("tiles");
        TEST_ASSERT(field.canvas->images().isEmpty());
        TEST_ASSERT(near(field.pixel(14, 15), white()));
    }

    TEST_FUNCTION(testAnchoredShapes)
    {
        TEST_START;

        Field field;
        auto* container = new QWidget(&field.target);
        container->setGeometry(0, 0, 240, 160);
        auto* box = new QWidget(container);
        box->setObjectName("box");
        box->setGeometry(50, 50, 40, 20);
        container->show();

        Outcome out;
        const Engine::RunId id = field.start(
            "const box = gfx.widget('box');\n"
            "gfx.rect(box, {fill: 'red', stroke: 'none', padding: 4});\n"
            "gfx.arrow(10, 60, box, {stroke: 'blue', width: 2, padding: 4});\n"
            "gfx.rect(gfx.widget('nothing'), {fill: 'red'});\n"
            "gfx.present();",
            out);
        TEST_ASSERT(field.shown(id));
        TEST_ASSERT(near(field.pixel(48, 48), Qt::red));                  // padded
        TEST_ASSERT(near(field.pixel(92, 72), Qt::red));
        TEST_ASSERT(near(field.pixel(97, 60), white()));
        TEST_ASSERT(near(field.pixel(25, 60), Qt::blue));                 // the arrow ends at its edge

        // Follows the widget, without a present().
        box->move(120, 90);
        TEST_ASSERT(TestSupport::spin([&]() { return near(field.pixel(140, 100), Qt::red); }));
        TEST_ASSERT(near(field.pixel(70, 60), white()));
        TEST_ASSERT(near(field.pixel(170, 100), white()));
        // ... and its parent.
        container->move(10, 0);
        TEST_ASSERT(TestSupport::spin([&]() { return near(field.pixel(170, 100), Qt::red); }));
        TEST_COMPARE(field.presented.value(id).size(), 1);

        // Hidden or gone: not drawn.
        box->hide();
        TEST_ASSERT(TestSupport::spin([&]() { return near(field.pixel(150, 100), white()); }));
        TEST_COMPARE(field.count(QRect(0, 0, 240, 160), true), 0);
        TEST_ASSERT(near(field.pixel(25, 60), white()));                  // the arrow too
        box->show();
        TEST_ASSERT(TestSupport::spin([&]() { return near(field.pixel(150, 100), Qt::red); }));
        delete box;
        TEST_ASSERT(TestSupport::spin([&]() { return near(field.pixel(150, 100), white()); }));
        TEST_COMPARE(field.count(QRect(0, 0, 240, 160), true), 0);
    }

    TEST_FUNCTION(testWidgetRect)
    {
        TEST_START;

        Field field;
        auto* container = new QWidget(&field.target);
        container->setGeometry(10, 0, 200, 100);
        auto* box = new QWidget(container);
        box->setObjectName("box");
        box->setGeometry(50, 50, 40, 20);
        container->show();

        Outcome out;
        const Engine::RunId id = field.start(
            "on('ask', () => log(JSON.stringify(gfx.widgetRect('box')) + ' '\n"
            "                    + JSON.stringify(gfx.widgetRect(gfx.widget('box'))) + ' '\n"
            "                    + JSON.stringify(gfx.widgetRect('nothing'))));",
            out);
        TEST_ASSERT(TestSupport::spin([&]() { return field.logs.value(id).contains("ready"); }));
        field.engine.postEvent("ask", QJsonObject());
        const QString at = "{\"x\":60,\"y\":50,\"width\":40,\"height\":20}";
        TEST_ASSERT(TestSupport::spin([&]() { return field.logs.value(id).contains(at + " " + at + " null"); }));

        box->hide();
        field.engine.postEvent("ask", QJsonObject());
        TEST_ASSERT(TestSupport::spin([&]() { return field.logs.value(id).contains("null null null"); }));
    }

    TEST_FUNCTION(testFrameLoop)
    {
        TEST_START;

        Field field;
        Outcome out;
        // No other handler: only the frame handler keeps it running.
        const Engine::RunId id = field.engine.run(
            "let frames = 0;\n"
            "gfx.onFrame(dt => {\n"
            "    if (typeof dt !== 'number' || !(dt >= 0)) log('bad dt ' + dt);\n"
            "    gfx.clear();\n"
            "    gfx.rect(10 + frames * 10, 10, 8, 8, {fill: 'red', stroke: 'none'});\n"
            "    gfx.present();\n"
            "    if (++frames === 5) { gfx.offFrame(); log('frames done'); }\n"
            "});\n"
            "log('started');",
            Options(), [&out](const QJsonObject& r) {
                out.result = r;
                out.done = true;
            });
        TEST_ASSERT(TestSupport::spin([&]() { return out.done; }));
        TEST_COMPARE(out.result["status"].toString(), QString("ok"));
        TEST_COMPARE(field.logs.value(id), (QStringList{"started", "frames done"}));
        TEST_ASSERT(!field.presented.value(id).isEmpty());
        // Nothing has a frame handler: the timer stops.
        auto* timer = field.canvas->findChild<QTimer*>();
        TEST_ASSERT(timer != nullptr);
        TEST_ASSERT(TestSupport::spin([&]() { return !timer->isActive(); }));

        // offFrame() from outside a frame; onFrame() wants a function.
        const QJsonObject result = TestSupport::runScript(field.engine,
            "gfx.onFrame(() => {});\n"
            "gfx.offFrame();\n"
            "try { gfx.onFrame(5); } catch (e) { return e.message; }");
        TEST_COMPARE(result["result"].toString(), QString("gfx.onFrame: expects a function"));
    }

    TEST_FUNCTION(testFrameLoopCoalesces)
    {
        TEST_START;

        Field field;
        Outcome out;
        // Four slow frames, then fast ones. Ticks missed meanwhile are not
        // queued: no burst of frames right after each other follows.
        const Engine::RunId id = field.engine.run(
            "let n = 0;\n"
            "const dts = [];\n"
            "gfx.onFrame(dt => {\n"
            "    ++n;\n"
            "    if (n > 1) dts.push(dt);\n"
            "    if (n <= 4) { const end = Date.now() + 60; while (Date.now() < end) {} }\n"
            "    if (n === 12) {\n"
            "        gfx.offFrame();\n"
            "        log('tiny ' + dts.filter(d => d < 1).length);\n"
            "    }\n"
            "});",
            Options(), [&out](const QJsonObject& r) {
                out.result = r;
                out.done = true;
            });
        TEST_ASSERT(TestSupport::spin([&]() { return out.done; }, 10000));
        TEST_COMPARE(out.result["status"].toString(), QString("ok"));
        const QStringList logs = field.logs.value(id);
        TEST_COMPARE(logs.size(), 1);
        TEST_ASSERT(logs.value(0).startsWith("tiny "));
        TEST_ASSERT(logs.value(0).mid(5).toInt() <= 3);
    }

    TEST_FUNCTION(testStopInsideAFrame)
    {
        TEST_START;

        // A stop while a frame runs, after enough frames that Qt compiled the
        // code that calls the handler: Qt 5.15 asserted ("hasException") when
        // that code had a try/catch (I-016).
        Field field;
        Outcome out;
        const Engine::RunId id = field.engine.run(
            "let n = 0;\n"
            "gfx.onFrame(dt => {\n"
            "    gfx.clear();\n"
            "    gfx.present();\n"
            "    if (++n === 20) { log('ready'); while (true) {} }\n"
            "});",
            Options(), [&out](const QJsonObject& r) {
                out.result = r;
                out.done = true;
            });
        TEST_ASSERT(TestSupport::spin([&]() { return field.logs.value(id).contains("ready"); }));
        TestSupport::spin([]() { return false; }, 50);
        field.engine.stop(id);
        TEST_ASSERT(TestSupport::spin([&]() { return out.done; }));
        TEST_COMPARE(out.result["phase"].toString(), QString("cancelled"));
    }

    TEST_FUNCTION(testHelpers)
    {
        TEST_START;

        Field field;
        const QJsonObject hits = TestSupport::runScript(field.engine,
            "return [gfx.pointInRect(5, 5, 0, 0, 10, 10), gfx.pointInRect(10, 5, 0, 0, 10, 10),\n"
            "        gfx.pointInCircle(3, 4, 0, 0, 5), gfx.pointInCircle(4, 4, 0, 0, 5),\n"
            "        gfx.rectsOverlap(0, 0, 10, 10, 9, 9, 5, 5), gfx.rectsOverlap(0, 0, 10, 10, 10, 0, 5, 5),\n"
            "        gfx.circlesOverlap(0, 0, 5, 8, 0, 3), gfx.circlesOverlap(0, 0, 5, 9, 0, 3)];");
        TEST_COMPARE(hits["result"].toArray(), (QJsonArray{true, false, true, false, true, false, true, false}));

        const QJsonObject eased = TestSupport::runScript(field.engine,
            "const t = gfx.tween(0, 100, 1000, 'inQuad');\n"
            "const a = t.step(500);\n"
            "const b = t.step(600);\n"
            "const done = t.done;\n"
            "t.reset();\n"
            "const own = gfx.tween(10, 20, 100, k => 0.5);\n"
            "return [gfx.ease.outQuad(0.5), gfx.ease.linear(0.3), a, b, done, t.value, t.done, own.step(10)];");
        TEST_COMPARE(eased["result"].toArray(), (QJsonArray{0.75, 0.3, 25, 100, true, 0, false, 15}));

        // Measured on the run's thread like on the GUI thread.
        const QJsonObject measured = TestSupport::runScript(field.engine,
            "return gfx.measureText('Hello', {size: 20, bold: true});");
        QFont font;
        font.setPixelSize(20);
        font.setBold(true);
        const QFontMetricsF metrics(font);
        const QJsonObject size = measured["result"].toObject();
        TEST_ASSERT(qAbs(size["width"].toDouble() - metrics.horizontalAdvance("Hello")) < 0.5);
        TEST_ASSERT(qAbs(size["height"].toDouble() - metrics.height()) < 0.5);
        TEST_ASSERT(qAbs(size["ascent"].toDouble() - metrics.ascent()) < 0.5);

        TEST_ASSERT(field.engine.check("gfx.ease.outQuad(0.5);\ngfx.tween(0, 1, 100).step(16);").ok);
        const Engine::Check typo = field.engine.check("gfx.ease.outQaud(0.5);");
        TEST_ASSERT(!typo.ok);
        TEST_COMPARE(typo.didYouMean, QString("gfx.ease.outQuad"));
        const QJsonObject wrong = TestSupport::runScript(field.engine,
            "try { gfx.tween(0, 1, 100, 'wobble'); } catch (e) { return e.message; }");
        TEST_COMPARE(wrong["result"].toString(), QString("gfx.tween: no easing 'wobble'"));
    }

    // gfx members switched off one by one: a function, an easing, the draw
    // namespace with a function added after it was switched off.
    TEST_FUNCTION(testMemberSwitches)
    {
        TEST_START;

        Field field;
        Engine& engine = field.engine;
        engine.setExtensionMemberEnabled("gfx", "text", false);
        engine.setExtensionMemberEnabled("gfx", "ease.outQuad", false);
        engine.setExtensionMemberEnabled("gfx", "draw", false);
        field.canvas->addDrawFunction("star", [](QPainter&, const QVariantList&) {},
                                      "function star(x: number, y: number): void;");

        Engine::Check check = engine.check("gfx.rect(1, 2, 3, 4);\ngfx.text(1, 2, 'hi');");
        TEST_ASSERT(!check.ok);
        TEST_COMPARE(check.line, 2);
        TEST_COMPARE(check.message, QString("'gfx.text' is disabled"));
        TEST_COMPARE(engine.check("gfx.draw.star(1, 2);").message, QString("'gfx.draw' is disabled"));
        TEST_COMPARE(engine.check("gfx.tween(0, 1, 100, gfx.ease.outQuad);").message,
                     QString("'gfx.ease.outQuad' is disabled"));
        TEST_ASSERT(engine.check("gfx.tween(0, 1, 100, gfx.ease.inQuad);\ngfx.present();").ok);

        const QString definition = engine.apiDefinition("gfx");
        TEST_ASSERT(!definition.contains("function text("));
        TEST_ASSERT(!definition.contains("One line of text"));
        TEST_ASSERT(!definition.contains("In a widget, placed by align"));
        TEST_ASSERT(!definition.contains("outQuad("));
        TEST_ASSERT(definition.contains("inQuad(t: number): number;"));
        TEST_ASSERT(definition.contains("inOutQuad(t: number): number;"));
        TEST_ASSERT(!definition.contains("namespace draw"));
        TEST_ASSERT(!definition.contains("function star("));
        TEST_ASSERT(definition.contains("function rect(x: number, y: number, w: number, h: number, style?: Style): void;"));
        TEST_ASSERT(definition.contains("function measureText("));
        TEST_ASSERT(definition.contains("interface Style {"));
        TEST_ASSERT(definition.contains("const width: number;"));
        TEST_ASSERT(definition.trimmed().endsWith("function present(): void;\n}"));

        const QJsonObject result = TestSupport::runScript(engine,
            "return [typeof gfx.text, typeof gfx.ease.outQuad, typeof gfx.ease.inQuad, typeof gfx.draw,\n"
            "        typeof gfx.rect, Object.isFrozen(gfx), gfx.width, gfx.tween(0, 10, 100, 'inQuad').step(50)];");
        TEST_COMPARE(result["status"].toString(), QString("ok"));
        const QJsonArray values = result["result"].toArray();
        TEST_COMPARE(values.at(0).toString(), QString("undefined"));
        TEST_COMPARE(values.at(1).toString(), QString("undefined"));
        TEST_COMPARE(values.at(2).toString(), QString("function"));
        TEST_COMPARE(values.at(3).toString(), QString("undefined"));
        TEST_COMPARE(values.at(4).toString(), QString("function"));
        TEST_ASSERT(values.at(5).toBool());
        TEST_COMPARE(values.at(6).toInt(), 240);
        TEST_COMPARE(values.at(7).toDouble(), 2.5);

        // Drawing still works with the rest; on again, the members are back.
        Outcome out;
        const Engine::RunId id = field.start("gfx.rect(10, 10, 20, 20, {fill: 'red', stroke: 'none'});\ngfx.present();", out);
        TEST_ASSERT(field.shown(id));
        TEST_ASSERT(near(field.pixel(20, 20), Qt::red));
        engine.setExtensionMemberEnabled("gfx", "text", true);
        engine.setExtensionMemberEnabled("gfx", "ease.outQuad", true);
        engine.setExtensionMemberEnabled("gfx", "draw", true);
        TEST_ASSERT(engine.check("gfx.text(1, 2, 'hi');\ngfx.draw.star(1, 2);\ngfx.ease.outQuad(0.5);").ok);
        TEST_ASSERT(engine.apiDefinition("gfx").contains("function star(x: number, y: number): void;"));
        TEST_COMPARE(TestSupport::runScript(engine, "return typeof gfx.text + typeof gfx.draw.star;")["result"].toString(),
                     QString("functionfunction"));
    }

    TEST_FUNCTION(testDrawFunctions)
    {
        TEST_START;

        Field field;
        int changes = 0;
        QObject::connect(&field.engine, &Engine::methodsChanged, [&changes]() { ++changes; });
        auto square = [](const QVariantList& a) {
            return QRectF(a.value(0).toDouble(), a.value(1).toDouble(), a.value(2).toDouble(), a.value(2).toDouble());
        };
        field.canvas->addDrawFunction(
            "square", [square](QPainter& painter, const QVariantList& args) { painter.drawRect(square(args)); },
            "function square(x: number, y: number, size: number): void;", square);
        TEST_ASSERT(changes >= 1);
        TEST_COMPARE(field.canvas->drawFunctions(), QStringList{"square"});
        TEST_ASSERT(field.engine.check("gfx.draw.square(1, 2, 3);").ok);
        const Engine::Check typo = field.engine.check("gfx.draw.sqare(1, 2, 3);");
        TEST_ASSERT(!typo.ok);
        TEST_COMPARE(typo.didYouMean, QString("gfx.draw.square"));
        TEST_ASSERT(field.engine.apiDefinition("gfx").contains("function square(x: number, y: number, size: number)"));

        // Painted with the run's style and transform; only its bounds repainted.
        Outcome out;
        const Engine::RunId id = field.start(
            "gfx.style({fill: 'red', stroke: 'none'});\n"
            "gfx.translate(10, 0);\n"
            "gfx.draw.square(20, 20, 10);\n"
            "gfx.present();",
            out);
        TEST_ASSERT(field.shown(id));
        TEST_ASSERT(near(field.pixel(35, 25), Qt::red));
        TEST_ASSERT(near(field.pixel(25, 25), white()));
        const QRect dirty = field.presented.value(id).value(0);
        TEST_ASSERT(dirty.contains(QRect(30, 20, 10, 10)));
        TEST_ASSERT(QRect(25, 15, 20, 20).contains(dirty));

        // Without bounds: all of the canvas. Without a declaration: any arguments.
        field.canvas->addDrawFunction("blob", [](QPainter&, const QVariantList&) {}, QString());
        TEST_ASSERT(field.engine.apiDefinition("gfx").contains("function blob(...args: any[]): void;"));
        Outcome blob;
        const Engine::RunId other = field.start("gfx.draw.blob(1, 'two', [3]);\ngfx.present();", blob);
        TEST_ASSERT(field.shown(other));
        TEST_ASSERT(field.presented.value(other).value(0).contains(field.canvas->rect()));

        // Not an identifier: refused.
        field.canvas->addDrawFunction("not a name", [](QPainter&, const QVariantList&) {}, QString());
        field.canvas->removeDrawFunction("square");
        TEST_COMPARE(field.canvas->drawFunctions(), QStringList{"blob"});
        TEST_ASSERT(!field.engine.check("gfx.draw.square(1, 2, 3);").ok);
        TEST_ASSERT(!field.engine.apiDefinition("gfx").contains("function square"));
    }

    // Prints what a frame of moving primitives costs: recording on the run's
    // thread, painting on the GUI thread (into an image, as the raster backing
    // store does). Asserts nothing about speed.
    TEST_FUNCTION(testBenchmark)
    {
        TEST_START;

        Field field(800, 600);
        field.canvas->addDrawFunction("dot", [](QPainter& painter, const QVariantList& a) {
            painter.drawRect(QRectF(a.value(0).toDouble(), a.value(1).toDouble(), 4, 4));
        }, "function dot(x: number, y: number): void;");
        const struct { const char* name; const char* draw; } forms[] = {
            {"single, style object", "gfx.rect(x, y, 4, 4, s);"},
            {"single, gfx.style() ", "gfx.rect(x, y, 4, 4);"},
            {"batch, gfx.rects()  ", "v.push(x, y, 4, 4);"},
            {"custom, no bounds   ", "gfx.draw.dot(x, y);"},
        };
        const int frames = 10;
        for (int count : {2000, 5000}) {
            for (const auto& form : forms) {
                Outcome out;
                const Engine::RunId id = field.start(QString(
                    "const s = {fill: '#3060c0', stroke: 'none'};\n"
                    "gfx.style(s);\n"
                    "const t0 = Date.now();\n"
                    "for (let f = 0; f < %1; ++f) {\n"
                    "    gfx.clear();\n"
                    "    const v = [];\n"
                    "    for (let i = 0; i < %2; ++i) {\n"
                    "        const x = (i * 7 + f * 3) % 790, y = (Math.floor(i / 100) * 11 + f) % 590;\n"
                    "        %3\n"
                    "    }\n"
                    "    if (v.length) gfx.rects(v);\n"
                    "    gfx.present();\n"
                    "}\n"
                    "log('record ' + (Date.now() - t0) / %1);").arg(frames).arg(count).arg(form.draw), out);
                TEST_ASSERT(TestSupport::spin([&]() { return field.logs.value(id).contains("ready"); }, 120000));
                TestSupport::spin([]() { return false; }, 50);      // the last frame taken
                QImage image(field.canvas->size(), QImage::Format_ARGB32_Premultiplied);
                image.fill(Qt::transparent);
                field.canvas->render(&image);                        // warm up
                QElapsedTimer clock;
                clock.start();
                for (int i = 0; i < frames; ++i) {
                    image.fill(Qt::transparent);
                    field.canvas->render(&image);
                }
                const double paint = double(clock.nsecsElapsed()) / 1e6 / frames;
                const QString record = field.logs.value(id).filter("record ").value(0).mid(7);
                std::printf("ScriptCanvas benchmark: %d primitives, %s: record %s ms/frame, paint %.2f ms/frame\n",
                      count, form.name, qPrintable(record), paint);
                std::fflush(stdout);
                field.engine.postEvent(id, "done", QJsonObject());
                TEST_ASSERT(TestSupport::spin([&]() { return out.done; }));
            }
        }
    }

    // ---- T-028: item anchors ------------------------------------------------

    // A canvas over a table: 30 rows of 24 px, 3 columns of 70 px, no row
    // header, scrolled by pixels.
    struct Table
    {
        Field         field{320, 240};
        QWidget*      container = nullptr;
        QTableWidget* table = nullptr;

        Table()
        {
            container = new QWidget(&field.target);
            container->setGeometry(0, 0, 320, 240);
            table = new QTableWidget(30, 3, container);
            table->setObjectName("table");
            table->setGeometry(10, 10, 260, 200);
            table->verticalHeader()->hide();
            table->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
            for (int row = 0; row < 30; ++row)
                table->setRowHeight(row, 24);
            for (int column = 0; column < 3; ++column)
                table->setColumnWidth(column, 70);
            container->show();
        }
        // The cell on the canvas.
        QRect cell(int row, int column) const
        {
            const QRect rect = table->visualRect(table->model()->index(row, column));
            return QRect(table->viewport()->mapTo(&field.target, rect.topLeft()), rect.size());
        }
        QRect viewport() const
        {
            return QRect(table->viewport()->mapTo(&field.target, QPoint()), table->viewport()->size());
        }
        bool red(const QPoint& at) { return near(field.pixel(at.x(), at.y()), Qt::red); }
        // Pure red pixels in the area: not the coloured fringes of the header's text.
        int reds(const QRect& area)
        {
            const QImage picture = field.image();
            const qreal ratio = picture.devicePixelRatio();
            int n = 0;
            for (int y = area.top(); y <= area.bottom(); ++y) {
                for (int x = area.left(); x <= area.right(); ++x) {
                    const QColor c = picture.pixelColor(qRound(x * ratio), qRound(y * ratio));
                    n += c.red() > 200 && c.green() < 60 && c.blue() < 60 ? 1 : 0;
                }
            }
            return n;
        }
    };

    static QString rectString(const QRect& r)
    {
        return QString("{\"x\":%1,\"y\":%2,\"width\":%3,\"height\":%4}").arg(r.x()).arg(r.y()).arg(r.width())
            .arg(r.height());
    }

    // Counts the paints of a widget.
    struct PaintCounter : QObject
    {
        int paints = 0;
        bool eventFilter(QObject* watched, QEvent* event) override
        {
            if (event->type() == QEvent::Paint)
                ++paints;
            return QObject::eventFilter(watched, event);
        }
    };

    TEST_FUNCTION(testItemAnchorIsTheCell)
    {
        TEST_START;

        Table t;
        // The id ui.* would give it; no ScriptUi here.
        t.table->setProperty("scripthost_id", 900001);
        Outcome out;
        const Engine::RunId id = t.field.start(
            "gfx.rect({widget: 'table', row: 2, column: 1}, {fill: 'red', stroke: 'none'});\n"
            "on('ask', () => log([gfx.widgetRect({widget: 'table', row: 2, column: 1}),\n"
            "                     gfx.widgetRect({widget: 900001, row: 2, column: 1}),\n"
            "                     gfx.widgetRect({widget: 900001}),\n"
            "                     gfx.widgetRect({widget: 'table', row: 99, column: 0})].map(JSON.stringify).join(' ')));\n"
            "gfx.present();",
            out);
        TEST_ASSERT(t.field.shown(id));
        const QRect cell = t.cell(2, 1);
        // Exactly the cell: every pixel of it red, none around it.
        TEST_COMPARE(t.field.count(cell, true), cell.width() * cell.height());
        TEST_COMPARE(t.field.count(cell.adjusted(-6, -6, 6, 6), true), cell.width() * cell.height());

        t.field.engine.postEvent("ask", QJsonObject());
        const QString expected = rectString(cell) + " " + rectString(cell) + " "
            + rectString(QRect(10, 10, 260, 200)) + " null";
        TEST_ASSERT(TestSupport::spin([&]() { return t.field.logs.value(id).contains(expected); }));
    }

    TEST_FUNCTION(testItemAnchorFollowsScrolling)
    {
        TEST_START;

        Table t;
        Outcome out;
        const Engine::RunId id = t.field.start(
            "gfx.rect({widget: 'table', row: 5, column: 1}, {fill: 'red', stroke: 'none'});\n"
            "gfx.present();",
            out);
        TEST_ASSERT(t.field.shown(id));
        const QRect before = t.cell(5, 1);
        TEST_ASSERT(t.red(before.center()));

        // Scrolled: it moves up with the cell.
        t.table->verticalScrollBar()->setValue(48);
        const QRect scrolled = t.cell(5, 1);
        TEST_COMPARE(scrolled.top(), before.top() - 48);
        TEST_ASSERT(TestSupport::spin([&]() { return t.red(scrolled.center()) && !t.red(before.center()); }));
        // A wider column before it: it moves right.
        t.table->setColumnWidth(0, 100);
        const QRect wider = t.cell(5, 1);
        TEST_COMPARE(wider.left(), scrolled.left() + 30);
        TEST_ASSERT(TestSupport::spin([&]() {
            return t.red(QPoint(wider.right() - 2, wider.center().y()))
                   && !t.red(QPoint(scrolled.left() + 2, scrolled.center().y()));
        }));
        // All without another present().
        TEST_COMPARE(t.field.presented.value(id).size(), 1);
    }

    TEST_FUNCTION(testItemAnchorOutOfViewAndClipped)
    {
        TEST_START;

        Table t;
        Outcome out;
        const Engine::RunId id = t.field.start(
            "gfx.rect({widget: 'table', row: 5, column: 0}, {fill: 'red', stroke: 'none'});\n"
            "gfx.rect({widget: 'table', row: 5, column: 1, clip: false}, {fill: 'blue', stroke: 'none'});\n"
            "gfx.rect({widget: 'table', row: 20, column: 2}, {fill: 'red', stroke: 'none'});\n"
            "on('ask', () => log(JSON.stringify(gfx.widgetRect({widget: 'table', row: 20, column: 2}))));\n"
            "gfx.present();",
            out);
        TEST_ASSERT(t.field.shown(id));
        // Row 20 is out of view: not drawn, no rect.
        const QRect view = t.viewport();
        TEST_ASSERT(t.cell(20, 2).top() > view.bottom());
        TEST_COMPARE(t.reds(QRect(0, view.bottom() + 1, 320, 240 - view.bottom() - 1)), 0);
        t.field.engine.postEvent("ask", QJsonObject());
        TEST_ASSERT(TestSupport::spin([&]() { return t.field.logs.value(id).contains("null"); }));

        // Row 5 half under the header: the clipped shape stays out of the
        // header, the one with clip: false covers it.
        t.table->verticalScrollBar()->setValue(5 * 24 + 10);
        const QRect red = t.cell(5, 0);
        const QRect blue = t.cell(5, 1);
        TEST_COMPARE(red.top(), view.top() - 10);
        const int header = view.top() - 5;
        TEST_ASSERT(TestSupport::spin([&]() {
            return t.red(QPoint(red.left() + 5, view.top() + 5))
                   && near(t.field.pixel(blue.left() + 5, header), Qt::blue);
        }));
        TEST_ASSERT(!t.red(QPoint(red.left() + 5, header)));
        TEST_COMPARE(t.reds(QRect(0, 0, 320, view.top())), 0);
    }

    TEST_FUNCTION(testItemAnchorFollowsZoom)
    {
        TEST_START;
        namespace Geometry = ScriptHost::ItemGeometry;

        Field field(320, 240);
        auto* scene = new QGraphicsScene(0, 0, 600, 600, &field.target);
        auto* item = scene->addRect(280, 280, 40, 40, QPen(Qt::NoPen));
        auto* view = new QGraphicsView(scene, &field.target);
        view->setObjectName("view");
        view->setGeometry(10, 10, 260, 200);
        view->centerOn(300, 300);
        view->show();
        Geometry::ListContext context;
        context.root = &field.target;
        const QJsonObject listed = Geometry::providerFor(view)->list(view, context);
        const int index = listed["items"].toArray().first()["ref"].toObject()["index"].toInt();
        // Where the item is on the canvas now.
        const auto place = [&]() {
            const QRect r = view->mapFromScene(item->sceneBoundingRect()).boundingRect();
            return QRect(view->viewport()->mapTo(&field.target, r.topLeft()), r.size());
        };

        Outcome out;
        const Engine::RunId id = field.start(
            QString("gfx.rect({widget: 'view', index: %1}, {fill: 'red', stroke: 'none'});\n"
                    "gfx.present();").arg(index),
            out);
        TEST_ASSERT(field.shown(id));
        const QRect before = place();
        TEST_ASSERT(near(field.pixel(before.center().x(), before.center().y()), Qt::red));
        const int area = field.count(QRect(0, 0, 320, 240), true);
        TEST_ASSERT(area > 30 * 30);

        // Zoomed in twice: four times the red, where the item is now.
        view->scale(2, 2);
        const QRect after = place();
        TEST_ASSERT(after.width() > before.width() * 3 / 2);
        TEST_ASSERT(TestSupport::spin([&]() { return field.count(QRect(0, 0, 320, 240), true) > area * 3; }));
        TEST_ASSERT(near(field.pixel(after.left() + 2, after.top() + 2), Qt::red));
        TEST_COMPARE(field.presented.value(id).size(), 1);
    }

    TEST_FUNCTION(testItemAnchorHidden)
    {
        TEST_START;

        Table t;
        // A table in a window of its own, a child of the target.
        auto* window = new QWidget(&t.field.target, Qt::Window);
        window->setAttribute(Qt::WA_DontShowOnScreen);
        auto* other = new QTableWidget(5, 2, window);
        other->setObjectName("other");
        window->setGeometry(0, 0, 200, 150);
        window->show();
        Outcome out;
        const Engine::RunId id = t.field.start(
            "gfx.rect({widget: 'table', row: 1, column: 0}, {fill: 'red', stroke: 'none'});\n"
            "gfx.rect({widget: 'other', row: 0, column: 0}, {fill: 'red', stroke: 'none'});\n"
            "gfx.present();",
            out);
        TEST_ASSERT(t.field.shown(id));
        const QRect cell = t.cell(1, 0);
        const int shown = cell.width() * cell.height();
        TEST_COMPARE(t.reds(QRect(0, 0, 320, 240)), shown);      // not the other window's

        // An ancestor hidden: nothing.
        t.container->hide();
        TEST_ASSERT(TestSupport::spin([&]() { return t.reds(QRect(0, 0, 320, 240)) == 0; }));
        t.container->show();
        TEST_ASSERT(TestSupport::spin([&]() { return t.reds(QRect(0, 0, 320, 240)) == shown; }));
        // Hidden from scripts (ScriptUi::hideWidget): nothing, from its next paint on.
        t.container->setProperty("scripthost_hidden", true);
        t.table->viewport()->update();
        TEST_ASSERT(TestSupport::spin([&]() { return t.reds(QRect(0, 0, 320, 240)) == 0; }));
    }

    TEST_FUNCTION(testItemAnchorNoRepaintLoop)
    {
        TEST_START;

        Table t;
        PaintCounter counter;
        t.field.canvas->installEventFilter(&counter);
        Outcome out;
        const Engine::RunId id = t.field.start(
            "gfx.rect({widget: 'table', row: 2, column: 1}, {fill: 'red', stroke: 'none', opacity: 0.5});\n"
            "gfx.present();",
            out);
        TEST_ASSERT(t.field.shown(id));
        t.field.image();
        TestSupport::spin([]() { return false; }, 100);
        // The canvas's repaint repaints the table under it, whose paint
        // resolves the anchor again: the same place, no new repaint.
        const int settled = counter.paints;
        t.table->viewport()->update();
        t.field.canvas->update();
        TestSupport::spin([]() { return false; }, 500);
        const int idle = counter.paints - settled;
        std::printf("ScriptCanvas item anchor: %d canvas paints while idle\n", idle);
        std::fflush(stdout);
        TEST_ASSERT(idle <= 3);
    }

    TEST_FUNCTION(testItemAnchorErrors)
    {
        TEST_START;

        Field field;
        const QJsonObject result = TestSupport::runScript(field.engine,
            "try { gfx.rect({widget: 'table', rows: 1}); } catch (e) { return e.message; }");
        TEST_COMPARE(result["result"].toString(), QString("gfx.rect: an anchor has no 'rows'"));
    }
};

TEST_INSTANTIATE(TST_ScriptCanvas);
