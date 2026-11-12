#pragma once

// Positions inside widgets (FR-007): ui.items and ui.itemAt through the
// ItemGeometry providers, the visible part of widgets, and the tools of
// the later tasks - one section per task.

#include "UnitTest.h"
#include "TestSupport.h"
#include "../../../widgets/src/ItemGeometry.h"
#include <QDialog>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QJsonArray>
#include <QMap>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QTabWidget>

class TST_ScriptUiItems : public UnitTest::Test
{
    TEST_CLASS(TST_ScriptUiItems)
public:
    TST_ScriptUiItems()
        : Test("TST_ScriptUiItems")
    {
        // T-025
        ADD_TEST(TST_ScriptUiItems::testDispatchWithoutProvider);
        ADD_TEST(TST_ScriptUiItems::testClippedByScrollAreaAndHiddenTab);
        ADD_TEST(TST_ScriptUiItems::testToolsAreSwitchable);
        // T-026
        // T-027
        ADD_TEST(TST_ScriptUiItems::testMapRoundTripsBetweenAllSpaces);
        ADD_TEST(TST_ScriptUiItems::testWindowAndScreenInfo);
        ADD_TEST(TST_ScriptUiItems::testCoveredBySecondWindow);
        ADD_TEST(TST_ScriptUiItems::testGraphicsItemsScrollAndZoom);
        ADD_TEST(TST_ScriptUiItems::testGraphicsRefsStayStable);
        ADD_TEST(TST_ScriptUiItems::testItemAtOnGraphicsItem);
        ADD_TEST(TST_ScriptUiItems::testWindowToolsAreSwitchable);
    }

private:
    using Engine = ScriptHost::ScriptEngine;

    // window "window" (500x400, no layout):
    //   tabs (170, 220, 150x100): page1, page2
    //   scroll (330, 220, 100x60): a 300x300 widget with inside (0, 0, 50x20),
    //       half (60, 10, 80x20), deep (200, 200, 50x20)
    struct Window
    {
        QWidget      window;
        QTabWidget*  tabs = nullptr;
        QScrollArea* scroll = nullptr;
        QPushButton* inside = nullptr;
        QPushButton* half = nullptr;
        QPushButton* deep = nullptr;

        Window()
        {
            window.setObjectName("window");
            tabs = named(new QTabWidget(&window), "tabs");
            tabs->setGeometry(170, 220, 150, 100);
            tabs->addTab(named(new QWidget, "page1"), "1");
            tabs->addTab(named(new QWidget, "page2"), "2");

            scroll = named(new QScrollArea(&window), "scroll");
            scroll->setGeometry(330, 220, 100, 60);
            auto* inner = new QWidget;
            inner->resize(300, 300);
            inside = named(new QPushButton("inside", inner), "inside");
            inside->setGeometry(0, 0, 50, 20);
            half = named(new QPushButton("half", inner), "half");
            half->setGeometry(60, 10, 80, 20);
            deep = named(new QPushButton("deep", inner), "deep");
            deep->setGeometry(200, 200, 50, 20);
            scroll->setWidget(inner);

            window.resize(500, 400);
            window.show();
            TestSupport::spin([this]() { return window.isVisible() && inside->isVisible(); });
        }

        template <class T>
        static T* named(T* widget, const char* name)
        {
            widget->setObjectName(name);
            return widget;
        }

        QRect inWindow(QWidget* widget) { return QRect(widget->mapTo(&window, QPoint()), widget->size()); }
    };

    static QJsonObject rectJson(const QRect& rect)
    {
        return QJsonObject{{"x", rect.x()}, {"y", rect.y()}, {"width", rect.width()}, {"height", rect.height()}};
    }

    static QJsonObject run(Engine& engine, const QString& code)
    {
        return TestSupport::runScript(engine, code);
    }

    static QJsonObject object(Engine& engine, const QString& code)
    {
        return run(engine, code)["result"].toObject();
    }

    // ---- T-025 ---------------------------------------------------------------

