#pragma once
// The canvas's display list and what the runs and the canvas share. Internal.
#include <QColor>
#include <QHash>
#include <QMutex>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QStringList>
#include <vector>

namespace ScriptHost
{

class ScriptCanvas;

// How an item is drawn. Kept once per change in a frame; items refer to it.
struct CanvasStyle
{
    enum HAlign : quint8 { Left, Center, Right };
    enum VAlign : quint8 { Top, Middle, Bottom, Baseline };

    QRgb    stroke = 0xff000000;
    QRgb    fill = 0;               // alpha 0: none
    double  width = 1;              // 0: no outline
    double  opacity = 1;
    double  head = 0;               // arrow head length; 0: from the width
    double  fontSize = 14;          // pixels
    QString fontFamily;
    quint8  dash = 1;               // Qt::PenStyle
    quint8  align = Left;
    quint8  valign = Top;
    bool    bold = false;
    bool    italic = false;

    bool operator==(const CanvasStyle& o) const
    {
        return stroke == o.stroke && fill == o.fill && width == o.width && opacity == o.opacity
               && head == o.head && fontSize == o.fontSize && dash == o.dash && align == o.align
               && valign == o.valign && bold == o.bold && italic == o.italic && fontFamily == o.fontFamily;
    }
    bool operator!=(const CanvasStyle& o) const { return !(*this == o); }

    double arrowHead() const { return head > 0 ? head : (width * 4 > 8 ? width * 4 : 8); }
    // How far the outline reaches out of the geometry: the pen's half width,
    // a square cap's corner or a join, and one pixel of antialiasing.
    double margin() const { return (qAlpha(stroke) && width > 0 ? width : 0) + 1; }
};

// One primitive: its numbers are coords[first .. first + count).
//   Rect, RoundedRect    x, y, w, h (, radius)
//   Ellipse              cx, cy, rx, ry
//   Line, Arrow          x1, y1, x2, y2
//   Polyline, Polygon    x0, y0, x1, y1, ...
//   Text                 x, y - the anchor; once laid out (GUI thread) the
//                        baseline start; texts[text]
struct CanvasItem
{
    enum Op : quint8 { Rect, RoundedRect, Ellipse, Line, Arrow, Polyline, Polygon, Text };

    quint8 op = Rect;
    int    style = 0;
    int    first = 0;
    int    count = 0;
    int    text = -1;
    QRectF bounds;                  // what it may paint; text: set when laid out
};

struct CanvasFrame
{
    std::vector<CanvasItem>  items;
    std::vector<double>      coords;
    std::vector<CanvasStyle> styles;
    QStringList              texts;
    QRectF                   bounds;    // of all items, text once laid out
    bool                     laidOut = false;

    // Keeps the vectors' memory: the buffers go round between a run and
    // the canvas.
    void clear()
    {
        items.clear();
        coords.clear();
        styles.clear();
        texts.clear();
        bounds = QRectF();
        laidOut = false;
    }
};

struct CanvasPending
{
    CanvasFrame frame;
    bool        fresh = false;      // presented, not yet taken by the canvas
};

// Between the runs (worker threads) and the canvas (GUI thread).
struct CanvasShared
{
    QMutex                         mutex;
    ScriptCanvas*                  canvas = nullptr;    // nullptr once it is gone
    QSize                          size;
    QHash<quint64, CanvasPending>  pending;             // by run
    bool                           flushQueued = false;
};

} // namespace ScriptHost
