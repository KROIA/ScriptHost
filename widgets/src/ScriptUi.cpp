#include "ScriptUi.h"
#include "ScriptEngine.h"
#include <QApplication>
#include <QBoxLayout>
#include <QColor>
#include <QFont>
#include <QFormLayout>
#include <QGridLayout>
#include <QKeySequence>
#include <QLineEdit>
#include <QMetaEnum>
#include <QMetaProperty>
#include <QSizePolicy>
#include <QUrl>
#include <QWidget>
#include <cstring>

namespace ScriptHost
{

namespace
{

// Engine property: the ScriptUi whose ui methods the engine has.
const char* const kOwner = "scripthost_scriptUi";
// Widget properties: its id, and "hidden from scripts".
const char* const kId = "scripthost_id";
const char* const kHidden = "scripthost_hidden";

const char* const kMethods[] = {"ui.root", "ui.tree", "ui.children", "ui.parent", "ui.find", "ui.at",
                                "ui.info", "ui.property", "ui.focused", "ui.tabOrder"};

// The id a script knows the widget by: given on first sight, kept in the
// widget, so it lives exactly as long as the widget and is the same for
// every ScriptUi.
int widgetId(QWidget* widget)
{
    static int lastId = 0;
    QVariant id = widget->property(kId);
    if (!id.isValid()) {
        id = ++lastId;
        widget->setProperty(kId, id);
    }
    return id.toInt();
}

// ScriptHost's own widgets (canvas, editor, scripts panel and what derives
// from them) and the ones the application hid.
bool isExcluded(const QWidget* widget)
{
    if (widget->property(kHidden).toBool())
        return true;
    for (const QMetaObject* meta = widget->metaObject(); meta; meta = meta->superClass()) {
        if (qstrncmp(meta->className(), "ScriptHost::", 12) == 0)
            return true;
    }
    return false;
}

bool isPassword(const QWidget* widget)
{
    const auto* edit = qobject_cast<const QLineEdit*>(widget);
    return edit && edit->echoMode() != QLineEdit::Normal;
}

QString className(const QObject* object)
{
    return QString::fromLatin1(object->metaObject()->className());
}

QJsonObject rectJson(const QRectF& rect)
{
    return QJsonObject{{"x", rect.x()}, {"y", rect.y()}, {"width", rect.width()}, {"height", rect.height()}};
}

QJsonObject sizeJson(const QSizeF& size)
{
    return QJsonObject{{"width", size.width()}, {"height", size.height()}};
}

// ------------------------------------------------------------ JSON of values

// What a property value is for a script. Enums are converted by
// propertyJson(), which knows their names.
QJsonValue toJson(const QVariant& value)
{
    switch (value.userType()) {
    case QMetaType::QSize:
    case QMetaType::QSizeF:
        return sizeJson(value.toSizeF());
    case QMetaType::QPoint:
    case QMetaType::QPointF: {
        const QPointF point = value.toPointF();
        return QJsonObject{{"x", point.x()}, {"y", point.y()}};
    }
    case QMetaType::QRect:
    case QMetaType::QRectF:
        return rectJson(value.toRectF());
    case QMetaType::QColor: {
        const QColor color = value.value<QColor>();
        return color.name(color.alpha() == 255 ? QColor::HexRgb : QColor::HexArgb);
    }
    case QMetaType::QFont: {
        const QFont font = value.value<QFont>();
        return QJsonObject{{"family", font.family()}, {"pointSize", font.pointSizeF()},
                           {"pixelSize", font.pixelSize()}, {"bold", font.bold()}, {"italic", font.italic()}};
    }
    case QMetaType::QSizePolicy: {
        const QSizePolicy policy = value.value<QSizePolicy>();
        const QMetaEnum names = QMetaEnum::fromType<QSizePolicy::Policy>();
        return QJsonObject{{"horizontal", QString::fromLatin1(names.valueToKey(policy.horizontalPolicy()))},
                           {"vertical", QString::fromLatin1(names.valueToKey(policy.verticalPolicy()))},
                           {"horizontalStretch", policy.horizontalStretch()},
                           {"verticalStretch", policy.verticalStretch()}};
    }
    case QMetaType::QKeySequence:
        return value.value<QKeySequence>().toString(QKeySequence::PortableText);
    case QMetaType::QUrl:
        return value.toUrl().toString();
    default:
        // Numbers, strings, booleans, lists and maps; null for what has no
        // JSON form (an icon, a palette).
        return QJsonValue::fromVariant(value);
    }
}

QJsonValue propertyJson(const QObject* object, const QMetaProperty& property)
{
    const QVariant value = property.read(object);
    if (!property.isEnumType())
        return toJson(value);
    // An enum property reads as the enum's type, or as int when that is not
    // registered; a flags type converts to int in neither Qt 5 nor Qt 6.
    bool ok = false;
    int number = value.toInt(&ok);
    if (!ok) {
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        const int size = QMetaType::sizeOf(value.userType());
#else
        const int size = int(value.metaType().sizeOf());
#endif
        number = 0;
        std::memcpy(&number, value.constData(), size_t(qBound(0, size, int(sizeof(int)))));
    }
    const QMetaEnum names = property.enumerator();
    return QString::fromLatin1(names.isFlag() ? names.valueToKeys(number) : QByteArray(names.valueToKey(number)));
}

// ------------------------------------------------------------ layouts

// The layout, `layout` or one nested in it, that has the widget as an item.
QLayout* containing(QLayout* layout, const QWidget* widget, int* index)
{
    for (int i = 0; i < layout->count(); ++i) {
        QLayoutItem* item = layout->itemAt(i);
        if (item->widget() == widget) {
            *index = i;
            return layout;
        }
        if (QLayout* nested = item->layout()) {
            if (QLayout* found = containing(nested, widget, index))
                return found;
        }
    }
    return nullptr;
}

// Where item `index` sits: its stretch in a box layout, its cell in a grid,
// its row and role in a form.
QJsonObject itemPlace(QLayout* layout, int index)
{
    QJsonObject place{{"index", index}};
    if (auto* box = qobject_cast<QBoxLayout*>(layout)) {
        place.insert("stretch", box->stretch(index));
    } else if (auto* grid = qobject_cast<QGridLayout*>(layout)) {
        int row = 0, column = 0, rowSpan = 0, columnSpan = 0;
        grid->getItemPosition(index, &row, &column, &rowSpan, &columnSpan);
        place.insert("row", row);
        place.insert("column", column);
        place.insert("rowSpan", rowSpan);
        place.insert("columnSpan", columnSpan);
    } else if (auto* form = qobject_cast<QFormLayout*>(layout)) {
        int row = 0;
        QFormLayout::ItemRole role = QFormLayout::FieldRole;
        form->getItemPosition(index, &row, &role);
        place.insert("row", row);
        place.insert("role", role == QFormLayout::LabelRole   ? QStringLiteral("label")
                             : role == QFormLayout::FieldRole ? QStringLiteral("field")
                                                              : QStringLiteral("spanning"));
    }
    return place;
}

} // namespace

// ------------------------------------------------------------ ScriptUi

ScriptUi::ScriptUi(ScriptEngine* engine, QWidget* root)
    : QObject(root)
    , m_engine(engine)
    , m_root(root)
{
    if (!engine || !root) {
        qWarning("ScriptHost::ScriptUi: no engine or no root widget");
        return;
    }
    registerMethods();
    engine->setProperty(kOwner, qulonglong(quintptr(this)));
}

ScriptUi::~ScriptUi()
{
    if (m_engine && m_engine->property(kOwner).toULongLong() == qulonglong(quintptr(this))) {
        for (const char* name : kMethods)
            m_engine->unregisterMethod(QString::fromLatin1(name));
        m_engine->setProperty(kOwner, QVariant());
    }
}

void ScriptUi::hideWidget(QWidget* widget, bool hidden)
{
    if (widget)
        widget->setProperty(kHidden, hidden ? QVariant(true) : QVariant());
}

bool ScriptUi::isVisible(const QWidget* widget) const
{
    for (const QWidget* w = widget; w; w = w->parentWidget()) {
        if (isExcluded(w))
            return false;
        if (w == m_root)
            return true;
    }
    return false;
}

QList<QWidget*> ScriptUi::visibleChildren(const QWidget* widget) const
{
    QList<QWidget*> children;
    for (QObject* child : widget->children()) {
        if (child->isWidgetType() && !isExcluded(static_cast<QWidget*>(child)))
            children.append(static_cast<QWidget*>(child));
    }
    return children;
}

QList<QWidget*> ScriptUi::visibleWidgets() const
{
    QList<QWidget*> widgets;
    if (!m_root || isExcluded(m_root))
        return widgets;
    widgets.append(m_root);
    // Pre-order: a widget, then its children's subtrees in order.
    for (int i = 0; i < widgets.size(); ++i) {
        const QList<QWidget*> children = visibleChildren(widgets.at(i));
        for (int c = 0; c < children.size(); ++c)
            widgets.insert(i + 1 + c, children.at(c));
    }
    return widgets;
}

QWidget* ScriptUi::resolve(const QJsonValue& widget) const
{
    // ponytail: a walk of the visible tree per call, a few hundred widgets
    // at most; an id -> widget hash if trees get much larger.
    const QList<QWidget*> widgets = visibleWidgets();
    if (widget.isDouble()) {
        const int id = widget.toInt();
        for (QWidget* w : widgets) {
            if (w->property(kId).toInt() == id)
                return w;
        }
        throw ScriptError(QStringLiteral("no widget with the id %1").arg(id));
    }
    const QString name = widget.toString();
    for (QWidget* w : widgets) {
        if (!name.isEmpty() && w->objectName() == name)
            return w;
    }
    throw ScriptError(QStringLiteral("no widget '%1'").arg(name));
}

bool ScriptUi::readText(const QWidget* widget, QString* text) const
{
    if (!m_textReadable || isPassword(widget))
        return false;
    const QMetaObject* meta = widget->metaObject();
    for (const char* name : {"text", "title", "plainText", "currentText"}) {
        const int index = meta->indexOfProperty(name);
        if (index >= 0) {
            *text = meta->property(index).read(widget).toString();
            return true;
        }
    }
    if (widget->isWindow() && !widget->windowTitle().isEmpty()) {
        *text = widget->windowTitle();
        return true;
    }
    return false;
}

bool ScriptUi::isReadable(const QWidget* widget, const QString& name, const QVariant& value) const
{
    if (isPassword(widget)
        && (name == QLatin1String("text") || name == QLatin1String("displayText")
            || name == QLatin1String("selectedText")))
        return false;
    if (!m_textReadable && name != QLatin1String("objectName")) {
        // By the value, so that a property of type QVariant holding a
        // string is caught too.
        const int type = value.userType();
        if (type == QMetaType::QString || type == QMetaType::QStringList || type == QMetaType::QChar
            || type == QMetaType::QUrl || type == QMetaType::QByteArray)
            return false;
    }
    return true;
}

QJsonObject ScriptUi::node(QWidget* widget) const
{
    const QPoint topLeft = m_root->mapFromGlobal(widget->mapToGlobal(QPoint(0, 0)));
    QJsonObject result{{"id", widgetId(widget)},
                       {"name", widget->objectName()},
                       {"class", className(widget)},
                       {"rect", rectJson(QRect(topLeft, widget->size()))},
                       {"visible", widget->isVisible()},
                       {"enabled", widget->isEnabled()},
                       {"focus", widget->window()->focusWidget() == widget}};
    QString text;
    if (readText(widget, &text))
        result.insert("text", text);
    return result;
}

QJsonObject ScriptUi::tree(QWidget* widget, int depth) const
{
    QJsonObject result = node(widget);
    if (depth != 0) {
        QJsonArray children;
        for (QWidget* child : visibleChildren(widget))
            children.append(tree(child, depth - 1));
        result.insert("children", children);
    }
    return result;
}

QJsonObject ScriptUi::layoutInfo(QLayout* layout) const
{
    const QMargins margins = layout->contentsMargins();
    QJsonArray items;
    for (int i = 0; i < layout->count(); ++i) {
        QLayoutItem* item = layout->itemAt(i);
        QJsonObject entry = itemPlace(layout, i);
        if (QWidget* widget = item->widget()) {
            if (!isVisible(widget))
                continue;
            entry.insert("widget", widgetId(widget));
            entry.insert("name", widget->objectName());
        } else if (QLayout* nested = item->layout()) {
            entry.insert("layout", layoutInfo(nested));
        } else if (item->spacerItem()) {
            entry.insert("spacer", sizeJson(item->sizeHint()));
        }
        items.append(entry);
    }
    return QJsonObject{{"type", className(layout)},
                       {"spacing", layout->spacing()},
                       {"margins", QJsonObject{{"left", margins.left()}, {"top", margins.top()},
                                               {"right", margins.right()}, {"bottom", margins.bottom()}}},
                       {"items", items}};
}

QJsonObject ScriptUi::info(QWidget* widget) const
{
    QJsonObject result = node(widget);
    result.insert("geometry", rectJson(widget->geometry()));
    result.insert("sizeHint", sizeJson(widget->sizeHint()));
    result.insert("minimumSizeHint", sizeJson(widget->minimumSizeHint()));
    result.insert("minimumSize", sizeJson(widget->minimumSize()));
    result.insert("maximumSize", sizeJson(widget->maximumSize()));
    result.insert("sizePolicy", toJson(QVariant::fromValue(widget->sizePolicy())));
    if (m_textReadable)
        result.insert("toolTip", widget->toolTip());

    // How the root sits in its parent is outside what scripts see.
    QJsonValue parentLayout = QJsonValue::Null;
    QWidget* parent = widget->parentWidget();
    int index = -1;
    if (widget != m_root && parent && parent->layout()) {
        if (QLayout* layout = containing(parent->layout(), widget, &index)) {
            QJsonObject place = itemPlace(layout, index);
            place.insert("owner", widgetId(parent));
            place.insert("type", className(layout));
            if (layout != parent->layout())
                place.insert("nested", true);
            parentLayout = place;
        }
    }
    result.insert("parentLayout", parentLayout);
    result.insert("layout", widget->layout() ? QJsonValue(layoutInfo(widget->layout())) : QJsonValue(QJsonValue::Null));
    return result;
}

QJsonValue ScriptUi::find(const QJsonObject& query) const
{
    const bool exact = query.value("exact").toBool();
    const bool byText = query.contains("text");
    if (byText && !m_textReadable)
        throw ScriptError(QStringLiteral("the application does not let scripts read widget text"));
    auto matches = [exact](const QString& value, const QString& wanted) {
        return exact ? value == wanted : value.contains(wanted, Qt::CaseInsensitive);
    };
    const QByteArray wantedClass = query.value("class").toString().toLatin1();
    QJsonArray found;
    for (QWidget* widget : visibleWidgets()) {
        if (query.contains("name") && !matches(widget->objectName(), query.value("name").toString()))
            continue;
        if (!wantedClass.isEmpty() && !widget->inherits(wantedClass.constData()))
            continue;
        QString text;
        if (byText && !(readText(widget, &text) && matches(text, query.value("text").toString())))
            continue;
        found.append(node(widget));
    }
    return found;
}

QJsonValue ScriptUi::at(int x, int y) const
{
    const QPoint point(x, y);
    if (!m_root->rect().contains(point))
        return QJsonValue::Null;
    QWidget* hit = m_root->childAt(point);
    if (!hit)
        hit = m_root;
    // Under a hidden widget: the nearest one above it a script may see.
    while (hit && !isVisible(hit))
        hit = hit->parentWidget();
    return hit ? QJsonValue(node(hit)) : QJsonValue(QJsonValue::Null);
}

QJsonValue ScriptUi::readProperty(QWidget* widget, const QString& name) const
{
    const QMetaObject* meta = widget->metaObject();
    const int index = meta->indexOfProperty(name.toLatin1().constData());
    if (index < 0 || !meta->property(index).isReadable())
        throw ScriptError(QStringLiteral("a %1 has no property '%2'").arg(className(widget), name));
    const QMetaProperty metaProperty = meta->property(index);
    if (!isReadable(widget, name, metaProperty.read(widget)))
        throw ScriptError(QStringLiteral("the application does not let scripts read '%1' of this widget").arg(name));
    return propertyJson(widget, metaProperty);
}

QJsonValue ScriptUi::focused() const
{
    QWidget* focus = QApplication::focusWidget();
    if (!focus || !isVisible(focus))
        focus = m_root->window()->focusWidget();
    return focus && isVisible(focus) ? QJsonValue(node(focus)) : QJsonValue(QJsonValue::Null);
}

QJsonValue ScriptUi::tabOrder(QWidget* widget) const
{
    QWidget* window = widget->window();
    QJsonArray order;
    QWidget* next = window;
    do {
        if ((next->focusPolicy() & Qt::TabFocus) && isVisible(next))
            order.append(node(next));
        next = next->nextInFocusChain();
    } while (next && next != window);
    return order;
}

// ------------------------------------------------------------ the methods

void ScriptUi::registerMethods()
{
    auto add = [this](ScriptMethod method, ScriptMethodHandler handler) {
        method.setGroup(QStringLiteral("UI")).setReadOnly(true);
        m_engine->registerMethod(method, std::move(handler));
    };
    const QJsonObject widget{{"type", QJsonArray{"integer", "string"}},
                             {"description", "The widget: its id or objectName"}};
    const QJsonObject rect{{"x", 0}, {"y", 0}, {"width", 80}, {"height", 24}};
    const QJsonObject example{{"id", 7}, {"name", "okButton"}, {"class", "QPushButton"}, {"rect", rect},
                              {"visible", true}, {"enabled", true}, {"focus", false}, {"text", "OK"}};
    const QString nodeText = QStringLiteral("{id, name, class, rect (root coordinates), visible, enabled, focus, text?}");

    add(ScriptMethod(QStringLiteral("ui.root"))
            .setDescription(QStringLiteral("The root widget scripts see: ") + nodeText + ".")
            .setReturns(QStringLiteral("object"), QString(), example),
        [this](const QJsonObject&) -> QJsonValue {
            return node(resolveRoot());
        });

    QJsonObject withChildren = example;
    withChildren.insert("children", QJsonArray{example});
    add(ScriptMethod(QStringLiteral("ui.tree"))
            .setDescription(QStringLiteral("The widget tree in one call: the node of the widget (default: the "
                                           "root) with its children, nested down to depth levels (default: all). "
                                           "A node below the depth has no children."))
            .addRawParameter(QStringLiteral("widget"), widget, false)
            .addParameter(QStringLiteral("depth"), QStringLiteral("integer"),
                          QStringLiteral("Levels of children; default all"), false)
            .setReturns(QStringLiteral("object"), QString(), withChildren),
        [this](const QJsonObject& args) -> QJsonValue {
            QWidget* from = args.contains("widget") ? resolve(args.value("widget")) : resolveRoot();
            return tree(from, args.contains("depth") ? qMax(0, args.value("depth").toInt()) : -1);
        });

    add(ScriptMethod(QStringLiteral("ui.children"))
            .setDescription(QStringLiteral("The nodes of the widget's children, without their children."))
            .addRawParameter(QStringLiteral("widget"), widget)
            .setReturns(QStringLiteral("array"), QString(), QJsonArray{example}),
        [this](const QJsonObject& args) -> QJsonValue {
            QJsonArray children;
            for (QWidget* child : visibleChildren(resolve(args.value("widget"))))
                children.append(node(child));
            return children;
        });

    add(ScriptMethod(QStringLiteral("ui.parent"))
            .setDescription(QStringLiteral("The node of the widget's parent; null for the root."))
            .addRawParameter(QStringLiteral("widget"), widget)
            .setReturns(QStringLiteral("object"), QString(), example),
        [this](const QJsonObject& args) -> QJsonValue {
            QWidget* child = resolve(args.value("widget"));
            return child == m_root ? QJsonValue(QJsonValue::Null) : QJsonValue(node(child->parentWidget()));
        });

    ScriptMethod findMethod = ScriptMethod::fromSchema(QStringLiteral("ui.find"), QJsonObject{
        {"type", "object"},
        {"properties", QJsonObject{
            {"name", QJsonObject{{"type", "string"}, {"description", "objectName"}}},
            {"class", QJsonObject{{"type", "string"}, {"description", "Class or a base class: \"QAbstractButton\""}}},
            {"text", QJsonObject{{"type", "string"}, {"description", "Shown text"}}},
            {"exact", QJsonObject{{"type", "boolean"},
                                  {"description", "Equal instead of a substring ignoring case; default false"}}}}}});
    add(findMethod
            .setDescription(QStringLiteral("The nodes, in tree order, of the widgets that match all criteria given: "
                                           "name and text as a substring ignoring case (or equal with exact), "
                                           "class by inheritance."))
            .setReturns(QStringLiteral("array"), QString(), QJsonArray{example}),
        [this](const QJsonObject& args) -> QJsonValue {
            resolveRoot();
            return find(args);
        });

    add(ScriptMethod(QStringLiteral("ui.at"))
            .setDescription(QStringLiteral("The node of the shown widget at a point in root coordinates; null "
                                           "outside the root."))
            .addParameter(QStringLiteral("x"), QStringLiteral("integer"), QStringLiteral("In the root"))
            .addParameter(QStringLiteral("y"), QStringLiteral("integer"), QStringLiteral("In the root"))
            .setReturns(QStringLiteral("object"), QString(), example),
        [this](const QJsonObject& args) -> QJsonValue {
            resolveRoot();
            return at(args.value("x").toInt(), args.value("y").toInt());
        });

    QJsonObject detail = example;
    const QJsonObject size{{"width", 80}, {"height", 24}};
    detail.insert("geometry", rect);
    detail.insert("sizeHint", size);
    detail.insert("minimumSizeHint", size);
    detail.insert("minimumSize", QJsonObject{{"width", 0}, {"height", 0}});
    detail.insert("maximumSize", QJsonObject{{"width", 16777215}, {"height", 16777215}});
    detail.insert("sizePolicy", QJsonObject{{"horizontal", "Minimum"}, {"vertical", "Fixed"},
                                            {"horizontalStretch", 0}, {"verticalStretch", 0}});
    detail.insert("toolTip", "");
    detail.insert("parentLayout", QJsonObject{{"owner", 3}, {"type", "QHBoxLayout"}, {"index", 1}, {"stretch", 0}});
    detail.insert("layout", QJsonValue::Null);
    add(ScriptMethod(QStringLiteral("ui.info"))
            .setDescription(QStringLiteral("The node plus geometry (in the parent), size hints, minimum and maximum "
                                           "size, size policy, tool tip, the layout the widget sits in "
                                           "(parentLayout: owner, type, index, stretch or row/column/spans) and "
                                           "its own layout (type, spacing, margins, items), or null."))
            .addRawParameter(QStringLiteral("widget"), widget)
            .setReturns(QStringLiteral("object"), QString(), detail),
        [this](const QJsonObject& args) -> QJsonValue {
            return info(resolve(args.value("widget")));
        });

    add(ScriptMethod(QStringLiteral("ui.property"))
            .setDescription(QStringLiteral("A readable Qt property of the widget as JSON: enums by name, sizes, "
                                           "points, rects and fonts as objects, colours as \"#rrggbb\"."))
            .addRawParameter(QStringLiteral("widget"), widget)
            .addParameter(QStringLiteral("name"), QStringLiteral("string"), QStringLiteral("Property name"))
            .setReturns(QStringLiteral("any")),
        [this](const QJsonObject& args) -> QJsonValue {
            return readProperty(resolve(args.value("widget")), args.value("name").toString());
        });

    add(ScriptMethod(QStringLiteral("ui.focused"))
            .setDescription(QStringLiteral("The node of the widget with the keyboard focus (while the root's "
                                           "window is not active, the one it gives the focus to), or null."))
            .setReturns(QStringLiteral("object"), QString(), example),
        [this](const QJsonObject&) -> QJsonValue {
            resolveRoot();
            return focused();
        });

    add(ScriptMethod(QStringLiteral("ui.tabOrder"))
            .setDescription(QStringLiteral("The nodes of the widgets in the window of the widget (default: the "
                                           "root) that take the focus by Tab, in tab order."))
            .addRawParameter(QStringLiteral("widget"), widget, false)
            .setReturns(QStringLiteral("array"), QString(), QJsonArray{example}),
        [this](const QJsonObject& args) -> QJsonValue {
            return tabOrder(args.contains("widget") ? resolve(args.value("widget")) : resolveRoot());
        });
}

QWidget* ScriptUi::resolveRoot() const
{
    if (!m_root || isExcluded(m_root))
        throw ScriptError(QStringLiteral("there is no widget tree"));
    return m_root;
}

ScriptUi* exposeWidgets(ScriptEngine* engine, QWidget* root)
{
    return new ScriptUi(engine, root);
}

} // namespace ScriptHost
