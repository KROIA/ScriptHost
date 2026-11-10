#include "ScriptMethod.h"
#include "ScriptSchema.h"
#include <QJsonDocument>
#include <QRegularExpression>

namespace ScriptHost
{

namespace
{

bool isIdentifier(const QString& text)
{
    static const QRegularExpression re(QStringLiteral("^[A-Za-z_$][A-Za-z0-9_$]*$"));
    return re.match(text).hasMatch();
}

QString compactJson(const QJsonValue& value)
{
    // QJsonDocument cannot hold a scalar in Qt 5, so serialise via an array.
    const QByteArray json = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(json.mid(1, json.size() - 2));
}

QString oneLine(const QString& text)
{
    return text.simplified();
}

// The TypeScript type a JSON value is an example of.
QString shapeOf(const QJsonValue& value, int depth)
{
    switch (value.type()) {
    case QJsonValue::Bool:   return QStringLiteral("boolean");
    case QJsonValue::Double: return QStringLiteral("number");
    case QJsonValue::String: return QStringLiteral("string");
    case QJsonValue::Array: {
        const QJsonArray array = value.toArray();
        if (array.isEmpty())
            return QStringLiteral("any[]");
        const QString element = shapeOf(array.first(), depth + 1);
        return element.contains(QLatin1Char('|')) ? QStringLiteral("(%1)[]").arg(element)
                                                  : element + QStringLiteral("[]");
    }
    case QJsonValue::Object: {
        const QJsonObject object = value.toObject();
        if (object.isEmpty() || depth >= 3)
            return QStringLiteral("object");
        QStringList fields;
        for (auto it = object.begin(); it != object.end(); ++it) {
            const QString key = isIdentifier(it.key()) ? it.key() : compactJson(it.key());
            fields << key + QStringLiteral(": ") + shapeOf(it.value(), depth + 1);
        }
        return QStringLiteral("{ ") + fields.join(QStringLiteral(", ")) + QStringLiteral(" }");
    }
    default:
        return QStringLiteral("any");
    }
}

// The TypeScript type a JSON schema describes.
QString typeOfSchema(const QJsonObject& schema, int depth)
{
    const QJsonArray enumValues = schema.value(QStringLiteral("enum")).toArray();
    if (!enumValues.isEmpty()) {
        QStringList values;
        for (const QJsonValue& v : enumValues)
            values << compactJson(v);
        return values.join(QStringLiteral(" | "));
    }
    const QString type = schema.value(QStringLiteral("type")).toString();
    if (type == QLatin1String("string") || type == QLatin1String("boolean"))
        return type;
    if (type == QLatin1String("integer") || type == QLatin1String("number"))
        return QStringLiteral("number");
    if (type == QLatin1String("array")) {
        const QJsonObject items = schema.value(QStringLiteral("items")).toObject();
        if (items.isEmpty())
            return QStringLiteral("any[]");
        const QString element = typeOfSchema(items, depth + 1);
        return element.contains(QLatin1Char('|')) ? QStringLiteral("(%1)[]").arg(element)
                                                  : element + QStringLiteral("[]");
    }
    if (type == QLatin1String("object")) {
        const QJsonObject properties = schema.value(QStringLiteral("properties")).toObject();
        if (properties.isEmpty() || depth >= 3)
            return QStringLiteral("object");
        QStringList required;
        for (const QJsonValue& v : schema.value(QStringLiteral("required")).toArray())
            required << v.toString();
        QStringList fields;
        for (auto it = properties.begin(); it != properties.end(); ++it) {
            const QString key = isIdentifier(it.key()) ? it.key() : compactJson(it.key());
            fields << key + (required.contains(it.key()) ? QStringLiteral(": ") : QStringLiteral("?: "))
                          + typeOfSchema(it.value().toObject(), depth + 1);
        }
        return QStringLiteral("{ ") + fields.join(QStringLiteral(", ")) + QStringLiteral(" }");
    }
    return QStringLiteral("any");
}

// "Object id (default 5, 1 to 10)" - what the schema says beyond the type.
QString parameterNote(const QJsonObject& schema)
{
    QString note = oneLine(schema.value(QStringLiteral("description")).toString());
    QStringList extras;
    if (schema.contains(QStringLiteral("default")))
        extras << QStringLiteral("default ") + compactJson(schema.value(QStringLiteral("default")));
    if (schema.contains(QStringLiteral("minimum")) && schema.contains(QStringLiteral("maximum")))
        extras << QStringLiteral("%1 to %2").arg(schema.value(QStringLiteral("minimum")).toDouble())
                                            .arg(schema.value(QStringLiteral("maximum")).toDouble());
    if (!extras.isEmpty())
        note += QStringLiteral(" (") + extras.join(QStringLiteral(", ")) + QLatin1Char(')');
    return note.trimmed();
}

} // namespace

ScriptMethod::ScriptMethod(const QString& name)
    : m_name(name)
{
}

ScriptMethod& ScriptMethod::setDescription(const QString& description)
{
    m_description = description;
    return *this;
}

ScriptMethod& ScriptMethod::setGroup(const QString& group)
{
    m_group = group;
    return *this;
}

ScriptMethod& ScriptMethod::addParameter(const QString& name, const QString& type,
                                         const QString& description, bool required,
                                         const QStringList& enumValues)
{
    QJsonObject schema;
    schema.insert(QStringLiteral("type"), type);
    schema.insert(QStringLiteral("description"), description);
    if (!enumValues.isEmpty())
        schema.insert(QStringLiteral("enum"), QJsonArray::fromStringList(enumValues));
    return addRawParameter(name, schema, required);
}

ScriptMethod& ScriptMethod::addRawParameter(const QString& name, const QJsonObject& schema, bool required)
{
    if (!m_properties.contains(name))
        m_order << name;
    m_properties.insert(name, schema);
    m_required.removeAll(name);
    if (required)
        m_required << name;
    return *this;
}

ScriptMethod& ScriptMethod::setRange(const QString& name, double minimum, double maximum)
{
    if (m_properties.contains(name)) {
        QJsonObject schema = m_properties.value(name).toObject();
        schema.insert(QStringLiteral("minimum"), minimum);
        schema.insert(QStringLiteral("maximum"), maximum);
        m_properties.insert(name, schema);
    }
    return *this;
}

ScriptMethod& ScriptMethod::setDefault(const QString& name, const QJsonValue& value)
{
    if (m_properties.contains(name)) {
        QJsonObject schema = m_properties.value(name).toObject();
        schema.insert(QStringLiteral("default"), value);
        m_properties.insert(name, schema);
    }
    return *this;
}

ScriptMethod& ScriptMethod::setReturns(const QString& type, const QString& description,
                                       const QJsonValue& example)
{
    m_returnType = type;
    m_returnDescription = description;
    m_returnExample = example;
    return *this;
}

ScriptMethod& ScriptMethod::setReadOnly(bool readOnly)
{
    m_readOnly = readOnly;
    return *this;
}

ScriptMethod ScriptMethod::fromSchema(const QString& name, const QJsonObject& parameterSchema)
{
    ScriptMethod method(name);
    method.m_takesObject = true;
    method.m_properties = parameterSchema.value(QStringLiteral("properties")).toObject();
    method.m_order = method.m_properties.keys();
    const QJsonArray required = parameterSchema.value(QStringLiteral("required")).toArray();
    for (const QJsonValue& v : required)
        method.m_required << v.toString();
    return method;
}

QString ScriptMethod::nameSpace() const
{
    const int dot = m_name.indexOf(QLatin1Char('.'));
    return dot < 0 ? QString() : m_name.left(dot);
}

QString ScriptMethod::functionName() const
{
    return m_name.mid(m_name.indexOf(QLatin1Char('.')) + 1);
}

QJsonObject ScriptMethod::parameterSchema() const
{
    QJsonObject schema;
    schema.insert(QStringLiteral("type"), QStringLiteral("object"));
    schema.insert(QStringLiteral("properties"), m_properties);
    if (!m_required.isEmpty())
        schema.insert(QStringLiteral("required"), QJsonArray::fromStringList(m_required));
    return schema;
}

ScriptMethod& ScriptMethod::setMaxCallsPerRun(int calls)
{
    m_maxCallsPerRun = calls;
    return *this;
}

bool ScriptMethod::isValidName(const QString& name)
{
    const QStringList parts = name.split(QLatin1Char('.'));
    if (parts.size() > 2)
        return false;
    for (const QString& part : parts) {
        if (!isIdentifier(part))
            return false;
    }
    return true;
}

QString ScriptMethod::typeOf(const QJsonObject& schema) const
{
    return typeOfSchema(schema, 0);
}

QString ScriptMethod::returnTypeText() const
{
    if (!m_returnExample.isUndefined())
        return shapeOf(m_returnExample, 0);
    if (m_returnType.isEmpty())
        return QStringLiteral("void");
    if (m_returnType == QLatin1String("integer"))
        return QStringLiteral("number");
    if (m_returnType == QLatin1String("array"))
        return QStringLiteral("any[]");
    return m_returnType;
}

QString ScriptMethod::signature() const
{
    const QJsonObject schema = parameterSchema();
    QString parameters;
    if (m_takesObject) {
        if (!m_order.isEmpty())
            parameters = QStringLiteral("args: ") + typeOf(schema);
    } else {
        const QJsonObject properties = schema.value(QStringLiteral("properties")).toObject();
        QStringList list;
        for (const QString& name : m_order) {
            list << name + (m_required.contains(name) ? QStringLiteral(": ") : QStringLiteral("?: "))
                         + typeOf(properties.value(name).toObject());
        }
        parameters = list.join(QStringLiteral(", "));
    }
    return QStringLiteral("%1(%2): %3").arg(m_name, parameters, returnTypeText());
}

QString ScriptMethod::declaration(const QString& indent, const QString& keyword) const
{
    QStringList doc;
    QString summary = oneLine(m_description);
    if (m_readOnly)
        summary += summary.isEmpty() ? QStringLiteral("Read-only.") : QStringLiteral(" Read-only.");
    if (!summary.isEmpty())
        doc << summary;

    const QJsonObject properties = parameterSchema().value(QStringLiteral("properties")).toObject();
    for (const QString& name : m_order) {
        const QString note = parameterNote(properties.value(name).toObject());
        if (!note.isEmpty())
            doc << QStringLiteral("@param %1%2 %3").arg(m_takesObject ? QStringLiteral("args.") : QString(), name, note);
    }
    if (!m_returnDescription.isEmpty())
        doc << QStringLiteral("@returns ") + oneLine(m_returnDescription);

    QString text;
    for (int i = 0; i < doc.size(); ++i) {
        text += indent + (i == 0 ? QStringLiteral("/** ") : QStringLiteral(" *  ")) + doc[i];
        text += i + 1 == doc.size() ? QStringLiteral(" */\n") : QStringLiteral("\n");
    }
    // "db.readObject(...)" -> "function readObject(...)"
    QString sig = signature();
    sig.remove(0, m_name.size() - functionName().size());
    text += indent + keyword + QLatin1Char(' ') + sig + QStringLiteral(";\n");
    return text;
}

bool ScriptMethod::bindArguments(const QJsonArray& arguments, QJsonObject& named, QString* error) const
{
    auto fail = [error](const QString& message) {
        if (error)
            *error = message;
        return false;
    };

    QJsonObject input;
    const QJsonObject schema = parameterSchema();
    const QJsonObject properties = schema.value(QStringLiteral("properties")).toObject();

    if (m_takesObject) {
        if (arguments.size() > 1 || (arguments.size() == 1 && !arguments.first().isObject()
                                     && !arguments.first().isNull()))
            return fail(QStringLiteral("takes one object with the named arguments"));
        input = arguments.first().toObject();
    } else {
        const QString firstType = m_order.isEmpty()
            ? QString()
            : properties.value(m_order.first()).toObject().value(QStringLiteral("type")).toString();
        const bool firstTakesObject = m_order.size() == 1
            && (firstType.isEmpty() || firstType == QLatin1String("object"));
        if (arguments.size() == 1 && arguments.first().isObject() && !m_order.isEmpty() && !firstTakesObject) {
            // db.setProperty({id: "a1", name: "status"}) - named arguments.
            input = arguments.first().toObject();
        } else {
            if (arguments.size() > m_order.size()) {
                return fail(m_order.isEmpty()
                    ? QStringLiteral("takes no arguments, got %1").arg(arguments.size())
                    : QStringLiteral("takes %1 argument(s), got %2").arg(m_order.size()).arg(arguments.size()));
            }
            for (int i = 0; i < arguments.size(); ++i) {
                // null stands for "left out" (undefined arrives as null).
                if (!arguments.at(i).isNull())
                    input.insert(m_order.at(i), arguments.at(i));
            }
        }
    }

    QString message, suggestion;
    if (!ScriptSchema::prepareInput(schema, input, &message, &suggestion)) {
        if (!suggestion.isEmpty())
            message += QStringLiteral(" (did you mean '%1'?)").arg(suggestion);
        return fail(message);
    }
    named = input;
    return true;
}

} // namespace ScriptHost
