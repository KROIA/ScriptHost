#include "CanvasExtension.h"
#include "ScriptCanvas.h"
#include <QColor>
#include <QJSValueIterator>
#include <QMetaObject>
#include <QMutexLocker>
#include <cmath>
#include <utility>

namespace ScriptHost
{

// ---- CanvasExtension -------------------------------------------------------

CanvasExtension::CanvasExtension(const QSharedPointer<CanvasShared>& shared)
    : ScriptExtension(QStringLiteral("gfx"))
    , m_shared(shared)
{
}

QStringList CanvasExtension::members() const
{
    return {QStringLiteral("clear"), QStringLiteral("style"), QStringLiteral("rect"),
            QStringLiteral("roundedRect"), QStringLiteral("circle"), QStringLiteral("ellipse"),
            QStringLiteral("line"), QStringLiteral("arrow"), QStringLiteral("polyline"),
            QStringLiteral("polygon"), QStringLiteral("text"), QStringLiteral("rects"),
            QStringLiteral("circles"), QStringLiteral("lines"), QStringLiteral("present"),
            QStringLiteral("width"), QStringLiteral("height")};
}

QString CanvasExtension::declaration() const
{
    return QStringLiteral(
        "/** Drawing over the application's window. Draw a frame, then present() it: it stays on\n"
        "    screen until the next present() and goes when the script ends - keep the script alive\n"
        "    (setInterval, on()) to keep a drawing up. Coordinates in pixels, (0, 0) top left. */\n"
        "declare namespace gfx {\n"
        "  /** What a style leaves out comes from gfx.style(). Colours: \"#rrggbb\", \"#aarrggbb\",\n"
        "      a CSS name or \"none\". Defaults: stroke black, width 1, no fill, font 14 px. */\n"
        "  interface Style {\n"
        "    stroke?: string;\n"
        "    fill?: string;\n"
        "    /** Outline width; 0: no outline. */\n"
        "    width?: number;\n"
        "    dash?: \"solid\" | \"dash\" | \"dot\" | \"dashDot\";\n"
        "    /** 0..1 */\n"
        "    opacity?: number;\n"
        "    /** Arrow head length; default 4 * width, at least 8. */\n"
        "    head?: number;\n"
        "    font?: { family?: string; size?: number; bold?: boolean; italic?: boolean };\n"
        "    /** Where text's (x, y) is: default left, top. */\n"
        "    align?: \"left\" | \"center\" | \"right\";\n"
        "    valign?: \"top\" | \"middle\" | \"bottom\" | \"baseline\";\n"
        "  }\n"
        "  /** The canvas size in pixels. On a resize, on(\"resize\", e => ...) gets {width, height}. */\n"
        "  const width: number;\n"
        "  const height: number;\n"
        "  /** Empties the frame being drawn (what is on screen stays until present()). */\n"
        "  function clear(): void;\n"
        "  /** The defaults for what follows; without an argument the built-in ones. */\n"
        "  function style(style?: Style): void;\n"
        "  function rect(x: number, y: number, w: number, h: number, style?: Style): void;\n"
        "  function roundedRect(x: number, y: number, w: number, h: number, radius: number, style?: Style): void;\n"
        "  function circle(cx: number, cy: number, r: number, style?: Style): void;\n"
        "  function ellipse(cx: number, cy: number, rx: number, ry: number, style?: Style): void;\n"
        "  function line(x1: number, y1: number, x2: number, y2: number, style?: Style): void;\n"
        "  /** The head at (x2, y2), in the stroke colour. */\n"
        "  function arrow(x1: number, y1: number, x2: number, y2: number, style?: Style): void;\n"
        "  /** points: [x0, y0, x1, y1, ...] */\n"
        "  function polyline(points: number[], style?: Style): void;\n"
        "  /** Closed; points: [x0, y0, x1, y1, ...] */\n"
        "  function polygon(points: number[], style?: Style): void;\n"
        "  /** One line of text; its colour is fill, else stroke. */\n"
        "  function text(x: number, y: number, text: string, style?: Style): void;\n"
        "  /** Many rectangles of one style: [x, y, w, h, x, y, w, h, ...]. Faster than rect() each. */\n"
        "  function rects(values: number[], style?: Style): void;\n"
        "  /** Many circles of one style: [cx, cy, r, ...]. */\n"
        "  function circles(values: number[], style?: Style): void;\n"
        "  /** Many lines of one style: [x1, y1, x2, y2, ...]. */\n"
        "  function lines(values: number[], style?: Style): void;\n"
        "  /** Shows the frame drawn since the last present() and starts an empty one. Does not wait. */\n"
        "  function present(): void;\n"
        "}\n");
}

QObject* CanvasExtension::createRunObject(const ScriptRunContext& context)
{
    return new CanvasRunObject(context, m_shared);
}

QString CanvasExtension::glue() const
{
    // Every argument passed: the engine refuses a call with fewer than the
    // native function's parameters.
    return QStringLiteral(
        "(function (n) {\n"
        "    function pts(a, f) {\n"
        "        if (Array.isArray(a)) return a;\n"
        "        if (a !== null && typeof a === 'object' && typeof a.length === 'number')\n"
        "            return Array.prototype.slice.call(a);\n"
        "        throw new TypeError('gfx.' + f + ': expects an array of numbers');\n"
        "    }\n"
        "    var g = {\n"
        "        clear: function () { n.clear(); },\n"
        "        style: function (s) { n.style(s); },\n"
        "        rect: function (x, y, w, h, s) { n.rect(x, y, w, h, s); },\n"
        "        roundedRect: function (x, y, w, h, r, s) { n.roundedRect(x, y, w, h, r, s); },\n"
        "        circle: function (x, y, r, s) { n.circle(x, y, r, s); },\n"
        "        ellipse: function (x, y, rx, ry, s) { n.ellipse(x, y, rx, ry, s); },\n"
        "        line: function (x1, y1, x2, y2, s) { n.line(x1, y1, x2, y2, s); },\n"
        "        arrow: function (x1, y1, x2, y2, s) { n.arrow(x1, y1, x2, y2, s); },\n"
        "        polyline: function (p, s) { n.polyline(pts(p, 'polyline'), s); },\n"
        "        polygon: function (p, s) { n.polygon(pts(p, 'polygon'), s); },\n"
        "        text: function (x, y, t, s) { n.text(x, y, String(t), s); },\n"
        "        rects: function (a, s) { n.rects(pts(a, 'rects'), s); },\n"
        "        circles: function (a, s) { n.circles(pts(a, 'circles'), s); },\n"
        "        lines: function (a, s) { n.lines(pts(a, 'lines'), s); },\n"
        "        present: function () { n.present(); }\n"
        "    };\n"
        "    Object.defineProperty(g, 'width', { get: function () { return n.width(); } });\n"
        "    Object.defineProperty(g, 'height', { get: function () { return n.height(); } });\n"
        "    return Object.freeze(g);\n"
        "})");
}

void CanvasExtension::runEnded(ScriptEngine::RunId id)
{
    ScriptCanvas* canvas = nullptr;
    {
        QMutexLocker lock(&m_shared->mutex);
        m_shared->pending.remove(id);
        canvas = m_shared->canvas;
    }
    // Same thread as the canvas's destructor: it cannot go meanwhile.
    if (canvas)
        canvas->removeLayer(id);
}

// ---- CanvasRunObject: helpers ----------------------------------------------

CanvasRunObject::CanvasRunObject(const ScriptRunContext& context, const QSharedPointer<CanvasShared>& shared)
    : m_context(context)
    , m_shared(shared)
    , m_id(context.runId())
{
}

bool CanvasRunObject::fail(const char* function, const QString& message) const
{
    m_context.throwError(QStringLiteral("gfx.%1: %2").arg(QString::fromLatin1(function), message));
    return false;
}

bool CanvasRunObject::finite(const char* function, std::initializer_list<double> values) const
{
    for (double value : values) {
        if (!std::isfinite(value))
            return fail(function, QStringLiteral("expects numbers"));
    }
    return true;
}

bool CanvasRunObject::finite(const char* function, const QVector<double>& values, int group) const
{
    if (values.size() % group != 0)
        return fail(function, QStringLiteral("the array's length must be a multiple of %1").arg(group));
    for (double value : values) {
        if (!std::isfinite(value))
            return fail(function, QStringLiteral("expects an array of numbers"));
    }
    return true;
}

bool CanvasRunObject::readColour(const QJSValue& value, QRgb& colour, const char* function)
{
    const QString name = value.toString();
    const auto known = m_colours.constFind(name);
    if (known != m_colours.constEnd()) {
        colour = *known;
        return true;
    }
    if (!value.isString())
        return fail(function, QStringLiteral("a colour is a string such as \"#ff0000\" or \"red\""));
    if (name == QLatin1String("none")) {
        colour = 0;
    } else {
#if QT_VERSION >= QT_VERSION_CHECK(6, 4, 0)
        const QColor parsed = QColor::fromString(name);
#else
        const QColor parsed(name);
#endif
        if (!parsed.isValid())
            return fail(function, QStringLiteral("'%1' is not a colour").arg(name));
        colour = parsed.rgba();
    }
    if (m_colours.size() >= 256)
        m_colours.clear();
    m_colours.insert(name, colour);
    return true;
}

bool CanvasRunObject::readStyle(const QJSValue& value, CanvasStyle& style, const char* function)
{
    if (value.isUndefined() || value.isNull())
        return true;
    if (!value.isObject())
        return fail(function, QStringLiteral("the style must be an object"));
    auto number = [&](const QJSValue& v, const QString& key, double& out) {
        const double n = v.toNumber();
        if (!v.isNumber() || !std::isfinite(n) || n < 0)
            return fail(function, QStringLiteral("the style's %1 must be a number, 0 or more").arg(key));
        out = n;
        return true;
    };
    auto choice = [&](const QJSValue& v, const QString& key, std::initializer_list<const char*> names,
                      quint8& out) {
        const QString given = v.toString();
        quint8 index = 0;
        QStringList all;
        for (const char* name : names) {
            if (given == QLatin1String(name)) {
                out = index;
                return true;
            }
            all << QLatin1String(name);
            ++index;
        }
        return fail(function, QStringLiteral("the style's %1 is one of %2").arg(key, all.join(QStringLiteral(", "))));
    };
    QJSValueIterator it(value);
    while (it.hasNext()) {
        it.next();
        const QString key = it.name();
        const QJSValue v = it.value();
        if (v.isUndefined())
            continue;
        bool ok = true;
        if (key == QLatin1String("stroke")) {
            ok = readColour(v, style.stroke, function);
        } else if (key == QLatin1String("fill")) {
            ok = readColour(v, style.fill, function);
        } else if (key == QLatin1String("width")) {
            ok = number(v, key, style.width);
        } else if (key == QLatin1String("opacity")) {
            ok = number(v, key, style.opacity);
            style.opacity = qMin(style.opacity, 1.0);
        } else if (key == QLatin1String("head")) {
            ok = number(v, key, style.head);
        } else if (key == QLatin1String("dash")) {
            quint8 dash = 0;
            ok = choice(v, key, {"solid", "dash", "dot", "dashDot"}, dash);
            style.dash = quint8(Qt::SolidLine + dash);
        } else if (key == QLatin1String("align")) {
            ok = choice(v, key, {"left", "center", "right"}, style.align);
        } else if (key == QLatin1String("valign")) {
            ok = choice(v, key, {"top", "middle", "bottom", "baseline"}, style.valign);
        } else if (key == QLatin1String("font")) {
            if (!v.isObject())
                return fail(function, QStringLiteral("the style's font must be an object {family, size, bold, italic}"));
            QJSValueIterator font(v);
            while (ok && font.hasNext()) {
                font.next();
                const QString part = font.name();
                const QJSValue f = font.value();
                if (part == QLatin1String("family"))
                    style.fontFamily = f.toString();
                else if (part == QLatin1String("size"))
                    ok = number(f, QStringLiteral("font size"), style.fontSize);
                else if (part == QLatin1String("bold"))
                    style.bold = f.toBool();
                else if (part == QLatin1String("italic"))
                    style.italic = f.toBool();
                else
                    ok = fail(function, QStringLiteral("unknown font field '%1'").arg(part));
            }
        } else {
            ok = fail(function, QStringLiteral("unknown style '%1'").arg(key));
        }
        if (!ok)
            return false;
    }
    return true;
}

int CanvasRunObject::addStyle(const CanvasStyle& style)
{
    if (m_frame.styles.empty() || m_frame.styles.back() != style)
        m_frame.styles.push_back(style);
    return int(m_frame.styles.size()) - 1;
}

int CanvasRunObject::styleFor(const QJSValue& value, const char* function)
{
    if (value.isUndefined() || value.isNull()) {
        if (m_baseIndex < 0)
            m_baseIndex = addStyle(m_base);
        return m_baseIndex;
    }
    CanvasStyle style = m_base;
    return readStyle(value, style, function) ? addStyle(style) : -1;
}

void CanvasRunObject::add(quint8 op, int style, const double* values, int count, const QRectF& bounds, int text)
{
    CanvasItem item;
    item.op = op;
    item.style = style;
    item.first = int(m_frame.coords.size());
    item.count = count;
    item.text = text;
    item.bounds = bounds;
    m_frame.coords.insert(m_frame.coords.end(), values, values + count);
    m_frame.items.push_back(item);
    if (!bounds.isNull())
        m_frame.bounds |= bounds;
}

void CanvasRunObject::addPath(quint8 op, const QVector<double>& points, const QJSValue& style, const char* function)
{
    if (!finite(function, points, 2))
        return;
    if (points.size() < 4)
        return void(fail(function, QStringLiteral("expects at least 2 points: [x0, y0, x1, y1, ...]")));
    const int s = styleFor(style, function);
    if (s < 0)
        return;
    double left = points[0], right = left, top = points[1], bottom = top;
    for (int i = 2; i < points.size(); i += 2) {
        left = qMin(left, points[i]);
        right = qMax(right, points[i]);
        top = qMin(top, points[i + 1]);
        bottom = qMax(bottom, points[i + 1]);
    }
    // A miter join reaches further than the margin; polylines are joined bevelled.
    const double m = m_frame.styles[size_t(s)].margin();
    add(op, s, points.constData(), int(points.size()), QRectF(left, top, right - left, bottom - top).adjusted(-m, -m, m, m));
}

// ---- CanvasRunObject: the script's calls ----------------------------------

void CanvasRunObject::clear()
{
    m_frame.clear();
    m_baseIndex = -1;
}

void CanvasRunObject::style(const QJSValue& style)
{
    CanvasStyle base;
    if (readStyle(style, base, "style")) {
        m_base = base;
        m_baseIndex = -1;
    }
}

void CanvasRunObject::rect(double x, double y, double w, double h, const QJSValue& style)
{
    if (!finite("rect", {x, y, w, h}))
        return;
    const int s = styleFor(style, "rect");
    if (s < 0)
        return;
    const double m = m_frame.styles[size_t(s)].margin();
    const double values[] = {x, y, w, h};
    add(CanvasItem::Rect, s, values, 4, QRectF(x, y, w, h).normalized().adjusted(-m, -m, m, m));
}

void CanvasRunObject::roundedRect(double x, double y, double w, double h, double radius, const QJSValue& style)
{
    if (!finite("roundedRect", {x, y, w, h, radius}))
        return;
    const int s = styleFor(style, "roundedRect");
    if (s < 0)
        return;
    const double m = m_frame.styles[size_t(s)].margin();
    const double values[] = {x, y, w, h, radius};
    add(CanvasItem::RoundedRect, s, values, 5, QRectF(x, y, w, h).normalized().adjusted(-m, -m, m, m));
}

void CanvasRunObject::circle(double cx, double cy, double r, const QJSValue& style)
{
    addEllipse(cx, cy, r, r, style, "circle");
}

void CanvasRunObject::ellipse(double cx, double cy, double rx, double ry, const QJSValue& style)
{
    addEllipse(cx, cy, rx, ry, style, "ellipse");
}

void CanvasRunObject::addEllipse(double cx, double cy, double rx, double ry, const QJSValue& style,
                                 const char* function)
{
    if (!finite(function, {cx, cy, rx, ry}))
        return;
    const int s = styleFor(style, function);
    if (s < 0)
        return;
    rx = std::abs(rx);
    ry = std::abs(ry);
    const double m = m_frame.styles[size_t(s)].margin();
    const double values[] = {cx, cy, rx, ry};
    add(CanvasItem::Ellipse, s, values, 4, QRectF(cx - rx - m, cy - ry - m, 2 * (rx + m), 2 * (ry + m)));
}

void CanvasRunObject::line(double x1, double y1, double x2, double y2, const QJSValue& style)
{
    if (!finite("line", {x1, y1, x2, y2}))
        return;
    const int s = styleFor(style, "line");
    if (s < 0)
        return;
    const double m = m_frame.styles[size_t(s)].margin();
    const double values[] = {x1, y1, x2, y2};
    add(CanvasItem::Line, s, values, 4, QRectF(QPointF(x1, y1), QPointF(x2, y2)).normalized().adjusted(-m, -m, m, m));
}

void CanvasRunObject::arrow(double x1, double y1, double x2, double y2, const QJSValue& style)
{
    if (!finite("arrow", {x1, y1, x2, y2}))
        return;
    const int s = styleFor(style, "arrow");
    if (s < 0)
        return;
    const CanvasStyle& st = m_frame.styles[size_t(s)];
    const double m = st.margin() + st.arrowHead();
    const double values[] = {x1, y1, x2, y2};
    add(CanvasItem::Arrow, s, values, 4, QRectF(QPointF(x1, y1), QPointF(x2, y2)).normalized().adjusted(-m, -m, m, m));
}

void CanvasRunObject::polyline(const QVector<double>& points, const QJSValue& style)
{
    addPath(CanvasItem::Polyline, points, style, "polyline");
}

void CanvasRunObject::polygon(const QVector<double>& points, const QJSValue& style)
{
    addPath(CanvasItem::Polygon, points, style, "polygon");
}

void CanvasRunObject::text(double x, double y, const QString& text, const QJSValue& style)
{
    if (!finite("text", {x, y}))
        return;
    const int s = styleFor(style, "text");
    if (s < 0)
        return;
    const double values[] = {x, y};
    // Its bounds need the font's metrics: on the GUI thread, once presented.
    m_frame.texts.append(text);
    add(CanvasItem::Text, s, values, 2, QRectF(), int(m_frame.texts.size()) - 1);
}

void CanvasRunObject::rects(const QVector<double>& values, const QJSValue& style)
{
    if (!finite("rects", values, 4))
        return;
    const int s = styleFor(style, "rects");
    if (s < 0)
        return;
    const double m = m_frame.styles[size_t(s)].margin();
    for (int i = 0; i < values.size(); i += 4) {
        const double* v = values.constData() + i;
        add(CanvasItem::Rect, s, v, 4, QRectF(v[0], v[1], v[2], v[3]).normalized().adjusted(-m, -m, m, m));
    }
}

void CanvasRunObject::circles(const QVector<double>& values, const QJSValue& style)
{
    if (!finite("circles", values, 3))
        return;
    const int s = styleFor(style, "circles");
    if (s < 0)
        return;
    const double m = m_frame.styles[size_t(s)].margin();
    for (int i = 0; i < values.size(); i += 3) {
        const double r = std::abs(values[i + 2]);
        const double v[] = {values[i], values[i + 1], r, r};
        add(CanvasItem::Ellipse, s, v, 4, QRectF(v[0] - r - m, v[1] - r - m, 2 * (r + m), 2 * (r + m)));
    }
}

void CanvasRunObject::lines(const QVector<double>& values, const QJSValue& style)
{
    if (!finite("lines", values, 4))
        return;
    const int s = styleFor(style, "lines");
    if (s < 0)
        return;
    const double m = m_frame.styles[size_t(s)].margin();
    for (int i = 0; i < values.size(); i += 4) {
        const double* v = values.constData() + i;
        add(CanvasItem::Line, s, v, 4,
            QRectF(QPointF(v[0], v[1]), QPointF(v[2], v[3])).normalized().adjusted(-m, -m, m, m));
    }
}

void CanvasRunObject::present()
{
    {
        QMutexLocker lock(&m_shared->mutex);
        if (ScriptCanvas* canvas = m_shared->canvas) {
            // The frame goes over; what comes back - the canvas's previous
            // one, or a frame it never took - is recycled.
            CanvasPending& pending = m_shared->pending[m_id];
            std::swap(pending.frame, m_frame);
            pending.fresh = true;
            if (!m_shared->flushQueued) {
                m_shared->flushQueued = true;
                QMetaObject::invokeMethod(canvas, [canvas]() { canvas->takeFrames(); }, Qt::QueuedConnection);
            }
        }
    }
    clear();
}

double CanvasRunObject::width() const
{
    QMutexLocker lock(&m_shared->mutex);
    return m_shared->size.width();
}

double CanvasRunObject::height() const
{
    QMutexLocker lock(&m_shared->mutex);
    return m_shared->size.height();
}

} // namespace ScriptHost
