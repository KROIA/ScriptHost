#pragma once
#include "ScriptHost_base.h"
#include "ScriptEngine.h"
#include <QList>
#include <QPointer>
#include <QRect>
#include <QSharedPointer>
#include <QStringList>
#include <QVariant>
#include <QWidget>
#include <functional>
#include <memory>

class QImage;
class QPainter;

namespace ScriptHost
{

class CanvasExtension;
struct CanvasFrame;
struct CanvasLayers;
struct CanvasShared;

// A drawing surface for scripts over a widget: the script global `gfx`.
//
//   ScriptHost::ScriptCanvas* canvas = ScriptHost::attachCanvas(engine, gameField);
//
//   // in a script:
//   gfx.clear();
//   gfx.rect(10, 10, 80, 40, {stroke: "red", width: 2});
//   gfx.text(50, 30, "Hello", {fill: "#202020", align: "center", valign: "middle"});
//   gfx.present();
//
// The canvas is a transparent child of `target` that takes no mouse clicks
// and no focus, fills `target` and follows its size, and stays above
// `target`'s other children (raised again when a child is added and on every
// present). It goes with `target`; deleting it detaches it: `gfx` is gone
// from new runs, and the runs that have it draw into nothing.
// ponytail: a sibling widget cannot paint over a native child (a
// QOpenGLWidget in native mode, an embedded native window); the upgrade path
// is a frameless, see-through, input-transparent tool window.
//
// Immediate mode: the gfx functions record into a display list on the run's
// own thread - no JSON, no thread hop, no method call (no maxCalls cost).
// present() hands the list to this widget without waiting and starts an
// empty one; the widget paints the newest list each run presented and drops
// the older ones. A presented frame stays on screen until the run presents
// the next, so a static highlight costs nothing once it is drawn. Only the
// union of the old and the new frame's bounds of the run is repainted.
//
// Layers: each run that presented has one, painted in run-start order (a later
// run on top), and cleared when the run ends - finished, stopped, failed or
// at a limit. To keep a drawing up, the script stays alive (gfx.onFrame(), a
// timer, an on() handler).
//
// Size: gfx.width and gfx.height; on resize every run with a handler gets
// the event "resize" {width, height} through on().
//
// The functions (style: an optional object; what it leaves out comes from
// gfx.style()); the full list with types is in ScriptEngine::apiDefinition("gfx"):
//   gfx.clear()                          empties the list not yet presented
//   gfx.style(style?)                    the run's defaults; without: back to
//                                        the built-in ones
//   gfx.save(), gfx.restore()            the transform and gfx.style()
//   gfx.translate(x, y), gfx.rotate(radians; clockwise), gfx.scale(x, y?),
//   gfx.resetTransform()                 for what follows; kept across frames
//   gfx.rect(x, y, w, h, style?)
//   gfx.roundedRect(x, y, w, h, radius, style?)
//   gfx.circle(cx, cy, r, style?)
//   gfx.ellipse(cx, cy, rx, ry, style?)
//   gfx.line(x1, y1, x2, y2, style?)
//   gfx.arrow(x1, y1, x2, y2, style?)    the head at (x2, y2)
//   gfx.polyline(points, style?)         points: [x0, y0, x1, y1, ...]
//   gfx.polygon(points, style?)
//   gfx.text(x, y, text, style?)         (x, y) placed by align / valign; the
//                                        colour is fill, else stroke
//   gfx.rects([x, y, w, h, ...], style?)     many at once, one style; plain
//   gfx.circles([cx, cy, r, ...], style?)    flat number arrays (typed arrays
//   gfx.lines([x1, y1, x2, y2, ...], style?) work, more slowly)
//   gfx.path().moveTo(x, y).lineTo(x, y).quadTo(..).cubicTo(..)
//      .arc(cx, cy, r, start, end, ccw?).close().draw(style?)
//   gfx.linearGradient(x0, y0, x1, y1, [[0, "red"], [1, "blue"]]),
//   gfx.radialGradient(cx, cy, r, stops, fx?, fy?)    as a style's fill
//   gfx.image(name, x, y, w?, h?, {sx, sy, sw, sh}?, style?), gfx.imageSize(name)
//                                        images the application added
//   gfx.widget(objectName)               a handle for: gfx.rect(w, style?),
//      gfx.roundedRect(w, r, style?), gfx.ellipse(w, style?),
//      gfx.text(w, text, style?), gfx.arrow(.., w, ..) - style.padding around it
//   gfx.widgetRect(objectName)           {x, y, width, height} or null
//   gfx.onFrame(fn(dt)), gfx.offFrame()  once per screen refresh
//   gfx.measureText(text, font?), gfx.pointInRect, gfx.pointInCircle,
//   gfx.rectsOverlap, gfx.circlesOverlap, gfx.ease.<name>(t), gfx.tween(..)
//   gfx.draw.<name>(...)                 addDrawFunction()
//   gfx.present()
//   gfx.width, gfx.height                the canvas size in pixels
// Style: {stroke, fill: "#rrggbb" | "#aarrggbb" | a CSS name | "none" | a
//         gradient; width (0: no outline), dash: "solid" | "dash" | "dot" |
//         "dashDot", opacity: 0..1, head (arrow head length), padding,
//         font: {family, size (px), bold, italic}, align: "left" | "center" |
//         "right", valign: "top" | "middle" | "bottom" | "baseline"}.
// Defaults: stroke black, width 1, no fill, solid, opacity 1, font 14 px,
// left / top. A wrong argument throws in the script (gfx.rect: ...).
// Speed: one style object or gfx.style() for many shapes, the batch forms
// for many shapes of one style, and one present() per frame.
//
// Anchored shapes: a shape drawn on gfx.widget("name") is placed on the GUI
// thread from where the descendant of `target` with that objectName is now:
// it follows moves, resizes and scrolling (only the layers that have it are
// repainted), and is not drawn while the widget is hidden or missing. A
// widget found by its name for the first time after the frame was presented
// shows at the run's next present(). Anchored shapes ignore the transform.
//
// Frames: gfx.onFrame(fn) calls fn(dt) - dt in milliseconds since its last
// frame - at the screen's refresh rate (60 when unknown), from one GUI timer
// that runs while some run has a frame handler. A handler slower than that
// skips frames; none pile up. A frame handler keeps the run alive.
//
// Requested runs (ScriptEngine::requestRun) get no gfx unless allowed:
// setAllowedForRequestedRuns(true), which also lets them have "resize".
//
// One canvas per engine: a second one attached to the same engine takes
// `gfx` over for new runs. GUI thread only.
class SCRIPT_HOST_WIDGETS_API ScriptCanvas : public QWidget
{
    Q_OBJECT
public:
    // Paints a gfx.draw.<name>(...) call: `args` are the script's arguments
    // (numbers, strings, arrays as QVariantList, objects as QVariantMap). The
    // painter has the call's transform, pen, brush and opacity set; what the
    // function changes on it is undone after it.
    using DrawFunction = std::function<void(QPainter& painter, const QVariantList& args)>;
    // What a call paints, in its own coordinates (before the transform).
    using BoundsFunction = std::function<QRectF(const QVariantList& args)>;

