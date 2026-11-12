#pragma once

// Positions inside widgets (FR-007): ui.items and ui.itemAt through the
// ItemGeometry providers, the visible part of widgets, and the tools of
// the later tasks - one section per task.

#include "UnitTest.h"
#include "TestSupport.h"
#include "../../../widgets/src/ItemGeometry.h"
#include <QComboBox>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSlider>
#include <QSpinBox>
#include <QTabBar>
#include <QTabWidget>
#include <QTextEdit>
#include <QToolBar>
#include <QToolBox>
#include <QToolTip>
#include <memory>

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
        ADD_TEST(TST_ScriptUiItems::testTabsAndToolBox);
        ADD_TEST(TST_ScriptUiItems::testMenus);
        ADD_TEST(TST_ScriptUiItems::testToolBarOverflow);
        ADD_TEST(TST_ScriptUiItems::testStyleParts);
        ADD_TEST(TST_ScriptUiItems::testTextGeometry);
        ADD_TEST(TST_ScriptUiItems::testPopups);
        ADD_TEST(TST_ScriptUiItems::testTextAndPopupToolsAreSwitchable);
        // T-027
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

    // window "parts" (600x420, no layout):
    //   tabs (10, 10, 250x100): "&One", "Two" (tool tip "second"), "Three"
    //       (disabled); toolbox (10, 300, 200x110): "Alpha", "Beta"
    //   menubar (270, 10, 320x24): "&File" (Open Ctrl+O, a separator,
    //       Checked, Recent >), "Edit" (a menu without a parent: Undo)
    //   toolbar (270, 50, 320x30): actions A..F
    //   slider (10, 130, 200x24) 0..100, spin (220, 130, 80x24), combo (310,
    //       130, 120x24) a, b, c, bar (570, 130, 16x150) 0..100
    //   line (10, 170, 200x24) "Hello world", password (220, 170, 120x24)
    //       "secret", label (350, 170, 200x24) "Label text"
    //   text (230, 300, 250x90) "First line\nSecond line"
    struct Parts
    {
        QWidget                window;
        QTabWidget*            tabs = nullptr;
        QToolBox*              toolbox = nullptr;
        QMenuBar*              menubar = nullptr;
        QMenu*                 file = nullptr;
        std::unique_ptr<QMenu> edit;
        QToolBar*              toolbar = nullptr;
        QSlider*               slider = nullptr;
        QSpinBox*              spin = nullptr;
        QComboBox*             combo = nullptr;
        QScrollBar*            bar = nullptr;
        QLineEdit*             line = nullptr;
        QLineEdit*             password = nullptr;
        QLabel*                label = nullptr;
        QTextEdit*             text = nullptr;

        Parts()
        {
            using W = Window;
            window.setObjectName("parts");
            tabs = W::named(new QTabWidget(&window), "tabs");
            tabs->setGeometry(10, 10, 250, 100);
            tabs->addTab(new QWidget, "&One");
            tabs->addTab(new QWidget, "Two");
            tabs->setTabToolTip(1, "second");
            tabs->addTab(new QWidget, "Three");
            tabs->setTabEnabled(2, false);
            toolbox = W::named(new QToolBox(&window), "toolbox");
            toolbox->setGeometry(10, 300, 200, 110);
            toolbox->addItem(new QWidget, "Alpha");
            toolbox->addItem(new QWidget, "Beta");

            menubar = W::named(new QMenuBar(&window), "menubar");
            menubar->setGeometry(270, 10, 320, 24);
            file = menubar->addMenu("&File");
            file->addAction("Open")->setShortcut(QKeySequence("Ctrl+O"));
            file->addSeparator();
            QAction* checked = file->addAction("Checked");
            checked->setCheckable(true);
            checked->setChecked(true);
            file->addMenu("Recent")->addAction("one.txt");
            edit = std::make_unique<QMenu>("Edit");
            edit->addAction("Undo");
            menubar->addMenu(edit.get());

            toolbar = W::named(new QToolBar(&window), "toolbar");
            toolbar->setGeometry(270, 50, 320, 30);
            for (const char* name : {"A", "B", "C", "D", "E", "F"})
                toolbar->addAction(name);

            slider = W::named(new QSlider(Qt::Horizontal, &window), "slider");
            slider->setGeometry(10, 130, 200, 24);
            slider->setRange(0, 100);
            spin = W::named(new QSpinBox(&window), "spin");
            spin->setGeometry(220, 130, 80, 24);
            combo = W::named(new QComboBox(&window), "combo");
            combo->setGeometry(310, 130, 120, 24);
            combo->addItems({"a", "b", "c"});
            bar = W::named(new QScrollBar(Qt::Vertical, &window), "bar");
            bar->setGeometry(570, 130, 16, 150);
            bar->setRange(0, 100);

            line = W::named(new QLineEdit("Hello world", &window), "line");
            line->setGeometry(10, 170, 200, 24);
            password = W::named(new QLineEdit("secret", &window), "password");
            password->setEchoMode(QLineEdit::Password);
            password->setGeometry(220, 170, 120, 24);
            label = W::named(new QLabel("Label text", &window), "label");
            label->setGeometry(350, 170, 200, 24);
            text = W::named(new QTextEdit(&window), "text");
            text->setGeometry(230, 300, 250, 90);
            text->setPlainText("First line\nSecond line");

            window.resize(600, 420);
            window.show();
            TestSupport::spin([this]() { return window.isVisible() && text->isVisible(); });
        }

        // `rect` in `widget` in the window's coordinates, also from another
        // window (a popup).
        QRect inWindow(const QWidget* widget, const QRect& rect)
        {
            return QRect(window.mapFromGlobal(widget->mapToGlobal(rect.topLeft())), rect.size());
        }
    };

    static QRect rectOf(const QJsonValue& rect)
    {
        const QJsonObject r = rect.toObject();
        return QRect(r["x"].toInt(), r["y"].toInt(), r["width"].toInt(), r["height"].toInt());
    }

    static QJsonArray items(Engine& engine, const QString& widget)
    {
        return object(engine, QString("return ui.items(%1);").arg(widget))["items"].toArray();
    }

    // The item of ui.items(widget) with ref {part}.
    static QJsonObject part(Engine& engine, const QString& widget, const QString& name)
    {
        for (const QJsonValue& item : items(engine, widget)) {
            if (item.toObject()["ref"].toObject()["part"].toString() == name)
                return item.toObject();
        }
        return QJsonObject();
    }

    static QJsonArray popups(Engine& engine) { return run(engine, "return ui.popups();")["result"].toArray(); }

    static int idOf(Engine& engine, const QString& name)
    {
        return object(engine, QString("return ui.info('%1');").arg(name))["id"].toInt();
    }

    TEST_FUNCTION(testTabsAndToolBox)
    {
        TEST_START;

        Engine engine;
        Parts p;
        ScriptHost::exposeWidgets(&engine, &p.window);
        QTabBar* bar = p.tabs->tabBar();

        const QJsonObject tabs = object(engine, "return ui.items('tabs');");
        TEST_COMPARE(tabs["kind"].toString(), QString("tabs"));
        const QJsonArray list = tabs["items"].toArray();
        TEST_COMPARE(list.size(), 3);
        for (int i = 0; i < 3; ++i) {
            const QJsonObject tab = list.at(i).toObject();
            TEST_COMPARE(tab["ref"].toObject(), (QJsonObject{{"index", i}}));
            TEST_COMPARE(tab["rect"].toObject(), rectJson(p.inWindow(bar, bar->tabRect(i))));
            TEST_COMPARE(tab["current"].toBool(), i == 0);
        }
        TEST_COMPARE(list.at(0).toObject()["text"].toString(), QString("One"));
        TEST_COMPARE(list.at(1).toObject()["tooltip"].toString(), QString("second"));
        TEST_ASSERT(list.at(0).toObject()["enabled"].toBool() && !list.at(2).toObject()["enabled"].toBool());

        // The current tab follows; the ref resolves in the tab widget's
        // coordinates, and ui.itemAt finds the tab.
        p.tabs->setCurrentIndex(1);
        TEST_ASSERT(items(engine, "'tabs'").at(1).toObject()["current"].toBool());
        const QRect tab1 = bar->tabRect(1);
        TEST_ASSERT(ScriptHost::ItemGeometry::itemRect(p.tabs, QJsonObject{{"index", 1}})
                    == std::optional<QRect>(tab1.translated(bar->pos())));
        TEST_ASSERT(!ScriptHost::ItemGeometry::itemRect(p.tabs, QJsonObject{{"index", 7}}).has_value());
        const QPoint center = p.inWindow(bar, tab1).center();
        const QJsonObject at = object(engine, QString("return ui.itemAt(%1, %2);").arg(center.x()).arg(center.y()));
        TEST_COMPARE(at["item"].toObject()["ref"].toObject(), (QJsonObject{{"index", 1}}));
        TEST_COMPARE(at["item"].toObject()["text"].toString(), QString("Two"));

        // The tool box's page buttons, one below the other.
        const QJsonArray pages = items(engine, "'toolbox'");
        TEST_COMPARE(pages.size(), 2);
        TEST_COMPARE(pages.at(1).toObject()["text"].toString(), QString("Beta"));
        TEST_ASSERT(pages.at(0).toObject()["current"].toBool() && !pages.at(1).toObject()["current"].toBool());
        const QRect alpha = rectOf(pages.at(0).toObject()["rect"]);
        const QRect beta = rectOf(pages.at(1).toObject()["rect"]);
        TEST_ASSERT(!alpha.isEmpty() && beta.top() > alpha.bottom());
        TEST_ASSERT(p.inWindow(p.toolbox, p.toolbox->rect()).contains(beta));
    }

    TEST_FUNCTION(testMenus)
    {
        TEST_START;

        Engine engine;
        Parts p;
        ScriptHost::exposeWidgets(&engine, &p.window);

        const QJsonObject menubar = object(engine, "return ui.items('menubar');");
        TEST_COMPARE(menubar["kind"].toString(), QString("menu"));
        const QJsonArray menus = menubar["items"].toArray();
        TEST_COMPARE(menus.size(), 2);
        TEST_COMPARE(menus.at(0).toObject()["text"].toString(), QString("File"));
        TEST_ASSERT(menus.at(0).toObject()["submenu"].toBool());
        TEST_COMPARE(menus.at(1).toObject()["rect"].toObject(),
                     rectJson(p.inWindow(p.menubar, p.menubar->actionGeometry(p.edit->menuAction()))));
        const int menubarId = idOf(engine, "menubar");

        // Opened: a popup of its own, listed with its opener, its actions
        // through ui.items.
        p.file->popup(p.window.mapToGlobal(QPoint(270, 40)));
        TEST_ASSERT(TestSupport::spin([&]() { return p.file->isVisible(); }));
        QJsonArray open = popups(engine);
        TEST_COMPARE(open.size(), 1);
        TEST_COMPARE(open.at(0).toObject()["kind"].toString(), QString("menu"));
        TEST_COMPARE(open.at(0).toObject()["opener"].toInt(), menubarId);
        const int fileId = open.at(0).toObject()["widget"].toObject()["id"].toInt();
        const QJsonArray actions = items(engine, QString::number(fileId));
        TEST_COMPARE(actions.size(), 4);
        const QJsonObject first = actions.at(0).toObject();
        TEST_COMPARE(first["text"].toString(), QString("Open"));
        TEST_COMPARE(first["shortcut"].toString(), QString("Ctrl+O"));
        TEST_COMPARE(first["rect"].toObject(),
                     rectJson(p.inWindow(p.file, p.file->actionGeometry(p.file->actions().at(0)))));
        TEST_ASSERT(actions.at(1).toObject()["separator"].toBool());
        TEST_ASSERT(actions.at(2).toObject()["checkable"].toBool() && actions.at(2).toObject()["checked"].toBool());
        TEST_ASSERT(actions.at(3).toObject()["submenu"].toBool() && !first["submenu"].toBool());
        p.file->hide();
        TEST_ASSERT(TestSupport::spin([&]() { return popups(engine).isEmpty(); }));

        // A menu without a parent belongs where its action sits; a script
        // finds it by its id.
        p.edit->popup(p.window.mapToGlobal(QPoint(330, 40)));
        TEST_ASSERT(TestSupport::spin([&]() { return p.edit->isVisible(); }));
        open = popups(engine);
        TEST_COMPARE(open.size(), 1);
        TEST_COMPARE(open.at(0).toObject()["opener"].toInt(), menubarId);
        const QString editId = QString::number(open.at(0).toObject()["widget"].toObject()["id"].toInt());
        TEST_COMPARE(items(engine, editId).at(0).toObject()["text"].toString(), QString("Undo"));
        p.edit->hide();
        TEST_ASSERT(TestSupport::spin([&]() { return !p.edit->isVisible(); }));
        TEST_COMPARE(run(engine, QString("return ui.items(%1);").arg(editId))["status"].toString(), QString("error"));

        // Hidden from scripts with the menubar.
        ScriptHost::ScriptUi::hideWidget(p.menubar);
        p.edit->popup(p.window.mapToGlobal(QPoint(330, 40)));
        TEST_ASSERT(TestSupport::spin([&]() { return p.edit->isVisible(); }));
        TEST_COMPARE(popups(engine).size(), 0);
        p.edit->hide();
        TEST_ASSERT(TestSupport::spin([&]() { return !p.edit->isVisible(); }));
    }

    TEST_FUNCTION(testToolBarOverflow)
    {
        TEST_START;

        Engine engine;
        Parts p;
        ScriptHost::exposeWidgets(&engine, &p.window);
        const QList<QAction*> actions = p.toolbar->actions();

        // A..F, then the extension button, hidden while all fit.
        QJsonArray list = items(engine, "'toolbar'");
        TEST_COMPARE(object(engine, "return ui.items('toolbar');")["kind"].toString(), QString("toolbar"));
        TEST_COMPARE(list.size(), 7);
        for (int i = 0; i < 6; ++i) {
            const QJsonObject action = list.at(i).toObject();
            TEST_ASSERT(!action["inOverflow"].toBool() && action["visible"].toBool());
            QWidget* button = p.toolbar->widgetForAction(actions.at(i));
            TEST_COMPARE(action["rect"].toObject(), rectJson(p.inWindow(button, button->rect())));
            TEST_ASSERT(action["widget"].isDouble());
        }
        TEST_COMPARE(object(engine, QString("return ui.info(%1);").arg(list.at(0).toObject()["widget"].toInt()))
                         ["class"].toString(), QString("QToolButton"));
        TEST_COMPARE(list.at(6).toObject()["ref"].toObject(), (QJsonObject{{"part", "extension"}}));
        TEST_ASSERT(!list.at(6).toObject()["visible"].toBool() && list.at(6).toObject()["rect"].isNull());

        // A hidden action is not in the overflow.
        actions.at(5)->setVisible(false);
        list = items(engine, "'toolbar'");
        TEST_ASSERT(!list.at(5).toObject()["visible"].toBool() && !list.at(5).toObject()["inOverflow"].toBool());
        TEST_ASSERT(list.at(5).toObject()["rect"].isNull());

        // Too narrow: the last ones go into the extension's menu.
        QWidget* extension = p.toolbar->findChild<QWidget*>("qt_toolbar_ext_button");
        p.toolbar->resize(90, 30);
        TEST_ASSERT(TestSupport::spin([&]() { return extension && extension->isVisible(); }));
        list = items(engine, "'toolbar'");
        TEST_ASSERT(!list.at(0).toObject()["inOverflow"].toBool() && list.at(0).toObject()["rect"].isObject());
        TEST_ASSERT(list.at(4).toObject()["inOverflow"].toBool() && list.at(4).toObject()["rect"].isNull());
        TEST_ASSERT(!list.at(5).toObject()["inOverflow"].toBool());
        TEST_ASSERT(list.at(6).toObject()["visible"].toBool());
        TEST_COMPARE(list.at(6).toObject()["rect"].toObject(), rectJson(p.inWindow(extension, extension->rect())));
    }

    TEST_FUNCTION(testStyleParts)
    {
        TEST_START;

        Engine engine;
        Parts p;
        ScriptHost::exposeWidgets(&engine, &p.window);
        TEST_COMPARE(object(engine, "return ui.items('slider');")["kind"].toString(), QString("parts"));

        // The slider's handle moves with the value; ItemGeometry agrees.
        const QRect handle0 = rectOf(part(engine, "'slider'", "handle")["rect"]);
        p.slider->setValue(100);
        const QJsonObject handle = part(engine, "'slider'", "handle");
        const QRect handle100 = rectOf(handle["rect"]);
        TEST_ASSERT(!handle0.isEmpty() && handle100.x() > handle0.x() + 50);
        TEST_COMPARE(handle["ref"].toObject(), (QJsonObject{{"part", "handle"}}));
        const std::optional<QRect> local = ScriptHost::ItemGeometry::itemRect(p.slider, QJsonObject{{"part", "handle"}});
        TEST_ASSERT(local.has_value() && p.inWindow(p.slider, *local) == handle100);
        TEST_ASSERT(rectOf(part(engine, "'slider'", "groove")["rect"]).intersects(handle100));
        // On the handle, ui.itemAt gives it rather than the groove under it.
        const QJsonObject at = object(engine, QString("return ui.itemAt(%1, %2);")
                                                  .arg(handle100.center().x()).arg(handle100.center().y()));
        TEST_COMPARE(at["widget"].toObject()["name"].toString(), QString("slider"));
        TEST_COMPARE(at["item"].toObject()["ref"].toObject(), (QJsonObject{{"part", "handle"}}));

        // Spin box arrows: up above down, both in the box; the edit field.
        const QRect spin = p.inWindow(p.spin, p.spin->rect());
        const QRect up = rectOf(part(engine, "'spin'", "up")["rect"]);
        const QRect down = rectOf(part(engine, "'spin'", "down")["rect"]);
        TEST_ASSERT(!up.isEmpty() && !down.isEmpty() && up.top() < down.top());
        TEST_ASSERT(spin.contains(up) && spin.contains(down));
        TEST_ASSERT(spin.contains(rectOf(part(engine, "'spin'", "edit")["rect"])));
        const QJsonObject onUp = object(engine, QString("return ui.itemAt(%1, %2);").arg(up.center().x()).arg(up.center().y()));
        TEST_COMPARE(onUp["item"].toObject()["ref"].toObject()["part"].toString(), QString("up"));

        // Combo box: the arrow right of the edit field.
        const QRect arrow = rectOf(part(engine, "'combo'", "arrow")["rect"]);
        TEST_ASSERT(!arrow.isEmpty() && arrow.left() >= rectOf(part(engine, "'combo'", "edit")["rect"]).left());

        // Scroll bar: the slider moves down with the value.
        const QRect slider0 = rectOf(part(engine, "'bar'", "slider")["rect"]);
        p.bar->setValue(100);
        const QRect slider100 = rectOf(part(engine, "'bar'", "slider")["rect"]);
        TEST_ASSERT(slider100.top() > slider0.top());
        for (const char* name : {"addLine", "subLine", "addPage", "subPage"})
            TEST_ASSERT(part(engine, "'bar'", name).contains("rect"));

        // A scroll area: the parts of its scroll bars, by the bar's name.
        Engine other;
        Window w;
        ScriptHost::exposeWidgets(&other, &w.window);
        QScrollBar* vertical = w.scroll->verticalScrollBar();
        const std::optional<QRect> thumb = ScriptHost::ItemGeometry::itemRect(vertical, QJsonObject{{"part", "slider"}});
        TEST_ASSERT(thumb.has_value());
        TEST_COMPARE(part(other, "'scroll'", "vertical.slider")["rect"].toObject(),
                     rectJson(QRect(vertical->mapTo(&w.window, thumb->topLeft()), thumb->size())));
        TEST_ASSERT(part(other, "'scroll'", "horizontal.addLine").contains("rect"));
    }

    TEST_FUNCTION(testTextGeometry)
    {
        TEST_START;

        Engine engine;
        Parts p;
        ScriptHost::ScriptUi* ui = ScriptHost::exposeWidgets(&engine, &p.window);

        // A line edit: the caret and a range, where Qt puts those positions.
        p.line->setCursorPosition(5);
        QJsonObject line = object(engine, "return ui.textGeometry('line', {from: 6, to: 11});");
        TEST_COMPARE(line["length"].toInt(), 11);
        const QJsonObject cursor = line["cursor"].toObject();
        TEST_COMPARE(cursor["index"].toInt(), 5);
        const QRect caret = rectOf(cursor["rect"]);
        const QRect lineRect = p.inWindow(p.line, p.line->rect());
        TEST_ASSERT(lineRect.contains(caret));
        TEST_COMPARE(p.line->cursorPositionAt(p.line->mapFromParent(caret.center())), 5);
        const QJsonObject range = line["range"].toObject();
        TEST_COMPARE(range["text"].toString(), QString("world"));
        TEST_COMPARE(range["rects"].toArray().size(), 1);
        const QRect world = rectOf(range["rects"].toArray().at(0));
        TEST_ASSERT(world.left() > caret.left() && world.width() > 10 && lineRect.contains(world));
        TEST_COMPARE(p.line->cursorPositionAt(p.line->mapFromParent(world.topLeft()) + QPoint(1, 2)), 6);
        TEST_COMPARE(p.line->cursorPositionAt(p.line->mapFromParent(world.topRight()) + QPoint(-1, 2)), 11);
        // Its selection; without one, from = to = the caret, no rects.
        TEST_COMPARE(line["selection"].toObject()["from"].toInt(), 5);
        TEST_COMPARE(line["selection"].toObject()["rects"].toArray().size(), 0);
        p.line->setSelection(0, 5);
        line = object(engine, "return ui.textGeometry('line');");
        TEST_ASSERT(!line.contains("range"));
        TEST_COMPARE(line["selection"].toObject()["text"].toString(), QString("Hello"));
        TEST_COMPARE(line["selection"].toObject()["to"].toInt(), 5);
        // The selection ends at the caret (now at 5).
        const QRect hello = rectOf(line["selection"].toObject()["rects"].toArray().at(0));
        TEST_ASSERT(qAbs(hello.right() + 1 - rectOf(line["cursor"].toObject()["rect"]).left()) <= 1);

        // A password field: rects, never text.
        const QJsonObject password = object(engine, "return ui.textGeometry('password', {from: 0, to: 3});");
        TEST_COMPARE(password["range"].toObject()["rects"].toArray().size(), 1);
        TEST_ASSERT(!password["range"].toObject().contains("text") && !password["selection"].toObject().contains("text"));

        // A text edit: a range over two paragraphs, a rect per line; the
        // caret where Qt draws it.
        const QJsonObject text = object(engine, "return ui.textGeometry('text', {from: 0, to: 17});");
        TEST_COMPARE(text["length"].toInt(), 22);
        TEST_COMPARE(text["range"].toObject()["text"].toString(), QString("First line\nSecond"));
        const QJsonArray lines = text["range"].toObject()["rects"].toArray();
        TEST_COMPARE(lines.size(), 2);
        TEST_ASSERT(rectOf(lines.at(1)).top() > rectOf(lines.at(0)).top());
        TEST_ASSERT(rectOf(lines.at(0)).width() > rectOf(lines.at(1)).width());
        const QRect qtCaret = p.inWindow(p.text->viewport(), p.text->cursorRect());
        const QRect textCaret = rectOf(text["cursor"].toObject()["rect"]);
        TEST_ASSERT(qAbs(textCaret.x() - qtCaret.x()) <= 1 && qAbs(textCaret.y() - qtCaret.y()) <= 1);
        TEST_COMPARE(text["cursor"].toObject()["index"].toInt(), p.text->textCursor().position());

        // A label: no caret; a range inside it.
        const QJsonObject label = object(engine, "return ui.textGeometry('label', {from: 0, to: 5});");
        TEST_ASSERT(label["cursor"].isNull());
        TEST_COMPARE(label["range"].toObject()["text"].toString(), QString("Label"));
        const QRect labelRange = rectOf(label["range"].toObject()["rects"].toArray().at(0));
        TEST_ASSERT(!labelRange.isEmpty() && p.inWindow(p.label, p.label->rect()).contains(labelRange));

        // An empty range is the caret there; no text when text is off; other
        // widgets refused.
        const QJsonObject empty = object(engine, "return ui.textGeometry('line', {from: 3, to: 3});");
        TEST_COMPARE(empty["range"].toObject()["rects"].toArray().size(), 1);
        ui->setTextReadable(false);
        const QJsonObject off = object(engine, "return ui.textGeometry('line', {from: 0, to: 5});");
        TEST_ASSERT(!off["range"].toObject().contains("text") && off["range"].toObject()["rects"].toArray().size() == 1);
        TEST_ASSERT(run(engine, "return ui.textGeometry('slider');")["message"].toString()
                        .contains("a QSlider has no text positions"));
        TEST_ASSERT(run(engine, "return ui.textGeometry('line', {from: 'a'});")["message"].toString()
                        .contains("must be of type integer"));
    }

    TEST_FUNCTION(testPopups)
    {
        TEST_START;

        Engine engine;
        Parts p;
        ScriptHost::exposeWidgets(&engine, &p.window);
        TEST_COMPARE(popups(engine).size(), 0);

        // The open combo box list, its list view one a script can ask for.
        p.combo->showPopup();
        TEST_ASSERT(TestSupport::spin([&]() { return p.combo->view()->isVisible(); }));
        QJsonArray open = popups(engine);
        TEST_COMPARE(open.size(), 1);
        const QJsonObject list = open.at(0).toObject();
        TEST_COMPARE(list["kind"].toString(), QString("combo"));
        TEST_COMPARE(list["combo"].toInt(), idOf(engine, "combo"));
        QWidget* container = p.combo->view()->window();
        TEST_COMPARE(list["widget"].toObject()["rect"].toObject(), rectJson(p.inWindow(container, container->rect())));
        const QJsonObject rows = run(engine, QString("return ui.items(%1);").arg(list["view"].toInt()));
        if (ScriptHost::ItemGeometry::providerFor(p.combo->view())) {
            // With the list provider of T-025.
            TEST_COMPARE(rows["result"].toObject()["items"].toArray().size(), 3);
        } else {
            TEST_ASSERT(rows["message"].toString().contains("has no items"));
        }
        p.combo->hidePopup();
        TEST_ASSERT(TestSupport::spin([&]() { return !p.combo->view()->isVisible(); }));

        // The tool tip over the root, with its text.
        QToolTip::showText(p.window.mapToGlobal(QPoint(40, 40)), "Tip text", &p.window);
        TEST_ASSERT(TestSupport::spin([]() { return QToolTip::isVisible(); }));
        open = popups(engine);
        TEST_COMPARE(open.size(), 1);
        TEST_COMPARE(open.at(0).toObject()["kind"].toString(), QString("tooltip"));
        TEST_COMPARE(open.at(0).toObject()["text"].toString(), QString("Tip text"));
        TEST_ASSERT(!rectOf(open.at(0).toObject()["rect"]).isEmpty());
        QToolTip::hideText();
        TEST_ASSERT(TestSupport::spin([]() { return !QToolTip::isVisible(); }));

        // Popups of another window are not the root's.
        QWidget other;
        auto* otherCombo = new QComboBox(&other);
        otherCombo->addItems({"x", "y"});
        auto* foreign = new QMenu(&other);
        foreign->addAction("Foreign");
        other.show();
        TEST_ASSERT(TestSupport::spin([&]() { return other.isVisible(); }));
        foreign->popup(other.mapToGlobal(QPoint(10, 10)));
        TEST_ASSERT(TestSupport::spin([&]() { return foreign->isVisible(); }));
        TEST_COMPARE(popups(engine).size(), 0);
        foreign->hide();
        otherCombo->showPopup();
        TEST_ASSERT(TestSupport::spin([&]() { return otherCombo->view()->isVisible(); }));
        TEST_COMPARE(popups(engine).size(), 0);
        otherCombo->hidePopup();
        TEST_ASSERT(TestSupport::spin([&]() { return !otherCombo->view()->isVisible(); }));
    }

    TEST_FUNCTION(testTextAndPopupToolsAreSwitchable)
    {
        TEST_START;

        Engine engine;
        Parts p;
        ScriptHost::exposeWidgets(&engine, &p.window);
        int found = 0;
        for (const ScriptHost::ScriptMethod& m : engine.methods()) {
            if (m.name() != "ui.textGeometry" && m.name() != "ui.popups")
                continue;
            ++found;
            TEST_ASSERT(m.isReadOnly());
            TEST_COMPARE(m.group(), QString("UI"));
        }
        TEST_COMPARE(found, 2);
        engine.setMethodEnabled("ui.textGeometry", false);
        TEST_COMPARE(run(engine, "return ui.textGeometry('line');")["status"].toString(), QString("error"));
        TEST_COMPARE(run(engine, "return ui.popups();")["status"].toString(), QString("ok"));
        engine.setMethodEnabled("ui.textGeometry", true);
        engine.setMethodEnabled("ui.popups", false);
        TEST_COMPARE(run(engine, "return ui.popups();")["status"].toString(), QString("error"));
        TEST_COMPARE(run(engine, "return ui.textGeometry('line');")["status"].toString(), QString("ok"));
        TEST_ASSERT(!engine.apiDefinition().contains("ui.popups - "));
        TEST_ASSERT(engine.apiDefinition().contains("ui.textGeometry - "));
    }

    // ---- T-027 ---------------------------------------------------------------
};

TEST_INSTANTIATE(TST_ScriptUiItems);
