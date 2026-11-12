#pragma once
#include "ScriptHost_base.h"
#include "ScriptMethod.h"
#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QPair>
#include <QPointer>
#include <QStringList>
#include <functional>

class QAbstractButton;
class QDialogButtonBox;
class QLayout;
class QPoint;
class QVariant;
class QWidget;

namespace ScriptHost
{

class ScriptEngine;

// Lets scripts read and change a widget tree, and build GUI of their own:
// the `ui.*` methods.
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
//   const d = ui.create({class: "QDialog", title: "Name?", layout: {type: "vbox", items: [
//       {class: "QLineEdit", name: "answer"},
//       {class: "QDialogButtonBox", standardButtons: "Ok|Cancel"}]}});
//   if (ui.exec(d.id).accepted)
//       log(ui.property(d.ids.answer, "text"));
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
//   ui.items(widget, {rows?: [first, last], columns?: [first, last],
//            all?}?)                the parts of a widget that has them:
//                                   {kind, items: [{ref, rect, visibleRect,
//                                   ...}], truncated?, ...}. `ref` names an
//                                   item (see "Items" below); rects in root
//                                   coordinates, null when the item is not
//                                   laid out or not seen. At most 5000 items,
//                                   then `truncated: true`. A widget without
//                                   items is refused.
//   ui.itemAt(x, y)                 {widget: node, item?}: the nearest widget
//                                   with items from the one under the point
//                                   up, with its item there as ui.items()
//                                   lists it; without an item there, the
//                                   node ui.at() gives. Null outside the root
//   ui.info(widget)                 the node plus geometry (local, in its
//                                   parent), size hints, minimum and maximum
//                                   size, size policy, tool tip, the layout it
//                                   sits in (`parentLayout`) and its own
//                                   (`layout`: type, spacing, margins, items
//                                   with index and stretch, row/column/spans,
//                                   or form row/role); a scroll area also
//                                   `scroll`: {x, y, maxX, maxY}, its scroll
//                                   bars' values
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
// Creating (what a run created goes when the run ends, also when it is
// stopped; a script keeps a dialog open by staying alive - on() handlers do
// that):
//   ui.create(spec, {parent?, at?}?)
//                                   builds a widget or a whole tree in one call
//                                   and returns {id, ids: {name: id}} - the
//                                   top widget and every named one. A spec:
//                                   {class, name?, title?, <property>: value,
//                                    layout?: {type: "vbox"|"hbox"|"grid"|"form",
//                                              spacing?, margins?, items: [...]},
//                                    children?: [spec] (pages of a QTabWidget,
//                                              named by their title; widgets
//                                              of a QSplitter; the one of a
//                                              QScrollArea),
//                                    items?: ["text"] (QComboBox, QListWidget),
//                                    role?: "accept"|"reject" (a button that
//                                              closes its dialog so)}
//                                   Properties as ui.setProperty() takes them;
//                                   `title` is the window title where the class
//                                   has no "title". An item of a layout is a
//                                   spec with its place: stretch (box); row,
//                                   column, rowSpan, columnSpan (grid; row
//                                   defaults to the item's index, column to 0);
//                                   label (form). {stretch: n} alone is a
//                                   stretch in a box. A QDialogButtonBox
//                                   accepts or rejects its dialog. Without
//                                   `parent` the widget is a window of its own,
//                                   closed until ui.show() or ui.exec(); with
//                                   it, it goes into the parent's layout at
//                                   `at` as in ui.moveToLayout(), or onto the
//                                   parent when that has no layout. Only the
//                                   classes allowed (widgetClasses()) are made.
//   ui.exec(dialog)                 opens a QDialog, modal for the user, and
//                                   waits until it closes - the script waits,
//                                   the application does not; returns
//                                   {accepted, button?: the objectName, else
//                                   the text, of the button clicked last}
//   ui.close(widget)                closes it (a dialog: rejected); not the root
//   ui.destroy(widget)              deletes a widget this run created, or one
//                                   below it
//
// Signals (read-only; a run's connections end with it):
//   ui.connect(widget, signal)      forwards the widget's signal - any widget,
//                                   created or the application's - to this run
//                                   as an event and returns the event's name,
//                                   "ui:<id>:<signal>": on(ui.connect(ok,
//                                   "clicked"), fn). `signal` is a name
//                                   ("clicked": its overload with the most
//                                   arguments) or a signature
//                                   ("currentIndexChanged(int)"). The event:
//                                   {widget: id, signal, args: [...]} plus each
//                                   argument by its name ("checked", "index";
//                                   "text", "value" or "arg<n>" where Qt has
//                                   none). A widget argument is its id; a
//                                   string argument of a password field, or of
//                                   any widget with setTextReadable(false), is
//                                   null.
//   ui.disconnect(widget, signal)   stops it; returns whether it was connected
//
// Items: a `ref` is {row, column} (a table cell), {path: [row, row below,
// ...], column?} (a tree item), {row} (a list item), {section} (a header
// section), {index} (a tab, an action), {part} (a part drawn by the style).
//
// A node: {id, name, class, rect: {x, y, width, height} in root
// coordinates, visibleRect, clipped, visible, enabled, focus (the focus
// widget of its window), text?}. `visibleRect` is the part of `rect` left
// by every ancestor up to the window - scroll areas and item viewports
// included - or null when nothing of it is shown (also on a hidden tab
// page); `clipped` is true when that is not the whole widget. Other windows
// on top are not taken into account. `widget` is an id (a number) or an objectName (the first in tree
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
// node or item has `text` or a tool tip, find() refuses `text`, and
// ui.property() refuses every string property but "objectName".
//
// Requested runs (ScriptEngine::requestRun) may call the ui methods like any
// other method, under the approval rules set (setApproval): reading the UI
// reveals how the application is built and what its widgets show, and the
// changing tools can rearrange it. Switch the group or single tools off, or
// hide widgets, if that matters. Signals reach a requested run only with
// ScriptEngine::allowEventForRequestedRuns("ui"); ui.connect() refuses
// otherwise.
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

