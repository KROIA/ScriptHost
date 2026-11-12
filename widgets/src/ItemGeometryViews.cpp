#include "ItemGeometry.h"
#include <QAbstractItemView>
#include <QHeaderView>
#include <QJsonArray>
#include <QListView>
#include <QTableView>
#include <QTreeView>
#include <utility>

// The providers of item views and headers (T-025): tables, trees, lists,
// other item views and header sections. Refs: {row, column} (a table or
// another view), {path, column} (a tree), {row} (a list), {section} (a
// header).

namespace ScriptHost
{
namespace ItemGeometry
{

namespace
{

QString kindOf(const QWidget* widget)
{
    if (qobject_cast<const QHeaderView*>(widget))
        return QStringLiteral("header");
    if (qobject_cast<const QTreeView*>(widget))
        return QStringLiteral("tree");
    if (qobject_cast<const QTableView*>(widget))
        return QStringLiteral("table");
    if (qobject_cast<const QListView*>(widget))
        return QStringLiteral("list");
    return QStringLiteral("view");
}

// [first, last] of lines in order; null when there are none.
QJsonValue pairJson(const QList<int>& lines)
{
    return lines.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(QJsonArray{lines.first(), lines.last()});
}

// In the header's viewport; empty for a hidden section.
QRect sectionRect(const QHeaderView* header, int section)
{
    if (section < 0 || section >= header->count() || header->isSectionHidden(section))
        return QRect();
    const int position = header->sectionViewportPosition(section);
    const int size = header->sectionSize(section);
    const QRect area = header->viewport()->rect();
    return header->orientation() == Qt::Horizontal ? QRect(position, 0, size, area.height())
                                                   : QRect(0, position, area.width(), size);
}

QModelIndex indexOf(const QAbstractItemView* view, const QJsonObject& ref)
{
    const QAbstractItemModel* model = view->model();
    if (!model)
        return QModelIndex();
    if (qobject_cast<const QTreeView*>(view)) {
        const QJsonArray path = ref.value("path").toArray();
        QModelIndex index = view->rootIndex();
        for (const QJsonValue& row : path) {
            index = model->index(row.toInt(-1), 0, index);
            if (!index.isValid())
                return QModelIndex();
        }
        return path.isEmpty() ? QModelIndex() : index.sibling(index.row(), ref.value("column").toInt(0));
    }
    if (const auto* list = qobject_cast<const QListView*>(view))
        return model->index(ref.value("row").toInt(-1), list->modelColumn(), view->rootIndex());
    return model->index(ref.value("row").toInt(-1), ref.value("column").toInt(-1), view->rootIndex());
}

QJsonObject refOf(const QAbstractItemView* view, const QModelIndex& index)
{
    if (qobject_cast<const QTreeView*>(view)) {
        QJsonArray path;
        for (QModelIndex i = index; i.isValid() && i != view->rootIndex(); i = i.parent())
            path.prepend(i.row());
        return QJsonObject{{"path", path}, {"column", index.column()}};
    }
    if (qobject_cast<const QListView*>(view))
        return QJsonObject{{"row", index.row()}};
    return QJsonObject{{"row", index.row()}, {"column", index.column()}};
}

QJsonObject itemJson(const QAbstractItemView* view, const QModelIndex& index, const ListContext& context)
{
    QJsonObject item{{"ref", refOf(view, index)}};
    if (context.textReadable)
        item.insert("text", index.data(Qt::DisplayRole).toString());
    addRects(&item, view->viewport(), view->visualRect(index), context.root);
    item.insert("selected", view->selectionModel() && view->selectionModel()->isSelected(index));
    item.insert("current", view->currentIndex() == index);
    item.insert("enabled", (index.flags() & Qt::ItemIsEnabled) != 0);
    const QVariant check = index.data(Qt::CheckStateRole);
    if (check.isValid()) {
        const int state = check.toInt();
        item.insert("checked", state == Qt::PartiallyChecked ? QJsonValue(QStringLiteral("partial"))
                                                             : QJsonValue(state == Qt::Checked));
    }
    if (const auto* tree = qobject_cast<const QTreeView*>(view)) {
        const QModelIndex first = index.sibling(index.row(), 0);
        const bool children = index.model()->hasChildren(first);
        item.insert("hasChildren", children);
        if (children)
            item.insert("expanded", tree->isExpanded(first));
    }
    return item;
}

QJsonObject sectionJson(const QHeaderView* header, int section, const ListContext& context)
{
    QJsonObject item{{"ref", QJsonObject{{"section", section}}}};
    if (context.textReadable && header->model())
        item.insert("text", header->model()->headerData(section, header->orientation()).toString());
    addRects(&item, header->viewport(), sectionRect(header, section), context.root);
    return item;
}

// The shown sections of `header` within `length` pixels of the view, in
// visual order.
QList<int> inView(const QHeaderView* header, int length)
{
    QList<int> sections;
    const int first = header->visualIndexAt(0);
    if (first < 0 || length <= 0)
        return sections;
    int last = header->visualIndexAt(length - 1);
    if (last < 0)
        last = header->count() - 1;
    for (int visual = first; visual <= last; ++visual) {
        const int section = header->logicalIndex(visual);
        if (!header->isSectionHidden(section))
            sections.append(section);
    }
    return sections;
}

// The lines (rows, columns, sections) to list: the range `key` of the
// options, else all of them with `all`, else the ones in view.
QList<int> chosen(const QJsonObject& options, const char* key, int count, const QList<int>& view)
{
    int first = 0;
    int last = count - 1;
    const QJsonArray range = options.value(QLatin1String(key)).toArray();
    if (range.size() == 2) {
        first = qMax(0, range.at(0).toInt());
        last = qMin(count - 1, range.at(1).toInt());
    } else if (!options.value("all").toBool()) {
        return view;
    }
    QList<int> lines;
    for (int line = first; line <= last; ++line)
        lines.append(line);
    return lines;
}

// The row of the item's ancestor at the top level of a tree.
int topRow(const QAbstractItemView* view, QModelIndex index)
{
    while (index.parent().isValid() && index.parent() != view->rootIndex())
        index = index.parent();
    return index.row();
}

// A header scrolls with its view, not with scroll bars of its own.
QJsonObject headerScroll(const QHeaderView* header)
{
    const bool horizontal = header->orientation() == Qt::Horizontal;
    const int length = horizontal ? header->viewport()->width() : header->viewport()->height();
    const int offset = header->offset();
    const int max = qMax(0, header->length() - length);
    return QJsonObject{{"x", horizontal ? offset : 0}, {"y", horizontal ? 0 : offset},
                       {"maxX", horizontal ? max : 0}, {"maxY", horizontal ? 0 : max}};
}

QJsonObject listHeader(const QHeaderView* header, const ListContext& context)
{
    QJsonArray list;
    bool truncated = false;
    const QWidget* area = header->viewport();
    const bool horizontal = header->orientation() == Qt::Horizontal;
    const QList<int> shown = inView(header, horizontal ? area->width() : area->height());
    for (int section : chosen(context.options, horizontal ? "columns" : "rows", header->count(), shown)) {
        if (list.size() >= context.maxItems) {
            truncated = true;
            break;
        }
        list.append(sectionJson(header, section, context));
    }
    QJsonObject result{{"kind", kindOf(header)},
                       {"visibleRows", horizontal ? QJsonValue(QJsonValue::Null) : pairJson(shown)},
                       {"visibleColumns", horizontal ? pairJson(shown) : QJsonValue(QJsonValue::Null)},
                       {"scroll", headerScroll(header)},
                       {"items", list}};
    if (truncated)
        result.insert("truncated", true);
    return result;
}

// A table's or tree's shown headers that scripts may see, as listHeader()
// lists them.
QJsonArray headersOf(const QAbstractItemView* view, const ListContext& context)
{
    QList<QHeaderView*> headers;
    if (const auto* table = qobject_cast<const QTableView*>(view))
        headers = {table->horizontalHeader(), table->verticalHeader()};
    else if (const auto* tree = qobject_cast<const QTreeView*>(view))
        headers = {tree->header()};
    QJsonArray list;
    for (QHeaderView* header : std::as_const(headers)) {
        const QJsonValue id = context.idOf ? context.idOf(header) : QJsonValue();
        if (header->isHidden() || id.isNull() || id.isUndefined())
            continue;
        const QJsonObject sections = listHeader(header, context);
        QJsonObject entry{{"widget", id},
                          {"orientation", header->orientation() == Qt::Horizontal ? "horizontal" : "vertical"},
                          {"items", sections.value("items")}};
        if (sections.contains("truncated"))
            entry.insert("truncated", true);
        list.append(entry);
    }
    return list;
}

QJsonObject listItems(QWidget* widget, const ListContext& context)
{
    if (const auto* header = qobject_cast<const QHeaderView*>(widget))
        return listHeader(header, context);
    const auto* view = qobject_cast<const QAbstractItemView*>(widget);
    const QJsonObject& options = context.options;
    QJsonArray list;
    bool truncated = false;
    QJsonValue visibleRows = QJsonValue::Null;
    QJsonValue visibleColumns = QJsonValue::Null;
    const QWidget* area = view->viewport();
    const QAbstractItemModel* model = view->model();

    if (model) {
        const QModelIndex top = view->rootIndex();
        QList<QModelIndex> rows;   // column 0 of each row to list
        QList<int>         columns;
        auto rowIndexes = [model, &top](const QList<int>& lines) {
            QList<QModelIndex> indexes;
            for (int line : lines)
                indexes.append(model->index(line, 0, top));
            return indexes;
        };
        if (const auto* table = qobject_cast<const QTableView*>(view)) {
            const QList<int> rowsShown = inView(table->verticalHeader(), area->height());
            const QList<int> columnsShown = inView(table->horizontalHeader(), area->width());
            visibleRows = pairJson(rowsShown);
            visibleColumns = pairJson(columnsShown);
            rows = rowIndexes(chosen(options, "rows", model->rowCount(top), rowsShown));
            columns = chosen(options, "columns", model->columnCount(top), columnsShown);
        } else if (const auto* tree = qobject_cast<const QTreeView*>(view)) {
            const QList<int> columnsShown = inView(tree->header(), area->width());
            visibleColumns = pairJson(columnsShown);
            columns = chosen(options, "columns", model->columnCount(top), columnsShown);
            // The rows in view: from the one at the top down to the one at
            // the bottom, through the expanded ones.
            QList<QModelIndex> shown;
            const QModelIndex first = columnsShown.isEmpty() ? QModelIndex() : tree->indexAt(QPoint(0, 0));
            const QModelIndex last = tree->indexAt(QPoint(0, area->height() - 1));
            for (QModelIndex i = first.sibling(first.row(), 0); i.isValid() && shown.size() < context.maxItems;
                 i = tree->indexBelow(i)) {
                shown.append(i);
                if (last.isValid() && i == last.sibling(last.row(), 0))
                    break;
            }
            if (!shown.isEmpty())
                visibleRows = QJsonArray{topRow(view, shown.first()), topRow(view, shown.last())};
            const bool all = options.value("all").toBool();
            if (!all && !options.contains("rows")) {
                rows = shown;
            } else {
                // The top-level rows chosen with what is below them: the
                // expanded part, or everything with all.
                std::function<void(const QModelIndex&)> walk = [&](const QModelIndex& index) {
                    if (rows.size() >= context.maxItems)
                        return;
                    rows.append(index);
                    if (all || tree->isExpanded(index)) {
                        for (int r = 0; r < model->rowCount(index); ++r)
                            walk(model->index(r, 0, index));
                    }
                };
                for (const QModelIndex& index : rowIndexes(chosen(options, "rows", model->rowCount(top), {})))
                    walk(index);
                truncated = rows.size() >= context.maxItems;
            }
        } else {
            // A list (its model column) or another item view (every column).
            const auto* listView = qobject_cast<const QListView*>(view);
            const int columnCount = model->columnCount(top);
            columns = listView ? QList<int>{listView->modelColumn()}
                               : chosen(options, "columns", columnCount,
                                        chosen(QJsonObject{{"all", true}}, "columns", columnCount, {}));
            visibleColumns = pairJson(columns);
            // ponytail: finds the rows in view by a scan of all rows, O(rows);
            // a search along the list's flow if lists of 100,000s turn up.
            QList<int> rowsShown;
            const int count = model->rowCount(top);
            for (int r = 0; r < count; ++r) {
                for (int column : std::as_const(columns)) {
                    if (view->visualRect(model->index(r, column, top)).intersects(area->rect())) {
                        rowsShown.append(r);
                        break;
                    }
                }
            }
            visibleRows = pairJson(rowsShown);
            rows = rowIndexes(chosen(options, "rows", count, rowsShown));
        }
        for (const QModelIndex& row : std::as_const(rows)) {
            for (int column : std::as_const(columns)) {
                const QModelIndex index = row.sibling(row.row(), column);
                if (!index.isValid())
                    continue;
                if (list.size() >= context.maxItems) {
                    truncated = true;
                    break;
                }
                list.append(itemJson(view, index, context));
            }
            if (truncated)
                break;
        }
    }

    QJsonObject result{{"kind", kindOf(widget)},
                       {"visibleRows", visibleRows},
                       {"visibleColumns", visibleColumns},
                       {"scroll", scroll(view)},
                       {"items", list}};
    if (truncated)
        result.insert("truncated", true);
    if (qobject_cast<const QTableView*>(view) || qobject_cast<const QTreeView*>(view))
        result.insert("headers", headersOf(view, context));
    return result;
}

std::optional<QRect> resolve(QWidget* widget, const QJsonObject& ref)
{
    QRect found;
    const QWidget* area = nullptr;
    if (const auto* header = qobject_cast<const QHeaderView*>(widget)) {
        area = header->viewport();
        found = sectionRect(header, ref.value("section").toInt(-1));
    } else {
        const auto* view = qobject_cast<const QAbstractItemView*>(widget);
        area = view->viewport();
        const QModelIndex index = indexOf(view, ref);
        if (index.isValid())
            found = view->visualRect(index);
    }
    if (found.isEmpty())
        return std::nullopt;
    return found.translated(area->mapTo(widget, QPoint()));
}

std::optional<QJsonObject> hit(QWidget* widget, const QPoint& local, const ListContext& context)
{
    const auto* view = qobject_cast<const QAbstractItemView*>(widget);
    const QWidget* area = view->viewport();
    const QPoint inArea = area->mapFrom(widget, local);
    if (!area->rect().contains(inArea))
        return std::nullopt;
    if (const auto* header = qobject_cast<const QHeaderView*>(view)) {
        const int section = header->logicalIndexAt(inArea);
        if (section < 0)
            return std::nullopt;
        return sectionJson(header, section, context);
    }
    const QModelIndex index = view->indexAt(inArea);
    if (!index.isValid())
        return std::nullopt;
    return itemJson(view, index, context);
}

template <class T>
bool is(const QWidget* widget)
{
    return qobject_cast<const T*>(widget) != nullptr;
}

} // namespace

QList<Provider> itemViewProviders()
{
    return {{"header", is<QHeaderView>, listItems, resolve, hit},
            {"tree", is<QTreeView>, listItems, resolve, hit},
            {"table", is<QTableView>, listItems, resolve, hit},
            {"list", is<QListView>, listItems, resolve, hit},
            {"view", is<QAbstractItemView>, listItems, resolve, hit}};
}

} // namespace ItemGeometry
} // namespace ScriptHost