    // attachCanvas() is the short way.
    ScriptCanvas(ScriptEngine* engine, QWidget* target);
    ~ScriptCanvas() override;

    ScriptEngine* engine() const;
    QWidget* target() const { return parentWidget(); }

    // Default false. Sets ScriptEngine::allowExtensionForRequestedRuns("gfx")
    // and allowEventForRequestedRuns("resize").
    void setAllowedForRequestedRuns(bool allowed);
    bool isAllowedForRequestedRuns() const;

    // The runs with a layer, in paint order.
    QList<ScriptEngine::RunId> layers() const;

    // Images scripts draw by name (gfx.image): sprites, icons, sprite sheets.
    // Scripts reach no files. Replacing or removing one repaints the canvas.
    void addImage(const QString& name, const QImage& image);
    void removeImage(const QString& name);
    QStringList images() const;

    // gfx.draw.<name>(...) for scripts: `name` an identifier, `declaration`
    // its TypeScript ("function star(x: number, y: number, r: number): void;"),
    // shown in apiDefinition(). Without `bounds` a call repaints the whole
    // canvas each time its frame is presented. New runs have it; check() knows
    // it at once (ScriptEngine::methodsChanged).
    void addDrawFunction(const QString& name, DrawFunction painter, const QString& declaration,
                         BoundsFunction bounds = nullptr);
    void removeDrawFunction(const QString& name);
    QStringList drawFunctions() const;

signals:
    // A run's presented frame reached this widget; `dirty` is the area
    // repainted for it: the union of its old and new frame's bounds.
    void presented(ScriptEngine::RunId id, const QRect& dirty);

protected:
    void paintEvent(QPaintEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    friend class CanvasExtension;
    friend class CanvasRunObject;

    // Takes the frames the runs presented (posted by present()).
    void takeFrames();
    // The run ended: its layer goes.
    void removeLayer(ScriptEngine::RunId id);
    void followTarget();
    // No other child widget of the target above this one.
    bool onTop() const;
    // Texts and custom calls get their bounds (once per frame).
    void layOut(CanvasFrame& frame);
    // Anchored items from where their widgets are now; returns their bounds.
    void resolveAnchors(CanvasFrame& frame);
    // A watched widget moved, was shown, hidden or deleted (nullptr).
    void anchorsMoved(QObject* widget);
    QWidget* anchorWidget(const QString& name);
    // Where the widget is on the canvas; false when it is missing or hidden.
    bool anchorRect(const QString& name, QRectF& rect);
    void startFrames();
    void tickFrames();

    QPointer<ScriptEngine>           m_engine;
    QSharedPointer<CanvasShared>     m_shared;
    QSharedPointer<CanvasExtension>  m_extension;
    std::unique_ptr<CanvasLayers>    m_layers;  // what is shown; this thread only
};

// Attaches a canvas to `target` and adds `gfx` to the engine's new runs.
// Returns the canvas, a child of `target`: delete it to detach.
SCRIPT_HOST_WIDGETS_API ScriptCanvas* attachCanvas(ScriptEngine* engine, QWidget* target);

} // namespace ScriptHost
