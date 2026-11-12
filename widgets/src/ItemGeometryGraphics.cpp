#include "ItemGeometry.h"
#include <QGraphicsItem>
#include <QGraphicsObject>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QJsonArray>
#include <QVariant>
#include <iterator>

// The providers of Graphics View items (T-027).
//
// An item's ref is {index: n}: an id unique in its scene that stays the
// item's own while it lives. It is given on first sight and kept in the item
// itself (QGraphicsItem::setData() under kIdKey), the scene's last id in a
// dynamic property of the scene; ids are never given twice, so a ref of a
// deleted item names nothing rather than a new item at the same address (a
// map from item pointers could not tell). Items come topmost first.

namespace ScriptHost
{
namespace ItemGeometry
{

namespace
{

// The data key of the id; applications must not use it for their own data.
const int kIdKey = 0x53480001;
const char* const kLastId = "scripthost_lastItemId";

int itemId(QGraphicsItem* item)
{
    QVariant id = item->data(kIdKey);
    if (!id.isValid()) {
        QGraphicsScene* scene = item->scene();
        id = scene->property(kLastId).toInt() + 1;
        scene->setProperty(kLastId, id);
        item->setData(kIdKey, id);
    }
    return id.toInt();
}

QGraphicsItem* findItem(const QGraphicsView* view, int id)
{
    if (id <= 0 || !view->scene())
        return nullptr;
    // ponytail: a walk over the scene's items per lookup; a hash per scene
    // if scenes with many thousand items are anchored to.
    for (QGraphicsItem* item : view->scene()->items()) {
        if (item->data(kIdKey).toInt() == id)
            return item;
    }
    return nullptr;
}

// The item's bounding rect in the viewport's coordinates.
QRect viewRect(const QGraphicsView* view, const QGraphicsItem* item)
{
    return view->mapFromScene(item->sceneBoundingRect()).boundingRect();
}

QString typeName(int type)
{
    // QGraphicsItem::type() of Qt's own item classes, from 1 (QGraphicsItem).
    static const char* const names[] = {"item", "path", "rect", "ellipse", "polygon", "line", "pixmap", "text",
                                        "simpleText", "group", "widget", "proxyWidget", "svg"};
    if (type >= QGraphicsItem::UserType)
        return QStringLiteral("user%1").arg(type - QGraphicsItem::UserType);
    if (type >= 1 && type <= int(std::size(names)))
        return QString::fromLatin1(names[type - 1]);
    return QString::number(type);
}

QJsonObject itemJson(QGraphicsView* view, QGraphicsItem* item, const ListContext& context)
{
    const QRectF scene = item->sceneBoundingRect();
    QJsonObject json{{"ref", QJsonObject{{"index", itemId(item)}}},
                     {"type", typeName(item->type())},
                     {"sceneRect", QJsonObject{{"x", scene.x()}, {"y", scene.y()},
                                               {"width", scene.width()}, {"height", scene.height()}}},
                     {"z", item->zValue()},
                     {"visible", item->isVisible()},
                     {"enabled", item->isEnabled()},
                     {"selected", item->isSelected()}};
    if (const QGraphicsObject* object = item->toGraphicsObject())
        json.insert("className", QString::fromLatin1(object->metaObject()->className()));
    addRects(&json, view->viewport(), viewRect(view, item), context.root);
    if (!item->isVisible())
        json.insert("visibleRect", QJsonValue::Null);
    if (context.textReadable && !item->toolTip().isEmpty())
        json.insert("tooltip", item->toolTip());
    const QJsonValue data0 = QJsonValue::fromVariant(item->data(0));
    if (!data0.isNull() && !data0.isUndefined() && (context.textReadable || !data0.isString()))
        json.insert("data0", data0);
    return json;
}

bool accepts(const QWidget* widget)
{
    return qobject_cast<const QGraphicsView*>(widget) != nullptr;
}

QJsonObject list(QWidget* widget, const ListContext& context)
{
    auto* view = static_cast<QGraphicsView*>(widget);
    QList<QGraphicsItem*> items;
    if (view->scene()) {
        items = context.options.value("all").toBool() ? view->scene()->items()
                                                       : view->items(view->viewport()->rect());
    }
    QJsonArray array;
    for (int i = 0; i < items.size() && i < context.maxItems; ++i)
        array.append(itemJson(view, items.at(i), context));
    QJsonObject result{{"kind", "graphics"}, {"scroll", scroll(view)}, {"items", array}};
    if (items.size() > context.maxItems)
        result.insert("truncated", true);
    return result;
}

std::optional<QRect> resolve(QWidget* widget, const QJsonObject& ref)
{
    auto* view = static_cast<QGraphicsView*>(widget);
    const QGraphicsItem* item = findItem(view, ref.value("index").toInt());
    if (!item || !item->isVisible())
        return std::nullopt;
    return viewRect(view, item).translated(view->viewport()->pos());
}

std::optional<QJsonObject> hit(QWidget* widget, const QPoint& local, const ListContext& context)
{
    auto* view = static_cast<QGraphicsView*>(widget);
    const QPoint point = local - view->viewport()->pos();
    QGraphicsItem* item = view->viewport()->rect().contains(point) ? view->itemAt(point) : nullptr;
    if (!item)
        return std::nullopt;
    return itemJson(view, item, context);
}

} // namespace

QList<Provider> graphicsProviders()
{
    return {Provider{"graphics", accepts, list, resolve, hit}};
}

} // namespace ItemGeometry
} // namespace ScriptHost
