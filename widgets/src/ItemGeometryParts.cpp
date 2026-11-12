#include "ItemGeometry.h"
#include <QAbstractItemView>
#include <QAction>
#include <QComboBox>
#include <QDockWidget>
#include <QGroupBox>
#include <QJsonArray>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QSlider>
#include <QSpinBox>
#include <QStyle>
#include <QStyleOption>
#include <QTabBar>
#include <QTabWidget>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextEdit>
#include <QTextLayout>
#include <QToolBar>
#include <QToolBox>
#include <initializer_list>
#include <memory>
#include <utility>
#include <vector>

// The providers of tabs, menus, toolbars, parts drawn by the style (T-026),
// and where text is in text widgets.

namespace ScriptHost
{
namespace ItemGeometry
{

namespace
{

// A part of a widget: its ref, its rect in the widget's coordinates (empty
// when not laid out) and what else ui.items tells of it.
struct Entry
{
    QJsonObject ref;
    QRect       rect;
    QJsonObject fields;
};
using Entries = QList<Entry>;

// "&File" -> "File", "&&" -> "&".
QString withoutMnemonic(QString text)
{
    for (int i = 0; i < text.size(); ++i) {
        if (text.at(i) == QLatin1Char('&'))
            text.remove(i, 1);
    }
    return text;
}

// initStyleOption() is protected in Qt 5; a using-declaration in a derived
// class makes it reachable through a pointer to member.
template <class W, class O>
O styleOption(const W* widget)
{
    struct Access : W
    {
        using W::initStyleOption;
    };
    O option;
    (widget->*(&Access::initStyleOption))(&option);
    return option;
}

// The sub-controls the option has, by name. Slider and scroll bar options
// name none (SC_None) and have them all.
Entries styleParts(const QWidget* widget, QStyle::ComplexControl control, const QStyleOptionComplex& option,
                   std::initializer_list<std::pair<const char*, QStyle::SubControl>> parts)
{
    Entries entries;
    for (const auto& [name, sub] : parts) {
        if (option.subControls == QStyle::SC_None || (option.subControls & sub))
            entries.append({QJsonObject{{"part", QLatin1String(name)}},
                            widget->style()->subControlRect(control, &option, sub, widget), QJsonObject()});
    }
    return entries;
}

Entries scrollBarParts(const QScrollBar* bar)
{
    return styleParts(bar, QStyle::CC_ScrollBar, styleOption<QScrollBar, QStyleOptionSlider>(bar),
                      {{"addLine", QStyle::SC_ScrollBarAddLine}, {"subLine", QStyle::SC_ScrollBarSubLine},
                       {"addPage", QStyle::SC_ScrollBarAddPage}, {"subPage", QStyle::SC_ScrollBarSubPage},
                       {"slider", QStyle::SC_ScrollBarSlider}});
}

// ------------------------------------------------------------ the families

struct Tabs
{
    static constexpr const char* kind = "tabs";
    static bool accepts(const QWidget* widget)
    {
        return qobject_cast<const QTabBar*>(widget) || qobject_cast<const QTabWidget*>(widget);
    }
    static Entries entries(const QWidget* widget, const ListContext& context)
    {
        const auto* tabWidget = qobject_cast<const QTabWidget*>(widget);
        const QTabBar* bar = tabWidget ? tabWidget->tabBar() : static_cast<const QTabBar*>(widget);
        const QPoint offset = bar->mapTo(widget, QPoint());
        Entries entries;
        for (int i = 0; i < bar->count(); ++i) {
            QJsonObject fields{{"current", i == bar->currentIndex()}, {"enabled", bar->isTabEnabled(i)}};
            if (context.textReadable) {
                fields.insert("text", withoutMnemonic(bar->tabText(i)));
                if (!bar->tabToolTip(i).isEmpty())
                    fields.insert("tooltip", bar->tabToolTip(i));
            }
            const QRect rect = bar->tabRect(i);
            entries.append({QJsonObject{{"index", i}}, rect.isEmpty() ? QRect() : rect.translated(offset), fields});
        }
        return entries;
    }
};

struct ToolBox
{
    static constexpr const char* kind = "toolbox";
    static bool accepts(const QWidget* widget) { return qobject_cast<const QToolBox*>(widget); }
    static Entries entries(const QWidget* widget, const ListContext& context)
    {
        const auto* box = static_cast<const QToolBox*>(widget);
        Entries entries;
        for (int i = 0; i < box->count(); ++i) {
            // The layout holds each page's button, then the page (qtoolbox.cpp).
            QLayoutItem* item = box->layout() ? box->layout()->itemAt(2 * i) : nullptr;
            const QWidget* button = item ? item->widget() : nullptr;
            QJsonObject fields{{"current", i == box->currentIndex()}, {"enabled", box->isItemEnabled(i)}};
            if (context.textReadable) {
                fields.insert("text", withoutMnemonic(box->itemText(i)));
                if (!box->itemToolTip(i).isEmpty())
                    fields.insert("tooltip", box->itemToolTip(i));
            }
            entries.append({QJsonObject{{"index", i}}, button && !button->isHidden() ? button->geometry() : QRect(),
                            fields});
        }
        return entries;
    }
};

QJsonObject actionFields(const QAction* action, const ListContext& context)
{
    QJsonObject fields{{"enabled", action->isEnabled()}, {"visible", action->isVisible()},
                       {"checkable", action->isCheckable()}, {"checked", action->isChecked()},
                       {"separator", action->isSeparator()}, {"submenu", action->menu() != nullptr},
                       {"shortcut", action->shortcut().toString()}};
    if (context.textReadable)
        fields.insert("text", withoutMnemonic(action->text()));
    return fields;
}

struct Menu
{
    static constexpr const char* kind = "menu";
    static bool accepts(const QWidget* widget)
    {
        return qobject_cast<const QMenuBar*>(widget) || qobject_cast<const QMenu*>(widget);
    }
    static Entries entries(const QWidget* widget, const ListContext& context)
    {
        const auto* bar = qobject_cast<const QMenuBar*>(widget);
        const QList<QAction*> actions = widget->actions();
        Entries entries;
        for (int i = 0; i < actions.size(); ++i) {
            QAction* action = actions.at(i);
            const QRect rect = bar ? bar->actionGeometry(action)
                                   : static_cast<const QMenu*>(widget)->actionGeometry(action);
            entries.append({QJsonObject{{"index", i}}, rect, actionFields(action, context)});
        }
        return entries;
    }
};

struct ToolBar
{
    static constexpr const char* kind = "toolbar";
    static bool accepts(const QWidget* widget) { return qobject_cast<const QToolBar*>(widget); }
    static Entries entries(const QWidget* widget, const ListContext& context)
    {
        const auto* bar = static_cast<const QToolBar*>(widget);
        // The button that opens what does not fit (qtoolbarextension.cpp).
        const QWidget* extension = bar->findChild<QWidget*>(QStringLiteral("qt_toolbar_ext_button"),
                                                            Qt::FindDirectChildrenOnly);
        const bool overflows = extension && !extension->isHidden();
        const QList<QAction*> actions = bar->actions();
        Entries entries;
        for (int i = 0; i < actions.size(); ++i) {
            QAction* action = actions.at(i);
            QWidget* button = bar->widgetForAction(action);
            const bool shown = button && !button->isHidden();
            QJsonObject fields = actionFields(action, context);
            // The layout hides what does not fit; the extension's menu has it.
            fields.insert("inOverflow", action->isVisible() && !shown && overflows);
            if (button && context.idOf) {
                const QJsonValue id = context.idOf(button);
                if (!id.isNull())
                    fields.insert("widget", id);
            }
            entries.append({QJsonObject{{"index", i}}, shown ? button->geometry() : QRect(), fields});
        }
        if (extension)
            entries.append({QJsonObject{{"part", "extension"}}, overflows ? extension->geometry() : QRect(),
                            QJsonObject{{"visible", overflows}}});
        return entries;
    }
};

struct Parts
{
    static constexpr const char* kind = "parts";
    static bool accepts(const QWidget* widget)
    {
        return qobject_cast<const QSlider*>(widget) || qobject_cast<const QScrollBar*>(widget)
            || qobject_cast<const QAbstractSpinBox*>(widget) || qobject_cast<const QComboBox*>(widget)
            || qobject_cast<const QGroupBox*>(widget) || qobject_cast<const QDockWidget*>(widget)
            || (qobject_cast<const QAbstractScrollArea*>(widget) && !qobject_cast<const QAbstractItemView*>(widget));
    }
    static Entries entries(const QWidget* widget, const ListContext&)
    {
        if (const auto* slider = qobject_cast<const QSlider*>(widget))
            return styleParts(slider, QStyle::CC_Slider, styleOption<QSlider, QStyleOptionSlider>(slider),
                              {{"groove", QStyle::SC_SliderGroove}, {"handle", QStyle::SC_SliderHandle}});
        if (const auto* bar = qobject_cast<const QScrollBar*>(widget))
            return scrollBarParts(bar);
        if (const auto* spin = qobject_cast<const QAbstractSpinBox*>(widget))
            return styleParts(spin, QStyle::CC_SpinBox, styleOption<QAbstractSpinBox, QStyleOptionSpinBox>(spin),
                              {{"up", QStyle::SC_SpinBoxUp}, {"down", QStyle::SC_SpinBoxDown},
                               {"edit", QStyle::SC_SpinBoxEditField}});
        if (const auto* combo = qobject_cast<const QComboBox*>(widget))
            return styleParts(combo, QStyle::CC_ComboBox, styleOption<QComboBox, QStyleOptionComboBox>(combo),
                              {{"arrow", QStyle::SC_ComboBoxArrow}, {"edit", QStyle::SC_ComboBoxEditField}});
        if (const auto* group = qobject_cast<const QGroupBox*>(widget))
            return styleParts(group, QStyle::CC_GroupBox, styleOption<QGroupBox, QStyleOptionGroupBox>(group),
                              {{"checkbox", QStyle::SC_GroupBoxCheckBox}, {"label", QStyle::SC_GroupBoxLabel}});
        if (const auto* dock = qobject_cast<const QDockWidget*>(widget)) {
            // The option's rect is the title bar (QDockWidget::initStyleOption).
            Entries entries{{QJsonObject{{"part", "title"}},
                             styleOption<QDockWidget, QStyleOptionDockWidget>(dock).rect, QJsonObject()}};
            for (const auto& [name, objectName] : {std::pair<const char*, const char*>("close", "qt_dockwidget_closebutton"),
                                                   std::pair<const char*, const char*>("float", "qt_dockwidget_floatbutton")}) {
                const QWidget* button = dock->findChild<QWidget*>(QLatin1String(objectName), Qt::FindDirectChildrenOnly);
                if (button)
                    entries.append({QJsonObject{{"part", QLatin1String(name)}},
                                    button->isHidden() ? QRect() : button->geometry(), QJsonObject()});
            }
            return entries;
        }
        // A scroll area: the parts of its scroll bars, "vertical.slider" ...
        const auto* area = static_cast<const QAbstractScrollArea*>(widget);
        Entries entries;
        for (const auto& [prefix, bar] : {std::pair<const char*, const QScrollBar*>("horizontal.", area->horizontalScrollBar()),
                                          std::pair<const char*, const QScrollBar*>("vertical.", area->verticalScrollBar())}) {
            if (!bar->isVisibleTo(area))
                continue;
            const QPoint offset = bar->mapTo(area, QPoint());
            for (Entry entry : scrollBarParts(bar)) {
                entry.ref["part"] = QLatin1String(prefix) + entry.ref.value("part").toString();
                entry.rect.translate(offset);
                entries.append(entry);
            }
        }
        return entries;
    }
};

// ------------------------------------------------------------ list, resolve, hit

// Every key of `entry` has the same value in `ref`.
bool sameRef(const QJsonObject& entry, const QJsonObject& ref)
{
    for (auto it = entry.constBegin(); it != entry.constEnd(); ++it) {
        if (ref.value(it.key()) != it.value())
            return false;
    }
    return true;
}

QJsonObject itemJson(QWidget* widget, const Entry& entry, const QWidget* root)
{
    QJsonObject item = entry.fields;
    item.insert("ref", entry.ref);
    addRects(&item, widget, entry.rect, root);
    return item;
}

template <class Family>
Provider provider()
{
    return Provider{
        Family::kind, &Family::accepts,
        [](QWidget* widget, const ListContext& context) {
            QJsonArray items;
            for (const Entry& entry : Family::entries(widget, context))
                items.append(itemJson(widget, entry, context.root));
            return QJsonObject{{"kind", Family::kind}, {"items", items}};
        },
        [](QWidget* widget, const QJsonObject& ref) -> std::optional<QRect> {
            ListContext context;
            context.textReadable = false;
            for (const Entry& entry : Family::entries(widget, context)) {
                if (sameRef(entry.ref, ref))
                    return entry.rect.isEmpty() ? std::nullopt : std::optional<QRect>(entry.rect);
            }
            return std::nullopt;
        },
        [](QWidget* widget, const QPoint& local, const ListContext& context) -> std::optional<QJsonObject> {
            // The smallest part there: a slider's handle, not its groove.
            const Entries entries = Family::entries(widget, context);
            const Entry* found = nullptr;
            for (const Entry& entry : entries) {
                const QRect& rect = entry.rect;
                if (!rect.isEmpty() && rect.contains(local)
                    && (!found || qint64(rect.width()) * rect.height()
                                      < qint64(found->rect.width()) * found->rect.height()))
                    found = &entry;
            }
            return found ? std::optional<QJsonObject>(itemJson(widget, *found, context.root)) : std::nullopt;
        }};
}

// ------------------------------------------------------------ text

const QLabel* plainLabel(const QWidget* widget)
{
    const auto* label = qobject_cast<const QLabel*>(widget);
    const bool plain = label
                    && (label->textFormat() == Qt::PlainText
                        || (label->textFormat() == Qt::AutoText && !Qt::mightBeRichText(label->text())));
    return plain ? label : nullptr;
}

// The rects of [from, to] in the lines of `layout`, whose text starts at
// position `start` and whose line positions count from `origin`. A caret
// (from == to) only once.
void appendLineRects(const QTextLayout& layout, int start, const QPointF& origin, int from, int to,
                     QList<QRect>* rects)
{
    for (int i = 0; i < layout.lineCount(); ++i) {
        const QTextLine line = layout.lineAt(i);
        const int a = qMax(from, start + line.textStart());
        const int b = qMin(to, start + line.textStart() + line.textLength());
        if (a > b || (a == b && (from != to || !rects->isEmpty())))
            continue;
        const qreal x1 = line.cursorToX(a - start);
        const qreal x2 = line.cursorToX(b - start);
        rects->append(QRectF(origin.x() + qMin(x1, x2), origin.y() + line.y(), qMax(1.0, qAbs(x2 - x1)),
                             line.height()).toRect());
    }
}

template <class Edit>
void appendDocumentRects(const Edit* edit, int from, int to, QList<QRect>* rects)
{
    const QPoint viewport = edit->viewport()->mapTo(edit, QPoint());
    const QTextDocument* document = edit->document();
    for (QTextBlock block = document->findBlock(from); block.isValid() && block.position() <= to;
         block = block.next()) {
        const QTextLayout* layout = block.layout();
        // ponytail: a block not laid out yet gives no rects; lay it out
        // here if documents that big show up.
        if (!layout || layout->lineCount() == 0)
            continue;
        // Where the block is: from the caret at its start, which
        // cursorRect() places with the scrolling and the margins.
        const QRect first = edit->cursorRect(QTextCursor(block)).translated(viewport);
        const QTextLine line = layout->lineAt(0);
        appendLineRects(*layout, block.position(), QPointF(first.x() - line.cursorToX(0), first.y() - line.y()),
                        from, to, rects);
    }
}

// ponytail: plain text as QPainter::drawText lays it out, without
// indent() and the indent of a framed label; a few pixels off there.
void appendLabelRects(const QLabel* label, int from, int to, QList<QRect>* rects)
{
    const int margin = label->margin();
    const QRect area = label->contentsRect().adjusted(margin, margin, -margin, -margin);
    const Qt::Alignment align = QStyle::visualAlignment(label->layoutDirection(), label->alignment());
    std::vector<std::unique_ptr<QTextLayout>> layouts;
    qreal height = 0;
    for (const QString& paragraph : label->text().split(QLatin1Char('\n'))) {
        auto layout = std::make_unique<QTextLayout>(paragraph, label->font());
        layout->beginLayout();
        for (QTextLine line = layout->createLine(); line.isValid(); line = layout->createLine()) {
            line.setLineWidth(label->wordWrap() ? area.width() : 1e6);
            const qreal free = area.width() - line.naturalTextWidth();
            line.setPosition(QPointF(align & Qt::AlignRight ? free : align & Qt::AlignHCenter ? free / 2 : 0, height));
            height += line.height();
        }
        layout->endLayout();
        layouts.push_back(std::move(layout));
    }
    const qreal top = align & Qt::AlignBottom    ? area.bottom() + 1 - height
                    : align & Qt::AlignVCenter   ? area.top() + (area.height() - height) / 2
                                                 : area.top();
    int start = 0;
    for (const auto& layout : layouts) {
        appendLineRects(*layout, start, QPointF(area.left(), top), from, to, rects);
        start += layout->text().size() + 1;
    }
}

} // namespace

QList<Provider> widgetPartProviders()
{
    return {provider<Tabs>(), provider<ToolBox>(), provider<Menu>(), provider<ToolBar>(), provider<Parts>()};
}

bool hasTextPositions(const QWidget* widget)
{
    return qobject_cast<const QLineEdit*>(widget) || qobject_cast<const QTextEdit*>(widget)
        || qobject_cast<const QPlainTextEdit*>(widget) || plainLabel(widget);
}

int textLength(const QWidget* widget)
{
    if (const auto* edit = qobject_cast<const QLineEdit*>(widget))
        return edit->displayText().size();
    if (const auto* edit = qobject_cast<const QTextEdit*>(widget))
        return edit->document()->characterCount() - 1;
    if (const auto* edit = qobject_cast<const QPlainTextEdit*>(widget))
        return edit->document()->characterCount() - 1;
    if (const QLabel* label = plainLabel(widget))
        return label->text().size();
    return 0;
}

QList<QRect> textRects(const QWidget* widget, int from, int to)
{
    const int length = textLength(widget);
    from = qBound(0, from, length);
    to = qBound(from, to, length);
    QList<QRect> rects;
    if (const auto* lineEdit = qobject_cast<const QLineEdit*>(widget)) {
        // Laid out as QWidgetLineControl does: the shown text, one line.
        QTextLayout layout(lineEdit->displayText(), lineEdit->font());
        layout.beginLayout();
        const QTextLine line = layout.createLine();
        layout.endLayout();
        // Qt's cursor rect starts 5 px left of the caret (rectForPos()).
        const QRect caret = lineEdit->inputMethodQuery(Qt::ImCursorRectangle).toRect();
        appendLineRects(layout, 0, QPointF(caret.x() + 5 - line.cursorToX(lineEdit->cursorPosition()), caret.y()),
                        from, to, &rects);
    } else if (const auto* textEdit = qobject_cast<const QTextEdit*>(widget)) {
        appendDocumentRects(textEdit, from, to, &rects);
    } else if (const auto* plainEdit = qobject_cast<const QPlainTextEdit*>(widget)) {
        appendDocumentRects(plainEdit, from, to, &rects);
    } else if (const QLabel* label = plainLabel(widget)) {
        appendLabelRects(label, from, to, &rects);
    }
    return rects;
}

} // namespace ItemGeometry
} // namespace ScriptHost
