#pragma once
#include <QJsonObject>
#include <QString>
#include <QStringList>

// Checking the arguments of a call against the JSON schema of a method's
// parameters. Private to the library.
namespace ScriptHost
{
namespace ScriptSchema
{

// Repairs and checks `args` against `schema` ({"type":"object","properties":
// {...},"required":[...]}) in one recursive pass: "5" becomes 5 and "true"
// becomes true where the schema wants a number or a boolean, the spelling of
// enum values is normalised, null for an optional parameter counts as left
// out, defaults are applied; then types, enums, ranges, required parameters
// and unknown names are checked, through nested objects and array items.
// Returns true with the repaired input in args; otherwise false, args
// untouched, *message set, and *didYouMean when an unknown name is close to a
// declared one.
bool prepareInput(const QJsonObject& schema, QJsonObject& args,
                  QString* message, QString* didYouMean = nullptr);

// The candidate closest to name, or empty when none is near enough to be a
// typo of it.
QString closestName(const QString& name, const QStringList& candidates);

} // namespace ScriptSchema
} // namespace ScriptHost
