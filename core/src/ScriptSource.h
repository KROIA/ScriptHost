#pragma once
#include <QList>
#include <QString>
#include <QStringList>

// Looking at script source without running it. Private to the library.
namespace ScriptHost
{
namespace ScriptSource
{

// The code with the contents of string, template and regular-expression
// literals and of comments replaced by spaces. Same length, same lines - a
// search in the result finds only real code, at the position it has in the
// original. Code inside `${...}` of a template stays.
QString blankLiterals(const QString& code);

// 1-based line of a position.
int lineAt(const QString& code, int position);

struct Reference
{
    QString nameSpace;   // empty for a global function
    QString function;    // empty: the namespace is used some other way (db[x], f(db))
    QString path;        // the whole chain of names after the namespace: "draw.star"
    int     line = 0;
};

// Where the code uses the given namespace objects ("db.readObject") and calls
// the given global functions ("log(...)"). A name the code declares itself
// (`const db = ...`) is its own and is left out.
QList<Reference> references(const QString& code,
                            const QStringList& nameSpaces,
                            const QStringList& globalFunctions);

// The code reaches for things by a name computed at run time (eval, Function,
// this, globalThis), so a look at the source cannot tell what it will call.
bool usesDynamicAccess(const QString& code);

// For the debugger: the code with a call
//   __sh_step(<line>, function(){ ... return {name: value, ...} })
// in front of every statement that starts a line. The function handed over
// reads the variables visible at that statement (declaredNames); one that
// does not exist there, or not yet, is left out. It is a closure and not an
// eval(): in strict mode the engine's eval does not see local variables.
// Lines stay where they are. Works on the text, not on a syntax tree: a
// statement it is not sure about gets no call, and the caller compiles the
// result and falls back to the original should it not compile.
QString instrumentForStepping(const QString& code);

// The names the code declares: variables, parameters, catch bindings - and
// `args`. What the debugger asks the paused scope for.
QStringList declaredNames(const QString& code);

} // namespace ScriptSource
} // namespace ScriptHost
