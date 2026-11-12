#include "ScriptUi.h"
#include "ItemGeometry.h"
#include "ScriptEngine.h"
#include "ScriptUiSupport.h"
#include <QJsonArray>
#include <QWidget>
#include <optional>

// ui.items and ui.itemAt: the parts of widgets, through the providers of
// ItemGeometry.

namespace ScriptHost
{

using namespace ScriptUiSupport;

QJsonValue ScriptUi::items(QWidget* widget, const QJsonObject& options) const
{
    const ItemGeometry::Provider* provider = ItemGeometry::providerFor(widget);
    if (!provider)
        throw ScriptError(QStringLiteral("a %1 has no items").arg(className(widget)));
    for (const char* key : {"rows", "columns"}) {
        const QJsonValue range = options.value(QLatin1String(key));
        const QJsonArray pair = range.toArray();
        if (!range.isUndefined() && !(pair.size() == 2 && pair.at(0).isDouble() && pair.at(1).isDouble()))
            throw ScriptError(QStringLiteral("%1 must be [first, last]").arg(QLatin1String(key)));
    }
    ItemGeometry::ListContext context;
    context.options = options;
    context.root = m_root;
    context.textReadable = m_textReadable;
    context.idOf = [this](QWidget* w) {
        return isVisible(w) ? QJsonValue(widgetId(w)) : QJsonValue(QJsonValue::Null);
    };
    return provider->list(widget, context);
}

QJsonValue ScriptUi::itemAt(int x, int y) const
{
    const QPoint point(x, y);
    QWidget* hit = widgetAt(point);
    if (!hit)
        return QJsonValue::Null;
    QJsonObject result{{"widget", node(hit)}};
    // The nearest widget with items, from the one under the point up (a
    // view for its viewport): its item there. Without one there, the widget
    // under the point stays.
    for (QWidget* w = hit; w; w = w == m_root ? nullptr : w->parentWidget()) {
        const ItemGeometry::Provider* provider = ItemGeometry::providerFor(w);
        if (!provider)
            continue;
        ItemGeometry::ListContext context;
        context.root = m_root;
        context.textReadable = m_textReadable;
        context.idOf = [this](QWidget* other) {
            return isVisible(other) ? QJsonValue(widgetId(other)) : QJsonValue(QJsonValue::Null);
        };
        const std::optional<QJsonObject> item = provider->hit(w, w->mapFromGlobal(m_root->mapToGlobal(point)),
                                                              context);
        if (item) {
            result.insert("widget", node(w));
            result.insert("item", *item);
        }
        break;
    }
    return result;
}

void ScriptUi::registerItemMethods()
{
    const QJsonObject rect{{"x", 0}, {"y", 0}, {"width", 80}, {"height", 24}};
    const QJsonObject range{{"type", "array"}, {"items", QJsonObject{{"type", "integer"}}},
                            {"description", "[first, last]"}};
    const QJsonObject item{{"ref", QJsonObject{{"row", 0}, {"column", 0}}}, {"text", "Apples"}, {"rect", rect},
                           {"visibleRect", rect}, {"selected", false}, {"current", false}, {"enabled", true}};

    add(ScriptMethod(QStringLiteral("ui.items"))
            .setDescription(QStringLiteral(
                "The parts of a widget that has them: {kind, items: [{ref, rect, visibleRect, ...}], "
                "truncated?, ...}. ref names the item: {row, column} in a table, {path: [row, ...], column} in a "
                "tree, {row} in a list, {section} in a header, {index} for tabs and actions, {part} for parts "
                "the style draws. Rects in root coordinates; null when the item is not laid out or not seen. By "
                "default the items in view; rows/columns ranges or all list more, at most 5000."))
            .addRawParameter(QStringLiteral("widget"), widgetSchema())
            .addRawParameter(QStringLiteral("options"),
                             QJsonObject{{"type", "object"},
                                         {"description", "Which items: {rows?, columns?, all?}"},
                                         {"properties", QJsonObject{
                                             {"rows", range},
                                             {"columns", range},
                                             {"all", QJsonObject{{"type", "boolean"},
                                                                 {"description", "Every item; default false"}}}}}},
                             false)
            .setReturns(QStringLiteral("object"), QString(),
                        QJsonObject{{"kind", "table"}, {"items", QJsonArray{item}}}),
        [this](const QJsonObject& args) -> QJsonValue {
            return items(resolve(args.value("widget")), args.value("options").toObject());
        });

    add(ScriptMethod(QStringLiteral("ui.itemAt"))
            .setDescription(QStringLiteral("What is at a point in root coordinates: {widget: node, item?} - the "
                                           "nearest widget with items there and its item as ui.items lists it, "
                                           "else the widget; null outside the root."))
            .addParameter(QStringLiteral("x"), QStringLiteral("integer"), QStringLiteral("In the root"))
            .addParameter(QStringLiteral("y"), QStringLiteral("integer"), QStringLiteral("In the root"))
            .setReturns(QStringLiteral("object"), QString(),
                        QJsonObject{{"widget", exampleNode()}, {"item", item}}),
        [this](const QJsonObject& args) -> QJsonValue {
            resolveRoot();
            return itemAt(args.value("x").toInt(), args.value("y").toInt());
        });
}

} // namespace ScriptHost
