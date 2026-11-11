#pragma once
#include "ScriptHost_base.h"
#include "ScriptMethod.h"
#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QPointer>
#include <functional>

class QLayout;
class QVariant;
class QWidget;

namespace ScriptHost
{

class ScriptEngine;

// Lets scripts read and change a widget tree: the `ui.*` methods.
//
//   ScriptHost::exposeWidgets(engine, mainWindow);
//
//   // in a script:
//   const t = ui.tree();                       // the whole tree, one call
//   const ok = ui.find({class: "QPushButton", text: "OK"})[0];
//   log(ui.info(ok.id).sizeHint.width);
//   ui.setProperty(ok.id, "text", "Apply");
//   ui.restore();                              // back as it was
//
// The tools are ordinary methods in the group "UI": each can be switched off
// with ScriptEngine::setMethodEnabled() ("ui.at") or all with
// setMethodGroupEnabled("UI"), they appear in ScriptsWidget, and each call
// counts against the call limits. The reading ones are read-only; the
// changing ones are not, so a dry run records them without changing anything
// and the approval rules for changing scripts apply.
//
// Reading:
//   ui.root()                       the root widget's node
//   ui.tree(widget?, depth?)        a node with `children`, nested down to
//                                   `depth` levels (default: all); a node
//                                   below the depth has no `children`
//   ui.children(widget)             its child nodes, without their children
//   ui.parent(widget)               the parent's node; null for the root
//   ui.find({name?, class?, text?, exact?})
//                                   the nodes in tree order that match all
//                                   given: name and text as a substring
//                                   ignoring case, or equal with exact: true;
//                                   class by inheritance ("QAbstractButton"
//                                   finds the push buttons)
//   ui.at(x, y)                     the node of the widget under the point
//                                   (root coordinates, shown widgets only);
//                                   null outside the root
//   ui.info(widget)                 the node plus geometry (local, in its
//                                   parent), size hints, minimum and maximum
//                                   size, size policy, tool tip, the layout it
//                                   sits in (`parentLayout`) and its own
//                                   (`layout`: type, spacing, margins, items
//                                   with index and stretch, row/column/spans,
//                                   or form row/role)
//   ui.property(widget, name)       a readable Q_PROPERTY as JSON
//   ui.focused()                    the node of the focus widget, or null
//   ui.tabOrder(widget?)            the nodes of the widget's window (default:
//                                   the root's) that take the focus by Tab,
//                                   in tab order
//
// Changing (each returns the widget's node unless said otherwise; with
// managedByLayout: true when a layout places the widget - the layout may
// move or resize it again at its next pass):
//   ui.move(widget, x, y), ui.resize(widget, width, height),
//   ui.setGeometry(widget, x, y, width, height)    in the parent's coordinates
//   ui.show(widget), ui.hide(widget)               a window opens or closes
//   ui.setEnabled(widget, enabled)
//   ui.raise(widget), ui.lower(widget)             among its siblings
//   ui.setFocus(widget)
//   ui.setTabOrder([widget, widget, ...])          Tab goes from each to the
//                                                  next, all in one window;
//                                                  returns the new tab order
//   ui.setCurrentIndex(widget, index)              QTabWidget, QStackedWidget,
//                                                  QComboBox, QTabBar, QToolBox
//   ui.setLayout(widget, {spacing?, margins?, alignment?, direction?})
//                                   the widget's own layout; margins a number
//                                   or {left?, top?, right?, bottom?},
//                                   alignment "AlignLeft|AlignTop", direction
//                                   "LeftToRight" ... "BottomToTop" (box
//                                   layouts); returns the layout as in info()
//   ui.setStretch(widget, stretch, at?)
//                                   the widget's stretch in its box layout;
//                                   with at {row} or {column}, that row's or
//                                   column's stretch in the widget's own grid
//                                   layout (returns the layout)
//   ui.setSplitterSizes(widget, [sizes])   returns the sizes taken
//   ui.moveToLayout(widget, target, at?)
//                                   into the layout of `target`, reparented:
//                                   at {index} in a box layout (default: the
//                                   end), {row, column, rowSpan?, columnSpan?}
//                                   in a grid, {index} as a new row of a form
//   ui.reorder(widget, index)       within its box layout, with its stretch
//   ui.setProperty(widget, name, value)
//                                   a writable Q_PROPERTY, from JSON as below;
//                                   strings for sizes "100x20", points
//                                   "10, 20", rects "10, 20, 100, 30"; colours
//                                   also as a name or {r, g, b, a?}; a font as
//                                   the parts to change
//   ui.restore()                    puts back everything this run changed
//                                   with these tools, the latest first, and
//                                   skips widgets that are gone; returns
//                                   {restored, skipped}
// Changes stay after the run ends unless it calls ui.restore(); what a run
// changed is remembered only while it runs.
//
// A node: {id, name, class, rect: {x, y, width, height} in root
// coordinates, visible, enabled, focus (the focus widget of its window),
// text?}. `widget` is an id (a number) or an objectName (the first in tree
// order). Every widget a script sees gets an id that stays its own while it
// lives. JSON of property values: numbers, strings, booleans; enums by their
// key name, flags as "AlignLeft|AlignTop"; sizes {width, height}, points
// {x, y}, rects {x, y, width, height}, colours "#rrggbb" or "#aarrggbb",
// fonts {family, pointSize, pixelSize, bold, italic}, size policies
// {horizontal, vertical, horizontalStretch, verticalStretch}; null for a
// type JSON has no form for.
//
// What scripts see: the root and the widgets below it - child windows such
// as dialogs included - and nothing else. Never ScriptHost's own widgets
// (canvas, script editor, scripts panel, approval dialog), and never a
// widget the application hid with hideWidget() or the dynamic property
// "scripthost_hidden" set to true, nor anything below it. A layout item of
// such a widget is left out of `layout.items` (the others keep their index).
//
// Text: `text` of a node is the widget's "text", "title", "plainText" or
// "currentText" property, or a window's title. The text of a QLineEdit
// whose echo mode is not Normal (a password) is never returned: no `text`
// in its node, find() does not match it, and ui.property() refuses its
// "text", "displayText" and "selectedText". With setTextReadable(false) no
// node has `text` or a tool tip, find() refuses `text`, and ui.property()
// refuses every string property but "objectName".
//
// Requested runs (ScriptEngine::requestRun) may call the ui methods like any
// other method, under the approval rules set (setApproval): reading the UI
// reveals how the application is built and what its widgets show, and the
// changing tools can rearrange it. Switch the group or single tools off, or
// hide widgets, if that matters.
//
// One ScriptUi per engine: a second one exposed on the same engine takes the
// ui methods over. It is a child of the root: delete it to stop - the ui
// methods are unregistered. GUI thread only.
class SCRIPT_HOST_WIDGETS_API ScriptUi : public QObject
{
    Q_OBJECT
public:
    // exposeWidgets() is the short way.
    ScriptUi(ScriptEngine* engine, QWidget* root);
    ~ScriptUi() override;

