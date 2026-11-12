#include "ScriptCanvas.h"
#include "CanvasExtension.h"
#include "ItemGeometry.h"
#include "ScriptUiSupport.h"
#include <QAbstractScrollArea>
#include <QChildEvent>
#include <QFontMetricsF>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QMutexLocker>
#include <QPaintEvent>
#include <QPainter>
#include <QRegularExpression>
#include <QScreen>
#include <QSet>
#include <QTimer>
#include <QVarLengthArray>
#include <QVector>
#include <cmath>
#include <utility>

namespace ScriptHost
{

struct CanvasDraw
{
    ScriptCanvas::DrawFunction   paint;
    ScriptCanvas::BoundsFunction bounds;
};

// An item anchor, read once from its text (kItemAnchor + JSON).
struct CanvasAnchor
{
    QJsonValue        widget;       // its objectName or its id
    QJsonObject       ref;          // the item; empty: the whole widget
    bool              clip = true;
    QPointer<QWidget> found;
};

// What is on screen: the newest frame each run presented, by run - in
// run-start order, as RunIds count up - and what the canvas keeps for them.
struct CanvasLayers
{
    QMap<ScriptEngine::RunId, CanvasFrame> frames;
    QVector<QPointF>                       points;     // scratch for polylines
    QHash<QString, QImage>                 images;
    QHash<QString, CanvasDraw>             draws;
    QHash<QString, QPointer<QWidget>>      widgets;    // anchors found by name
    QSet<QObject*>                         watched;    // anchors and their parents
    // ponytail: item anchors by their text, never pruned; prune with the frames if scripts make thousands.
    QHash<QString, CanvasAnchor>           items;
    QSet<QObject*>                         painted;    // item anchors and their viewports
    bool                                   resolveQueued = false;
    QTimer*                                frameTimer = nullptr;
};

namespace
{

// Engine property: the canvas whose `gfx` the engine has.
const char* const kOwner = "scripthost_canvas";

// A custom call without bounds: everywhere.
const QRectF kEverywhere(-1e6, -1e6, 2e6, 2e6);

// Widget property: the id ui.* gave the widget (ScriptUi.cpp keeps it there).
const char* const kWidgetId = "scripthost_id";

// Moves (x, y) - placed by align / valign - to the baseline start; returns
// the text's box. Italic overhang and antialiasing included.
// ponytail: one line per text, no wrapping (I-011); QTextLayout when scripts need more.
QRectF placeText(const QFontMetricsF& metrics, const CanvasStyle& style, const QString& text, double& x, double& y)
{
    const double w = metrics.horizontalAdvance(text);
    const double ascent = metrics.ascent();
    const double h = metrics.height();
    x -= style.align == CanvasStyle::Center ? w / 2 : style.align == CanvasStyle::Right ? w : 0;
    switch (style.valign) {
    case CanvasStyle::Top:      y += ascent; break;
    case CanvasStyle::Middle:   y += ascent - h / 2; break;
    case CanvasStyle::Bottom:   y += ascent - h; break;
    default:                    break;
    }
    const double m = 2 + style.fontSize / 4;
    return QRectF(x, y - ascent, w, h).adjusted(-m, -m, m, m);
}

// Where the line from the middle of `rect` to `toward` leaves it.
QPointF edge(const QRectF& rect, const QPointF& toward)
{
    const QPointF centre = rect.center();
    const QPointF d = toward - centre;
    double t = 1;
    if (d.x() != 0)
        t = qMin(t, rect.width() / 2 / std::abs(d.x()));
    if (d.y() != 0)
        t = qMin(t, rect.height() / 2 / std::abs(d.y()));
    return centre + d * t;
}

void applyStyle(QPainter& painter, const CanvasStyle& style)
{
    if (qAlpha(style.stroke) && style.width > 0)
        painter.setPen(QPen(QColor::fromRgba(style.stroke), style.width, Qt::PenStyle(style.dash)));
    else
        painter.setPen(Qt::NoPen);
    if (style.gradient)
        painter.setBrush(QBrush(*style.gradient));
    else
        painter.setBrush(qAlpha(style.fill) ? QBrush(QColor::fromRgba(style.fill)) : QBrush(Qt::NoBrush));
    painter.setOpacity(style.opacity);
}

void drawArrow(QPainter& painter, const CanvasStyle& style, const double* v)
{
    const QPointF from(v[0], v[1]);
    const QPointF to(v[2], v[3]);
    const double length = QLineF(from, to).length();
    if (length <= 0)
        return;
    const double head = style.arrowHead();
    const QPointF along = (to - from) / length;
    const QPointF across(-along.y() * head / 2, along.x() * head / 2);
    const QPointF base = to - along * head;
    // The shaft ends inside the head, so that a wide pen does not show at the tip.
    painter.drawLine(from, to - along * (head / 2));
    const QPointF tip[3] = {to, base + across, base - across};
    painter.save();
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor::fromRgba(style.stroke));
    painter.drawPolygon(tip, 3);
    painter.restore();
}

void paintFrame(QPainter& painter, const CanvasFrame& frame, const QRectF& area, CanvasLayers& layers)
{
    int current = -1;
    int transform = -1;
    for (const CanvasItem& item : frame.items) {
        if (!item.bounds.intersects(area))
            continue;
        const CanvasStyle& style = frame.styles[size_t(item.style)];
        if (item.transform != transform) {
            if (item.transform < 0)
                painter.resetTransform();
            else
                painter.setTransform(frame.transforms[size_t(item.transform)]);
            transform = item.transform;
        }
        if (item.style != current) {
            applyStyle(painter, style);
            current = item.style;
        }
        const double* v = frame.coords.data() + item.first;
        const bool clipped = !item.clip.isNull();
        if (clipped) {
            painter.save();
            painter.setClipRect(item.clip);
        }
        switch (item.op) {
        case CanvasItem::Rect:
            painter.drawRect(QRectF(v[0], v[1], v[2], v[3]));
            break;
        case CanvasItem::RoundedRect:
            painter.drawRoundedRect(QRectF(v[0], v[1], v[2], v[3]), v[4], v[4]);
            break;
        case CanvasItem::Ellipse:
            painter.drawEllipse(QPointF(v[0], v[1]), v[2], v[3]);
            break;
        case CanvasItem::Line:
            painter.drawLine(QPointF(v[0], v[1]), QPointF(v[2], v[3]));
            break;
        case CanvasItem::Arrow:
            drawArrow(painter, style, v);
            break;
        case CanvasItem::Polyline:
        case CanvasItem::Polygon: {
            QVector<QPointF>& points = layers.points;
            points.resize(item.count / 2);
            for (int i = 0; i < points.size(); ++i)
                points[i] = QPointF(v[2 * i], v[2 * i + 1]);
            if (item.op == CanvasItem::Polyline)
                painter.drawPolyline(points.constData(), int(points.size()));
            else
                painter.drawPolygon(points.constData(), int(points.size()));
            break;
        }
        case CanvasItem::Text:
            if (style.gradient)
                painter.setPen(QPen(QBrush(*style.gradient), 1));
            else
                painter.setPen(QColor::fromRgba(qAlpha(style.fill) ? style.fill : style.stroke));
            painter.setFont(style.font());
            painter.drawText(QPointF(v[0], v[1]), frame.texts.at(item.extra));
            current = -1;
            break;
        case CanvasItem::Path:
            painter.drawPath(frame.paths[size_t(item.extra)]);
            break;
        case CanvasItem::Image: {
            const auto image = layers.images.constFind(frame.texts.at(item.extra));
            if (image != layers.images.constEnd())
                painter.drawImage(QRectF(v[0], v[1], v[2], v[3]), *image, QRectF(v[4], v[5], v[6], v[7]));
            break;
        }
        case CanvasItem::Custom: {
            const CanvasCall& call = frame.calls[size_t(item.extra)];
            const auto draw = layers.draws.constFind(call.name);
            if (draw != layers.draws.constEnd()) {
                painter.save();
                draw->paint(painter, call.args);
                painter.restore();
            }
            break;
        }
        default:
            break;
        }
        if (clipped)
            painter.restore();
    }
    painter.resetTransform();
}

} // namespace

