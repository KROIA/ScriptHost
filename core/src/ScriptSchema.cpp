#include "ScriptSchema.h"
#include <QJsonArray>
#include <QJsonValue>
#include <QVector>
#include <algorithm>
#include <cmath>

namespace ScriptHost
{
namespace ScriptSchema
{

namespace
{

// Type check for a single JSON-Schema primitive name. Unknown type names
// pass - forward compatibility beats false rejections here.
bool matchesJsonType(const QString& type, const QJsonValue& value)
{
    if (type == QLatin1String("string"))  return value.isString();
    if (type == QLatin1String("boolean")) return value.isBool();
    if (type == QLatin1String("array"))   return value.isArray();
    if (type == QLatin1String("object"))  return value.isObject();
    if (type == QLatin1String("null"))    return value.isNull();
    if (type == QLatin1String("number"))  return value.isDouble();
    if (type == QLatin1String("integer")) {
        if (!value.isDouble())
            return false;
        const double d = value.toDouble();
        return d == static_cast<double>(static_cast<qint64>(d));
    }
    return true;
}

// "type" is a single name or a list of names.
QStringList schemaTypes(const QJsonObject& schema)
{
    const QJsonValue type = schema.value(QStringLiteral("type"));
    if (type.isString())
        return {type.toString()};
    QStringList types;
    const QJsonArray list = type.toArray();
    for (const QJsonValue& t : list)
        types.append(t.toString());
    return types;
}

bool matchesAnyType(const QStringList& types, const QJsonValue& value)
{
    if (types.isEmpty())
        return true;
    for (const QString& type : types) {
        if (matchesJsonType(type, value))
            return true;
    }
    return false;
}

int editDistance(const QString& a, const QString& b)
{
    QVector<int> row(b.size() + 1);
    for (int j = 0; j <= b.size(); ++j)
        row[j] = j;
    for (int i = 1; i <= a.size(); ++i) {
        int diagonal = row[0];
        row[0] = i;
        for (int j = 1; j <= b.size(); ++j) {
            const int above = row[j];
            row[j] = std::min({row[j] + 1, row[j - 1] + 1,
                               diagonal + (a[i - 1] == b[j - 1] ? 0 : 1)});
            diagonal = above;
        }
    }
    return row[b.size()];
}

struct Pass
{
    QString message;
    QString didYouMean;
};

QString joinPath(const QString& path, const QString& key)
{
    return path.isEmpty() ? key : path + QLatin1Char('.') + key;
}

bool fail(Pass& pass, const QString& message)
{
    pass.message = message;
    return false;
}

bool prepareObject(const QJsonObject& schema, QJsonObject& object, const QString& path, Pass& pass);

bool prepareValue(const QJsonObject& schema, QJsonValue& value, const QString& path, Pass& pass)
{
    const QStringList types = schemaTypes(schema);

    // "5" for 5 and "true" for true.
    if (value.isString() && !types.contains(QStringLiteral("string"))) {
        const QString text = value.toString().trimmed();
        bool ok = false;
        const double d = text.toDouble(&ok);
        const bool isTrue = text.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0;
        const bool isFalse = text.compare(QLatin1String("false"), Qt::CaseInsensitive) == 0;
        if (ok && std::isfinite(d)
            && (types.contains(QStringLiteral("number"))
                || (types.contains(QStringLiteral("integer"))
                    && matchesJsonType(QStringLiteral("integer"), d))))
            value = d;
        else if (types.contains(QStringLiteral("boolean")) && (isTrue || isFalse))
            value = isTrue;
    }

    const QJsonArray enumValues = schema.value(QStringLiteral("enum")).toArray();
    if (value.isString()) {
        // The spelling of the definition, whatever case the script used.
        for (const QJsonValue& allowed : enumValues) {
            if (allowed.isString()
                && allowed.toString().compare(value.toString(), Qt::CaseInsensitive) == 0) {
                value = allowed;
                break;
            }
        }
    }

    if (!matchesAnyType(types, value)) {
        return fail(pass, QStringLiteral("'%1' must be of type %2")
                              .arg(path, types.join(QLatin1Char('|'))));
    }

    if (!enumValues.isEmpty() && !enumValues.contains(value)) {
        QStringList allowed;
        for (const QJsonValue& v : enumValues)
            allowed << v.toVariant().toString();
        return fail(pass, QStringLiteral("'%1' must be one of: %2")
                              .arg(path, allowed.join(QStringLiteral(", "))));
    }

    if (value.isDouble()) {
        const double d = value.toDouble();
        const QJsonValue minimum = schema.value(QStringLiteral("minimum"));
        const QJsonValue maximum = schema.value(QStringLiteral("maximum"));
        if (minimum.isDouble() && d < minimum.toDouble())
            return fail(pass, QStringLiteral("'%1' must be at least %2").arg(path).arg(minimum.toDouble()));
        if (maximum.isDouble() && d > maximum.toDouble())
            return fail(pass, QStringLiteral("'%1' must be at most %2").arg(path).arg(maximum.toDouble()));
    }

    if (value.isObject() && schema.contains(QStringLiteral("properties"))) {
        QJsonObject nested = value.toObject();
        if (!prepareObject(schema, nested, path, pass))
            return false;
        value = nested;
    }

    if (value.isArray() && schema.value(QStringLiteral("items")).isObject()) {
        const QJsonObject itemSchema = schema.value(QStringLiteral("items")).toObject();
        QJsonArray items = value.toArray();
        for (int i = 0; i < items.size(); ++i) {
            QJsonValue item = items[i];
            if (!prepareValue(itemSchema, item, QStringLiteral("%1[%2]").arg(path).arg(i), pass))
                return false;
            items[i] = item;
        }
        value = items;
    }
    return true;
}

bool prepareObject(const QJsonObject& schema, QJsonObject& object, const QString& path, Pass& pass)
{
    const QJsonObject properties = schema.value(QStringLiteral("properties")).toObject();
    const QJsonArray required = schema.value(QStringLiteral("required")).toArray();

    // null for an optional parameter means "left out"; then the defaults.
    const QStringList given = object.keys();
    for (const QString& key : given) {
        if (object.value(key).isNull() && properties.contains(key) && !required.contains(key))
            object.remove(key);
    }
    for (auto it = properties.begin(); it != properties.end(); ++it) {
        const QJsonObject property = it.value().toObject();
        if (!object.contains(it.key()) && property.contains(QStringLiteral("default")))
            object.insert(it.key(), property.value(QStringLiteral("default")));
    }

    // Only a schema that declares its properties can call a name unknown.
    const QJsonValue additional = schema.value(QStringLiteral("additionalProperties"));
    if (schema.contains(QStringLiteral("properties")) && !additional.isObject()
        && additional != QJsonValue(true)) {
        const QStringList keys = object.keys();
        for (const QString& key : keys) {
            if (properties.contains(key))
                continue;
            pass.didYouMean = closestName(key, properties.keys());
            return fail(pass, QStringLiteral("unknown parameter '%1'; there are: %2")
                                  .arg(joinPath(path, key), properties.keys().join(QStringLiteral(", "))));
        }
    }

    for (const QJsonValue& name : required) {
        if (!object.contains(name.toString()))
            return fail(pass, QStringLiteral("'%1' is missing").arg(joinPath(path, name.toString())));
    }

    for (auto it = properties.begin(); it != properties.end(); ++it) {
        if (!object.contains(it.key()))
            continue;
        QJsonValue value = object.value(it.key());
        if (!prepareValue(it.value().toObject(), value, joinPath(path, it.key()), pass))
            return false;
        object.insert(it.key(), value);
    }
    return true;
}

} // namespace

QString closestName(const QString& name, const QStringList& candidates)
{
    // The distance must stay below the shorter length, so that "x" never
    // suggests "id".
    QString best;
    int bestDistance = 3;
    for (const QString& candidate : candidates) {
        const int d = editDistance(name.toLower(), candidate.toLower());
        if (d < bestDistance && d < std::min(int(name.size()), int(candidate.size()))) {
            best = candidate;
            bestDistance = d;
        }
    }
    return best;
}

bool prepareInput(const QJsonObject& schema, QJsonObject& args, QString* message, QString* didYouMean)
{
    Pass pass;
    QJsonObject repaired = args;
    if (!prepareObject(schema, repaired, QString(), pass)) {
        if (message)
            *message = pass.message;
        if (didYouMean)
            *didYouMean = pass.didYouMean;
        return false;
    }
    args = repaired;
    return true;
}

} // namespace ScriptSchema
} // namespace ScriptHost