    // A widget no provider takes: ui.items refuses it, ui.itemAt gives its
    // node alone, ItemGeometry resolves nothing.
    TEST_FUNCTION(testDispatchWithoutProvider)
    {
        TEST_START;

        Engine engine;
        Window w;
        ScriptHost::exposeWidgets(&engine, &w.window);

        TEST_ASSERT(run(engine, "return ui.items('inside');")["message"].toString()
                        .contains("a QPushButton has no items"));
        const QPoint center = w.inWindow(w.inside).center();
        const QJsonObject at = object(engine, QString("return ui.itemAt(%1, %2);").arg(center.x()).arg(center.y()));
        TEST_COMPARE(at["widget"].toObject()["name"].toString(), QString("inside"));
        TEST_ASSERT(!at.contains("item"));
        TEST_ASSERT(run(engine, "return ui.itemAt(-5, -5);")["result"].isNull());
        TEST_ASSERT(run(engine, "return ui.items('nothing');")["message"].toString().contains("no widget 'nothing'"));

        TEST_ASSERT(ScriptHost::ItemGeometry::providerFor(w.inside) == nullptr);
        TEST_ASSERT(!ScriptHost::ItemGeometry::itemRect(w.inside, QJsonObject{{"row", 0}}).has_value());
    }

    TEST_FUNCTION(testClippedByScrollAreaAndHiddenTab)
    {
        TEST_START;

        Engine engine;
        Window w;
        ScriptHost::exposeWidgets(&engine, &w.window);
        const QRect viewport = w.inWindow(w.scroll->viewport());

        const QJsonObject inside = object(engine, "return ui.info('inside');");
        TEST_ASSERT(!inside["clipped"].toBool());
        TEST_COMPARE(inside["visibleRect"].toObject(), inside["rect"].toObject());
        const QJsonObject half = object(engine, "return ui.info('half');");
        TEST_ASSERT(half["clipped"].toBool());
        TEST_COMPARE(half["visibleRect"].toObject(), rectJson(w.inWindow(w.half) & viewport));
        TEST_ASSERT(half["visibleRect"].toObject()["width"].toInt() < 80);
        const QJsonObject deep = object(engine, "return ui.info('deep');");
        TEST_ASSERT(deep["clipped"].toBool() && deep["visibleRect"].isNull());

        // The scroll position; scrolled to it, deep is all there.
        const QJsonObject scroll = object(engine, "return ui.info('scroll').scroll;");
        TEST_COMPARE(scroll["x"].toInt(), 0);
        TEST_COMPARE(scroll["maxY"].toInt(), w.scroll->verticalScrollBar()->maximum());
        TEST_ASSERT(scroll["maxX"].toInt() > 0);
        w.scroll->ensureWidgetVisible(w.deep, 0, 0);
        const QJsonObject shown = object(engine, "return ui.info('deep');");
        TEST_ASSERT(!shown["clipped"].toBool());
        TEST_COMPARE(shown["visibleRect"].toObject(), rectJson(w.inWindow(w.deep)));
        TEST_ASSERT(object(engine, "return ui.info('scroll').scroll;")["y"].toInt() > 0);
        TEST_ASSERT(!object(engine, "return ui.info('inside');").contains("scroll"));

        // The page of the tab not shown is clipped away, in ui.tree too.
        const QJsonObject page1 = object(engine, "return ui.info('page1');");
        TEST_ASSERT(!page1["clipped"].toBool() && page1["visibleRect"].isObject());
        const QJsonObject page2 = object(engine, "return ui.info('page2');");
        TEST_ASSERT(page2["clipped"].toBool() && page2["visibleRect"].isNull());
        const QJsonObject inTree = object(engine, "const find = (n) => n.name === 'page2' ? n : "
                                                  "(n.children || []).map(find).find(Boolean); "
                                                  "return find(ui.tree('tabs'));");
        TEST_ASSERT(inTree["clipped"].toBool() && inTree["visibleRect"].isNull());
        const QJsonObject tree = object(engine, "return ui.tree();");
        TEST_ASSERT(tree["visibleRect"].isObject() && !tree["clipped"].toBool());
    }

