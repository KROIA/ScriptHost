#pragma once
#include "ScriptHost_base.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QSharedPointer>
#include <QString>
#include <QStringList>
#include <functional>
#include <stdexcept>

namespace ScriptHost
{

// The definition of one method a script can call: what it is named, what it
// does, what it takes and what it returns. The application writes it once;
// whoever writes scripts reads it (ScriptEngine::apiDefinition, the script
// editor's reference), and calls are checked against it.
//
//   ScriptHost::ScriptMethod m("db.readObject");
//   m.setDescription("Reads one object of the project.")
//    .addParameter("id", "string", "Object id")
//    .setReturns("object", "The object", QJsonObject{{"id", "a1"}, {"name", "Pump"}})
//    .setReadOnly(true);
//
// In a script: `const o = db.readObject("a1");` - arguments are positional,
// in the order the parameters were added.
class SCRIPT_HOST_API ScriptMethod
{
public:
    ScriptMethod() = default;
    // "name" for a global function, "space.name" for a function of the
    // object `space`. See isValidName().
    explicit ScriptMethod(const QString& name);

    ScriptMethod& setDescription(const QString& description);
    // Grouping in settings lists and in the reference; also what
    // ScriptEngine::setMethodGroupEnabled() addresses.
    ScriptMethod& setGroup(const QString& group);

    // type: "string", "integer", "number", "boolean", "array", "object".
    // A parameter is required unless `required` is false (the default is
    // true, here and in addRawParameter). Optional parameters go last; a
    // script simply leaves them out.
    ScriptMethod& addParameter(const QString& name,
                               const QString& type,
                               const QString& description,
                               bool required = true,
                               const QStringList& enumValues = {});
    // The parameter's JSON schema, verbatim.
    ScriptMethod& addRawParameter(const QString& name, const QJsonObject& schema, bool required = true);
    ScriptMethod& setRange(const QString& name, double minimum, double maximum);
    ScriptMethod& setDefault(const QString& name, const QJsonValue& value);

    // What the method returns. The example is the most useful part: its shape
    // becomes the return type in the definition, and a dry run answers a call
    // to a method that changes something with it.
    ScriptMethod& setReturns(const QString& type,
                             const QString& description = QString(),
                             const QJsonValue& example = QJsonValue(QJsonValue::Undefined));

    // The method only reads. Such methods run in a dry run and need no
    // approval where only changing scripts are approved.
    ScriptMethod& setReadOnly(bool readOnly);

    // At most this many calls in one run; the run is stopped at the next
    // one. For methods that are expensive. 0 (default) = only the engine's
    // limit on all calls applies.
    ScriptMethod& setMaxCallsPerRun(int calls);

    // A method that takes one object, described by a JSON schema
    // ({"type":"object","properties":{...},"required":[...]}), instead of
    // positional arguments: name({a: 1, b: 2}). For interfaces that are
    // defined by such a schema already.
    static ScriptMethod fromSchema(const QString& name, const QJsonObject& parameterSchema);

    QString name() const { return m_name; }
    QString nameSpace() const;      // "db" for "db.readObject", empty for a global function
    QString functionName() const;   // "readObject"
    QString description() const { return m_description; }
    QString group() const { return m_group; }
    bool isReadOnly() const { return m_readOnly; }
    bool takesObject() const { return m_takesObject; }   // one object argument (fromSchema)
    int maxCallsPerRun() const { return m_maxCallsPerRun; }
    QStringList parameterNames() const { return m_order; }
    QJsonObject parameterSchema() const;
    QString returnType() const { return m_returnType; }
    QString returnDescription() const { return m_returnDescription; }
    QJsonValue returnExample() const { return m_returnExample; }

    // "db.readObject(id: string): { id: string, name: string }"
    QString signature() const;
    // The TypeScript declaration with its doc comment, as it stands inside
    // `declare namespace`. A global function is declared with the keyword
    // "declare function".
    QString declaration(const QString& indent = QString(),
                        const QString& keyword = QStringLiteral("function")) const;

    // An identifier, or two joined by a dot: "log", "db.readObject".
    static bool isValidName(const QString& name);

    // Turns the arguments of a call into the named input a handler gets and
    // checks it against the definition (types repaired where unambiguous,
    // defaults applied). A single object passed to a method with several
    // parameters is taken as named arguments - unless the first parameter
    // is of type "object": then it is that argument. Returns false with
    // *error set.
    bool bindArguments(const QJsonArray& arguments, QJsonObject& named, QString* error) const;

private:
    QString typeOf(const QJsonObject& schema) const;
    QString returnTypeText() const;

    QString     m_name;
    QString     m_description;
    QString     m_group;
    QJsonObject m_properties;       // parameter name -> its JSON schema
    QStringList m_order;            // parameter names in call order
    QStringList m_required;
    QString     m_returnType;
    QString     m_returnDescription;
    QJsonValue  m_returnExample = QJsonValue(QJsonValue::Undefined);
    bool        m_readOnly = false;
    bool        m_takesObject = false;
    int         m_maxCallsPerRun = 0;
};

// Thrown by a method handler to fail the call: the script gets an exception
// with this message (and stops, unless it catches it).
class ScriptError : public std::runtime_error
{
public:
    explicit ScriptError(const QString& message) : std::runtime_error(message.toStdString()) {}
};

// A method: gets its input by parameter name, returns the value the script
// receives. Runs on the GUI thread.
using ScriptMethodHandler = std::function<QJsonValue(const QJsonObject& args)>;

// How an asynchronous method reports back. Copyable, usable from any thread.
// Only the first call counts.
class SCRIPT_HOST_API ScriptCompletion
{
public:
    ScriptCompletion() = default;
    void finish(const QJsonValue& value = QJsonValue()) const;
    void fail(const QString& message) const;

private:
    friend class ScriptEngine;
    struct State;
    QSharedPointer<State> d;
};

// A method that answers later: returns at once and calls done.finish() or
// done.fail() when it knows. The script waits meanwhile; the application does not.
using AsyncScriptMethodHandler = std::function<void(const QJsonObject& args, ScriptCompletion done)>;

} // namespace ScriptHost