// ---- ScriptCanvas ----------------------------------------------------------

ScriptCanvas::ScriptCanvas(ScriptEngine* engine, QWidget* target)
    : QWidget(target)
    , m_engine(engine)
    , m_shared(QSharedPointer<CanvasShared>::create())
    , m_layers(new CanvasLayers)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setFocusPolicy(Qt::NoFocus);
    m_shared->canvas = this;
    if (!target) {
        qWarning("ScriptHost::ScriptCanvas: no target widget");
        return;
    }
    m_shared->size = target->size();
    setGeometry(target->rect());
    target->installEventFilter(this);
    if (engine) {
        m_extension = QSharedPointer<CanvasExtension>::create(m_shared);
        engine->addExtension(m_extension);
        engine->setProperty(kOwner, qulonglong(quintptr(this)));
    }
    raise();
    show();
}

ScriptCanvas::~ScriptCanvas()
{
    {
        QMutexLocker lock(&m_shared->mutex);
        m_shared->canvas = nullptr;
        m_shared->pending.clear();
    }
    // The filter reads m_layers, which goes before QWidget's destructor.
    for (QObject* watched : std::as_const(m_layers->watched))
        watched->removeEventFilter(this);
    for (QObject* watched : std::as_const(m_layers->painted))
        watched->removeEventFilter(this);
    if (parentWidget())
        parentWidget()->removeEventFilter(this);
    if (m_engine && m_engine->property(kOwner).toULongLong() == qulonglong(quintptr(this))) {
        m_engine->removeExtension(QStringLiteral("gfx"));
        m_engine->setProperty(kOwner, QVariant());
    }
}

