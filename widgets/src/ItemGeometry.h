#pragma once
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QPoint>
#include <QRect>
#include <functional>
#include <optional>

class QAbstractScrollArea;
class QWidget;

namespace ScriptHost
{

// Where the parts of a widget are and what of them can be seen. Shared by
// ScriptUi (ui.items, ui.itemAt, visibleRect of nodes) and the canvas (item
// anchors). It keeps no state and knows nothing of ScriptUi, so it works
// without exposeWidgets(); the caller decides what scripts may see (the
// widget, whether text is readable).
//
// An item reference (`ref`) names a part of a widget as JSON:
//   {row, column}                    a cell of a table
//   {path: [r0, r1, ...], column?}   an item of a tree: its row at the top
//                                    level, then below that, ...
//   {row}                            an item of a list
//   {section}                        a section of a header
//   {index}                          a tab, an action: its position
//   {index}                          a graphics item: an id unique in
//                                    its scene that stays the same, not
//                                    a position
//   {part}                           a part drawn by the style
//   {text: {from, to}}               a range of text
//
// Each widget family is a Provider in a .cpp file of its own, listed in
// providers() (ItemGeometry.cpp).
namespace ItemGeometry
{

const int kMaxItems = 5000;

// What a provider needs to list or hit items for a caller.
struct ListContext
{
    QJsonObject    options;                 // {rows?: [first, last], columns?: [first, last], all?: bool}
    const QWidget* root = nullptr;          // rects are in its coordinates
    bool           textReadable = true;     // item text at all; a password field's text never
    int            maxItems = kMaxItems;    // more: `truncated: true`
    // The id scripts know another widget by (a view's header, say), or
    // null when they may not see it. Unset: such widgets are left out.
    std::function<QJsonValue(QWidget*)> idOf;
};

// One widget family. Its functions get only widgets `accepts` took.
struct Provider
{
    const char* kind;                                   // "table", "tabs", "parts", ...
    bool (*accepts)(const QWidget* widget);
    // The ui.items answer: {kind, items: [{ref, rect, visibleRect, ...}],
    // truncated?, ...}, rects in root coordinates (see addRects()).
    QJsonObject (*list)(QWidget* widget, const ListContext& context);
    // The rect of the item `ref` names, in the widget's coordinates; none
    // when there is no such item or it is not laid out.
    std::optional<QRect> (*resolve)(QWidget* widget, const QJsonObject& ref);
    // The item at `local` (widget coordinates) as list() gives it; none
    // when there is none there.
    std::optional<QJsonObject> (*hit)(QWidget* widget, const QPoint& local, const ListContext& context);
};

// Every provider, the most specific first.
const QList<Provider>& providers();
// The families providers() lists, each in a file of its own.
QList<Provider> itemViewProviders();     // ItemGeometryViews.cpp: tables, trees, lists, headers
QList<Provider> graphicsProviders();     // ItemGeometryGraphics.cpp: Graphics View items
QList<Provider> widgetPartProviders();   // ItemGeometryParts.cpp: tabs, menus, toolbars, style parts
// The first provider that accepts the widget, or nullptr.
const Provider* providerFor(const QWidget* widget);

// The rect of the item `ref` names in `widget`'s coordinates; none when
// the widget has no provider or no such item.
std::optional<QRect> itemRect(QWidget* widget, const QJsonObject& ref);

// ---- what is visible -------------------------------------------------------

// `rect` in `widget`'s coordinates mapped to `root`'s, also across windows.
QRect mapToRoot(const QWidget* widget, const QRect& rect, const QWidget* root);

// The part of `rect` (in `widget`'s coordinates) left by the widget and
// every ancestor up to its window - viewports, scroll areas, the window -
// in `widget`'s coordinates; empty when the widget is not shown (also on a
// hidden tab page).
QRect visiblePart(const QWidget* widget, const QRect& rect);

// The visible part of the whole widget in `root`'s coordinates; empty when
// nothing of it is shown.
QRect visibleRect(const QWidget* widget, const QWidget* root);

// {x, y, maxX, maxY}: the scroll bars' values.
QJsonObject scroll(const QAbstractScrollArea* area);

// ---- for providers ---------------------------------------------------------

QJsonObject rectJson(const QRect& rect);

// Sets the item's "rect" and "visibleRect" in root coordinates from `rect`
// in `area`'s coordinates (a viewport, the widget); each null when empty or
// not seen.
void addRects(QJsonObject* item, const QWidget* area, const QRect& rect, const QWidget* root);

// ---- text (ItemGeometryParts.cpp) ------------------------------------------

// QLineEdit, QTextEdit, QPlainTextEdit and a QLabel with plain text.
bool hasTextPositions(const QWidget* widget);
// The positions in its text: QString indexes, a paragraph of a QTextEdit
// ends with one.
int textLength(const QWidget* widget);
// One rect per line for the text from `from` to `to` (clamped, from <= to)
// in the widget's coordinates, not clipped to what is seen; from == to gives
// the caret there, 1 wide. Empty for other widgets.
QList<QRect> textRects(const QWidget* widget, int from, int to);

} // namespace ItemGeometry
} // namespace ScriptHost
