#pragma once
// The canvas's display list and what the runs and the canvas share. Internal.
#include "ScriptExtension.h"
#include <QColor>
#include <QFont>
#include <QGradient>
#include <QHash>
#include <QMap>
#include <QMutex>
#include <QPainterPath>
#include <QRectF>
#include <QSharedPointer>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QTransform>
#include <QVariant>
#include <vector>

namespace ScriptHost
{

class CanvasRunObject;
class ScriptCanvas;

// How an item is drawn. Kept once per change in a frame; items refer to it.
struct CanvasStyle
{
    enum HAlign : quint8 { Left, Center, Right };
    enum VAlign : quint8 { Top, Middle, Bottom, Baseline };

    QRgb    stroke = 0xff000000;
    QRgb    fill = 0;               // alpha 0: none
    QSharedPointer<QGradient> gradient;     // a fill instead of `fill`; read-only once made
    double  width = 1;              // 0: no outline
    double  opacity = 1;
    double  head = 0;               // arrow head length; 0: from the width
    double  padding = 0;            // around an anchor widget
    double  fontSize = 14;          // pixels
    QString fontFamily;
    quint8  dash = 1;               // Qt::PenStyle
    quint8  align = Left;
    quint8  valign = Top;
    bool    bold = false;
    bool    italic = false;

    bool operator==(const CanvasStyle& o) const
    {
        return stroke == o.stroke && fill == o.fill && gradient == o.gradient && width == o.width
               && opacity == o.opacity && head == o.head && padding == o.padding && fontSize == o.fontSize
               && dash == o.dash && align == o.align && valign == o.valign && bold == o.bold
               && italic == o.italic && fontFamily == o.fontFamily;
    }
    bool operator!=(const CanvasStyle& o) const { return !(*this == o); }

    bool hasFill() const { return gradient || qAlpha(fill); }
    double arrowHead() const { return head > 0 ? head : (width * 4 > 8 ? width * 4 : 8); }
    // How far the outline reaches out of the geometry: the pen's half width,
    // a square cap's corner or a bevel join, and one pixel of antialiasing.
    double margin() const { return (qAlpha(stroke) && width > 0 ? width : 0) + 1; }
    QFont font() const
    {
        QFont f;
        if (!fontFamily.isEmpty())
            f.setFamily(fontFamily);
        f.setPixelSize(qMax(1, qRound(fontSize)));
        f.setBold(bold);
        f.setItalic(italic);
        return f;
    }
};

// One primitive: its numbers are coords[first .. first + count).
//   Rect, RoundedRect    x, y, w, h (, radius)
//   Ellipse              cx, cy, rx, ry
//   Line, Arrow          x1, y1, x2, y2
//   Polyline, Polygon    x0, y0, x1, y1, ...
//   Text                 x, y - the anchor; once laid out (GUI thread) the
//                        baseline start; texts[extra]
//   Path                 paths[extra]
//   Image                x, y, w, h, sx, sy, sw, sh; texts[extra] the name
//   Custom               calls[extra]
// Anchored (anchor / anchor2: texts[] the widget's name, or for an item
// anchor kItemAnchor and its JSON): the numbers are written on the GUI
// thread from the widget's or item's place; no transform.
struct CanvasItem
{
    enum Op : quint8 { Rect, RoundedRect, Ellipse, Line, Arrow, Polyline, Polygon, Text, Path, Image, Custom };

    quint8 op = Rect;
    int    style = 0;
    int    first = 0;
    int    count = 0;
    int    extra = -1;
    int    transform = -1;          // transforms[]; -1: none
    int    anchor = -1;             // the shape's widget, the arrow's start
    int    anchor2 = -1;            // the arrow's end
    QRectF bounds;                  // what it may paint, on the canvas; null: nothing
    QRectF clip;                    // an item anchor's view, on the canvas; null: none
};

// What an item anchor's text in CanvasFrame::texts starts with; a widget
// anchor's is its objectName.
const QChar kItemAnchor(1);

struct CanvasCall
{
    QString      name;
    QVariantList args;
};

struct CanvasFrame
{
    std::vector<CanvasItem>   items;
    std::vector<double>       coords;
    std::vector<CanvasStyle>  styles;
    std::vector<QTransform>   transforms;
    std::vector<QPainterPath> paths;
    std::vector<CanvasCall>   calls;
    QStringList               texts;        // texts, image and widget names
    QRectF                    bounds;       // of the items not anchored; texts and custom ones once laid out
    QRectF                    anchored;     // of the anchored items, as last resolved
    bool                      laidOut = false;
    bool                      hasAnchors = false;

    QRectF all() const { return bounds | anchored; }

    // Keeps the vectors' memory: the buffers go round between a run and
    // the canvas.
    void clear()
    {
        items.clear();
        coords.clear();
        styles.clear();
        transforms.clear();
        paths.clear();
        calls.clear();
        texts.clear();
        bounds = QRectF();
        anchored = QRectF();
        laidOut = false;
        hasAnchors = false;
    }
};

struct CanvasPending
{
    CanvasFrame frame;
    bool        fresh = false;      // presented, not yet taken by the canvas
};

// A run with gfx.onFrame(): the canvas schedules its frames.
struct CanvasFrameRun
{
    ScriptRunContext context;
    CanvasRunObject* object = nullptr;  // only handed to the run's own thread
};

// Between the runs (worker threads) and the canvas (GUI thread).
struct CanvasShared
{
    QMutex                          mutex;
    ScriptCanvas*                   canvas = nullptr;   // nullptr once it is gone
    QSize                           size;
    QHash<quint64, CanvasPending>   pending;            // by run
    bool                            flushQueued = false;
    QHash<QString, QSize>           imageSizes;         // ScriptCanvas::addImage()
    QMap<QString, QString>          drawDeclarations;   // ScriptCanvas::addDrawFunction()
    QHash<quint64, CanvasFrameRun>  frameRuns;
};

} // namespace ScriptHost
