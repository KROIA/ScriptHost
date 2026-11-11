#include "CanvasExtension.h"
#include "ScriptCanvas.h"
#include <QColor>
#include <QFontMetricsF>
#include <QJSValueIterator>
#include <QMetaObject>
#include <QMutexLocker>
#include <QSemaphore>
#include <cmath>
#include <utility>

namespace ScriptHost
{

// ---- CanvasExtension -------------------------------------------------------

namespace
{

const char* const kEasings[] = {"linear", "inQuad", "outQuad", "inOutQuad", "inCubic", "outCubic",
                                "inOutCubic", "outBack", "outBounce"};

} // namespace

CanvasExtension::CanvasExtension(const QSharedPointer<CanvasShared>& shared)
    : ScriptExtension(QStringLiteral("gfx"))
    , m_shared(shared)
{
}

QStringList CanvasExtension::members() const
{
    QStringList names{
        QStringLiteral("clear"), QStringLiteral("style"), QStringLiteral("save"), QStringLiteral("restore"),
        QStringLiteral("translate"), QStringLiteral("rotate"), QStringLiteral("scale"),
        QStringLiteral("resetTransform"), QStringLiteral("rect"), QStringLiteral("roundedRect"),
        QStringLiteral("circle"), QStringLiteral("ellipse"), QStringLiteral("line"), QStringLiteral("arrow"),
        QStringLiteral("polyline"), QStringLiteral("polygon"), QStringLiteral("text"), QStringLiteral("rects"),
        QStringLiteral("circles"), QStringLiteral("lines"), QStringLiteral("path"), QStringLiteral("image"),
        QStringLiteral("imageSize"), QStringLiteral("widget"), QStringLiteral("widgetRect"),
        QStringLiteral("onFrame"), QStringLiteral("offFrame"), QStringLiteral("linearGradient"),
        QStringLiteral("radialGradient"), QStringLiteral("measureText"), QStringLiteral("pointInRect"),
        QStringLiteral("pointInCircle"), QStringLiteral("rectsOverlap"), QStringLiteral("circlesOverlap"),
        QStringLiteral("tween"), QStringLiteral("present"), QStringLiteral("width"), QStringLiteral("height")};
    for (const char* easing : kEasings)
        names << QStringLiteral("ease.") + QLatin1String(easing);
    QMutexLocker lock(&m_shared->mutex);
    for (auto it = m_shared->drawDeclarations.cbegin(); it != m_shared->drawDeclarations.cend(); ++it)
        names << QStringLiteral("draw.") + it.key();
    return names;
}

QString CanvasExtension::declaration() const
{
    QString draw;
    {
        QMutexLocker lock(&m_shared->mutex);
        for (auto it = m_shared->drawDeclarations.cbegin(); it != m_shared->drawDeclarations.cend(); ++it)
            draw += QStringLiteral("    ") + it.value().trimmed() + QLatin1Char('\n');
    }
    if (!draw.isEmpty()) {
        draw = QStringLiteral("  /** Drawing functions of the application; they use the style and the transform. */\n"
                              "  namespace draw {\n")
               + draw + QStringLiteral("  }\n");
    }
    return QStringLiteral(
        "/** Drawing over the application's window. Draw a frame, then present() it: it stays on\n"
        "    screen until the next present() and goes when the script ends - keep the script alive\n"
        "    (onFrame, setInterval, on()) to keep a drawing up. Coordinates in pixels, (0, 0) top left. */\n"
        "declare namespace gfx {\n"
        "  /** What a style leaves out comes from gfx.style(). Colours: \"#rrggbb\", \"#aarrggbb\",\n"
        "      a CSS name or \"none\". Defaults: stroke black, width 1, no fill, font 14 px. */\n"
        "  interface Style {\n"
        "    stroke?: string;\n"
        "    /** A colour, or a gradient from linearGradient() / radialGradient(). */\n"
        "    fill?: string | Gradient;\n"
        "    /** Outline width; 0: no outline. */\n"
        "    width?: number;\n"
        "    dash?: \"solid\" | \"dash\" | \"dot\" | \"dashDot\";\n"
        "    /** 0..1 */\n"
        "    opacity?: number;\n"
        "    /** Arrow head length; default 4 * width, at least 8. */\n"
        "    head?: number;\n"
        "    /** Space around an anchor widget. */\n"
        "    padding?: number;\n"
        "    font?: Font;\n"
        "    /** Where text's (x, y) is: default left, top. */\n"
        "    align?: \"left\" | \"center\" | \"right\";\n"
        "    valign?: \"top\" | \"middle\" | \"bottom\" | \"baseline\";\n"
        "  }\n"
        "  interface Font { family?: string; size?: number; bold?: boolean; italic?: boolean }\n"
        "  interface Gradient { readonly gradient: \"linear\" | \"radial\" }\n"
        "  /** A widget of the window by its objectName, found when drawn: shapes drawn on it\n"
        "      follow it, and are not drawn while it is hidden or missing. */\n"
        "  interface Widget { readonly widget: string }\n"
        "  interface Path {\n"
        "    moveTo(x: number, y: number): Path;\n"
        "    lineTo(x: number, y: number): Path;\n"
        "    quadTo(cx: number, cy: number, x: number, y: number): Path;\n"
        "    cubicTo(c1x: number, c1y: number, c2x: number, c2y: number, x: number, y: number): Path;\n"
        "    /** Angles in radians, clockwise from 3 o'clock. */\n"
        "    arc(cx: number, cy: number, r: number, start: number, end: number, counterclockwise?: boolean): Path;\n"
        "    close(): Path;\n"
        "    /** Adds it to the frame: outlined with stroke, filled with fill. Can be drawn again. */\n"
        "    draw(style?: Style): void;\n"
        "  }\n"
        "  interface Tween { value: number; done: boolean; step(dtMs: number): number; reset(): void }\n"
        "  /** The canvas size in pixels. On a resize, on(\"resize\", e => ...) gets {width, height}. */\n"
        "  const width: number;\n"
        "  const height: number;\n"
        "  /** Empties the frame being drawn (what is on screen stays until present()). */\n"
        "  function clear(): void;\n"
        "  /** The defaults for what follows; without an argument the built-in ones. */\n"
        "  function style(style?: Style): void;\n"
        "  /** Keeps the transform and gfx.style() until restore(). */\n"
        "  function save(): void;\n"
        "  function restore(): void;\n"
        "  /** Transforms what follows; they stay across frames: save() / restore() or resetTransform(). */\n"
        "  function translate(x: number, y: number): void;\n"
        "  /** Radians, clockwise. */\n"
        "  function rotate(angle: number): void;\n"
        "  function scale(x: number, y?: number): void;\n"
        "  function resetTransform(): void;\n"
        "  function rect(x: number, y: number, w: number, h: number, style?: Style): void;\n"
        "  /** Around a widget, `padding` away. */\n"
        "  function rect(widget: Widget, style?: Style): void;\n"
        "  function roundedRect(x: number, y: number, w: number, h: number, radius: number, style?: Style): void;\n"
        "  function roundedRect(widget: Widget, radius: number, style?: Style): void;\n"
        "  function circle(cx: number, cy: number, r: number, style?: Style): void;\n"
        "  function ellipse(cx: number, cy: number, rx: number, ry: number, style?: Style): void;\n"
        "  function ellipse(widget: Widget, style?: Style): void;\n"
        "  function line(x1: number, y1: number, x2: number, y2: number, style?: Style): void;\n"
        "  /** The head at the end, in the stroke colour. An end at a widget is on its edge. */\n"
        "  function arrow(x1: number, y1: number, x2: number, y2: number, style?: Style): void;\n"
        "  function arrow(x1: number, y1: number, to: Widget, style?: Style): void;\n"
        "  function arrow(from: Widget, x2: number, y2: number, style?: Style): void;\n"
        "  function arrow(from: Widget, to: Widget, style?: Style): void;\n"
        "  /** points: [x0, y0, x1, y1, ...] */\n"
        "  function polyline(points: number[], style?: Style): void;\n"
        "  /** Closed; points: [x0, y0, x1, y1, ...] */\n"
        "  function polygon(points: number[], style?: Style): void;\n"
        "  /** One line of text; its colour is fill, else stroke. */\n"
        "  function text(x: number, y: number, text: string, style?: Style): void;\n"
        "  /** In a widget, placed by align / valign. */\n"
        "  function text(widget: Widget, text: string, style?: Style): void;\n"
        "  /** Many rectangles of one style: [x, y, w, h, x, y, w, h, ...]. Faster than rect() each. */\n"
        "  function rects(values: number[], style?: Style): void;\n"
        "  /** Many circles of one style: [cx, cy, r, ...]. */\n"
        "  function circles(values: number[], style?: Style): void;\n"
        "  /** Many lines of one style: [x1, y1, x2, y2, ...]. */\n"
        "  function lines(values: number[], style?: Style): void;\n"
        "  /** A path to build, then draw(). */\n"
        "  function path(): Path;\n"
        "  /** An image the application added, by name: at its size, or scaled to w x h; `source` cuts\n"
        "      a part out of it (a sprite of a sheet). Style: opacity. */\n"
        "  function image(name: string, x: number, y: number, w?: number, h?: number,\n"
        "                 source?: { sx?: number; sy?: number; sw?: number; sh?: number }, style?: Style): void;\n"
        "  /** null: no such image. */\n"
        "  function imageSize(name: string): { width: number; height: number } | null;\n"
        "  function widget(objectName: string): Widget;\n"
        "  /** Where the widget is now on the canvas; null when it is missing or hidden. Asks the\n"
        "      application's thread: do not call it for every shape. */\n"
        "  function widgetRect(objectName: string): { x: number; y: number; width: number; height: number } | null;\n"
        "  /** `handler(dt)` once per screen refresh, dt in milliseconds since the last frame. A slow\n"
        "      handler skips frames. Keeps the script running until offFrame(). */\n"
        "  function onFrame(handler: (dt: number) => void): void;\n"
        "  function offFrame(): void;\n"
        "  /** stops: [[offset 0..1, colour], ...] */\n"
        "  function linearGradient(x0: number, y0: number, x1: number, y1: number, stops: [number, string][]): Gradient;\n"
        "  function radialGradient(cx: number, cy: number, r: number, stops: [number, string][],\n"
        "                          fx?: number, fy?: number): Gradient;\n"
        "  /** Without a font: gfx.style()'s font. */\n"
        "  function measureText(text: string, font?: Font): { width: number; height: number; ascent: number; descent: number };\n"
        "  function pointInRect(px: number, py: number, x: number, y: number, w: number, h: number): boolean;\n"
        "  function pointInCircle(px: number, py: number, cx: number, cy: number, r: number): boolean;\n"
        "  function rectsOverlap(x1: number, y1: number, w1: number, h1: number,\n"
        "                        x2: number, y2: number, w2: number, h2: number): boolean;\n"
        "  function circlesOverlap(x1: number, y1: number, r1: number, x2: number, y2: number, r2: number): boolean;\n"
        "  /** Easing functions: 0..1 to 0..1. */\n"
        "  const ease: { linear(t: number): number; inQuad(t: number): number; outQuad(t: number): number;\n"
        "                inOutQuad(t: number): number; inCubic(t: number): number; outCubic(t: number): number;\n"
        "                inOutCubic(t: number): number; outBack(t: number): number; outBounce(t: number): number };\n"
        "  /** From `from` to `to` in `ms`: step(dt) moves it on and returns the value. easing: a name\n"
        "      of gfx.ease or a function; default linear. */\n"
        "  function tween(from: number, to: number, ms: number, easing?: string | ((t: number) => number)): Tween;\n"
        "  /** Shows the frame drawn since the last present() and starts an empty one. Does not wait. */\n"
        "  function present(): void;\n")
        + draw + QStringLiteral("}\n");
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
        "    function isW(a) { return a !== null && typeof a === 'object' && typeof a.widget === 'string'; }\n"
        "    function Path() { this.c = []; }\n"
        "    Path.prototype.moveTo = function (x, y) { this.c.push(0, x, y); return this; };\n"
        "    Path.prototype.lineTo = function (x, y) { this.c.push(1, x, y); return this; };\n"
        "    Path.prototype.quadTo = function (cx, cy, x, y) { this.c.push(2, cx, cy, x, y); return this; };\n"
        "    Path.prototype.cubicTo = function (ax, ay, bx, by, x, y) { this.c.push(3, ax, ay, bx, by, x, y); return this; };\n"
        "    Path.prototype.arc = function (cx, cy, r, a0, a1, ccw) { this.c.push(4, cx, cy, r, a0, a1, ccw ? 1 : 0); return this; };\n"
        "    Path.prototype.close = function () { this.c.push(5); return this; };\n"
        "    Path.prototype.draw = function (s) { n.drawPath(this.c, s); };\n"
        "    function gradient(o, f) {\n"
        "        if (!Array.isArray(o.stops)) throw new TypeError('gfx.' + f + ': stops are [[offset, colour], ...]');\n"
        "        o.stops = Object.freeze(o.stops.map(function (s) { return Object.freeze([s[0], s[1]]); }));\n"
        "        return Object.freeze(o);\n"
        "    }\n"
        "    function bounce(t) {\n"
        "        if (t < 1 / 2.75) return 7.5625 * t * t;\n"
        "        if (t < 2 / 2.75) { t -= 1.5 / 2.75; return 7.5625 * t * t + 0.75; }\n"
        "        if (t < 2.5 / 2.75) { t -= 2.25 / 2.75; return 7.5625 * t * t + 0.9375; }\n"
        "        t -= 2.625 / 2.75; return 7.5625 * t * t + 0.984375;\n"
        "    }\n"
        "    var ease = Object.freeze({\n"
        "        linear: function (t) { return t; },\n"
        "        inQuad: function (t) { return t * t; },\n"
        "        outQuad: function (t) { return t * (2 - t); },\n"
        "        inOutQuad: function (t) { return t < 0.5 ? 2 * t * t : -1 + (4 - 2 * t) * t; },\n"
        "        inCubic: function (t) { return t * t * t; },\n"
        "        outCubic: function (t) { t -= 1; return t * t * t + 1; },\n"
        "        inOutCubic: function (t) { return t < 0.5 ? 4 * t * t * t : (t - 1) * (2 * t - 2) * (2 * t - 2) + 1; },\n"
        "        outBack: function (t) { t -= 1; return 1 + 2.70158 * t * t * t + 1.70158 * t * t; },\n"
        "        outBounce: bounce\n"
        "    });\n"
        "    var draw = {};\n"
        "    n.drawNames().forEach(function (k) {\n"
        "        draw[k] = function () { n.custom(k, Array.prototype.slice.call(arguments)); };\n"
        "    });\n"
        "    var g = {\n"
        "        clear: function () { n.clear(); },\n"
        "        style: function (s) { n.style(s); },\n"
        "        save: function () { n.save(); },\n"
        "        restore: function () { n.restore(); },\n"
        "        translate: function (x, y) { n.translate(x, y); },\n"
        "        rotate: function (a) { n.rotate(a); },\n"
        "        scale: function (x, y) { n.scale(x, y === undefined ? x : y); },\n"
        "        resetTransform: function () { n.resetTransform(); },\n"
        "        rect: function (x, y, w, h, s) { if (isW(x)) n.rectAt(x.widget, y); else n.rect(x, y, w, h, s); },\n"
        "        roundedRect: function (x, y, w, h, r, s) {\n"
        "            if (isW(x)) n.roundedRectAt(x.widget, y, w); else n.roundedRect(x, y, w, h, r, s);\n"
        "        },\n"
        "        circle: function (x, y, r, s) { n.circle(x, y, r, s); },\n"
        "        ellipse: function (x, y, rx, ry, s) { if (isW(x)) n.ellipseAt(x.widget, y); else n.ellipse(x, y, rx, ry, s); },\n"
        "        line: function (x1, y1, x2, y2, s) { n.line(x1, y1, x2, y2, s); },\n"
        "        arrow: function () {\n"
        "            var a = arguments, i = 0, f = '', t = '', x1 = 0, y1 = 0, x2 = 0, y2 = 0;\n"
        "            if (isW(a[0])) { f = a[0].widget; i = 1; } else { x1 = a[0]; y1 = a[1]; i = 2; }\n"
        "            if (isW(a[i])) { t = a[i].widget; i += 1; } else { x2 = a[i]; y2 = a[i + 1]; i += 2; }\n"
        "            if (f === '' && t === '') n.arrow(x1, y1, x2, y2, a[i]); else n.arrowAt(f, x1, y1, t, x2, y2, a[i]);\n"
        "        },\n"
        "        polyline: function (p, s) { n.polyline(pts(p, 'polyline'), s); },\n"
        "        polygon: function (p, s) { n.polygon(pts(p, 'polygon'), s); },\n"
        "        text: function (x, y, t, s) {\n"
        "            if (isW(x)) n.textAt(x.widget, String(y), t); else n.text(x, y, String(t), s);\n"
        "        },\n"
        "        rects: function (a, s) { n.rects(pts(a, 'rects'), s); },\n"
        "        circles: function (a, s) { n.circles(pts(a, 'circles'), s); },\n"
        "        lines: function (a, s) { n.lines(pts(a, 'lines'), s); },\n"
        "        path: function () { return new Path(); },\n"
        "        image: function (name, x, y, w, h, src, s) { n.image(String(name), x, y, w, h, src, s); },\n"
        "        imageSize: function (name) {\n"
        "            var s = n.imageSize(String(name));\n"
        "            return s.length ? { width: s[0], height: s[1] } : null;\n"
        "        },\n"
        "        widget: function (name) { return Object.freeze({ widget: String(name) }); },\n"
        "        widgetRect: function (name) {\n"
        "            var r = n.widgetRect(isW(name) ? name.widget : String(name));\n"
        "            return r.length ? { x: r[0], y: r[1], width: r[2], height: r[3] } : null;\n"
        "        },\n"
        "        onFrame: function (fn) {\n"
        "            if (typeof fn !== 'function') throw new TypeError('gfx.onFrame: expects a function');\n"
        "            n.onFrame(fn);\n"
        "        },\n"
        "        offFrame: function () { n.offFrame(); },\n"
        "        linearGradient: function (x0, y0, x1, y1, stops) {\n"
        "            return gradient({ gradient: 'linear', x0: x0, y0: y0, x1: x1, y1: y1, stops: stops }, 'linearGradient');\n"
        "        },\n"
        "        radialGradient: function (cx, cy, r, stops, fx, fy) {\n"
        "            return gradient({ gradient: 'radial', cx: cx, cy: cy, r: r, fx: fx === undefined ? cx : fx,\n"
        "                              fy: fy === undefined ? cy : fy, stops: stops }, 'radialGradient');\n"
        "        },\n"
        "        measureText: function (t, font) {\n"
        "            var m = n.measureText(String(t), font);\n"
        "            return { width: m[0], height: m[1], ascent: m[2], descent: m[3] };\n"
        "        },\n"
        "        pointInRect: function (px, py, x, y, w, h) { return px >= x && px < x + w && py >= y && py < y + h; },\n"
        "        pointInCircle: function (px, py, cx, cy, r) {\n"
        "            var dx = px - cx, dy = py - cy; return dx * dx + dy * dy <= r * r;\n"
        "        },\n"
        "        rectsOverlap: function (x1, y1, w1, h1, x2, y2, w2, h2) {\n"
        "            return x1 < x2 + w2 && x2 < x1 + w1 && y1 < y2 + h2 && y2 < y1 + h1;\n"
        "        },\n"
        "        circlesOverlap: function (x1, y1, r1, x2, y2, r2) {\n"
        "            var dx = x1 - x2, dy = y1 - y2, r = r1 + r2; return dx * dx + dy * dy <= r * r;\n"
        "        },\n"
        "        ease: ease,\n"
        "        tween: function (from, to, ms, easing) {\n"
        "            var f = typeof easing === 'function' ? easing : ease[easing === undefined ? 'linear' : easing];\n"
        "            if (typeof f !== 'function') throw new TypeError(\"gfx.tween: no easing '\" + easing + \"'\");\n"
        "            var t = { value: from, done: false, elapsed: 0 };\n"
        "            t.step = function (dt) {\n"
        "                t.elapsed = Math.min(ms, t.elapsed + (dt > 0 ? dt : 0));\n"
        "                var k = ms > 0 ? t.elapsed / ms : 1;\n"
        "                t.value = k >= 1 ? to : from + (to - from) * f(k);\n"
        "                t.done = k >= 1;\n"
        "                return t.value;\n"
        "            };\n"
        "            t.reset = function () { t.elapsed = 0; t.value = from; t.done = false; };\n"
        "            return t;\n"
        "        },\n"
        "        draw: Object.freeze(draw),\n"
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
        m_shared->frameRuns.remove(id);
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

CanvasRunObject::~CanvasRunObject()
{
    QMutexLocker lock(&m_shared->mutex);
    m_shared->frameRuns.remove(m_id);
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

// The objects of gfx.linearGradient() are frozen: the last one is kept made.
bool CanvasRunObject::readGradient(const QJSValue& value, CanvasStyle& style, const char* function)
{
    if (!m_gradient || !value.strictlyEquals(m_gradientValue)) {
        auto number = [&](const char* key, double& out) {
            out = value.property(QLatin1String(key)).toNumber();
            return std::isfinite(out) ? true : fail(function, QStringLiteral("the gradient's %1 must be a number")
                                                                  .arg(QLatin1String(key)));
        };
        const QString type = value.property(QStringLiteral("gradient")).toString();
        QSharedPointer<QGradient> made;
        if (type == QLatin1String("linear")) {
            double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
            if (!number("x0", x0) || !number("y0", y0) || !number("x1", x1) || !number("y1", y1))
                return false;
            made.reset(new QLinearGradient(x0, y0, x1, y1));
        } else if (type == QLatin1String("radial")) {
            double cx = 0, cy = 0, r = 0, fx = 0, fy = 0;
            if (!number("cx", cx) || !number("cy", cy) || !number("r", r) || !number("fx", fx) || !number("fy", fy))
                return false;
            made.reset(new QRadialGradient(cx, cy, r, fx, fy));
        } else {
            return fail(function, QStringLiteral("a fill is a colour or a gradient from gfx.linearGradient() "
                                                 "or gfx.radialGradient()"));
        }
        const QJSValue stops = value.property(QStringLiteral("stops"));
        const int count = stops.property(QStringLiteral("length")).toInt();
        for (int i = 0; i < count; ++i) {
            const QJSValue stop = stops.property(quint32(i));
            const double offset = stop.property(quint32(0)).toNumber();
            QRgb colour = 0;
            if (!std::isfinite(offset) || offset < 0 || offset > 1)
                return fail(function, QStringLiteral("a gradient stop's offset is 0..1"));
            if (!readColour(stop.property(quint32(1)), colour, function))
                return false;
            made->setColorAt(offset, QColor::fromRgba(colour));
        }
        m_gradient = made;
        m_gradientValue = value;
    }
    style.gradient = m_gradient;
    style.fill = 0;
    return true;
}

bool CanvasRunObject::readFont(const QJSValue& value, CanvasStyle& style, const char* function)
{
    if (!value.isObject())
        return fail(function, QStringLiteral("a font is an object {family, size, bold, italic}"));
    QJSValueIterator it(value);
    while (it.hasNext()) {
        it.next();
        const QString part = it.name();
        const QJSValue f = it.value();
        if (part == QLatin1String("family")) {
            style.fontFamily = f.toString();
        } else if (part == QLatin1String("size")) {
            const double size = f.toNumber();
            if (!f.isNumber() || !std::isfinite(size) || size <= 0)
                return fail(function, QStringLiteral("the font size must be a number above 0"));
            style.fontSize = size;
        } else if (part == QLatin1String("bold")) {
            style.bold = f.toBool();
        } else if (part == QLatin1String("italic")) {
            style.italic = f.toBool();
        } else {
            return fail(function, QStringLiteral("unknown font field '%1'").arg(part));
        }
    }
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
            if (v.isObject()) {
                ok = readGradient(v, style, function);
            } else {
                ok = readColour(v, style.fill, function);
                style.gradient.reset();
            }
        } else if (key == QLatin1String("width")) {
            ok = number(v, key, style.width);
        } else if (key == QLatin1String("opacity")) {
            ok = number(v, key, style.opacity);
            style.opacity = qMin(style.opacity, 1.0);
        } else if (key == QLatin1String("head")) {
            ok = number(v, key, style.head);
        } else if (key == QLatin1String("padding")) {
            ok = number(v, key, style.padding);
        } else if (key == QLatin1String("dash")) {
            quint8 dash = 0;
            ok = choice(v, key, {"solid", "dash", "dot", "dashDot"}, dash);
            style.dash = quint8(Qt::SolidLine + dash);
        } else if (key == QLatin1String("align")) {
            ok = choice(v, key, {"left", "center", "right"}, style.align);
        } else if (key == QLatin1String("valign")) {
            ok = choice(v, key, {"top", "middle", "bottom", "baseline"}, style.valign);
        } else if (key == QLatin1String("font")) {
            ok = readFont(v, style, function);
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

int CanvasRunObject::addText(const QString& text)
{
    m_frame.texts.append(text);
    return int(m_frame.texts.size()) - 1;
}

void CanvasRunObject::add(quint8 op, int style, const double* values, int count, QRectF bounds, int extra,
                          int anchor, int anchor2)
{
    CanvasItem item;
    item.op = op;
    item.style = style;
    item.first = int(m_frame.coords.size());
    item.count = count;
    item.extra = extra;
    item.anchor = anchor;
    item.anchor2 = anchor2;
    if (anchor < 0 && anchor2 < 0 && !m_transform.isIdentity()) {
        if (m_transformIndex < 0) {
            m_frame.transforms.push_back(m_transform);
            m_transformIndex = int(m_frame.transforms.size()) - 1;
        }
        item.transform = m_transformIndex;
        if (!bounds.isNull())
            bounds = m_transform.mapRect(bounds);
    }
    item.bounds = bounds;
    if (count > 0)
        m_frame.coords.insert(m_frame.coords.end(), values, values + count);
    m_frame.items.push_back(item);
    if (anchor >= 0 || anchor2 >= 0)
        m_frame.hasAnchors = true;
    else if (!bounds.isNull())
        m_frame.bounds |= bounds;
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
    const double m = m_frame.styles[size_t(s)].margin();
    add(op, s, points.constData(), int(points.size()),
        QRectF(left, top, right - left, bottom - top).adjusted(-m, -m, m, m));
}

void CanvasRunObject::addAnchored(quint8 op, const QString& widget, const double* values, int count,
                                  const QJSValue& style, const char* function, int extra)
{
    if (widget.isEmpty())
        return void(fail(function, QStringLiteral("the widget has no name")));
    const int s = styleFor(style, function);
    if (s < 0)
        return;
    // Where it is: worked out on the GUI thread, from the widget.
    add(op, s, values, count, QRectF(), extra, addText(widget));
}

// ---- CanvasRunObject: state and transforms ----------------------------------

void CanvasRunObject::clear()
{
    m_frame.clear();
    m_baseIndex = -1;
    m_transformIndex = -1;
}

void CanvasRunObject::style(const QJSValue& style)
{
    CanvasStyle base;
    if (readStyle(style, base, "style")) {
        m_base = base;
        m_baseIndex = -1;
    }
}

void CanvasRunObject::save()
{
    if (m_saved.size() >= 256)
        return void(fail("save", QStringLiteral("saved 256 times without restore()")));
    m_saved.append({m_transform, m_base});
}

void CanvasRunObject::restore()
{
    if (m_saved.isEmpty())
        return;
    const Saved saved = m_saved.takeLast();
    m_transform = saved.transform;
    m_transformIndex = -1;
    m_base = saved.base;
    m_baseIndex = -1;
}

void CanvasRunObject::translate(double x, double y)
{
    if (finite("translate", {x, y})) {
        m_transform.translate(x, y);
        m_transformIndex = -1;
    }
}

void CanvasRunObject::rotate(double radians)
{
    if (finite("rotate", {radians})) {
        m_transform.rotateRadians(radians);
        m_transformIndex = -1;
    }
}

void CanvasRunObject::scale(double x, double y)
{
    if (finite("scale", {x, y})) {
        m_transform.scale(x, y);
        m_transformIndex = -1;
    }
}

void CanvasRunObject::resetTransform()
{
    m_transform.reset();
    m_transformIndex = -1;
}

// ---- CanvasRunObject: shapes ----------------------------------------------

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
    add(CanvasItem::Text, s, values, 2, QRectF(), addText(text));
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

// The commands of the glue's Path: 0 moveTo x y, 1 lineTo x y, 2 quadTo cx cy
// x y, 3 cubicTo ax ay bx by x y, 4 arc cx cy r start end ccw, 5 close.
void CanvasRunObject::drawPath(const QVector<double>& commands, const QJSValue& style)
{
    static const int kArgs[] = {2, 2, 4, 6, 6, 0};
    const double tau = 6.283185307179586;
    QPainterPath path;
    QPointF start;
    int state = 0;      // 0 no point yet, 1 in a subpath, 2 after close: from `start`
    // Canvas rules: a line or curve without a current point starts there.
    auto begin = [&](const QPointF& at) {
        if (state == 0) {
            path.moveTo(at);
            start = at;
        } else if (state == 2) {
            path.moveTo(start);
        }
        state = 1;
    };
    for (int i = 0; i < commands.size();) {
        const double code = commands[i];
        const int op = code >= 0 && code <= 5 ? int(code) : -1;
        if (op < 0 || i + 1 + kArgs[op] > commands.size())
            return void(fail("path", QStringLiteral("a broken path")));
        const double* a = commands.constData() + i + 1;
        for (int k = 0; k < kArgs[op]; ++k) {
            if (!std::isfinite(a[k]))
                return void(fail("path", QStringLiteral("expects numbers")));
        }
        switch (op) {
        case 0:
            path.moveTo(a[0], a[1]);
            start = QPointF(a[0], a[1]);
            state = 1;
            break;
        case 1:
            begin(QPointF(a[0], a[1]));
            path.lineTo(a[0], a[1]);
            break;
        case 2:
            begin(QPointF(a[0], a[1]));
            path.quadTo(a[0], a[1], a[2], a[3]);
            break;
        case 3:
            begin(QPointF(a[0], a[1]));
            path.cubicTo(a[0], a[1], a[2], a[3], a[4], a[5]);
            break;
        case 4: {
            const double r = a[2];
            if (r < 0)
                return void(fail("path", QStringLiteral("an arc's radius must not be negative")));
            const bool ccw = a[5] != 0;
            double sweep = ccw ? a[3] - a[4] : a[4] - a[3];
            sweep = sweep >= tau ? tau : std::fmod(std::fmod(sweep, tau) + tau, tau);
            if (ccw)
                sweep = -sweep;
            begin(QPointF(a[0] + r * std::cos(a[3]), a[1] + r * std::sin(a[3])));
            // Qt counts degrees counterclockwise; the canvas radians clockwise.
            path.arcTo(QRectF(a[0] - r, a[1] - r, 2 * r, 2 * r), -a[3] * 360 / tau, -sweep * 360 / tau);
            break;
        }
        default:
            if (state == 1) {
                path.closeSubpath();
                state = 2;
            }
            break;
        }
        i += 1 + kArgs[op];
    }
    if (path.isEmpty())
        return;
    const int s = styleFor(style, "path");
    if (s < 0)
        return;
    const double m = m_frame.styles[size_t(s)].margin();
    m_frame.paths.push_back(path);
    add(CanvasItem::Path, s, nullptr, 0, path.controlPointRect().adjusted(-m, -m, m, m),
        int(m_frame.paths.size()) - 1);
}

void CanvasRunObject::image(const QString& name, double x, double y, const QJSValue& w, const QJSValue& h,
                            const QJSValue& source, const QJSValue& style)
{
    if (!finite("image", {x, y}))
        return;
    QSize size;
    {
        QMutexLocker lock(&m_shared->mutex);
        const auto it = m_shared->imageSizes.constFind(name);
        if (it == m_shared->imageSizes.constEnd())
            return void(fail("image", QStringLiteral("no image '%1'").arg(name)));
        size = *it;
    }
    // Absent: `fallback`.
    auto number = [&](const QJSValue& v, double fallback, double& out) {
        if (v.isUndefined() || v.isNull()) {
            out = fallback;
            return true;
        }
        out = v.toNumber();
        return v.isNumber() && std::isfinite(out) ? true : fail("image", QStringLiteral("expects numbers"));
    };
    double sx = 0, sy = 0, sw = 0, sh = 0, dw = 0, dh = 0;
    if (!source.isUndefined() && !source.isNull() && !source.isObject())
        return void(fail("image", QStringLiteral("the source is {sx, sy, sw, sh}")));
    if (!number(source.property(QStringLiteral("sx")), 0, sx) || !number(source.property(QStringLiteral("sy")), 0, sy)
        || !number(source.property(QStringLiteral("sw")), size.width() - sx, sw)
        || !number(source.property(QStringLiteral("sh")), size.height() - sy, sh) || !number(w, sw, dw)
        || !number(h, sh, dh))
        return;
    const int s = styleFor(style, "image");
    if (s < 0)
        return;
    const double values[] = {x, y, dw, dh, sx, sy, sw, sh};
    add(CanvasItem::Image, s, values, 8, QRectF(x, y, dw, dh).normalized().adjusted(-1, -1, 1, 1), addText(name));
}

QVariantList CanvasRunObject::imageSize(const QString& name) const
{
    QMutexLocker lock(&m_shared->mutex);
    const auto it = m_shared->imageSizes.constFind(name);
    if (it == m_shared->imageSizes.constEnd())
        return QVariantList();
    return {it->width(), it->height()};
}

// ---- CanvasRunObject: anchored to widgets -----------------------------------

void CanvasRunObject::rectAt(const QString& widget, const QJSValue& style)
{
    const double values[] = {0, 0, 0, 0};
    addAnchored(CanvasItem::Rect, widget, values, 4, style, "rect");
}

void CanvasRunObject::roundedRectAt(const QString& widget, double radius, const QJSValue& style)
{
    if (!finite("roundedRect", {radius}))
        return;
    const double values[] = {0, 0, 0, 0, radius};
    addAnchored(CanvasItem::RoundedRect, widget, values, 5, style, "roundedRect");
}

void CanvasRunObject::ellipseAt(const QString& widget, const QJSValue& style)
{
    const double values[] = {0, 0, 0, 0};
    addAnchored(CanvasItem::Ellipse, widget, values, 4, style, "ellipse");
}

void CanvasRunObject::textAt(const QString& widget, const QString& text, const QJSValue& style)
{
    const double values[] = {0, 0};
    const int textIndex = addText(text);
    addAnchored(CanvasItem::Text, widget, values, 2, style, "text", textIndex);
}

void CanvasRunObject::arrowAt(const QString& from, double x1, double y1, const QString& to, double x2, double y2,
                              const QJSValue& style)
{
    if ((from.isEmpty() && !finite("arrow", {x1, y1})) || (to.isEmpty() && !finite("arrow", {x2, y2})))
        return;
    const int s = styleFor(style, "arrow");
    if (s < 0)
        return;
    const double values[] = {x1, y1, x2, y2};
    const int a = from.isEmpty() ? -1 : addText(from);
    const int b = to.isEmpty() ? -1 : addText(to);
    add(CanvasItem::Arrow, s, values, 4, QRectF(), -1, a, b);
}

QVariantList CanvasRunObject::widgetRect(const QString& widget)
{
    struct Answer
    {
        QSemaphore done;
        QRectF     rect;
        bool       found = false;
    };
    auto answer = QSharedPointer<Answer>::create();
    {
        QMutexLocker lock(&m_shared->mutex);
        ScriptCanvas* canvas = m_shared->canvas;
        if (!canvas)
            return QVariantList();
        QMetaObject::invokeMethod(canvas, [canvas, widget, answer]() {
            answer->found = canvas->anchorRect(widget, answer->rect);
            answer->done.release();
        }, Qt::QueuedConnection);
    }
    // The GUI thread answers between its events. Given up when the run stops
    // or the canvas goes (the call is then dropped with it).
    while (!answer->done.tryAcquire(1, 10)) {
        if (!m_context.isActive())
            return QVariantList();
        QMutexLocker lock(&m_shared->mutex);
        if (!m_shared->canvas)
            return QVariantList();
    }
    if (!answer->found)
        return QVariantList();
    return {answer->rect.x(), answer->rect.y(), answer->rect.width(), answer->rect.height()};
}

// ---- CanvasRunObject: frames, text, custom ----------------------------------

void CanvasRunObject::onFrame(const QJSValue& handler)
{
    m_frameHandler = handler;
    m_frameClock.start();
    m_context.setKeepAlive(QStringLiteral("frame"), true);
    QMutexLocker lock(&m_shared->mutex);
    m_shared->frameRuns.insert(m_id, {m_context, this});
    if (ScriptCanvas* canvas = m_shared->canvas)
        QMetaObject::invokeMethod(canvas, [canvas]() { canvas->startFrames(); }, Qt::QueuedConnection);
}

void CanvasRunObject::offFrame()
{
    m_frameHandler = QJSValue();
    m_context.setKeepAlive(QStringLiteral("frame"), false);
    QMutexLocker lock(&m_shared->mutex);
    m_shared->frameRuns.remove(m_id);
}

void CanvasRunObject::frame()
{
    if (!m_frameHandler.isCallable())
        return;
    const double dt = double(m_frameClock.nsecsElapsed()) / 1e6;
    m_frameClock.restart();
    m_context.call(m_frameHandler, {QJSValue(dt)});
}

QVariantList CanvasRunObject::measureText(const QString& text, const QJSValue& font)
{
    CanvasStyle style = m_base;
    if (!font.isUndefined() && !font.isNull() && !readFont(font, style, "measureText"))
        return QVariantList();
    // ponytail: QFontMetricsF off the GUI thread - fine on Windows, macOS and
    // X11 with threaded font rendering; a GUI round trip where that is missing.
    const QFontMetricsF metrics(style.font());
    return {metrics.horizontalAdvance(text), metrics.height(), metrics.ascent(), metrics.descent()};
}

QStringList CanvasRunObject::drawNames() const
{
    QMutexLocker lock(&m_shared->mutex);
    return m_shared->drawDeclarations.keys();
}

void CanvasRunObject::custom(const QString& name, const QJSValue& args)
{
    const int s = styleFor(QJSValue(), "draw");
    m_frame.calls.push_back({name, args.toVariant().toList()});
    // Its bounds come from the application, on the GUI thread.
    add(CanvasItem::Custom, s, nullptr, 0, QRectF(), int(m_frame.calls.size()) - 1);
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