ScriptEngine* ScriptCanvas::engine() const
{
    return m_engine;
}

void ScriptCanvas::setAllowedForRequestedRuns(bool allowed)
{
    if (!m_engine)
        return;
    m_engine->allowExtensionForRequestedRuns(QStringLiteral("gfx"), allowed);
    m_engine->allowEventForRequestedRuns(QStringLiteral("resize"), allowed);
}

bool ScriptCanvas::isAllowedForRequestedRuns() const
{
    return m_engine && m_engine->isExtensionAllowedForRequestedRuns(QStringLiteral("gfx"));
}

QList<ScriptEngine::RunId> ScriptCanvas::layers() const
{
    return m_layers->frames.keys();
}

// ---- images and draw functions ---------------------------------------------

void ScriptCanvas::addImage(const QString& name, const QImage& image)
{
    m_layers->images.insert(name, image.convertToFormat(QImage::Format_ARGB32_Premultiplied));
    {
        QMutexLocker lock(&m_shared->mutex);
        m_shared->imageSizes.insert(name, image.size());
    }
    update();
}

void ScriptCanvas::removeImage(const QString& name)
{
    m_layers->images.remove(name);
    {
        QMutexLocker lock(&m_shared->mutex);
        m_shared->imageSizes.remove(name);
    }
    update();
}

QStringList ScriptCanvas::images() const
{
    QStringList names = m_layers->images.keys();
    names.sort();
    return names;
}

void ScriptCanvas::addDrawFunction(const QString& name, DrawFunction painter, const QString& declaration,
                                   BoundsFunction bounds)
{
    static const QRegularExpression identifier(QStringLiteral("^[A-Za-z_$][\\w$]*$"));
    if (!identifier.match(name).hasMatch() || !painter) {
        qWarning("ScriptHost::ScriptCanvas: a draw function needs a name that is an identifier and a painter "
                 "function: '%s'", qPrintable(name));
        return;
    }
    m_layers->draws.insert(name, {std::move(painter), std::move(bounds)});
    {
        QMutexLocker lock(&m_shared->mutex);
        m_shared->drawDeclarations.insert(name, declaration.trimmed().isEmpty()
            ? QStringLiteral("function %1(...args: any[]): void;").arg(name) : declaration);
    }
    if (m_extension)
        emit m_extension->changed();
    update();
}

void ScriptCanvas::removeDrawFunction(const QString& name)
{
    if (!m_layers->draws.remove(name))
        return;
    {
        QMutexLocker lock(&m_shared->mutex);
        m_shared->drawDeclarations.remove(name);
    }
    if (m_extension)
        emit m_extension->changed();
    update();
}

QStringList ScriptCanvas::drawFunctions() const
{
    QStringList names = m_layers->draws.keys();
    names.sort();
    return names;
}

// ---- painting --------------------------------------------------------------

