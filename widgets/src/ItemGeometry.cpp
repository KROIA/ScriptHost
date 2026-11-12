#include "ItemGeometry.h"
#include <QAbstractScrollArea>
#include <QScrollBar>
#include <QWidget>

namespace ScriptHost
{
namespace ItemGeometry
{

// ------------------------------------------------------------ providers

const QList<Provider>& providers()
{
    // One line per widget family (its providers() in its own .cpp file),
    // the most specific first.
    static const QList<Provider> all = QList<Provider>()
        + itemViewProviders()       // ItemGeometryViews.cpp (T-025)
        + graphicsProviders()       // ItemGeometryGraphics.cpp (T-027)
        + widgetPartProviders();    // ItemGeometryParts.cpp (T-026)
    return all;
}

const Provider* providerFor(const QWidget* widget)
{
    for (const Provider& provider : providers()) {
        if (provider.accepts(widget))
            return &provider;
    }
    return nullptr;
}

std::optional<QRect> itemRect(QWidget* widget, const QJsonObject& ref)
{
    const Provider* provider = providerFor(widget);
    return provider ? provider->resolve(widget, ref) : std::nullopt;
}

// ------------------------------------------------------------ what is visible

QRect mapToRoot(const QWidget* widget, const QRect& rect, const QWidget* root)
{
    return QRect(root->mapFromGlobal(widget->mapToGlobal(rect.topLeft())), rect.size());
}

QRect visiblePart(const QWidget* widget, const QRect& rect)
{
    if (!widget->isVisible())
        return QRect();
    QRect visible = rect & widget->rect();
    QPoint origin;  // the widget's top left in the parent of `w`
    for (const QWidget* w = widget; !w->isWindow() && w->parentWidget(); w = w->parentWidget()) {
        origin += w->pos();
        visible &= w->parentWidget()->rect().translated(-origin);
    }
    return visible;
}

QRect visibleRect(const QWidget* widget, const QWidget* root)
{
    const QRect visible = visiblePart(widget, widget->rect());
    return visible.isEmpty() ? QRect() : mapToRoot(widget, visible, root);
}

QJsonObject scroll(const QAbstractScrollArea* area)
{
    return QJsonObject{{"x", area->horizontalScrollBar()->value()}, {"y", area->verticalScrollBar()->value()},
                       {"maxX", area->horizontalScrollBar()->maximum()},
                       {"maxY", area->verticalScrollBar()->maximum()}};
}

// ------------------------------------------------------------ for providers

QJsonObject rectJson(const QRect& rect)
{
    return QJsonObject{{"x", rect.x()}, {"y", rect.y()}, {"width", rect.width()}, {"height", rect.height()}};
}

void addRects(QJsonObject* item, const QWidget* area, const QRect& rect, const QWidget* root)
{
    const QRect visible = rect.isEmpty() ? QRect() : visiblePart(area, rect);
    item->insert("rect", rect.isEmpty() ? QJsonValue(QJsonValue::Null)
                                        : QJsonValue(rectJson(mapToRoot(area, rect, root))));
    item->insert("visibleRect", visible.isEmpty() ? QJsonValue(QJsonValue::Null)
                                                  : QJsonValue(rectJson(mapToRoot(area, visible, root))));
}

} // namespace ItemGeometry
} // namespace ScriptHost