    TEST_FUNCTION(testToolsAreSwitchable)
    {
        TEST_START;

        Engine engine;
        Window w;
        ScriptHost::exposeWidgets(&engine, &w.window);
        int found = 0;
        for (const ScriptHost::ScriptMethod& m : engine.methods()) {
            if (m.name() != "ui.items" && m.name() != "ui.itemAt")
                continue;
            ++found;
            TEST_ASSERT(m.isReadOnly());
            TEST_COMPARE(m.group(), QString("UI"));
        }
        TEST_COMPARE(found, 2);
        engine.setMethodEnabled("ui.items", false);
        TEST_COMPARE(run(engine, "return ui.items('inside');")["status"].toString(), QString("error"));
        TEST_COMPARE(run(engine, "return ui.itemAt(20, 20);")["status"].toString(), QString("ok"));
        engine.setMethodEnabled("ui.items", true);
        engine.setMethodEnabled("ui.itemAt", false);
        TEST_COMPARE(run(engine, "return ui.itemAt(20, 20);")["status"].toString(), QString("error"));
        TEST_ASSERT(!engine.apiDefinition().contains("ui.itemAt - "));
        TEST_ASSERT(engine.apiDefinition().contains("ui.items - "));
    }

    // ---- T-026 ---------------------------------------------------------------

    // ---- T-027 ---------------------------------------------------------------

    // A dialog of the window - a window of its own that scripts see - with
    // the button "dialogButton" at (20, 30).
    static QDialog* openDialog(Window& w, const QPoint& at, bool modal = false)
    {
        auto* dialog = Window::named(new QDialog(&w.window), "dialog");
        dialog->setWindowTitle("Dialog");
        dialog->setModal(modal);
        Window::named(new QPushButton("in dialog", dialog), "dialogButton")->setGeometry(20, 30, 80, 24);
        dialog->setGeometry(QRect(at, QSize(200, 120)));
        dialog->show();
        TestSupport::spin([dialog]() { return dialog->isVisible(); });
        return dialog;
    }

    // view (10, 10, 150x150) on a 1000x1000 scene: rect (10, 10, 40x30, z 2,
    // tool tip, data 0, selectable), text at (20, 60), far (an ellipse at
    // 600, 600).
    struct Graphics
    {
        QGraphicsView*        view = nullptr;
        QGraphicsRectItem*    rect = nullptr;
        QGraphicsTextItem*    text = nullptr;
        QGraphicsEllipseItem* far = nullptr;

        explicit Graphics(Window& w)
        {
            view = Window::named(new QGraphicsView(&w.window), "view");
            view->setGeometry(10, 10, 150, 150);
            auto* scene = new QGraphicsScene(0, 0, 1000, 1000, view);
            view->setScene(scene);
            rect = scene->addRect(10, 10, 40, 30);
            rect->setToolTip("a rect");
            rect->setData(0, "first");
            rect->setFlag(QGraphicsItem::ItemIsSelectable);
            rect->setZValue(2);
            text = scene->addText("hello");
            text->setPos(20, 60);
            far = scene->addEllipse(600, 600, 50, 50);
            view->show();
            TestSupport::spin([this]() { return view->isVisible(); });
            view->horizontalScrollBar()->setValue(0);
            view->verticalScrollBar()->setValue(0);
        }

        // The item's bounding rect as the view shows it, in the window.
        QRect inWindow(Window& w, QGraphicsItem* item) const
        {
            const QRect r = view->mapFromScene(item->sceneBoundingRect()).boundingRect();
            return QRect(view->viewport()->mapTo(&w.window, r.topLeft()), r.size());
        }
    };

    // type -> item of a ui.items answer.
    static QMap<QString, QJsonObject> byType(const QJsonObject& items)
    {
        QMap<QString, QJsonObject> found;
        for (const QJsonValue& item : items["items"].toArray())
            found.insert(item["type"].toString(), item.toObject());
        return found;
    }

