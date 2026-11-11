#pragma once
#include "ScriptHost_base.h"
#include "ScriptEngine.h"
#include <QList>
#include <QPointer>
#include <QRect>
#include <QSharedPointer>
#include <QWidget>
#include <memory>

namespace ScriptHost
{

class CanvasExtension;
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
// at a limit. To keep a drawing up, the script stays alive (a timer, an on()
// handler).
//
// Size: gfx.width and gfx.height; on resize every run with a handler gets
// the event "resize" {width, height} through on().
//
// The functions (style: an optional object; what it leaves out comes from
// gfx.style()):
//   gfx.clear()                          empties the list not yet presented
//   gfx.style(style?)                    the run's defaults; without: back to
//                                        the built-in ones
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
//   gfx.present()
//   gfx.width, gfx.height                the canvas size in pixels
// Style: {stroke, fill: "#rrggbb" | "#aarrggbb" | a CSS name | "none";
//         width (0: no outline), dash: "solid" | "dash" | "dot" | "dashDot",
//         opacity: 0..1, head (arrow head length), font: {family, size (px),
//         bold, italic}, align: "left" | "center" | "right",
//         valign: "top" | "middle" | "bottom" | "baseline"}.
// Defaults: stroke black, width 1, no fill, solid, opacity 1, font 14 px,
// left / top. A wrong argument throws in the script (gfx.rect: ...).
// Speed: one style object or gfx.style() for many shapes, the batch forms
// for many shapes of one style, and one present() per frame.
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

    QPointer<ScriptEngine>           m_engine;
    QSharedPointer<CanvasShared>     m_shared;
    QSharedPointer<CanvasExtension>  m_extension;
    std::unique_ptr<CanvasLayers>    m_layers;  // what is shown; this thread only
};

// Attaches a canvas to `target` and adds `gfx` to the engine's new runs.
// Returns the canvas, a child of `target`: delete it to detach.
SCRIPT_HOST_WIDGETS_API ScriptCanvas* attachCanvas(ScriptEngine* engine, QWidget* target);

} // namespace ScriptHost