void ScriptCanvas::paintEvent(QPaintEvent* event)
{
    if (m_layers->frames.isEmpty())
        return;
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF area(event->rect());
    for (const CanvasFrame& frame : std::as_const(m_layers->frames)) {
        if (frame.all().intersects(area))
            paintFrame(painter, frame, area, *m_layers);
    }
}

void ScriptCanvas::takeFrames()
{
    struct Taken
    {
        ScriptEngine::RunId id;
        QRectF              old;
    };
    QVarLengthArray<Taken, 8> taken;
    {
        QMutexLocker lock(&m_shared->mutex);
        m_shared->flushQueued = false;
        for (auto it = m_shared->pending.begin(); it != m_shared->pending.end(); ++it) {
            if (!it->fresh)
                continue;
            it->fresh = false;
            CanvasFrame& shown = m_layers->frames[it.key()];
            taken.append({it.key(), shown.all()});
            std::swap(shown, it->frame);
        }
    }
    // Raising repaints all of this widget: only when something came over it.
    if (!taken.isEmpty() && !onTop())
        raise();
    for (const Taken& t : taken) {
        CanvasFrame& frame = m_layers->frames[t.id];
        layOut(frame);
        resolveAnchors(frame);
        const QRect dirty = (t.old | frame.all()).toAlignedRect();
        update(dirty);
        emit presented(t.id, dirty);
    }
}

void ScriptCanvas::removeLayer(ScriptEngine::RunId id)
{
    const auto it = m_layers->frames.find(id);
    if (it == m_layers->frames.end())
        return;
    const QRect dirty = it->all().toAlignedRect();
    m_layers->frames.erase(it);
    update(dirty);
}

void ScriptCanvas::layOut(CanvasFrame& frame)
{
    if (frame.laidOut)
        return;
    frame.laidOut = true;
    int fontStyle = -1;
    QFontMetricsF metrics{QFont()};
    for (CanvasItem& item : frame.items) {
        if (item.anchor >= 0 || item.anchor2 >= 0 || (item.op != CanvasItem::Text && item.op != CanvasItem::Custom))
            continue;
        const CanvasStyle& style = frame.styles[size_t(item.style)];
        QRectF box;
        if (item.op == CanvasItem::Text) {
            if (item.style != fontStyle) {
                metrics = QFontMetricsF(style.font());
                fontStyle = item.style;
            }
            box = placeText(metrics, style, frame.texts.at(item.extra), frame.coords[size_t(item.first)],
                            frame.coords[size_t(item.first) + 1]);
        } else {
            const CanvasCall& call = frame.calls[size_t(item.extra)];
            const auto draw = m_layers->draws.constFind(call.name);
            if (draw == m_layers->draws.constEnd())
                continue;           // not drawn
            if (!draw->bounds) {
                item.bounds = kEverywhere;
                frame.bounds |= kEverywhere;
                continue;
            }
            const double m = style.margin();
            box = draw->bounds(call.args).normalized().adjusted(-m, -m, m, m);
        }
        if (item.transform >= 0)
            box = frame.transforms[size_t(item.transform)].mapRect(box);
        item.bounds = box;
        frame.bounds |= box;
    }
}

// ---- anchors ---------------------------------------------------------------

QWidget* ScriptCanvas::anchorWidget(const QString& name)
{
    QWidget* target = parentWidget();
    QPointer<QWidget>& found = m_layers->widgets[name];
    if (found && found->objectName() == name && target->isAncestorOf(found))
        return found;
    found = nullptr;
    const QList<QWidget*> candidates = target->findChildren<QWidget*>(name);
    for (QWidget* candidate : candidates) {
        if (candidate != this && !candidate->isWindow()) {
            found = candidate;
            break;
        }
    }
    watch(found);
    return found;
}

void ScriptCanvas::watch(QWidget* widget)
{
    QWidget* target = parentWidget();
    for (QWidget* w = widget; w && w != target; w = w->parentWidget()) {
        if (m_layers->watched.contains(w))
            break;
        m_layers->watched.insert(w);
        w->installEventFilter(this);
        connect(w, &QObject::destroyed, this, [this, w]() {
            m_layers->watched.remove(w);
            // Later: its parent may be going too.
            QMetaObject::invokeMethod(this, [this]() { anchorsMoved(nullptr); }, Qt::QueuedConnection);
        });
    }
}