    TEST_FUNCTION(testMapRoundTripsBetweenAllSpaces)
    {
        TEST_START;

        Engine engine;
        Window w;
        ScriptHost::exposeWidgets(&engine, &w.window);
        QDialog* dialog = openDialog(w, w.window.frameGeometry().topRight() + QPoint(20, 0));
        QWidget* dialogButton = dialog->findChild<QWidget*>("dialogButton");

        // Every pair, a point and a rect, there and back.
        const QString roundTrips =
            "const spaces = ['widget', 'root', 'window', 'frame', 'screen'];"
            "const same = (a, b) => Object.keys(a).length === Object.keys(b).length"
            "    && Object.keys(b).every(k => a[k] === b[k]);"
            "const failed = [];"
            "for (const from of spaces) for (const to of spaces) for (const v of"
            "        [{x: 7, y: 9}, {x: 3, y: 4, width: 50, height: 20}]) {"
            "    const there = ui.map('%1', v, from, to);"
            "    if (!same(ui.map('%1', there, to, from), v) || (v.width && there.width !== v.width))"
            "        failed.push(from + '>' + to);"
            "}"
            "return failed;";
        TEST_COMPARE(run(engine, roundTrips.arg("inside"))["result"].toArray(), QJsonArray());
        TEST_COMPARE(run(engine, roundTrips.arg("dialogButton"))["result"].toArray(), QJsonArray());

        // Where they land.
        auto map = [&](const QString& args) {
            const QJsonObject p = object(engine, "return ui.map(" + args + ");");
            return QPoint(p["x"].toInt(), p["y"].toInt());
        };
        TEST_COMPARE(map("'inside', {x: 0, y: 0}, 'widget', 'screen'"), w.inside->mapToGlobal(QPoint()));
        TEST_COMPARE(map("'inside', {x: 0, y: 0}, 'widget', 'root'"), w.inWindow(w.inside).topLeft());
        TEST_COMPARE(map("'dialogButton', {x: 0, y: 0}, 'widget', 'window'"), QPoint(20, 30));
        TEST_COMPARE(map("'dialogButton', {x: 0, y: 0}, 'widget', 'root'"),
                     w.window.mapFromGlobal(dialogButton->mapToGlobal(QPoint())));
        TEST_COMPARE(map("'dialogButton', {x: 0, y: 0}, 'window', 'frame'"),
                     dialog->geometry().topLeft() - dialog->frameGeometry().topLeft());
        TEST_COMPARE(map("'dialogButton', {x: 0, y: 0}, 'frame', 'screen'"), dialog->frameGeometry().topLeft());
        TEST_COMPARE(map("null, {x: 1, y: 2}, 'root', 'window'"), QPoint(1, 2));
        TEST_COMPARE(map("null, {x: 0, y: 0}, 'window', 'screen'"), w.window.geometry().topLeft());
        TEST_ASSERT(run(engine, "return ui.map(null, {x: 0, y: 0}, 'widget', 'root');")["message"].toString()
                        .contains("needs a widget"));
        TEST_ASSERT(run(engine, "return ui.map('inside', {x: 0, y: 0, width: 5}, 'widget', 'root');")["message"]
                        .toString().contains("width and height"));
        TEST_COMPARE(run(engine, "return ui.map('inside', {x: 0, y: 0}, 'widget', 'moon');")["status"].toString(),
                     QString("error"));

        // Nodes in the dialog name their window; the root's do not.
        const int dialogId = object(engine, "return ui.info('dialog');")["id"].toInt();
        TEST_COMPARE(object(engine, "return ui.info('dialogButton');")["window"].toInt(), dialogId);
        TEST_COMPARE(object(engine, "return ui.info('dialog');")["window"].toInt(), dialogId);
        TEST_ASSERT(!object(engine, "return ui.info('inside');").contains("window"));
        TEST_ASSERT(!object(engine, "return ui.root();").contains("window"));
    }

