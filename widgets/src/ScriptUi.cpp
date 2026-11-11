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
#include <QRegularExpression>
#include <QSizePolicy>
#include <QSplitter>
#include <QUrl>
#include <QWidget>
#include <algorithm>
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
                                "ui.info", "ui.property", "ui.focused", "ui.tabOrder",
                                "ui.move", "ui.resize", "ui.setGeometry", "ui.show", "ui.hide", "ui.setEnabled",
                                "ui.raise", "ui.lower", "ui.setFocus", "ui.setTabOrder", "ui.setCurrentIndex",
                                "ui.setLayout", "ui.setStretch", "ui.setSplitterSizes", "ui.moveToLayout",
                                "ui.reorder", "ui.setProperty", "ui.restore"};

// The classes ui.setCurrentIndex() takes; each has the properties
// "currentIndex" and "count".
const char* const kIndexed[] = {"QTabWidget", "QStackedWidget", "QComboBox", "QTabBar", "QToolBox"};

QJsonObject widgetSchema()
{
    return QJsonObject{{"type", QJsonArray{"integer", "string"}},
                       {"description", "The widget: its id or objectName"}};
}

QJsonObject exampleNode()
{
    return QJsonObject{{"id", 7}, {"name", "okButton"}, {"class", "QPushButton"},
                       {"rect", QJsonObject{{"x", 0}, {"y", 0}, {"width", 80}, {"height", 24}}},
                       {"visible", true}, {"enabled", true}, {"focus", false}, {"text", "OK"}};
}

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

// Where a widget sits in a layout, to put it there (again).
struct Place
{
    QPointer<QLayout> layout;
    int               index = -1;       // box: the position (-1: the end); form: the row of a new row
    int               row = 0;          // grid, form
    int               column = 0;
    int               rowSpan = 1;
    int               columnSpan = 1;
    int               role = -1;        // form: the QFormLayout::ItemRole of a cell; -1: a new row
    int               stretch = 0;      // box
    Qt::Alignment     alignment;
};

// The layout the widget is an item of, and where in it.
bool placeOf(QWidget* widget, Place* place)
{
    QWidget* parent = widget->parentWidget();
    int index = -1;
    QLayout* layout = parent && parent->layout() && !widget->isWindow()
                          ? containing(parent->layout(), widget, &index) : nullptr;
    if (!layout)
        return false;
    place->layout = layout;
    place->index = index;
    place->alignment = layout->itemAt(index)->alignment();
    if (auto* box = qobject_cast<QBoxLayout*>(layout)) {
        place->stretch = box->stretch(index);
    } else if (auto* grid = qobject_cast<QGridLayout*>(layout)) {
        grid->getItemPosition(index, &place->row, &place->column, &place->rowSpan, &place->columnSpan);
    } else if (auto* form = qobject_cast<QFormLayout*>(layout)) {
        QFormLayout::ItemRole role = QFormLayout::FieldRole;
        form->getItemPosition(index, &place->row, &role);
        place->role = int(role);
    }
    return true;
}

bool canInsert(const QLayout* layout)
{
    return qobject_cast<const QBoxLayout*>(layout) || qobject_cast<const QGridLayout*>(layout)
           || qobject_cast<const QFormLayout*>(layout);
}

// Puts the widget into the layout at the place; the layout reparents it.
void insertAt(QLayout* layout, QWidget* widget, const Place& place)
{
    if (auto* box = qobject_cast<QBoxLayout*>(layout))
        box->insertWidget(place.index, widget, place.stretch, place.alignment);
    else if (auto* grid = qobject_cast<QGridLayout*>(layout))
        grid->addWidget(widget, place.row, place.column, place.rowSpan, place.columnSpan, place.alignment);
    else if (auto* form = qobject_cast<QFormLayout*>(layout); form && place.role >= 0)
        form->setWidget(place.row, QFormLayout::ItemRole(place.role), widget);
    else if (form)
        form->insertRow(place.index, widget);
}

// Out of the layout it is in, if any; its parent stays.
void takeOut(QWidget* widget)
{
    Place place;
    if (placeOf(widget, &place))
        place.layout->removeWidget(widget);
}

// Hidden by hide() - or a window not open. A child that was simply never
// hidden is not: it shows with its parent, and setVisible(true) keeps it so.
bool isHiddenByHand(const QWidget* widget)
{
    return widget->isHidden() && (widget->isWindow() || widget->testAttribute(Qt::WA_WState_ExplicitShowHide));
}

// The sibling right above the widget in the stacking order.
QWidget* widgetAbove(const QWidget* widget)
{
    if (!widget->parentWidget())
        return nullptr;
    const QObjectList& siblings = widget->parentWidget()->children();
    for (int i = int(siblings.indexOf(const_cast<QWidget*>(widget))) + 1; i < siblings.size(); ++i) {
        if (siblings.at(i)->isWidgetType())
            return static_cast<QWidget*>(siblings.at(i));
    }
    return nullptr;
}

