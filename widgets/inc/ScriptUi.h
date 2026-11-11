#pragma once
#include "ScriptHost_base.h"
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QPointer>

class QLayout;
class QVariant;
class QWidget;

namespace ScriptHost
{

class ScriptEngine;

// Lets scripts read a widget tree: the `ui.*` methods.
//
//   ScriptHost::exposeWidgets(engine, mainWindow);
//
//   // in a script:
//   const t = ui.tree();                       // the whole tree, one call
//   const ok = ui.find({class: "QPushButton", text: "OK"})[0];
//   log(ui.info(ok.id).sizeHint.width);
//
// The tools are ordinary methods (group "UI", all read-only): each can be
// switched off with ScriptEngine::setMethodEnabled() ("ui.at") or all with
// setMethodGroupEnabled("UI"), they appear in ScriptsWidget, run in dry
// runs, and each call counts against the call limits.
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
// other read-only method, under the approval rules set (setApproval):
// reading the UI reveals how the application is built and what its widgets
// show, so switch the group off (setMethodGroupEnabled("UI", false)) or
// hide widgets if that matters.
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
    void registerMethods();

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

    QPointer<ScriptEngine> m_engine;
    QPointer<QWidget>      m_root;
    bool                   m_textReadable = true;
};

// Registers the ui methods for `root`'s widget tree. Returns the ScriptUi, a
// child of `root`: delete it to stop.
SCRIPT_HOST_WIDGETS_API ScriptUi* exposeWidgets(ScriptEngine* engine, QWidget* root);

} // namespace ScriptHost