void ScriptCanvas::watchPaints(QWidget* widget)
{
    if (m_layers->painted.contains(widget))
        return;
    m_layers->painted.insert(widget);
    widget->installEventFilter(this);
    connect(widget, &QObject::destroyed, this, [this, widget]() { m_layers->painted.remove(widget); });
}

QWidget* ScriptCanvas::anchoredWidget(const QString& key) const
{
    if (key.startsWith(kItemAnchor))
        return m_layers->items.value(key).found;
    return m_layers->widgets.value(key);
}

bool ScriptCanvas::anchorRect(const QString& key, QRectF& rect, QRectF* clip)
{
    if (key.startsWith(kItemAnchor))
        return itemRect(key, rect, clip);
    QWidget* widget = anchorWidget(key);
    if (!widget || !widget->isVisibleTo(parentWidget()))
        return false;
    rect = QRectF(QRect(widget->mapTo(parentWidget(), QPoint()), widget->size()));
    return true;
}

bool ScriptCanvas::itemRect(const QString& key, QRectF& rect, QRectF* clip)
{
    QWidget* target = parentWidget();
    auto it = m_layers->items.find(key);
    if (it == m_layers->items.end()) {
        CanvasAnchor anchor;
        anchor.ref = QJsonDocument::fromJson(key.mid(1).toUtf8()).object();    // made by the glue
        anchor.widget = anchor.ref.take(QStringLiteral("widget"));
        anchor.clip = anchor.ref.take(QStringLiteral("clip")).toBool(true);
        it = m_layers->items.insert(key, anchor);
    }
    CanvasAnchor& anchor = *it;
    QWidget* widget = nullptr;
    if (anchor.widget.isString()) {
        widget = anchorWidget(anchor.widget.toString());
    } else {
        // An id from ui.*: no ScriptUi needed, the widget keeps it.
        widget = anchor.found;
        const int id = anchor.widget.toInt(0);
        if (!widget || !target->isAncestorOf(widget) || widget->property(kWidgetId).toInt() != id) {
            widget = nullptr;
            // ponytail: a scan of the target's children while the id is not
            // found; an id-to-widget map in ScriptUiSupport if that shows up.
            const QList<QWidget*> all = target->findChildren<QWidget*>();
            for (QWidget* candidate : all) {
                const QVariant has = candidate->property(kWidgetId);
                if (candidate != this && has.isValid() && has.toInt() == id) {
                    widget = candidate;
                    break;
                }
            }
            watch(widget);
        }
    }
    anchor.found = widget;
    if (!widget || widget->window() != target->window() || !widget->isVisibleTo(target))
        return false;
    for (const QWidget* w = widget; w != target; w = w->parentWidget()) {
        if (ScriptUiSupport::isExcluded(w))
            return false;
    }
    // A scroll area's items live in its viewport; the parts the style draws
    // (its scroll bars) do not.
    QWidget* area = widget;
    auto* scrollArea = qobject_cast<QAbstractScrollArea*>(widget);
    if (scrollArea && !anchor.ref.isEmpty() && !anchor.ref.contains(QStringLiteral("part")))
        area = scrollArea->viewport();
    watchPaints(widget);
    if (scrollArea)
        watchPaints(scrollArea->viewport());

    std::optional<QRect> local;
    if (anchor.ref.isEmpty()) {
        local = widget->rect();
    } else if (anchor.ref.contains(QStringLiteral("text"))) {
        const QJsonObject range = anchor.ref.value(QStringLiteral("text")).toObject();
        const QList<QRect> lines = ItemGeometry::hasTextPositions(widget)
            ? ItemGeometry::textRects(widget, range.value(QStringLiteral("from")).toInt(),
                                      range.value(QStringLiteral("to")).toInt())
            : QList<QRect>();
        // ponytail: one shape over the range's lines; a shape per line if scripts need it.
        for (const QRect& line : lines)
            local = local ? *local | line : line;
    } else {
        local = ItemGeometry::itemRect(widget, anchor.ref);
    }
    if (!local)
        return false;
    const QRect inArea = local->translated(area->mapFrom(widget, QPoint()));
    if (ItemGeometry::visiblePart(area, inArea).isEmpty())
        return false;
    const QPoint offset = area->mapTo(target, QPoint());
    rect = QRectF(inArea.translated(offset));
    if (clip && anchor.clip)
        *clip = QRectF(ItemGeometry::visiblePart(area, area->rect()).translated(offset));
    return true;
}

