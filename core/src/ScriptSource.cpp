#include "ScriptSource.h"
#include <QRegularExpression>
#include <QSet>
#include <QVector>

namespace ScriptHost
{
namespace ScriptSource
{

namespace
{

bool isWordChar(QChar c)
{
    return c.isLetterOrNumber() || c == QLatin1Char('_') || c == QLatin1Char('$');
}

// After these a '/' starts a regular expression; after anything else that
// ends a value it divides.
bool regexMayFollow(QChar lastSignificant, const QString& lastWord)
{
    if (lastSignificant.isNull())
        return true;
    if (isWordChar(lastSignificant)) {
        static const QSet<QString> keywords{
            QStringLiteral("return"), QStringLiteral("typeof"), QStringLiteral("case"),
            QStringLiteral("in"), QStringLiteral("of"), QStringLiteral("delete"),
            QStringLiteral("void"), QStringLiteral("throw"), QStringLiteral("new"),
            QStringLiteral("else"), QStringLiteral("do")};
        return keywords.contains(lastWord);
    }
    return QStringLiteral("(,=:[!&|?{;+-*%<>~^").contains(lastSignificant);
}

} // namespace

QString blankLiterals(const QString& code)
{
    enum State { Code, LineComment, BlockComment, Single, Double, Template, Regex, RegexClass };

    QString out = code;
    State state = Code;
    // One entry per open `${` of a template: the depth of braces inside it.
    QVector<int> expressionDepth;
    QChar lastSignificant;
    QString lastWord;
    const int n = code.size();
    const QChar blank = QLatin1Char(' ');
    const QChar newline = QLatin1Char('\n');

    // An escape inside a literal: blank it and what it escapes.
    auto blankEscape = [&](int& i) {
        out[i] = blank;
        if (i + 1 < n && code[i + 1] != newline)
            out[++i] = blank;
    };

    for (int i = 0; i < n; ++i) {
        const QChar c = code[i];
        const QChar next = i + 1 < n ? code[i + 1] : QChar();

        switch (state) {
        case Code:
            if (c == QLatin1Char('/') && next == QLatin1Char('/')) {
                out[i] = blank;
                out[++i] = blank;
                state = LineComment;
            } else if (c == QLatin1Char('/') && next == QLatin1Char('*')) {
                out[i] = blank;
                out[++i] = blank;
                state = BlockComment;
            } else if (c == QLatin1Char('\'')) {
                state = Single;
            } else if (c == QLatin1Char('"')) {
                state = Double;
            } else if (c == QLatin1Char('`')) {
                state = Template;
            } else if (c == QLatin1Char('/') && regexMayFollow(lastSignificant, lastWord)) {
                state = Regex;
            } else {
                if (!expressionDepth.isEmpty()) {
                    if (c == QLatin1Char('{')) {
                        ++expressionDepth.last();
                    } else if (c == QLatin1Char('}')) {
                        if (expressionDepth.last() == 0) {
                            expressionDepth.removeLast();
                            state = Template;
                            break;
                        }
                        --expressionDepth.last();
                    }
                }
                if (isWordChar(c)) {
                    if (!isWordChar(lastSignificant) || (i > 0 && !isWordChar(code[i - 1])))
                        lastWord.clear();
                    lastWord += c;
                    lastSignificant = c;
                } else if (!c.isSpace()) {
                    lastSignificant = c;
                }
            }
            break;

        case LineComment:
            if (c == newline)
                state = Code;
            else
                out[i] = blank;
            break;

        case BlockComment:
            if (c == QLatin1Char('*') && next == QLatin1Char('/')) {
                out[i] = blank;
                out[++i] = blank;
                state = Code;
            } else if (c != newline) {
                out[i] = blank;
            }
            break;

        case Single:
        case Double:
            if (c == QLatin1Char('\\')) {
                blankEscape(i);
            } else if (c == (state == Single ? QLatin1Char('\'') : QLatin1Char('"'))) {
                lastSignificant = c;     // a value ended here
                state = Code;
            } else if (c == newline) {
                state = Code;            // unterminated; the compiler reports it
            } else {
                out[i] = blank;
            }
            break;

        case Template:
            if (c == QLatin1Char('\\')) {
                blankEscape(i);
            } else if (c == QLatin1Char('`')) {
                lastSignificant = c;
                state = Code;
            } else if (c == QLatin1Char('$') && next == QLatin1Char('{')) {
                ++i;
                expressionDepth.append(0);
                lastSignificant = QLatin1Char('{');
                state = Code;
            } else if (c != newline) {
                out[i] = blank;
            }
            break;

        case Regex:
            if (c == QLatin1Char('\\')) {
                blankEscape(i);
            } else if (c == QLatin1Char('[')) {
                out[i] = blank;
                state = RegexClass;
            } else if (c == QLatin1Char('/')) {
                lastSignificant = QLatin1Char(')');   // a value ended here
                state = Code;
            } else if (c == newline) {
                state = Code;
            } else {
                out[i] = blank;
            }
            break;

        case RegexClass:
            if (c == QLatin1Char('\\')) {
                blankEscape(i);
            } else if (c == QLatin1Char(']')) {
                out[i] = blank;
                state = Regex;
            } else if (c == newline) {
                state = Code;
            } else {
                out[i] = blank;
            }
            break;
        }
    }
    return out;
}

int lineAt(const QString& code, int position)
{
    int line = 1;
    const int end = qMin(position, int(code.size()));
    for (int i = 0; i < end; ++i) {
        if (code[i] == QLatin1Char('\n'))
            ++line;
    }
    return line;
}

QList<Reference> references(const QString& code,
                            const QStringList& nameSpaces,
                            const QStringList& globalFunctions)
{
    const QString source = blankLiterals(code);

    // Names the script declares are its own, whatever the application calls its objects.
    QSet<QString> declared;
    static const QRegularExpression declaration(
        QStringLiteral("\\b(?:let|const|var|function|class)\\s+([A-Za-z_$][\\w$]*)"));
    auto declarations = declaration.globalMatch(source);
    while (declarations.hasNext())
        declared.insert(declarations.next().captured(1));

    QList<Reference> found;
    static const QRegularExpression member(QStringLiteral("\\G\\s*\\.\\s*([A-Za-z_$][\\w$]*)"));

    for (const QString& nameSpace : nameSpaces) {
        if (nameSpace.isEmpty() || declared.contains(nameSpace))
            continue;
        const QRegularExpression use(QStringLiteral("(?<![\\w$.])%1(?![\\w$])")
                                         .arg(QRegularExpression::escape(nameSpace)));
        auto uses = use.globalMatch(source);
        while (uses.hasNext()) {
            const QRegularExpressionMatch match = uses.next();
            Reference reference;
            reference.nameSpace = nameSpace;
            reference.line = lineAt(source, match.capturedStart());
            const QRegularExpressionMatch function = member.match(source, match.capturedEnd());
            if (function.hasMatch())
                reference.function = function.captured(1);
            found.append(reference);
        }
    }

    for (const QString& function : globalFunctions) {
        if (function.isEmpty() || declared.contains(function))
            continue;
        const QRegularExpression call(QStringLiteral("(?<![\\w$.])%1\\s*\\(")
                                          .arg(QRegularExpression::escape(function)));
        auto calls = call.globalMatch(source);
        while (calls.hasNext()) {
            Reference reference;
            reference.function = function;
            reference.line = lineAt(source, calls.next().capturedStart());
            found.append(reference);
        }
    }
    return found;
}

bool usesDynamicAccess(const QString& code)
{
    static const QRegularExpression dynamic(
        QStringLiteral("(?<![\\w$.])(?:eval|Function|globalThis|this)(?![\\w$])"));
    return dynamic.match(blankLiterals(code)).hasMatch();
}

} // namespace ScriptSource
} // namespace ScriptHost
