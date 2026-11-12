#pragma once

// Positions inside widgets (FR-007): ui.items and ui.itemAt through the
// ItemGeometry providers, the visible part of widgets, and the tools of
// the later tasks - one section per task.

#include "UnitTest.h"
#include "TestSupport.h"
#include "../../../widgets/src/ItemGeometry.h"
#include <QAbstractTableModel>
#include <QElapsedTimer>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTableView>
#include <QTableWidget>
#include <QTabWidget>
#include <QTreeWidget>

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

    // ---- T-027 ---------------------------------------------------------------
};

TEST_INSTANTIATE(TST_ScriptUiItems);
