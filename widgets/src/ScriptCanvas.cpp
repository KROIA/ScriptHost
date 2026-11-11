#include "ScriptCanvas.h"
#include "CanvasExtension.h"
#include <QChildEvent>
#include <QFontMetricsF>
#include <QJsonObject>
#include <QMap>
#include <QMutexLocker>
#include <QPaintEvent>
#include <QPainter>
#include <QVarLengthArray>
#include <QVector>
#include <utility>

namespace ScriptHost
{

// What is on screen: the newest frame each run presented, by run - in
// run-start order, as RunIds count up.
struct CanvasLayers
{
    QMap<ScriptEngine::RunId, CanvasFrame> frames;
    QVector<QPointF>                       points;     // scratch for paths
};

namespace
{

// Engine property: the canvas whose `gfx` the engine has.
const char* const kOwner = "scripthost_canvas";

QFont fontOf(const CanvasStyle& style)
{
    QFont font;
    if (!style.fontFamily.isEmpty())
        font.setFamily(style.fontFamily);
    font.setPixelSize(qMax(1, qRound(style.fontSize)));
    font.setBold(style.bold);
    font.setItalic(style.italic);
    return font;
}

// Places the texts: their anchor becomes the baseline start, and they get
// their bounds. Needs the fonts, so here on the GUI thread.
// ponytail: one line per text, no wrapping; QTextLayout when scripts need more.
void layOut(CanvasFrame& frame)
{
    if (frame.laidOut || frame.texts.isEmpty()) {
        frame.laidOut = true;
        return;
    }
    frame.laidOut = true;
    int fontStyle = -1;
    QFontMetricsF metrics{QFont()};
    for (CanvasItem& item : frame.items) {
        if (item.op != CanvasItem::Text)
            continue;
        const CanvasStyle& style = frame.styles[size_t(item.style)];
        if (item.style != fontStyle) {
            metrics = QFontMetricsF(fontOf(style));
            fontStyle = item.style;
        }
        const QString& text = frame.texts.at(item.text);
        const double w = metrics.horizontalAdvance(text);
        const double ascent = metrics.ascent();
        const double h = metrics.height();
        double& x = frame.coords[size_t(item.first)];
        double& y = frame.coords[size_t(item.first) + 1];
        x -= style.align == CanvasStyle::Center ? w / 2 : style.align == CanvasStyle::Right ? w : 0;
        switch (style.valign) {
        case CanvasStyle::Top:      y += ascent; break;
        case CanvasStyle::Middle:   y += ascent - h / 2; break;
        case CanvasStyle::Bottom:   y += ascent - h; break;
        default:                    break;
        }
        // Italic overhang and antialiasing.
        const double m = 2 + style.fontSize / 4;
        item.bounds = QRectF(x, y - ascent, w, h).adjusted(-m, -m, m, m);
        frame.bounds |= item.bounds;
    }
}

void applyStyle(QPainter& painter, const CanvasStyle& style)
{
    if (qAlpha(style.stroke) && style.width > 0)
        painter.setPen(QPen(QColor::fromRgba(style.stroke), style.width, Qt::PenStyle(style.dash)));
    else
        painter.setPen(Qt::NoPen);
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

void paintFrame(QPainter& painter, const CanvasFrame& frame, const QRectF& area, QVector<QPointF>& points)
{
    int current = -1;
    for (const CanvasItem& item : frame.items) {
        if (!item.bounds.intersects(area))
            continue;
        const CanvasStyle& style = frame.styles[size_t(item.style)];
        if (item.style != current) {
            applyStyle(painter, style);
            current = item.style;
        }
        const double* v = frame.coords.data() + item.first;
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
        case CanvasItem::Polygon:
            points.resize(item.count / 2);
            for (int i = 0; i < points.size(); ++i)
                points[i] = QPointF(v[2 * i], v[2 * i + 1]);
            if (item.op == CanvasItem::Polyline)
                painter.drawPolyline(points.constData(), int(points.size()));
            else
                painter.drawPolygon(points.constData(), int(points.size()));
            break;
        case CanvasItem::Text:
            painter.setPen(QColor::fromRgba(qAlpha(style.fill) ? style.fill : style.stroke));
            painter.setFont(fontOf(style));
            painter.drawText(QPointF(v[0], v[1]), frame.texts.at(item.text));
            current = -1;
            break;
        default:
            break;
        }
    }
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

void ScriptCanvas::paintEvent(QPaintEvent* event)
{
    if (m_layers->frames.isEmpty())
        return;
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF area(event->rect());
    for (const CanvasFrame& frame : std::as_const(m_layers->frames)) {
        if (frame.bounds.intersects(area))
            paintFrame(painter, frame, area, m_layers->points);
    }
}

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
            taken.append({it.key(), shown.bounds});
            std::swap(shown, it->frame);
        }
    }
    // Raising repaints all of this widget: only when something came over it.
    if (!taken.isEmpty() && !onTop())
        raise();
    for (const Taken& t : taken) {
        CanvasFrame& frame = m_layers->frames[t.id];
        layOut(frame);
        const QRect dirty = (t.old | frame.bounds).toAlignedRect();
        update(dirty);
        emit presented(t.id, dirty);
    }
}

void ScriptCanvas::removeLayer(ScriptEngine::RunId id)
{
    const auto it = m_layers->frames.find(id);
    if (it == m_layers->frames.end())
        return;
    const QRect dirty = it->bounds.toAlignedRect();
    m_layers->frames.erase(it);
    update(dirty);
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