    // The classes ui.create() makes. Allowed from the start: QWidget,
    // QDialog, QFrame, QGroupBox, QTabWidget, QScrollArea, QSplitter, QLabel,
    // QPushButton, QToolButton, QCheckBox, QRadioButton, QLineEdit, QTextEdit,
    // QPlainTextEdit, QSpinBox, QDoubleSpinBox, QSlider, QComboBox,
    // QListWidget, QProgressBar, QDialogButtonBox. A factory returns a new
    // widget without a parent; adding a name again replaces its factory.
    //   ui->addWidgetFactory("Gauge", [] { return new Gauge; });
    using WidgetFactory = std::function<QWidget*()>;
    void addWidgetFactory(const QString& className, WidgetFactory factory);
    void removeWidgetClass(const QString& className);
    QStringList widgetClasses() const;

private:
    void add(ScriptMethod method, ScriptMethodHandler handler, bool readOnly = true);
    void addAsync(ScriptMethod method, AsyncScriptMethodHandler handler);
    void registerMethods();
    void registerChangingMethods();
    void registerCreatingMethods();
    // The tools of FR-007, each set in a file of its own; registerMethods()
    // calls them.
    void registerItemMethods();     // ScriptUiItems.cpp: ui.items, ui.itemAt
    void registerTextMethods();     // ScriptUiText.cpp: ui.textGeometry, ui.popups
    void registerWindowMethods();   // ScriptUiWindows.cpp: ui.map, ui.window

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
    // The widget under the point (root coordinates) a script may see.
    QWidget* widgetAt(const QPoint& point) const;
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

    // Creating, and signals.
    struct Built
    {
        QJsonObject                          ids;       // name -> id
        QList<QPair<QAbstractButton*, bool>> roles;     // a button, and whether it accepts
        QList<QDialogButtonBox*>             boxes;
    };
    QJsonValue create(const QJsonObject& spec, const QJsonObject& options);
    // The spec's widget with everything below it, without a parent; the
    // caller owns it.
    QWidget* build(const QJsonObject& spec, Built* built);
    void buildLayout(QWidget* owner, const QJsonObject& spec, Built* built);
    void exec(const QJsonObject& args, ScriptCompletion done);
    // Created by the calling run, or below such a widget.
    bool isCreated(const QWidget* widget) const;
    // The index of the signal in the widget's class, and its event's name.
    int signalOf(QWidget* widget, const QString& signal, QString* event) const;
    QJsonValue connectSignal(QWidget* widget, const QString& signal);
    QJsonObject signalData(QWidget* widget, int index, void** argv) const;

    // ---- ScriptUiItems.cpp
    QJsonValue items(QWidget* widget, const QJsonObject& options) const;
    QJsonValue itemAt(int x, int y) const;

    // ---- ScriptUiText.cpp

    // ---- ScriptUiWindows.cpp


    QPointer<ScriptEngine> m_engine;
    QPointer<QWidget>      m_root;
    QStringList            m_methods;   // the names this ScriptUi registered
    bool                   m_textReadable = true;
    QHash<quint64, QList<std::function<bool()>>> m_journal;    // run -> its undo steps, oldest first
    QHash<quint64, QList<QPointer<QWidget>>>     m_created;    // run -> the top widgets it created
    QHash<quint64, QHash<QString, QPointer<QObject>>> m_connections;   // run -> event -> forwarder
    QHash<QString, WidgetFactory>                m_factories;  // class -> factory
};

// Registers the ui methods for `root`'s widget tree. Returns the ScriptUi, a
// child of `root`: delete it to stop.
SCRIPT_HOST_WIDGETS_API ScriptUi* exposeWidgets(ScriptEngine* engine, QWidget* root);

} // namespace ScriptHost