    TEST_FUNCTION(testWindowAndScreenInfo)
    {
        TEST_START;

        Engine engine;
        Window w;
        w.window.setWindowTitle("Main");
        ScriptHost::ScriptUi* ui = ScriptHost::exposeWidgets(&engine, &w.window);

        const QJsonObject window = object(engine, "return ui.window();");
        TEST_COMPARE(window["id"].toInt(), object(engine, "return ui.root();")["id"].toInt());
        TEST_COMPARE(window["title"].toString(), QString("Main"));
        TEST_COMPARE(window["geometry"].toObject(), rectJson(w.window.geometry()));
        TEST_COMPARE(window["frameGeometry"].toObject(), rectJson(w.window.frameGeometry()));
        TEST_COMPARE(window["active"].toBool(), w.window.isActiveWindow());
        TEST_ASSERT(!window["minimized"].toBool() && !window["maximized"].toBool());
        TEST_ASSERT(!window["fullScreen"].toBool() && !window["modal"].toBool());
        TEST_COMPARE(object(engine, "return ui.window('inside');"), window);

        const QScreen* screen = w.window.screen();
        const QJsonObject s = window["screen"].toObject();
        TEST_COMPARE(s["name"].toString(), screen->name());
        TEST_COMPARE(s["geometry"].toObject(), rectJson(screen->geometry()));
        TEST_COMPARE(s["availableGeometry"].toObject(), rectJson(screen->availableGeometry()));
        TEST_COMPARE(s["devicePixelRatio"].toDouble(), screen->devicePixelRatio());
        TEST_COMPARE(s["logicalDpi"].toDouble(), screen->logicalDotsPerInch());

        // A modal dialog is a window of its own.
        QDialog* dialog = openDialog(w, w.window.frameGeometry().topRight() + QPoint(20, 0), true);
        const QJsonObject other = object(engine, "return ui.window('dialogButton');");
        TEST_COMPARE(other["id"].toInt(), object(engine, "return ui.info('dialog');")["id"].toInt());
        TEST_COMPARE(other["title"].toString(), QString("Dialog"));
        TEST_ASSERT(other["modal"].toBool());
        TEST_COMPARE(other["geometry"].toObject(), rectJson(dialog->geometry()));
        TEST_COMPARE(other["frameGeometry"].toObject(), rectJson(dialog->frameGeometry()));

        ui->setTextReadable(false);
        TEST_ASSERT(!object(engine, "return ui.window();").contains("title"));
    }

    TEST_FUNCTION(testCoveredBySecondWindow)
    {
        TEST_START;

        Engine engine;
        Window w;
        ScriptHost::exposeWidgets(&engine, &w.window);
        auto info = [&](const char* name) { return object(engine, QString("return ui.info('%1');").arg(name)); };
        const QPoint inside = w.inside->mapToGlobal(QPoint(5, 5));

        TEST_ASSERT(!info("inside")["covered"].toBool() && !info("inside").contains("coveredBy"));

        // A dialog of the root on top: covered, by the dialog.
        QDialog* dialog = openDialog(w, inside);
        const int dialogId = info("dialog")["id"].toInt();
        TEST_ASSERT(info("inside")["covered"].toBool());
        TEST_COMPARE(info("inside")["coveredBy"].toArray(), QJsonArray{dialogId});
        TEST_ASSERT(!info("tabs")["covered"].toBool());
        TEST_ASSERT(!info("deep")["covered"].toBool());    // nothing of it is shown
        TEST_ASSERT(!info("dialogButton")["covered"].toBool());
        dialog->move(w.window.frameGeometry().topRight() + QPoint(20, 0));
        TEST_ASSERT(!info("inside")["covered"].toBool());

        // A window of the application outside the root: covered, by no id.
        QWidget cover;
        cover.setWindowFlags(Qt::FramelessWindowHint);
        cover.setGeometry(QRect(inside, QSize(60, 40)));
        cover.show();
        cover.raise();
        TestSupport::spin([&]() { return cover.isVisible(); });
        TEST_ASSERT(info("inside")["covered"].toBool());
        TEST_COMPARE(info("inside")["coveredBy"].toArray(), QJsonArray());

        // The root's window raised over it: not covered.
        w.window.raise();
        TestSupport::spin([]() { return false; }, 100);
        TEST_ASSERT(!info("inside")["covered"].toBool());
    }