void ScriptCanvas::resolveAnchors(CanvasFrame& frame)
{
    frame.anchored = QRectF();
    if (!frame.hasAnchors)
        return;
    int fontStyle = -1;
    QFontMetricsF metrics{QFont()};
    for (CanvasItem& item : frame.items) {
        if (item.anchor < 0 && item.anchor2 < 0)
            continue;
        item.bounds = QRectF();     // not drawn while a widget is missing or hidden
        item.clip = QRectF();
        const CanvasStyle& style = frame.styles[size_t(item.style)];
        QRectF a;
        QRectF b;
        QRectF clip;
        if ((item.anchor >= 0 && !anchorRect(frame.texts.at(item.anchor), a, &clip))
            || (item.anchor2 >= 0 && !anchorRect(frame.texts.at(item.anchor2), b)))
            continue;
        const double pad = style.padding;
        a.adjust(-pad, -pad, pad, pad);
        b.adjust(-pad, -pad, pad, pad);
        double* v = frame.coords.data() + item.first;
        const double m = style.margin();
        switch (item.op) {
        case CanvasItem::Rect:
        case CanvasItem::RoundedRect:
            v[0] = a.x();
            v[1] = a.y();
            v[2] = a.width();
            v[3] = a.height();
            item.bounds = a.adjusted(-m, -m, m, m);
            break;
        case CanvasItem::Ellipse:
            v[0] = a.center().x();
            v[1] = a.center().y();
            v[2] = a.width() / 2;
            v[3] = a.height() / 2;
            item.bounds = a.adjusted(-m, -m, m, m);
            break;
        case CanvasItem::Text:
            if (item.style != fontStyle) {
                metrics = QFontMetricsF(style.font());
                fontStyle = item.style;
            }
            v[0] = style.align == CanvasStyle::Left ? a.left() : style.align == CanvasStyle::Center ? a.center().x()
                                                                                                 : a.right();
            v[1] = style.valign == CanvasStyle::Top ? a.top() : style.valign == CanvasStyle::Middle ? a.center().y()
                                                                                                 : a.bottom();
            item.bounds = placeText(metrics, style, frame.texts.at(item.extra), v[0], v[1]);
            break;
        case CanvasItem::Arrow: {
            const QPointF from = item.anchor >= 0 ? a.center() : QPointF(v[0], v[1]);
            const QPointF to = item.anchor2 >= 0 ? b.center() : QPointF(v[2], v[3]);
            const QPointF p1 = item.anchor >= 0 ? edge(a, to) : from;
            const QPointF p2 = item.anchor2 >= 0 ? edge(b, from) : to;
            v[0] = p1.x();
            v[1] = p1.y();
            v[2] = p2.x();
            v[3] = p2.y();
            const double reach = m + style.arrowHead();
            item.bounds = QRectF(p1, p2).normalized().adjusted(-reach, -reach, reach, reach);
            clip = QRectF();        // it points at the item: not cut
            break;
        }
        default:
            break;
        }
        if (!clip.isNull()) {
            item.clip = clip;
            item.bounds = item.bounds.intersected(clip);
        }
        frame.anchored |= item.bounds;
    }
}

void ScriptCanvas::anchorsMoved(QObject* widget)
{
    QWidget* moved = qobject_cast<QWidget*>(widget);
    for (CanvasFrame& frame : m_layers->frames) {
        if (!frame.hasAnchors)
            continue;
        if (moved) {
            bool holds = false;
            for (const CanvasItem& item : frame.items) {
                for (int anchor : {item.anchor, item.anchor2}) {
                    if (anchor < 0)
                        continue;
                    QWidget* w = anchoredWidget(frame.texts.at(anchor));
                    holds = holds || (w && (w == moved || moved->isAncestorOf(w)));
                }
                if (holds)
                    break;
            }
            if (!holds)
                continue;
        }
        // Repainted only when a place changed: the canvas's own repaint
        // repaints the widgets under it, and their paints come back here.
        QVarLengthArray<QRectF, 32> before;
        for (const CanvasItem& item : frame.items) {
            if (item.anchor >= 0 || item.anchor2 >= 0)
                before << item.bounds << item.clip;
        }
        const QRectF old = frame.anchored;
        resolveAnchors(frame);
        int i = 0;
        bool changed = false;
        for (const CanvasItem& item : frame.items) {
            if (item.anchor < 0 && item.anchor2 < 0)
                continue;
            changed = changed || item.bounds != before[i] || item.clip != before[i + 1];
            i += 2;
        }
        if (changed)
            update((old | frame.anchored).toAlignedRect());
    }
}

