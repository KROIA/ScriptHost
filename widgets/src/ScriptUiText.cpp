#include "ScriptUi.h"
#include "ItemGeometry.h"
#include "ScriptEngine.h"
#include "ScriptUiSupport.h"
#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QToolTip>
#include <QWidget>
#include <utility>

// ui.textGeometry and ui.popups (T-026).

namespace ScriptHost
{

using namespace ScriptUiSupport;

namespace
{

// The text whose positions ItemGeometry::textRects() takes.
QString shownText(const QWidget* widget)
{
    if (const auto* edit = qobject_cast<const QLineEdit*>(widget))
        return edit->displayText();
    if (const auto* edit = qobject_cast<const QTextEdit*>(widget))
        return edit->toPlainText();
    if (const auto* edit = qobject_cast<const QPlainTextEdit*>(widget))
        return edit->toPlainText();
    if (const auto* label = qobject_cast<const QLabel*>(widget))
        return label->text();
    return QString();
}

} // namespace

QWidget* ScriptUiSupport::popupOpener(const QWidget* widget)
{
    // ponytail: the first widget the menu's action sits in, and at most 16
    // parentless menus up - a ring of menus ends there.
    for (int step = 0; step < 16; ++step) {
        const auto* menu = qobject_cast<const QMenu*>(widget);
        if (!menu)
            return nullptr;
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        const QList<QWidget*> associated = menu->menuAction()->associatedWidgets();
#else
        const QList<QObject*> associated = menu->menuAction()->associatedObjects();
#endif
        QWidget* next = nullptr;
        for (QObject* object : associated) {
            if (object != menu && object->isWidgetType()) {
                next = static_cast<QWidget*>(object);
                break;
            }
        }
        if (!next || next->parentWidget() || !qobject_cast<QMenu*>(next))
            return next;
        widget = next;
    }
    return nullptr;
}

QJsonValue ScriptUi::textGeometry(QWidget* widget, const QJsonObject& options) const
{
    if (!ItemGeometry::hasTextPositions(widget))
        throw ScriptError(QStringLiteral("a %1 has no text positions").arg(className(widget)));
    // The engine checked the types of from and to against the schema.
    const int length = ItemGeometry::textLength(widget);
    const bool readable = m_textReadable && !isPassword(widget);
    const QString text = readable ? shownText(widget) : QString();
    auto rectsJson = [&](int from, int to) {
        QJsonArray rects;
        for (const QRect& rect : ItemGeometry::textRects(widget, from, to))
            rects.append(ItemGeometry::rectJson(ItemGeometry::mapToRoot(widget, rect, m_root)));
        return rects;
    };
    // A range in order and in the text; `caret`: a caret rect when empty.
    auto part = [&](int from, int to, bool caret) {
        from = qBound(0, from, length);
        to = qBound(0, to, length);
        if (from > to)
            std::swap(from, to);
        QJsonObject result{{"from", from}, {"to", to},
                           {"rects", from < to || caret ? rectsJson(from, to) : QJsonArray()}};
        if (readable)
            result.insert("text", text.mid(from, to - from));
        return result;
    };

    // A label has no caret; its selection is all it has.
    int cursor = -1;
    int selectionFrom = 0;
    int selectionTo = 0;
    if (const auto* lineEdit = qobject_cast<const QLineEdit*>(widget)) {
        cursor = lineEdit->cursorPosition();
        selectionFrom = lineEdit->hasSelectedText() ? lineEdit->selectionStart() : cursor;
        selectionTo = selectionFrom + lineEdit->selectionLength();
    } else if (const auto* label = qobject_cast<const QLabel*>(widget)) {
        selectionFrom = qMax(0, label->selectionStart());
        selectionTo = selectionFrom + (label->hasSelectedText() ? label->selectedText().size() : 0);
    } else {
        const QTextCursor textCursor = qobject_cast<const QTextEdit*>(widget)
                                           ? static_cast<const QTextEdit*>(widget)->textCursor()
                                           : static_cast<const QPlainTextEdit*>(widget)->textCursor();
        cursor = textCursor.position();
        selectionFrom = textCursor.selectionStart();
        selectionTo = textCursor.selectionEnd();
    }

    QJsonObject result{{"length", length}, {"selection", part(selectionFrom, selectionTo, false)}};
    QJsonValue caret = QJsonValue::Null;
    if (cursor >= 0) {
        const QJsonArray rect = rectsJson(cursor, cursor);
        caret = QJsonObject{{"index", cursor}, {"rect", rect.isEmpty() ? QJsonValue(QJsonValue::Null) : rect.at(0)}};
    }
    result.insert("cursor", caret);
    if (options.contains("from") || options.contains("to"))
        result.insert("range", part(options.value("from").toInt(0), options.value("to").toInt(length), true));
    return result;
}

QList<QWidget*> ScriptUi::popupWindows() const
{
    QList<QWidget*> windows;
    if (!m_root)
        return windows;
    for (QWidget* window : QApplication::topLevelWidgets()) {
        const Qt::WindowType type = window->windowType();
        if (window->isVisible() && (type == Qt::Popup || type == Qt::ToolTip) && isVisible(window))
            windows.append(window);
    }
    return windows;
}

QJsonValue ScriptUi::popups() const
{
    // QToolTip's label (qtooltip.cpp) comes last, on its own.
    const QLatin1String tipName("qtooltip_label");
    QJsonArray result;
    for (QWidget* popup : popupWindows()) {
        if (popup->objectName() == tipName)
            continue;
        QJsonObject entry{{"kind", "popup"}, {"widget", node(popup)}};
        auto* combo = qobject_cast<QComboBox*>(popup->parentWidget());
        if (combo && popup->inherits("QComboBoxPrivateContainer")) {
            entry.insert("kind", "combo");
            entry.insert("combo", widgetId(combo));
            entry.insert("view", widgetId(combo->view()));
        } else if (qobject_cast<QMenu*>(popup)) {
            entry.insert("kind", "menu");
            if (QWidget* opener = popup->parentWidget() ? popup->parentWidget() : popupOpener(popup))
                entry.insert("opener", widgetId(opener));
        }
        result.append(entry);
    }
    if (!QToolTip::isVisible())
        return result;
    // ponytail: QToolTip does not tell for which widget it shows, and on
    // Windows its label has no parent: it belongs to the root when it lies
    // on it or within the 16 px Qt places it from the pointer.
    const QRect root(m_root->mapToGlobal(QPoint()), m_root->size());
    for (QWidget* tip : QApplication::topLevelWidgets()) {
        if (tip->objectName() != tipName || !tip->isVisible() || isExcluded(tip)
            || !(isVisible(tip) || root.adjusted(-16, -16, 16, 16).intersects(tip->geometry())))
            continue;
        QJsonObject entry{{"kind", "tooltip"},
                          {"rect", ItemGeometry::rectJson(ItemGeometry::mapToRoot(tip, tip->rect(), m_root))}};
        if (m_textReadable)
            entry.insert("text", QToolTip::text());
        result.append(entry);
    }
    return result;
}

void ScriptUi::registerTextMethods()
{
    const QJsonObject line{{"x", 12}, {"y", 8}, {"width", 30}, {"height", 16}};
    const QJsonObject range{{"from", 0}, {"to", 5}, {"rects", QJsonArray{line}}, {"text", "Hello"}};
    const QJsonObject position{{"type", "integer"}, {"description", "A position in the text"}};
    add(ScriptMethod(QStringLiteral("ui.textGeometry"))
            .setDescription(QStringLiteral(
                "Where the text of a QLineEdit, QTextEdit, QPlainTextEdit or QLabel (plain text) is: {length, "
                "cursor: {index, rect} (null for a label), selection: {from, to, rects, text?}, range?: {from, to, "
                "rects, text?}}. range when from or to is given (default 0 and the end). Positions are character "
                "indexes, rects one per line in root coordinates, not clipped. A password field gives no text."))
            .addRawParameter(QStringLiteral("widget"), widgetSchema())
            .addRawParameter(QStringLiteral("options"),
                             QJsonObject{{"type", "object"},
                                         {"description", "A range to measure: {from?, to?}"},
                                         {"properties", QJsonObject{{"from", position}, {"to", position}}}},
                             false)
            .setReturns(QStringLiteral("object"), QString(),
                        QJsonObject{{"length", 11},
                                    {"cursor", QJsonObject{{"index", 5},
                                                           {"rect", QJsonObject{{"x", 42}, {"y", 8}, {"width", 1},
                                                                                {"height", 16}}}}},
                                    {"selection", range},
                                    {"range", range}}),
        [this](const QJsonObject& args) -> QJsonValue {
            return textGeometry(resolve(args.value("widget")), args.value("options").toObject());
        });

    add(ScriptMethod(QStringLiteral("ui.popups"))
            .setDescription(QStringLiteral(
                "The open popups of the root: a combo box list {kind: \"combo\", widget: node, combo, view} (its "
                "items: ui.items(view)), a menu {kind: \"menu\", widget, opener?} (its actions: ui.items(widget.id)), "
                "another popup {kind: \"popup\", widget}, the tool tip {kind: \"tooltip\", rect, text?}. Rects in "
                "root coordinates."))
            .setReturns(QStringLiteral("array"), QString(),
                        QJsonArray{QJsonObject{{"kind", "menu"}, {"widget", exampleNode()}, {"opener", -1}}}),
        [this](const QJsonObject&) -> QJsonValue {
            resolveRoot();
            return popups();
        });
}

} // namespace ScriptHost
