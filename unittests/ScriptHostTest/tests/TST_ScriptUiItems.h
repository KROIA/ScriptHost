#pragma once

// Positions inside widgets (FR-007): ui.items and ui.itemAt through the
// ItemGeometry providers, the visible part of widgets, and the tools of
// the later tasks - one section per task.

#include "UnitTest.h"
#include "TestSupport.h"
#include "../../../widgets/src/ItemGeometry.h"
#include <QJsonArray>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
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
};

TEST_INSTANTIATE(TST_ScriptUiItems);
