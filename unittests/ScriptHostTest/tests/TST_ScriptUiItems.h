#pragma once

// Positions inside widgets (FR-007): ui.items and ui.itemAt through the
// ItemGeometry providers, the visible part of widgets, and the tools of
// the later tasks - one section per task.

#include "UnitTest.h"
#include "TestSupport.h"
#include "../../../widgets/src/ItemGeometry.h"
#include <QAbstractTableModel>
#include <QComboBox>
#include <QDialog>
#include <QElapsedTimer>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMap>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QSlider>
#include <QSpinBox>
#include <QTabBar>
#include <QTableView>
#include <QTableWidget>
#include <QTabWidget>
#include <QTextEdit>
#include <QToolBar>
#include <QToolBox>
#include <QToolTip>
#include <QTreeWidget>
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
        ADD_TEST(TST_ScriptUiItems::testTableCellsMatchVisualRects);
        ADD_TEST(TST_ScriptUiItems::testScrollingChangesWhatIsInView);
        ADD_TEST(TST_ScriptUiItems::testTreePathsAndCollapsedChildren);
        ADD_TEST(TST_ScriptUiItems::testListAndHeaderSections);
        ADD_TEST(TST_ScriptUiItems::testItemAt);
        ADD_TEST(TST_ScriptUiItems::testTextSwitchAndHiddenViews);
        ADD_TEST(TST_ScriptUiItems::testItemRectResolvesReferences);
        ADD_TEST(TST_ScriptUiItems::testBigTableAnswersFast);
        // T-026
        ADD_TEST(TST_ScriptUiItems::testTabsAndToolBox);
        ADD_TEST(TST_ScriptUiItems::testMenus);
        ADD_TEST(TST_ScriptUiItems::testToolBarOverflow);
        ADD_TEST(TST_ScriptUiItems::testStyleParts);
        ADD_TEST(TST_ScriptUiItems::testTextGeometry);
        ADD_TEST(TST_ScriptUiItems::testPopups);
        ADD_TEST(TST_ScriptUiItems::testTextAndPopupToolsAreSwitchable);
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

    // The item views: window "window" (500x400, no layout):
    //   table (10, 10, 300x200): 100 rows x 4 columns "r<row>c<column>",
    //       columns "A".."D"
    //   tree (320, 10, 170x200): "Name", "Size"; a (a0, a1) expanded,
    //       b (b0 (b00)) collapsed, c
    //   list (10, 220, 150x100): "item 0".."item 49"; item 1 checked,
    //       item 2 partly
    //   plain (170, 220, 60x20): a button
    struct Views
    {
        QWidget       window;
        QTableWidget* table = nullptr;
        QTreeWidget*  tree = nullptr;
        QListWidget*  list = nullptr;
        QPushButton*  plain = nullptr;

        Views()
        {
            window.setObjectName("window");
            table = named(new QTableWidget(100, 4, &window), "table");
            table->setGeometry(10, 10, 300, 200);
            table->setHorizontalHeaderLabels({"A", "B", "C", "D"});
            table->setVerticalScrollMode(QAbstractItemView::ScrollPerItem);
            for (int r = 0; r < 100; ++r) {
                for (int c = 0; c < 4; ++c)
                    table->setItem(r, c, new QTableWidgetItem(QString("r%1c%2").arg(r).arg(c)));
            }

            tree = named(new QTreeWidget(&window), "tree");
            tree->setGeometry(320, 10, 170, 200);
            tree->setHeaderLabels({"Name", "Size"});
            auto* a = new QTreeWidgetItem(tree, {"a", "1"});
            new QTreeWidgetItem(a, {"a0", "2"});
            new QTreeWidgetItem(a, {"a1", "3"});
            auto* b = new QTreeWidgetItem(tree, {"b", "4"});
            new QTreeWidgetItem(new QTreeWidgetItem(b, {"b0", "5"}), {"b00", "6"});
            new QTreeWidgetItem(tree, {"c", "7"});
            a->setExpanded(true);

            list = named(new QListWidget(&window), "list");
            list->setGeometry(10, 220, 150, 100);
            for (int i = 0; i < 50; ++i)
                list->addItem(QString("item %1").arg(i));
            list->item(1)->setCheckState(Qt::Checked);
            list->item(2)->setCheckState(Qt::PartiallyChecked);

            plain = named(new QPushButton("plain", &window), "plain");
            plain->setGeometry(170, 220, 60, 20);

            window.resize(500, 400);
            window.show();
            TestSupport::spin([this]() {
                return window.isVisible() && table->visualRect(table->model()->index(0, 0)).height() > 0
                       && tree->visualRect(tree->model()->index(0, 0)).height() > 0;
            });
        }

        template <class T>
        static T* named(T* widget, const char* name)
        {
            widget->setObjectName(name);
            return widget;
        }

        // An item's rect in the window, as Qt has it.
        QJsonObject cell(QAbstractItemView* view, const QModelIndex& index)
        {
            const QRect rect = view->visualRect(index);
            return rectJson(QRect(view->viewport()->mapTo(&window, rect.topLeft()), rect.size()));
        }

        QJsonObject section(QHeaderView* header, int section)
        {
            const QPoint at = header->viewport()->mapTo(&window, QPoint(header->sectionViewportPosition(section), 0));
            return rectJson(QRect(at, QSize(header->sectionSize(section), header->viewport()->height())));
        }

        QRect inWindow(QWidget* widget) { return QRect(widget->mapTo(&window, QPoint()), widget->size()); }

        // A tree item by its path (QTreeWidget::indexFromItem is protected in Qt 5).
        QModelIndex treeIndex(std::initializer_list<int> path, int column = 0)
        {
            QModelIndex index;
            for (int row : path)
                index = tree->model()->index(row, 0, index);
            return index.sibling(index.row(), column);
        }
    };

    static QJsonArray pair(int first, int last) { return QJsonArray{first, last}; }

    // The refs of items, as compact JSON.
    static QStringList refs(const QJsonArray& items)
    {
        QStringList list;
        for (const QJsonValue& item : items)
            list << QString::fromUtf8(QJsonDocument(item.toObject()["ref"].toObject()).toJson(QJsonDocument::Compact));
        return list;
    }

    TEST_FUNCTION(testTableCellsMatchVisualRects)
    {
        TEST_START;

        Engine engine;
        Views w;
        ScriptHost::exposeWidgets(&engine, &w.window);
        w.table->setCurrentCell(2, 1);

        const QJsonObject result = object(engine, "return ui.items('table');");
        TEST_COMPARE(result["kind"].toString(), QString("table"));
        const int last = w.table->rowAt(w.table->viewport()->height() - 1);
        TEST_ASSERT(last > 2);
        TEST_COMPARE(result["visibleRows"].toArray(), pair(0, last));
        TEST_COMPARE(result["visibleColumns"].toArray(), pair(0, w.table->columnAt(w.table->viewport()->width() - 1)));
        TEST_COMPARE(result["scroll"].toObject()["y"].toInt(), 0);
        TEST_COMPARE(result["scroll"].toObject()["maxY"].toInt(), w.table->verticalScrollBar()->maximum());
        TEST_ASSERT(!result.contains("truncated"));

        // Every cell in view, row by row, at the rect Qt draws it.
        const QJsonArray items = result["items"].toArray();
        const int columns = result["visibleColumns"].toArray()[1].toInt() + 1;
        TEST_COMPARE(items.size(), (last + 1) * columns);
        for (const QJsonValue& value : items) {
            const QJsonObject item = value.toObject();
            const int row = item["ref"].toObject()["row"].toInt();
            const int column = item["ref"].toObject()["column"].toInt();
            TEST_COMPARE(item["rect"].toObject(), w.cell(w.table, w.table->model()->index(row, column)));
            TEST_COMPARE(item["text"].toString(), QString("r%1c%2").arg(row).arg(column));
            TEST_COMPARE(item["current"].toBool(), row == 2 && column == 1);
            TEST_COMPARE(item["selected"].toBool(), row == 2 && column == 1);
            TEST_ASSERT(item["enabled"].toBool());
            TEST_ASSERT(!item.contains("checked") && !item.contains("expanded"));
        }
        TEST_COMPARE(items[0].toObject()["visibleRect"].toObject(), items[0].toObject()["rect"].toObject());

        // Both headers, with their sections.
        const QJsonArray headers = result["headers"].toArray();
        TEST_COMPARE(headers.size(), 2);
        TEST_COMPARE(headers[0].toObject()["orientation"].toString(), QString("horizontal"));
        TEST_COMPARE(headers[1].toObject()["orientation"].toString(), QString("vertical"));
        TEST_COMPARE(headers[0].toObject()["items"].toArray().size(), columns);
        TEST_COMPARE(headers[1].toObject()["items"].toArray().size(), last + 1);
        TEST_COMPARE(headers[0].toObject()["items"].toArray()[1].toObject()["text"].toString(), QString("B"));

        // A range, and all of them (100 x 4 is below the cap).
        const QJsonObject range = object(engine, "return ui.items('table', {rows: [90, 99], columns: [3, 3]});");
        TEST_COMPARE(refs(range["items"].toArray()).first(), QString("{\"column\":3,\"row\":90}"));
        TEST_COMPARE(range["items"].toArray().size(), 10);
        TEST_ASSERT(range["items"].toArray()[0].toObject()["rect"].isObject());
        TEST_ASSERT(range["items"].toArray()[0].toObject()["visibleRect"].isNull());
        TEST_COMPARE(range["visibleRows"].toArray(), pair(0, last));
        TEST_COMPARE(object(engine, "return ui.items('table', {all: true});")["items"].toArray().size(), 400);
        TEST_ASSERT(run(engine, "return ui.items('table', {rows: [1]});")["message"].toString()
                        .contains("rows must be [first, last]"));
        TEST_ASSERT(run(engine, "return ui.items('plain');")["message"].toString().contains("has no items"));
    }

    TEST_FUNCTION(testScrollingChangesWhatIsInView)
    {
        TEST_START;

        Engine engine;
        Views w;
        ScriptHost::exposeWidgets(&engine, &w.window);

        const QJsonObject before = object(engine, "return ui.items('table');");
        w.table->verticalScrollBar()->setValue(50);
        const QJsonObject after = object(engine, "return ui.items('table');");
        TEST_COMPARE(after["visibleRows"].toArray()[0].toInt(), 50);
        TEST_COMPARE(after["scroll"].toObject()["y"].toInt(), 50);
        TEST_COMPARE(after["items"].toArray()[0].toObject()["ref"].toObject()["row"].toInt(), 50);
        // Row 50 is where row 0 was.
        TEST_COMPARE(after["items"].toArray()[0].toObject()["rect"].toObject(),
                     before["items"].toArray()[0].toObject()["rect"].toObject());
        TEST_COMPARE(after["headers"].toArray()[1].toObject()["items"].toArray()[0].toObject()["ref"].toObject()["section"].toInt(), 50);

        // Row 0 is out of view now: a rect, nothing visible.
        const QJsonObject first = object(engine, "return ui.items('table', {rows: [0, 0], columns: [0, 0]}).items[0];");
        TEST_ASSERT(first["visibleRect"].isNull());
        TEST_COMPARE(first["rect"].toObject(), w.cell(w.table, w.table->model()->index(0, 0)));
        TEST_ASSERT(first["rect"].toObject()["y"].toInt() < 10);

        // The row at the bottom edge is cut by the viewport.
        const QJsonArray items = after["items"].toArray();
        const QJsonObject bottom = items[items.size() - 1].toObject();
        const QRect viewport(w.table->viewport()->mapTo(&w.window, QPoint()), w.table->viewport()->size());
        const QJsonObject r = bottom["rect"].toObject();
        const QRect rect(r["x"].toInt(), r["y"].toInt(), r["width"].toInt(), r["height"].toInt());
        TEST_COMPARE(bottom["visibleRect"].toObject(), rectJson(rect & viewport));
    }

    TEST_FUNCTION(testTreePathsAndCollapsedChildren)
    {
        TEST_START;

        Engine engine;
        Views w;
        ScriptHost::exposeWidgets(&engine, &w.window);

        const QJsonObject result = object(engine, "return ui.items('tree');");
        TEST_COMPARE(result["kind"].toString(), QString("tree"));
        TEST_COMPARE(result["visibleRows"].toArray(), pair(0, 2));
        const QJsonArray items = result["items"].toArray();
        QStringList paths;
        for (const QJsonValue& item : items) {
            const QJsonObject ref = item.toObject()["ref"].toObject();
            if (ref["column"].toInt() == 0)
                paths << QString::fromUtf8(QJsonDocument(ref["path"].toArray()).toJson(QJsonDocument::Compact));
        }
        TEST_COMPARE(paths, (QStringList{"[0]", "[0,0]", "[0,1]", "[1]", "[2]"}));
        TEST_COMPARE(items.size(), 10);
        const QJsonObject a = items[0].toObject();
        const QJsonObject size = items[1].toObject();
        const QJsonObject b = items[6].toObject();
        TEST_COMPARE(a["text"].toString(), QString("a"));
        TEST_COMPARE(size["text"].toString(), QString("1"));
        TEST_COMPARE(size["ref"].toObject()["column"].toInt(), 1);
        TEST_ASSERT(a["hasChildren"].toBool() && a["expanded"].toBool());
        TEST_ASSERT(b["hasChildren"].toBool() && !b["expanded"].toBool());
        TEST_ASSERT(!items[8].toObject()["hasChildren"].toBool() && !items[8].toObject().contains("expanded"));
        TEST_COMPARE(items[4].toObject()["rect"].toObject(), w.cell(w.tree, w.treeIndex({0, 1})));
        TEST_COMPARE(result["headers"].toArray().size(), 1);
        TEST_COMPARE(result["headers"].toArray()[0].toObject()["items"].toArray()[1].toObject()["text"].toString(),
                     QString("Size"));

        // all: also below collapsed items, which are not laid out.
        const QJsonArray all = object(engine, "return ui.items('tree', {all: true, columns: [0, 0]});")["items"].toArray();
        TEST_COMPARE(all.size(), 7);
        TEST_COMPARE(all[4].toObject()["ref"].toObject()["path"].toArray(), (QJsonArray{1, 0}));
        TEST_COMPARE(all[5].toObject()["ref"].toObject()["path"].toArray(), (QJsonArray{1, 0, 0}));
        TEST_ASSERT(all[5].toObject()["rect"].isNull() && all[5].toObject()["visibleRect"].isNull());
        TEST_COMPARE(all[5].toObject()["text"].toString(), QString("b00"));

        // A range of top-level rows: the expanded part only.
        TEST_COMPARE(object(engine, "return ui.items('tree', {rows: [1, 1]});")["items"].toArray().size(), 2);
        w.tree->topLevelItem(1)->setExpanded(true);
        const QJsonArray open = object(engine, "return ui.items('tree', {rows: [1, 1], columns: [0, 0]});")["items"].toArray();
        TEST_COMPARE(open.size(), 2);
        TEST_COMPARE(open[1].toObject()["ref"].toObject()["path"].toArray(), (QJsonArray{1, 0}));
        TEST_ASSERT(open[1].toObject()["rect"].isObject());
    }

    TEST_FUNCTION(testListAndHeaderSections)
    {
        TEST_START;

        Engine engine;
        Views w;
        ScriptHost::exposeWidgets(&engine, &w.window);

        const QJsonObject list = object(engine, "return ui.items('list');");
        TEST_COMPARE(list["kind"].toString(), QString("list"));
        const QJsonArray items = list["items"].toArray();
        const int last = w.list->indexAt(QPoint(1, w.list->viewport()->height() - 1)).row();
        TEST_ASSERT(last > 2);
        TEST_COMPARE(list["visibleRows"].toArray(), pair(0, last));
        TEST_COMPARE(list["visibleColumns"].toArray(), pair(0, 0));
        TEST_COMPARE(items.size(), last + 1);
        TEST_COMPARE(refs(items).at(3), QString("{\"row\":3}"));
        TEST_COMPARE(items[3].toObject()["text"].toString(), QString("item 3"));
        TEST_COMPARE(items[3].toObject()["rect"].toObject(), w.cell(w.list, w.list->model()->index(3, 0)));
        TEST_ASSERT(!items[0].toObject().contains("checked"));
        TEST_COMPARE(items[1].toObject().value("checked"), QJsonValue(true));
        TEST_COMPARE(items[2].toObject().value("checked"), QJsonValue("partial"));
        TEST_COMPARE(object(engine, "return ui.items('list', {rows: [45, 60]});")["items"].toArray().size(), 5);
        TEST_ASSERT(!list.contains("headers"));

        // A header on its own: the same sections the table lists.
        const QJsonObject header = object(engine, "const t = ui.items('table');\n"
                                                  "const h = ui.items(t.headers[0].widget);\n"
                                                  "h.same = JSON.stringify(h.items) === JSON.stringify(t.headers[0].items);\n"
                                                  "return h;");
        TEST_COMPARE(header["kind"].toString(), QString("header"));
        TEST_ASSERT(header["same"].toBool());
        TEST_ASSERT(header["visibleRows"].isNull());
        const QJsonArray sections = header["items"].toArray();
        TEST_COMPARE(header["visibleColumns"].toArray(), pair(0, sections.size() - 1));
        TEST_COMPARE(sections[2].toObject()["ref"].toObject()["section"].toInt(), 2);
        TEST_COMPARE(sections[2].toObject()["text"].toString(), QString("C"));
        TEST_COMPARE(sections[2].toObject()["rect"].toObject(), w.section(w.table->horizontalHeader(), 2));
        const QJsonObject vertical = object(engine, "return ui.items(ui.items('table').headers[1].widget, "
                                                    "{rows: [10, 11]});");
        TEST_COMPARE(vertical["items"].toArray().size(), 2);
        TEST_COMPARE(vertical["items"].toArray()[0].toObject()["text"].toString(), QString("11"));
    }

    TEST_FUNCTION(testItemAt)
    {
        TEST_START;

        Engine engine;
        Views w;
        ScriptHost::exposeWidgets(&engine, &w.window);
        auto at = [&engine](const QPoint& point) {
            return object(engine, QString("return ui.itemAt(%1, %2);").arg(point.x()).arg(point.y()));
        };
        auto center = [](const QJsonObject& rect) {
            return QPoint(rect["x"].toInt() + rect["width"].toInt() / 2, rect["y"].toInt() + rect["height"].toInt() / 2);
        };

        // A cell.
        const QJsonObject cell = at(center(w.cell(w.table, w.table->model()->index(1, 1))));
        TEST_COMPARE(cell["widget"].toObject()["name"].toString(), QString("table"));
        TEST_COMPARE(cell["item"].toObject()["ref"].toObject(), (QJsonObject{{"row", 1}, {"column", 1}}));
        TEST_COMPARE(cell["item"].toObject()["text"].toString(), QString("r1c1"));

        // A header section, and a tree item.
        const QJsonObject section = at(center(w.section(w.table->horizontalHeader(), 2)));
        TEST_COMPARE(section["widget"].toObject()["class"].toString(), QString("QHeaderView"));
        TEST_COMPARE(section["item"].toObject()["ref"].toObject(), (QJsonObject{{"section", 2}}));
        const QJsonObject leaf = at(center(w.cell(w.tree, w.treeIndex({0, 0}))));
        TEST_COMPARE(leaf["item"].toObject()["ref"].toObject()["path"].toArray(), (QJsonArray{0, 0}));

        // A plain widget, an empty part of a view (the widget there: the
        // viewport, as ui.at gives it), outside the root.
        const QJsonObject button = at(w.inWindow(w.plain).center());
        TEST_COMPARE(button["widget"].toObject()["name"].toString(), QString("plain"));
        TEST_ASSERT(!button.contains("item"));
        const QRect below = w.inWindow(w.tree);
        const QPoint spot(below.center().x(), below.bottom() - 5);
        const QJsonObject empty = at(spot);
        TEST_COMPARE(empty["widget"].toObject(),
                     object(engine, QString("return ui.at(%1, %2);").arg(spot.x()).arg(spot.y())));
        TEST_ASSERT(!empty.contains("item"));
        TEST_ASSERT(run(engine, "return ui.itemAt(-5, -5);")["result"].isNull());
        // ui.at stays as it was: the viewport.
        TEST_COMPARE(object(engine, QString("return ui.at(%1, %2);").arg(w.inWindow(w.table).center().x())
                                        .arg(w.inWindow(w.table).center().y()))["name"].toString(),
                     QString("qt_scrollarea_viewport"));
    }

    TEST_FUNCTION(testTextSwitchAndHiddenViews)
    {
        TEST_START;

        Engine engine;
        Views w;
        ScriptHost::ScriptUi* ui = ScriptHost::exposeWidgets(&engine, &w.window);
        ui->setTextReadable(false);

        const QJsonObject table = object(engine, "return ui.items('table');");
        TEST_ASSERT(!table["items"].toArray()[0].toObject().contains("text"));
        TEST_ASSERT(table["items"].toArray()[0].toObject()["rect"].isObject());
        TEST_ASSERT(!table["headers"].toArray()[0].toObject()["items"].toArray()[0].toObject().contains("text"));
        TEST_ASSERT(!object(engine, "return ui.items('tree');")["items"].toArray()[0].toObject().contains("text"));
        TEST_ASSERT(!object(engine, "return ui.items('list');")["items"].toArray()[0].toObject().contains("text"));
        const QRect cell = w.table->visualRect(w.table->model()->index(0, 0));
        const QPoint point = w.table->viewport()->mapTo(&w.window, cell.center());
        const QJsonObject item = object(engine, QString("return ui.itemAt(%1, %2).item;").arg(point.x()).arg(point.y()));
        TEST_COMPARE(item["ref"].toObject(), (QJsonObject{{"row", 0}, {"column", 0}}));
        TEST_ASSERT(!item.contains("text"));
        ui->setTextReadable(true);

        // A hidden view is refused; over it, ui.itemAt finds the window.
        ScriptHost::ScriptUi::hideWidget(w.table);
        TEST_ASSERT(run(engine, "return ui.items('table');")["message"].toString().contains("no widget 'table'"));
        const QJsonObject over = object(engine, QString("return ui.itemAt(%1, %2);").arg(point.x()).arg(point.y()));
        TEST_COMPARE(over["widget"].toObject()["name"].toString(), QString("window"));
        TEST_ASSERT(!over.contains("item"));
        // A hidden header is left out of the view's headers.
        ScriptHost::ScriptUi::hideWidget(w.table, false);
        ScriptHost::ScriptUi::hideWidget(w.table->verticalHeader());
        TEST_COMPARE(object(engine, "return ui.items('table');")["headers"].toArray().size(), 1);
    }

    // What the canvas will use: a reference to the rect in the widget,
    // without ScriptUi.
    TEST_FUNCTION(testItemRectResolvesReferences)
    {
        TEST_START;

        namespace Geometry = ScriptHost::ItemGeometry;
        Views w;
        auto inView = [](QAbstractItemView* view, const QModelIndex& index) {
            return view->visualRect(index).translated(view->viewport()->pos());
        };
        TEST_COMPARE(Geometry::itemRect(w.table, QJsonObject{{"row", 1}, {"column", 2}}).value_or(QRect()),
                     inView(w.table, w.table->model()->index(1, 2)));
        TEST_COMPARE(Geometry::itemRect(w.tree, QJsonObject{{"path", QJsonArray{0, 1}}}).value_or(QRect()),
                     inView(w.tree, w.treeIndex({0, 1})));
        TEST_COMPARE(Geometry::itemRect(w.tree, QJsonObject{{"path", QJsonArray{2}}, {"column", 1}}).value_or(QRect()),
                     inView(w.tree, w.treeIndex({2}, 1)));
        TEST_COMPARE(Geometry::itemRect(w.list, QJsonObject{{"row", 3}}).value_or(QRect()),
                     inView(w.list, w.list->model()->index(3, 0)));
        QHeaderView* header = w.table->horizontalHeader();
        TEST_COMPARE(Geometry::itemRect(header, QJsonObject{{"section", 1}}).value_or(QRect()),
                     QRect(header->viewport()->pos() + QPoint(header->sectionViewportPosition(1), 0),
                           QSize(header->sectionSize(1), header->viewport()->height())));

        // Nothing there, or not laid out.
        TEST_ASSERT(!Geometry::itemRect(w.tree, QJsonObject{{"path", QJsonArray{1, 0}}}).has_value());
        TEST_ASSERT(!Geometry::itemRect(w.table, QJsonObject{{"row", 100}, {"column", 0}}).has_value());
        TEST_ASSERT(!Geometry::itemRect(w.table, QJsonObject()).has_value());
        TEST_ASSERT(!Geometry::itemRect(w.tree, QJsonObject{{"path", QJsonArray()}}).has_value());
        TEST_ASSERT(!Geometry::itemRect(w.plain, QJsonObject{{"row", 0}}).has_value());
        TEST_ASSERT(Geometry::providerFor(w.plain) == nullptr);
        TEST_COMPARE(QString(Geometry::providerFor(header)->kind), QString("header"));
        TEST_COMPARE(QString(Geometry::providerFor(w.tree)->kind), QString("tree"));
        TEST_COMPARE(QString(Geometry::providerFor(w.list)->kind), QString("list"));
    }

    // 10,000 rows x 5 columns, made on demand.
    class BigModel : public QAbstractTableModel
    {
    public:
        int rowCount(const QModelIndex& parent = QModelIndex()) const override { return parent.isValid() ? 0 : 10000; }
        int columnCount(const QModelIndex& parent = QModelIndex()) const override { return parent.isValid() ? 0 : 5; }
        QVariant data(const QModelIndex& index, int role) const override
        {
            if (role != Qt::DisplayRole)
                return QVariant();
            return QString("r%1c%2").arg(index.row()).arg(index.column());
        }
    };

    TEST_FUNCTION(testBigTableAnswersFast)
    {
        TEST_START;

        Engine engine;
        QWidget window;
        window.setObjectName("bigWindow");
        BigModel model;
        auto* view = new QTableView(&window);
        view->setObjectName("big");
        view->setModel(&model);
        view->setGeometry(0, 0, 600, 400);
        window.resize(600, 400);
        window.show();
        TestSupport::spin([&]() { return view->visualRect(model.index(0, 0)).height() > 0; });
        ScriptHost::exposeWidgets(&engine, &window);
        view->verticalScrollBar()->setValue(5000);

        // The part in view, as a script sees it (call included) and as
        // ItemGeometry builds it.
        const QJsonObject timed = object(engine, "ui.items('big');\n"
                                                 "const start = Date.now();\n"
                                                 "let r;\n"
                                                 "for (let i = 0; i < 20; ++i)\n"
                                                 "    r = ui.items('big');\n"
                                                 "return {ms: (Date.now() - start) / 20, count: r.items.length,\n"
                                                 "        first: r.visibleRows[0]};");
        ScriptHost::ItemGeometry::ListContext context;
        context.root = &window;
        const auto list = ScriptHost::ItemGeometry::providerFor(view)->list;
        QElapsedTimer timer;
        timer.start();
        for (int i = 0; i < 20; ++i)
            list(view, context);
        const double direct = double(timer.nsecsElapsed()) / 20 / 1e6;
        std::printf("TST_ScriptUiItems: visible part of a 10,000-row table: %.2f ms per ui.items() call, "
                    "%.2f ms in ItemGeometry, %d items\n",
                    timed["ms"].toDouble(), direct, timed["count"].toInt());
        TEST_COMPARE(timed["first"].toInt(), 5000);
        TEST_ASSERT(timed["count"].toInt() > 20 && timed["count"].toInt() < 500);
        TEST_ASSERT(timed["ms"].toDouble() < 50);

        // All of it: capped.
        const QJsonObject all = object(engine, "const r = ui.items('big', {all: true});\n"
                                               "return {count: r.items.length, truncated: r.truncated};");
        TEST_COMPARE(all["count"].toInt(), ScriptHost::ItemGeometry::kMaxItems);
        TEST_ASSERT(all["truncated"].toBool());
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