    TEST_FUNCTION(testGraphicsItemsScrollAndZoom)
    {
        TEST_START;

        Engine engine;
        Window w;
        Graphics g(w);
        ScriptHost::ScriptUi* ui = ScriptHost::exposeWidgets(&engine, &w.window);

        // In view: the rect and the text, not the far ellipse.
        QJsonObject answer = object(engine, "return ui.items('view');");
        TEST_COMPARE(answer["kind"].toString(), QString("graphics"));
        TEST_COMPARE(answer["scroll"].toObject()["y"].toInt(), 0);
        QMap<QString, QJsonObject> items = byType(answer);
        TEST_COMPARE(items.keys(), (QStringList{"rect", "text"}));
        const QJsonObject rect = items["rect"];
        TEST_COMPARE(rect["rect"].toObject(), rectJson(g.inWindow(w, g.rect)));
        TEST_COMPARE(rect["visibleRect"].toObject(), rect["rect"].toObject());
        const QRectF scene = g.rect->sceneBoundingRect();
        TEST_COMPARE(rect["sceneRect"].toObject()["x"].toDouble(), scene.x());
        TEST_COMPARE(rect["sceneRect"].toObject()["width"].toDouble(), scene.width());
        TEST_COMPARE(rect["z"].toDouble(), 2.0);
        TEST_COMPARE(rect["tooltip"].toString(), QString("a rect"));
        TEST_COMPARE(rect["data0"].toString(), QString("first"));
        TEST_ASSERT(rect["visible"].toBool() && rect["enabled"].toBool() && !rect["selected"].toBool());
        TEST_ASSERT(!rect.contains("className"));
        TEST_COMPARE(items["text"]["className"].toString(), QString("QGraphicsTextItem"));
        TEST_COMPARE(items["text"]["rect"].toObject(), rectJson(g.inWindow(w, g.text)));
        g.rect->setSelected(true);
        TEST_ASSERT(byType(object(engine, "return ui.items('view');"))["rect"]["selected"].toBool());

        // The ref resolves in the view's coordinates.
        const QJsonObject ref = rect["ref"].toObject();
        const QRect inView = g.view->mapFromScene(g.rect->sceneBoundingRect()).boundingRect()
                                 .translated(g.view->viewport()->pos());
        TEST_COMPARE(ScriptHost::ItemGeometry::itemRect(g.view, ref).value_or(QRect()), inView);

        // Scrolled to the ellipse: only it is in view; all lists every item.
        g.view->centerOn(g.far);
        answer = object(engine, "return ui.items('view');");
        TEST_COMPARE(byType(answer).keys(), QStringList{"ellipse"});
        TEST_ASSERT(answer["scroll"].toObject()["y"].toInt() > 0);
        const QJsonObject far = byType(answer)["ellipse"];
        TEST_COMPARE(far["rect"].toObject(), rectJson(g.inWindow(w, g.far)));
        TEST_COMPARE(far["visibleRect"].toObject(), far["rect"].toObject());
        items = byType(object(engine, "return ui.items('view', {all: true});"));
        TEST_COMPARE(items.size(), 3);
        TEST_COMPARE(items["rect"]["ref"].toObject(), ref);
        TEST_ASSERT(items["rect"]["visibleRect"].isNull());     // scrolled out
        TEST_COMPARE(items["rect"]["rect"].toObject(), rectJson(g.inWindow(w, g.rect)));

        // Zoomed: twice the size.
        g.view->scale(2, 2);
        g.view->centerOn(g.far);
        const QJsonObject zoomed = byType(object(engine, "return ui.items('view');"))["ellipse"];
        TEST_COMPARE(zoomed["rect"].toObject(), rectJson(g.inWindow(w, g.far)));
        TEST_ASSERT(zoomed["rect"].toObject()["width"].toInt() >= 2 * far["rect"].toObject()["width"].toInt() - 2);
        TEST_COMPARE(zoomed["ref"].toObject(), far["ref"].toObject());

        // At most maxItems, then truncated.
        ScriptHost::ItemGeometry::ListContext context;
        context.root = &w.window;
        context.options = QJsonObject{{"all", true}};
        context.maxItems = 2;
        const QJsonObject capped = ScriptHost::ItemGeometry::providerFor(g.view)->list(g.view, context);
        TEST_COMPARE(capped["items"].toArray().size(), 2);
        TEST_ASSERT(capped["truncated"].toBool());
        TEST_ASSERT(!object(engine, "return ui.items('view', {all: true});").contains("truncated"));

        // Text off: no tool tip, no string data.
        ui->setTextReadable(false);
        const QJsonObject hidden = byType(object(engine, "return ui.items('view', {all: true});"))["rect"];
        TEST_ASSERT(!hidden.contains("tooltip") && !hidden.contains("data0"));
    }