// ------------------------------------------------------------ values from JSON

// The numbers of an object with these keys, or of a string "10, 20" /
// "100x20"; empty when it is neither.
QList<double> numbersOf(const QJsonValue& value, const QStringList& keys)
{
    QList<double> numbers;
    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        for (const QString& key : keys) {
            if (!object.value(key).isDouble())
                return {};
            numbers << object.value(key).toDouble();
        }
    } else if (value.isString()) {
        static const QRegularExpression separators(QStringLiteral("[\\s,x]+"));
        for (const QString& part : value.toString().split(separators, Qt::SkipEmptyParts)) {
            bool ok = false;
            numbers << part.toDouble(&ok);
            if (!ok)
                return {};
        }
    }
    return numbers.size() == keys.size() ? numbers : QList<double>();
}

ScriptError takesError(const QMetaProperty& property, const QString& what)
{
    return ScriptError(QStringLiteral("'%1' takes %2").arg(QString::fromLatin1(property.name()), what));
}

// A property value from what a script passed - the reverse of toJson() and
// propertyJson(). `current` is the value now (a font is changed, not
// replaced). Throws ScriptError when the value does not fit.
QVariant fromJson(const QJsonValue& value, const QMetaProperty& property, const QVariant& current)
{
    if (property.isEnumType()) {
        const QMetaEnum names = property.enumerator();
        int number = 0;
        if (value.isDouble()) {
            number = value.toInt();
        } else if (value.isString()) {
            bool ok = false;
            const QByteArray keys = value.toString().toLatin1();
            number = names.isFlag() ? names.keysToValue(keys.constData(), &ok) : names.keyToValue(keys.constData(), &ok);
            if (!ok)
                throw ScriptError(QStringLiteral("'%1' is not a value of %2")
                                      .arg(value.toString(), QString::fromLatin1(names.name())));
        } else {
            throw takesError(property, QStringLiteral("a name of %1").arg(QString::fromLatin1(names.name())));
        }
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        // Qt 6 converts an int to an enum, but not to a flags type.
        if (property.metaType().sizeOf() == qsizetype(sizeof(int)))
            return QVariant(property.metaType(), &number);
#endif
        return number;
    }

    const auto numbers = [&](const QStringList& keys, const QString& form) {
        const QList<double> list = numbersOf(value, keys);
        if (list.isEmpty())
            throw takesError(property, form);
        return list;
    };
    switch (property.userType()) {
    case QMetaType::Bool:
        if (!value.isBool())
            throw takesError(property, QStringLiteral("true or false"));
        return value.toBool();
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::Long:
    case QMetaType::ULong:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
    case QMetaType::Short:
    case QMetaType::UShort:
        if (!value.isDouble())
            throw takesError(property, QStringLiteral("a number"));
        return qint64(value.toDouble());
    case QMetaType::Double:
    case QMetaType::Float:
        if (!value.isDouble())
            throw takesError(property, QStringLiteral("a number"));
        return value.toDouble();
    case QMetaType::QString:
        if (!value.isString())
            throw takesError(property, QStringLiteral("a string"));
        return value.toString();
    case QMetaType::QStringList: {
        const QJsonArray items = value.toArray();
        QStringList list;
        for (const QJsonValue& item : items)
            list << item.toString();
        const bool allStrings = std::all_of(items.begin(), items.end(),
                                            [](const QJsonValue& item) { return item.isString(); });
        if (!value.isArray() || !allStrings)
            throw takesError(property, QStringLiteral("an array of strings"));
        return list;
    }
    case QMetaType::QSize:
    case QMetaType::QSizeF: {
        const QList<double> n = numbers({"width", "height"}, QStringLiteral("{width, height} or \"width x height\""));
        return property.userType() == QMetaType::QSize ? QVariant(QSize(qRound(n[0]), qRound(n[1])))
                                                       : QVariant(QSizeF(n[0], n[1]));
    }
    case QMetaType::QPoint:
    case QMetaType::QPointF: {
        const QList<double> n = numbers({"x", "y"}, QStringLiteral("{x, y} or \"x, y\""));
        return property.userType() == QMetaType::QPoint ? QVariant(QPoint(qRound(n[0]), qRound(n[1])))
                                                        : QVariant(QPointF(n[0], n[1]));
    }
    case QMetaType::QRect:
    case QMetaType::QRectF: {
        const QList<double> n = numbers({"x", "y", "width", "height"},
                                        QStringLiteral("{x, y, width, height} or \"x, y, width, height\""));
        return property.userType() == QMetaType::QRect
                   ? QVariant(QRect(qRound(n[0]), qRound(n[1]), qRound(n[2]), qRound(n[3])))
                   : QVariant(QRectF(n[0], n[1], n[2], n[3]));
    }
    case QMetaType::QColor: {
        QColor color;
        if (value.isString()) {
            color = QColor(value.toString());
        } else if (value.isObject()) {
            const QJsonObject rgb = value.toObject();
            color = QColor(rgb.value("r").toInt(), rgb.value("g").toInt(), rgb.value("b").toInt(),
                           rgb.value("a").toInt(255));
        }
        if (!color.isValid())
            throw takesError(property, QStringLiteral("a colour: \"#rrggbb\", \"#aarrggbb\", a name or {r, g, b, a?}"));
        return color;
    }
    case QMetaType::QFont: {
        if (!value.isObject())
            throw takesError(property, QStringLiteral("{family?, pointSize?, pixelSize?, bold?, italic?}"));
        const QJsonObject changes = value.toObject();
        QFont font = current.value<QFont>();
        if (changes.contains("family"))
            font.setFamily(changes.value("family").toString());
        if (changes.contains("pointSize"))
            font.setPointSizeF(changes.value("pointSize").toDouble());
        if (changes.contains("pixelSize"))
            font.setPixelSize(changes.value("pixelSize").toInt());
        if (changes.contains("bold"))
            font.setBold(changes.value("bold").toBool());
        if (changes.contains("italic"))
            font.setItalic(changes.value("italic").toBool());
        return font;
    }
    case QMetaType::QKeySequence:
        if (!value.isString())
            throw takesError(property, QStringLiteral("a key sequence such as \"Ctrl+S\""));
        return QKeySequence(value.toString(), QKeySequence::PortableText);
    default:
        // ponytail: other types as QVariant converts them from JSON; a size
        // policy, a palette or an icon cannot be set.
        return value.toVariant();
    }
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
    registerChangingMethods();
    engine->setProperty(kOwner, qulonglong(quintptr(this)));
    // The journal of a run goes with it; its changes stay.
    connect(engine, &ScriptEngine::runFinished, this, [this](ScriptEngine::RunId id, const QJsonObject&) {
        m_journal.remove(id);
    });
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

QList<QWidget*> ScriptUi::tabChain(QWidget* widget) const
{
    QWidget* window = widget->window();
    QList<QWidget*> chain;
    QWidget* next = window;
    do {
        if ((next->focusPolicy() & Qt::TabFocus) && isVisible(next))
            chain.append(next);
        next = next->nextInFocusChain();
    } while (next && next != window);
    return chain;
}

QJsonValue ScriptUi::tabOrder(QWidget* widget) const
{
    QJsonArray order;
    for (QWidget* w : tabChain(widget))
        order.append(node(w));
    return order;
}

// ------------------------------------------------------------ the methods

void ScriptUi::add(ScriptMethod method, ScriptMethodHandler handler, bool readOnly)
{
    method.setGroup(QStringLiteral("UI")).setReadOnly(readOnly);
    m_engine->registerMethod(method, std::move(handler));
}

void ScriptUi::registerMethods()
{
    const QJsonObject widget = widgetSchema();
    const QJsonObject rect{{"x", 0}, {"y", 0}, {"width", 80}, {"height", 24}};
    const QJsonObject example = exampleNode();
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

// ------------------------------------------------------------ changing tools

void ScriptUi::record(QWidget* widget, std::function<void(QWidget*)> undo)
{
    const QPointer<QWidget> target(widget);
    m_journal[m_engine->callingRun()].append([target, undo]() {
        if (!target)
            return false;
        undo(target);
        return true;
    });
}

QJsonValue ScriptUi::restore()
{
    const QList<std::function<bool()>> steps = m_journal.take(m_engine->callingRun());
    int restored = 0;
    for (int i = int(steps.size()) - 1; i >= 0; --i)
        restored += steps.at(i)() ? 1 : 0;
    return QJsonObject{{"restored", restored}, {"skipped", int(steps.size()) - restored}};
}

QJsonObject ScriptUi::changed(QWidget* widget) const
{
    QJsonObject result = node(widget);
    Place place;
    result.insert("managedByLayout", placeOf(widget, &place));
    return result;
}

void ScriptUi::registerChangingMethods()
{
    const QJsonObject widget = widgetSchema();
    const QString integer = QStringLiteral("integer");
    const QString object = QStringLiteral("object");
    QJsonObject example = exampleNode();
    example.insert("managedByLayout", false);
    const QString returnsNode = QStringLiteral(" Returns its node, with managedByLayout: true when a layout places "
                                               "the widget - the layout may move or resize it again.");

    add(ScriptMethod(QStringLiteral("ui.move"))
            .setDescription(QStringLiteral("Moves the widget to x, y in its parent.") + returnsNode)
            .addRawParameter(QStringLiteral("widget"), widget)
            .addParameter(QStringLiteral("x"), integer, QStringLiteral("In the parent"))
            .addParameter(QStringLiteral("y"), integer, QStringLiteral("In the parent"))
            .setReturns(object, QString(), example),
        [this](const QJsonObject& args) -> QJsonValue {
            QWidget* w = resolve(args.value("widget"));
            const QPoint old = w->pos();
            record(w, [old](QWidget* t) { t->move(old); });
            w->move(args.value("x").toInt(), args.value("y").toInt());
            return changed(w);
        }, false);

    add(ScriptMethod(QStringLiteral("ui.resize"))
            .setDescription(QStringLiteral("Resizes the widget.") + returnsNode)
            .addRawParameter(QStringLiteral("widget"), widget)
            .addParameter(QStringLiteral("width"), integer, QString())
            .addParameter(QStringLiteral("height"), integer, QString())
            .setReturns(object, QString(), example),
        [this](const QJsonObject& args) -> QJsonValue {
            QWidget* w = resolve(args.value("widget"));
            const QSize old = w->size();
            record(w, [old](QWidget* t) { t->resize(old); });
            w->resize(args.value("width").toInt(), args.value("height").toInt());
            return changed(w);
        }, false);

    add(ScriptMethod(QStringLiteral("ui.setGeometry"))
            .setDescription(QStringLiteral("Places the widget at x, y in its parent with this size.") + returnsNode)
            .addRawParameter(QStringLiteral("widget"), widget)
            .addParameter(QStringLiteral("x"), integer, QStringLiteral("In the parent"))
            .addParameter(QStringLiteral("y"), integer, QStringLiteral("In the parent"))
            .addParameter(QStringLiteral("width"), integer, QString())
            .addParameter(QStringLiteral("height"), integer, QString())
            .setReturns(object, QString(), example),
        [this](const QJsonObject& args) -> QJsonValue {
            QWidget* w = resolve(args.value("widget"));
            const QRect old = w->geometry();
            record(w, [old](QWidget* t) { t->setGeometry(old); });
            w->setGeometry(args.value("x").toInt(), args.value("y").toInt(), args.value("width").toInt(),
                           args.value("height").toInt());
            return changed(w);
        }, false);

    for (const bool visible : {true, false}) {
        add(ScriptMethod(visible ? QStringLiteral("ui.show") : QStringLiteral("ui.hide"))
                .setDescription((visible ? QStringLiteral("Shows the widget; a window opens.")
                                         : QStringLiteral("Hides the widget.")) + returnsNode)
                .addRawParameter(QStringLiteral("widget"), widget)
                .setReturns(object, QString(), example),
            [this, visible](const QJsonObject& args) -> QJsonValue {
                QWidget* w = resolve(args.value("widget"));
                const bool wasHidden = isHiddenByHand(w);
                record(w, [wasHidden](QWidget* t) { t->setVisible(!wasHidden); });
                w->setVisible(visible);
                return changed(w);
            }, false);
    }

    add(ScriptMethod(QStringLiteral("ui.setEnabled"))
            .setDescription(QStringLiteral("Enables or disables the widget, and with it the ones below it.")
                            + returnsNode)
            .addRawParameter(QStringLiteral("widget"), widget)
            .addParameter(QStringLiteral("enabled"), QStringLiteral("boolean"), QString())
            .setReturns(object, QString(), example),
        [this](const QJsonObject& args) -> QJsonValue {
            QWidget* w = resolve(args.value("widget"));
            const bool old = !w->testAttribute(Qt::WA_ForceDisabled);
            record(w, [old](QWidget* t) { t->setEnabled(old); });
            w->setEnabled(args.value("enabled").toBool());
            return changed(w);
        }, false);

    for (const bool up : {true, false}) {
        add(ScriptMethod(up ? QStringLiteral("ui.raise") : QStringLiteral("ui.lower"))
                .setDescription(up ? QStringLiteral("Puts the widget above its siblings.")
                                   : QStringLiteral("Puts the widget below its siblings."))
                .addRawParameter(QStringLiteral("widget"), widget)
                .setReturns(object, QString(), example),
            [this, up](const QJsonObject& args) -> QJsonValue {
                QWidget* w = resolve(args.value("widget"));
                const QPointer<QWidget> above = widgetAbove(w);
                record(w, [above](QWidget* t) {
                    if (above && above->parentWidget() == t->parentWidget())
                        t->stackUnder(above);
                    else
                        t->raise();
                });
                if (up)
                    w->raise();
                else
                    w->lower();
                return changed(w);
            }, false);
    }

    add(ScriptMethod(QStringLiteral("ui.setFocus"))
            .setDescription(QStringLiteral("Gives the widget the keyboard focus (in its window, while the window "
                                           "is not active)."))
            .addRawParameter(QStringLiteral("widget"), widget)
            .setReturns(object, QString(), example),
        [this](const QJsonObject& args) -> QJsonValue {
            QWidget* w = resolve(args.value("widget"));
            const QPointer<QWidget> before = w->window()->focusWidget();
            record(w, [before](QWidget* t) {
                if (before)
                    before->setFocus();
                else
                    t->clearFocus();
            });
            w->setFocus();
            return changed(w);
        }, false);

    add(ScriptMethod(QStringLiteral("ui.setTabOrder"))
            .setDescription(QStringLiteral("Tab goes from each widget to the next in the list (all in one "
                                           "window). Returns the window's new tab order."))
            .addRawParameter(QStringLiteral("widgets"),
                             QJsonObject{{"type", "array"}, {"items", QJsonObject{{"type", QJsonArray{"integer", "string"}}}},
                                         {"description", "Two widgets or more: ids or objectNames"}})
            .setReturns(QStringLiteral("array"), QString(), QJsonArray{exampleNode()}),
        [this](const QJsonObject& args) -> QJsonValue {
            QList<QWidget*> widgets;
            for (const QJsonValue& value : args.value("widgets").toArray()) {
                QWidget* w = resolve(value);
                if (widgets.contains(w))
                    throw ScriptError(QStringLiteral("a widget is in the list twice"));
                if (!widgets.isEmpty() && w->window() != widgets.first()->window())
                    throw ScriptError(QStringLiteral("the widgets are not in one window"));
                widgets << w;
            }
            if (widgets.size() < 2)
                throw ScriptError(QStringLiteral("takes two widgets or more"));
            QList<QPointer<QWidget>> before;
            for (QWidget* w : tabChain(widgets.first()))
                before << w;
            record(widgets.first()->window(), [before](QWidget*) {
                QList<QWidget*> alive;
                for (const QPointer<QWidget>& w : before) {
                    if (w)
                        alive << w;
                }
                for (int i = 1; i < alive.size(); ++i)
                    QWidget::setTabOrder(alive.at(i - 1), alive.at(i));
            });
            for (int i = 1; i < widgets.size(); ++i)
                QWidget::setTabOrder(widgets.at(i - 1), widgets.at(i));
            return tabOrder(widgets.first());
        }, false);

    add(ScriptMethod(QStringLiteral("ui.setCurrentIndex"))
            .setDescription(QStringLiteral("Shows page or item `index` of a QTabWidget, QStackedWidget, QComboBox, "
                                           "QTabBar or QToolBox."))
            .addRawParameter(QStringLiteral("widget"), widget)
            .addParameter(QStringLiteral("index"), integer, QStringLiteral("From 0"))
            .setReturns(object, QString(), example),
        [this](const QJsonObject& args) -> QJsonValue {
            QWidget* w = resolve(args.value("widget"));
            if (std::none_of(std::begin(kIndexed), std::end(kIndexed), [w](const char* c) { return w->inherits(c); }))
                throw ScriptError(QStringLiteral("a %1 has no current index").arg(className(w)));
            const int index = args.value("index").toInt();
            const int count = w->property("count").toInt();
            if (index < 0 || index >= count)
                throw ScriptError(QStringLiteral("the index %1 is not between 0 and %2").arg(index).arg(count - 1));
            const int old = w->property("currentIndex").toInt();
            record(w, [old](QWidget* t) { t->setProperty("currentIndex", old); });
            w->setProperty("currentIndex", index);
            return changed(w);
        }, false);

    const QJsonObject layoutOptions{
        {"type", "object"},
        {"description", "What to change"},
        {"properties", QJsonObject{
            {"spacing", QJsonObject{{"type", "integer"}}},
            {"margins", QJsonObject{{"type", QJsonArray{"integer", "object"}},
                                    {"description", "All four, or {left?, top?, right?, bottom?}"}}},
            {"alignment", QJsonObject{{"type", "string"}, {"description", "\"AlignLeft|AlignTop\""}}},
            {"direction", QJsonObject{{"type", "string"},
                                      {"enum", QJsonArray{"LeftToRight", "RightToLeft", "TopToBottom",
                                                          "BottomToTop"}}}}}}};
    add(ScriptMethod(QStringLiteral("ui.setLayout"))
            .setDescription(QStringLiteral("Changes the widget's own layout: spacing, margins, alignment, and the "
                                           "direction of a box layout. Returns the layout as ui.info() shows it."))
            .addRawParameter(QStringLiteral("widget"), widget)
            .addRawParameter(QStringLiteral("options"), layoutOptions)
            .setReturns(object),
        [this](const QJsonObject& args) -> QJsonValue {
            QWidget* w = resolve(args.value("widget"));
            QLayout* layout = w->layout();
            if (!layout)
                throw ScriptError(QStringLiteral("the widget has no layout"));
            return setLayout(w, layout, args.value("options").toObject());
        }, false);

    add(ScriptMethod(QStringLiteral("ui.setStretch"))
            .setDescription(QStringLiteral("The stretch of the widget in its box layout; with `at`, the stretch "
                                           "of a row or column of the widget's own grid layout."))
            .addRawParameter(QStringLiteral("widget"), widget)
            .addParameter(QStringLiteral("stretch"), integer, QStringLiteral("0: none"))
            .addRawParameter(QStringLiteral("at"),
                             QJsonObject{{"type", "object"},
                                         {"description", "{row} or {column} of the widget's grid layout"},
                                         {"properties", QJsonObject{{"row", QJsonObject{{"type", "integer"}}},
                                                                    {"column", QJsonObject{{"type", "integer"}}}}}},
                             false)
            .setReturns(object),
        [this](const QJsonObject& args) -> QJsonValue {
            return setStretch(resolve(args.value("widget")), args.value("stretch").toInt(), args);
        }, false);

    add(ScriptMethod(QStringLiteral("ui.setSplitterSizes"))
            .setDescription(QStringLiteral("The sizes of the widgets of a QSplitter, one per widget. Returns the "
                                           "sizes the splitter took."))
            .addRawParameter(QStringLiteral("widget"), widget)
            .addRawParameter(QStringLiteral("sizes"), QJsonObject{{"type", "array"},
                                                                  {"items", QJsonObject{{"type", "integer"}}}})
            .setReturns(QStringLiteral("array"), QString(), QJsonArray{100, 200}),
        [this](const QJsonObject& args) -> QJsonValue {
            auto* splitter = qobject_cast<QSplitter*>(resolve(args.value("widget")));
            if (!splitter)
                throw ScriptError(QStringLiteral("the widget is not a QSplitter"));
            QList<int> sizes;
            for (const QJsonValue& size : args.value("sizes").toArray())
                sizes << qMax(0, size.toInt());
            if (sizes.size() != splitter->count())
                throw ScriptError(QStringLiteral("the splitter has %1 widgets").arg(splitter->count()));
            const QList<int> old = splitter->sizes();
            record(splitter, [old](QWidget* t) { static_cast<QSplitter*>(t)->setSizes(old); });
            splitter->setSizes(sizes);
            QJsonArray now;
            for (int size : splitter->sizes())
                now.append(size);
            return now;
        }, false);

    add(ScriptMethod(QStringLiteral("ui.moveToLayout"))
            .setDescription(QStringLiteral("Moves the widget into the layout of `target` (a box, grid or form "
                                           "layout), reparenting it: at {index} in a box layout (default: the "
                                           "end), {row, column, rowSpan?, columnSpan?} in a grid, {index} as a "
                                           "new row of a form.") + returnsNode)
            .addRawParameter(QStringLiteral("widget"), widget)
            .addRawParameter(QStringLiteral("target"), widgetSchema())
            .addRawParameter(QStringLiteral("at"),
                             QJsonObject{{"type", "object"},
                                         {"properties", QJsonObject{{"index", QJsonObject{{"type", "integer"}}},
                                                                    {"row", QJsonObject{{"type", "integer"}}},
                                                                    {"column", QJsonObject{{"type", "integer"}}},
                                                                    {"rowSpan", QJsonObject{{"type", "integer"}}},
                                                                    {"columnSpan", QJsonObject{{"type", "integer"}}}}}},
                             false)
            .setReturns(object, QString(), example),
        [this](const QJsonObject& args) -> QJsonValue {
            return moveToLayout(resolve(args.value("widget")), resolve(args.value("target")),
                                args.value("at").toObject());
        }, false);

    add(ScriptMethod(QStringLiteral("ui.reorder"))
            .setDescription(QStringLiteral("Moves the widget to position `index` in its box layout; its stretch "
                                           "goes with it."))
            .addRawParameter(QStringLiteral("widget"), widget)
            .addParameter(QStringLiteral("index"), integer, QStringLiteral("From 0, as in ui.info().layout.items"))
            .setReturns(object, QString(), example),
        [this](const QJsonObject& args) -> QJsonValue {
            QWidget* w = resolve(args.value("widget"));
            Place place;
            auto* box = placeOf(w, &place) ? qobject_cast<QBoxLayout*>(place.layout) : nullptr;
            if (!box)
                throw ScriptError(QStringLiteral("the widget is not in a box layout"));
            const int index = args.value("index").toInt();
            if (index < 0 || index >= box->count())
                throw ScriptError(QStringLiteral("the index %1 is not between 0 and %2").arg(index).arg(box->count() - 1));
            record(w, [place](QWidget* t) {
                takeOut(t);
                if (place.layout)
                    insertAt(place.layout, t, place);
            });
            box->removeWidget(w);
            Place to = place;
            to.index = index;
            insertAt(box, w, to);
            return changed(w);
        }, false);

    add(ScriptMethod(QStringLiteral("ui.setProperty"))
            .setDescription(QStringLiteral("Sets a writable Qt property of the widget: numbers, strings, booleans; "
                                           "enums and flags by name (\"AlignLeft|AlignTop\"); sizes {width, height} "
                                           "or \"100x20\", points {x, y} or \"10, 20\", rects {x, y, width, height} "
                                           "or \"10, 20, 100, 30\", colours \"#rrggbb\", \"#aarrggbb\", a name or "
                                           "{r, g, b, a?}; a font as the parts to change {family?, pointSize?, "
                                           "pixelSize?, bold?, italic?}.") + returnsNode)
            .addRawParameter(QStringLiteral("widget"), widget)
            .addParameter(QStringLiteral("name"), QStringLiteral("string"), QStringLiteral("Property name"))
            .addRawParameter(QStringLiteral("value"), QJsonObject{{"description", "The new value"}})
            .setReturns(object, QString(), example),
        [this](const QJsonObject& args) -> QJsonValue {
            QWidget* w = resolve(args.value("widget"));
            const QString name = args.value("name").toString();
            const QMetaObject* meta = w->metaObject();
            const int index = meta->indexOfProperty(name.toLatin1().constData());
            if (index < 0)
                throw ScriptError(QStringLiteral("a %1 has no property '%2'").arg(className(w), name));
            const QMetaProperty metaProperty = meta->property(index);
            if (!metaProperty.isWritable())
                throw ScriptError(QStringLiteral("'%1' of a %2 cannot be set").arg(name, className(w)));
            const QVariant old = metaProperty.read(w);
            if (!metaProperty.write(w, fromJson(args.value("value"), metaProperty, old)))
                throw ScriptError(QStringLiteral("'%1' cannot be set to that value").arg(name));
            const QByteArray key = name.toLatin1();
            record(w, [key, old](QWidget* t) { t->setProperty(key.constData(), old); });
            return changed(w);
        }, false);

    add(ScriptMethod(QStringLiteral("ui.restore"))
            .setDescription(QStringLiteral("Puts back everything this run changed with the ui methods, the latest "
                                           "change first; a widget that is gone is skipped. Changes a run does not "
                                           "restore stay after it ends."))
            .setReturns(object, QString(), QJsonObject{{"restored", 3}, {"skipped", 0}}),
        [this](const QJsonObject&) -> QJsonValue {
            return restore();
        }, false);
}

QJsonValue ScriptUi::setLayout(QWidget* widget, QLayout* layout, const QJsonObject& options)
{
    auto* box = qobject_cast<QBoxLayout*>(layout);
    // Everything checked before anything changes.
    QMargins margins = layout->contentsMargins();
    const QJsonValue newMargins = options.value("margins");
    if (newMargins.isDouble()) {
        const int all = newMargins.toInt();
        margins = QMargins(all, all, all, all);
    } else if (newMargins.isObject()) {
        const QJsonObject sides = newMargins.toObject();
        margins = QMargins(sides.value("left").toInt(margins.left()), sides.value("top").toInt(margins.top()),
                           sides.value("right").toInt(margins.right()), sides.value("bottom").toInt(margins.bottom()));
    }
    Qt::Alignment alignment = layout->alignment();
    if (options.contains("alignment")) {
        bool ok = false;
        const QByteArray keys = options.value("alignment").toString().toLatin1();
        const int value = QMetaEnum::fromType<Qt::Alignment>().keysToValue(keys.constData(), &ok);
        if (!ok)
            throw ScriptError(QStringLiteral("'%1' is not an alignment").arg(options.value("alignment").toString()));
        alignment = Qt::Alignment(value);
    }
    const QBoxLayout::Direction oldDirection = box ? box->direction() : QBoxLayout::TopToBottom;
    QBoxLayout::Direction direction = oldDirection;
    if (options.contains("direction")) {
        if (!box)
            throw ScriptError(QStringLiteral("only a box layout has a direction"));
        // The order of QBoxLayout::Direction.
        const QStringList names{"LeftToRight", "RightToLeft", "TopToBottom", "BottomToTop"};
        direction = QBoxLayout::Direction(names.indexOf(options.value("direction").toString()));
    }

    auto* grid = qobject_cast<QGridLayout*>(layout);
    const int oldSpacing = layout->spacing();
    const int oldHorizontal = grid ? grid->horizontalSpacing() : 0;
    const int oldVertical = grid ? grid->verticalSpacing() : 0;
    const QMargins oldMargins = layout->contentsMargins();
    const Qt::Alignment oldAlignment = layout->alignment();
    record(widget, [=](QWidget* t) {
        QLayout* now = t->layout();
        if (!now)
            return;
        if (auto* g = qobject_cast<QGridLayout*>(now)) {
            g->setHorizontalSpacing(oldHorizontal);
            g->setVerticalSpacing(oldVertical);
        } else {
            now->setSpacing(oldSpacing);
        }
        now->setContentsMargins(oldMargins);
        now->setAlignment(oldAlignment);
        if (auto* b = qobject_cast<QBoxLayout*>(now))
            b->setDirection(oldDirection);
    });
    if (options.contains("spacing"))
        layout->setSpacing(options.value("spacing").toInt());
    layout->setContentsMargins(margins);
    layout->setAlignment(alignment);
    if (box)
        box->setDirection(direction);
    return layoutInfo(layout);
}

QJsonValue ScriptUi::setStretch(QWidget* widget, int stretch, const QJsonObject& args)
{
    if (args.contains("at")) {
        auto* grid = qobject_cast<QGridLayout*>(widget->layout());
        if (!grid)
            throw ScriptError(QStringLiteral("the widget has no grid layout"));
        const QJsonObject at = args.value("at").toObject();
        const bool row = at.contains("row");
        if (row == at.contains("column"))
            throw ScriptError(QStringLiteral("'at' takes {row} or {column}"));
        const int line = (row ? at.value("row") : at.value("column")).toInt();
        if (line < 0)
            throw ScriptError(QStringLiteral("the row or column %1 is not 0 or more").arg(line));
        const int old = row ? grid->rowStretch(line) : grid->columnStretch(line);
        record(widget, [row, line, old](QWidget* t) {
            if (auto* g = qobject_cast<QGridLayout*>(t->layout())) {
                if (row)
                    g->setRowStretch(line, old);
                else
                    g->setColumnStretch(line, old);
            }
        });
        if (row)
            grid->setRowStretch(line, stretch);
        else
            grid->setColumnStretch(line, stretch);
        return layoutInfo(grid);
    }

    Place place;
    auto* box = placeOf(widget, &place) ? qobject_cast<QBoxLayout*>(place.layout) : nullptr;
    if (!box)
        throw ScriptError(QStringLiteral("the widget is not in a box layout (a grid's row or column: "
                                         "ui.setStretch(owner, stretch, {row}))"));
    record(widget, [old = place.stretch](QWidget* t) {
        Place now;
        if (!placeOf(t, &now))
            return;
        if (auto* b = qobject_cast<QBoxLayout*>(now.layout))
            b->setStretch(now.index, old);
    });
    box->setStretch(place.index, stretch);
    return changed(widget);
}

QJsonValue ScriptUi::moveToLayout(QWidget* widget, QWidget* target, const QJsonObject& at)
{
    if (widget == m_root || widget->isWindow())
        throw ScriptError(QStringLiteral("the root or a window cannot move into a layout"));
    QLayout* layout = target->layout();
    if (!layout || !canInsert(layout))
        throw ScriptError(QStringLiteral("the target has no box, grid or form layout"));
    if (target == widget || widget->isAncestorOf(target))
        throw ScriptError(QStringLiteral("a widget cannot move into itself"));

    Place to;
    to.index = at.value("index").toInt(-1);
    if (qobject_cast<QGridLayout*>(layout)) {
        if (!at.contains("row") || !at.contains("column"))
            throw ScriptError(QStringLiteral("a grid layout takes {row, column, rowSpan?, columnSpan?}"));
        to.row = at.value("row").toInt();
        to.column = at.value("column").toInt();
        to.rowSpan = at.value("rowSpan").toInt(1);
        to.columnSpan = at.value("columnSpan").toInt(1);
        if (to.row < 0 || to.column < 0 || to.rowSpan < 1 || to.columnSpan < 1)
            throw ScriptError(QStringLiteral("the cell is not in the grid"));
    } else if (qobject_cast<QBoxLayout*>(layout) && to.index > layout->count()) {
        throw ScriptError(QStringLiteral("the index %1 is not between 0 and %2").arg(to.index).arg(layout->count()));
    }

    Place from;
    const bool wasInLayout = placeOf(widget, &from);
    const QPointer<QWidget> oldParent = widget->parentWidget();
    const QRect oldGeometry = widget->geometry();
    const bool wasHidden = isHiddenByHand(widget);
    record(widget, [from, wasInLayout, oldParent, oldGeometry, wasHidden](QWidget* t) {
        takeOut(t);
        if (wasInLayout && from.layout) {
            insertAt(from.layout, t, from);
            return;
        }
        if (oldParent && t->parentWidget() != oldParent)
            t->setParent(oldParent);
        t->setGeometry(oldGeometry);
        t->setVisible(!wasHidden);
    });
    takeOut(widget);
    insertAt(layout, widget, to);
    return changed(widget);
}

ScriptUi* exposeWidgets(ScriptEngine* engine, QWidget* root)
{
    return new ScriptUi(engine, root);
}

} // namespace ScriptHost