    QWidget* root() const { return m_root; }

    // Hides the widget and everything below it from scripts (sets the
    // dynamic property "scripthost_hidden"), for every ScriptUi.
    static void hideWidget(QWidget* widget, bool hidden = true);

    // Default true. See "Text" above.
    void setTextReadable(bool readable) { m_textReadable = readable; }
    bool isTextReadable() const { return m_textReadable; }

private:
    void add(ScriptMethod method, ScriptMethodHandler handler, bool readOnly = true);
    void registerMethods();
    void registerChangingMethods();

    // Resolving and describing widgets - what every ui tool builds on. The
    // ones that find a widget throw ScriptError when there is none.
    // The widget a script names: an id or an objectName, among the visible ones.
    QWidget* resolve(const QJsonValue& widget) const;
    QWidget* resolveRoot() const;
    // In the root's subtree and not hidden from scripts.
    bool isVisible(const QWidget* widget) const;
    QList<QWidget*> visibleChildren(const QWidget* widget) const;
    // The root and all visible widgets below it, in tree order.
    QList<QWidget*> visibleWidgets() const;
    // The widget's text, when it has one and scripts may read it.
    bool readText(const QWidget* widget, QString* text) const;
    // May scripts read this value of the widget's property?
    bool isReadable(const QWidget* widget, const QString& name, const QVariant& value) const;

    // The tools.
    QJsonObject node(QWidget* widget) const;
    QJsonObject tree(QWidget* widget, int depth) const;
    QJsonObject info(QWidget* widget) const;
    QJsonObject layoutInfo(QLayout* layout) const;
    QJsonValue find(const QJsonObject& query) const;
    QJsonValue at(int x, int y) const;
    QJsonValue readProperty(QWidget* widget, const QString& name) const;
    QJsonValue focused() const;
    QJsonValue tabOrder(QWidget* widget) const;
    // The widgets of the widget's window that take the focus by Tab.
    QList<QWidget*> tabChain(QWidget* widget) const;

    // The changing tools. Each records how to undo it in the journal of the
    // calling run; `undo` runs only while the widget lives.
    void record(QWidget* widget, std::function<void(QWidget*)> undo);
    QJsonValue restore();
    // The node, with managedByLayout.
    QJsonObject changed(QWidget* widget) const;
    QJsonValue setLayout(QWidget* widget, QLayout* layout, const QJsonObject& options);
    QJsonValue setStretch(QWidget* widget, int stretch, const QJsonObject& args);
    QJsonValue moveToLayout(QWidget* widget, QWidget* target, const QJsonObject& at);

    QPointer<ScriptEngine> m_engine;
    QPointer<QWidget>      m_root;
    bool                   m_textReadable = true;
    QHash<quint64, QList<std::function<bool()>>> m_journal;    // run -> its undo steps, oldest first
};

// Registers the ui methods for `root`'s widget tree. Returns the ScriptUi, a
// child of `root`: delete it to stop.
SCRIPT_HOST_WIDGETS_API ScriptUi* exposeWidgets(ScriptEngine* engine, QWidget* root);

} // namespace ScriptHost