    TEST_FUNCTION(testGraphicsRefsStayStable)
    {
        TEST_START;

        Engine engine;
        Window w;
        Graphics g(w);
        ScriptHost::exposeWidgets(&engine, &w.window);
        auto refs = [&]() {
            QMap<QString, int> found;
            for (const QJsonObject& item : byType(object(engine, "return ui.items('view', {all: true});")))
                found.insert(item["type"].toString(), item["ref"].toObject()["index"].toInt());
            return found;
        };
        const QMap<QString, int> before = refs();
        TEST_COMPARE(before.size(), 3);
        TEST_COMPARE(QSet<int>(before.begin(), before.end()).size(), 3);

        // Another stacking order and a new item: the old refs stay.
        g.rect->setZValue(-1);
        g.view->scene()->addLine(0, 0, 100, 100);
        QMap<QString, int> after = refs();
        TEST_COMPARE(after.size(), 4);
        for (const QString& type : before.keys())
            TEST_COMPARE(after.value(type), before.value(type));
        TEST_ASSERT(!before.values().contains(after.value("line")));

        // A deleted item's ref names nothing, and is not given again.
        const QJsonObject ref{{"index", before.value("rect")}};
        TEST_ASSERT(ScriptHost::ItemGeometry::itemRect(g.view, ref).has_value());
        delete g.rect;
        TEST_ASSERT(!ScriptHost::ItemGeometry::itemRect(g.view, ref).has_value());
        g.view->scene()->addRect(10, 10, 40, 30);
        after = refs();
        TEST_COMPARE(after.size(), 4);
        TEST_ASSERT(after.value("rect") != before.value("rect"));
        TEST_COMPARE(after.value("text"), before.value("text"));

        // A hidden item resolves to nothing.
        g.text->hide();
        TEST_ASSERT(!ScriptHost::ItemGeometry::itemRect(g.view, QJsonObject{{"index", before.value("text")}})
                         .has_value());
    }

    TEST_FUNCTION(testItemAtOnGraphicsItem)
    {
        TEST_START;

        Engine engine;
        Window w;
        Graphics g(w);
        ScriptHost::exposeWidgets(&engine, &w.window);
        const QJsonObject listed = byType(object(engine, "return ui.items('view');"))["rect"];

        const QPoint center = g.inWindow(w, g.rect).center();
        const QJsonObject at = object(engine, QString("return ui.itemAt(%1, %2);").arg(center.x()).arg(center.y()));
        TEST_COMPARE(at["widget"].toObject()["name"].toString(), QString("view"));
        TEST_COMPARE(at["item"].toObject()["ref"].toObject(), listed["ref"].toObject());
        TEST_COMPARE(at["item"].toObject()["type"].toString(), QString("rect"));

        // An empty spot of the view: no item.
        const QPoint empty = g.view->viewport()->mapTo(&w.window, QPoint(110, 110));
        const QJsonObject none = object(engine, QString("return ui.itemAt(%1, %2);").arg(empty.x()).arg(empty.y()));
        TEST_ASSERT(none["widget"].isObject() && !none.contains("item"));
    }

    TEST_FUNCTION(testWindowToolsAreSwitchable)
    {
        TEST_START;

        Engine engine;
        Window w;
        ScriptHost::exposeWidgets(&engine, &w.window);
        int found = 0;
        for (const ScriptHost::ScriptMethod& m : engine.methods()) {
            if (m.name() != "ui.map" && m.name() != "ui.window")
                continue;
            ++found;
            TEST_ASSERT(m.isReadOnly());
            TEST_COMPARE(m.group(), QString("UI"));
        }
        TEST_COMPARE(found, 2);
        const QString map = "return ui.map(null, {x: 1, y: 1}, 'root', 'screen');";
        engine.setMethodEnabled("ui.map", false);
        TEST_COMPARE(run(engine, map)["status"].toString(), QString("error"));
        TEST_COMPARE(run(engine, "return ui.window();")["status"].toString(), QString("ok"));
        TEST_ASSERT(!engine.apiDefinition().contains("ui.map - "));
        engine.setMethodEnabled("ui.map", true);
        engine.setMethodEnabled("ui.window", false);
        TEST_COMPARE(run(engine, "return ui.window();")["status"].toString(), QString("error"));
        TEST_COMPARE(run(engine, map)["status"].toString(), QString("ok"));
        TEST_ASSERT(!engine.apiDefinition().contains("ui.window - "));
        TEST_ASSERT(engine.apiDefinition().contains("ui.map - "));
    }
};

TEST_INSTANTIATE(TST_ScriptUiItems);