// ---- the target and the frame loop -----------------------------------------

bool ScriptCanvas::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == parentWidget()) {
        if (event->type() == QEvent::Resize) {
            followTarget();
        } else if (event->type() == QEvent::ChildAdded && static_cast<QChildEvent*>(event)->child() != this) {
            // Later: the child is still being made.
            QMetaObject::invokeMethod(this, [this]() {
                if (!onTop())
                    raise();
            }, Qt::QueuedConnection);
        }
    } else {
        switch (event->type()) {
        case QEvent::Move:
        case QEvent::Resize:
        case QEvent::Show:
        case QEvent::Hide:
            if (m_layers->watched.contains(watched))
                anchorsMoved(watched);
            break;
        case QEvent::Paint:
            // A scroll, a layout change, a sort, a zoom, a new value: each
            // repaints an item anchor's widget or viewport. Resolved after
            // the paint, once for all paints until then.
            if (m_layers->painted.contains(watched) && !m_layers->resolveQueued) {
                m_layers->resolveQueued = true;
                QMetaObject::invokeMethod(this, [this]() {
                    m_layers->resolveQueued = false;
                    anchorsMoved(nullptr);
                }, Qt::QueuedConnection);
            }
            break;
        default:
            break;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void ScriptCanvas::followTarget()
{
    const QSize size = parentWidget()->size();
    setGeometry(QRect(QPoint(), size));
    bool changed = false;
    {
        QMutexLocker lock(&m_shared->mutex);
        changed = m_shared->size != size;
        m_shared->size = size;
    }
    if (changed && m_engine)
        m_engine->postEvent(QStringLiteral("resize"), QJsonObject{{"width", size.width()}, {"height", size.height()}});
}

bool ScriptCanvas::onTop() const
{
    const QObjectList& siblings = parentWidget()->children();
    for (auto it = siblings.crbegin(); it != siblings.crend(); ++it) {
        if ((*it)->isWidgetType() && !static_cast<QWidget*>(*it)->isWindow())
            return *it == this;
    }
    return true;
}

void ScriptCanvas::startFrames()
{
    QTimer*& timer = m_layers->frameTimer;
    if (!timer) {
        timer = new QTimer(this);
        timer->setTimerType(Qt::PreciseTimer);
        connect(timer, &QTimer::timeout, this, &ScriptCanvas::tickFrames);
    }
    if (timer->isActive())
        return;
    const qreal rate = screen() ? screen()->refreshRate() : 0;
    timer->start(qMax(1, qRound(1000.0 / (rate >= 1 ? rate : 60.0))));
}

// One tick for every run with a frame handler. Coalesced per run: a run
// still busy with its last frame gets one frame waiting, not a queue.
void ScriptCanvas::tickFrames()
{
    QVarLengthArray<CanvasFrameRun, 8> runs;
    {
        QMutexLocker lock(&m_shared->mutex);
        for (const CanvasFrameRun& run : std::as_const(m_shared->frameRuns))
            runs.append(run);
    }
    if (runs.isEmpty()) {
        m_layers->frameTimer->stop();
        return;
    }
    for (const CanvasFrameRun& run : runs) {
        CanvasRunObject* object = run.object;
        run.context.schedule(QStringLiteral("frame"), [object]() { object->frame(); });
    }
}

// ----------------------------------------------------------------------------

ScriptCanvas* attachCanvas(ScriptEngine* engine, QWidget* target)
{
    if (!engine || !target) {
        qWarning("ScriptHost::attachCanvas: the engine and the target must not be null");
        return nullptr;
    }
    return new ScriptCanvas(engine, target);
}

} // namespace ScriptHost
