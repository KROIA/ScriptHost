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
    static const QRegularExpression member(
        QStringLiteral("\\G\\s*\\.\\s*([A-Za-z_$][\\w$]*)((?:\\s*\\.\\s*[A-Za-z_$][\\w$]*)*)"));
    static const QRegularExpression space(QStringLiteral("\\s+"));

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
            if (function.hasMatch()) {
                reference.function = function.captured(1);
                reference.path = function.captured(1) + function.captured(2);
                reference.path.remove(space);
            }
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

QString instrumentForStepping(const QString& code)
{
    // A line that starts with one of these continues what came before.
    static const QSet<QString> neverStarts{
        QStringLiteral("else"), QStringLiteral("catch"), QStringLiteral("finally"), QStringLiteral("case"),
        QStringLiteral("default"), QStringLiteral("in"), QStringLiteral("of"), QStringLiteral("instanceof"),
        QStringLiteral("extends")};
    // Words an expression cannot end with: after them a line break ends nothing.
    static const QSet<QString> notAValue{
        QStringLiteral("break"), QStringLiteral("case"), QStringLiteral("catch"), QStringLiteral("class"),
        QStringLiteral("const"), QStringLiteral("continue"), QStringLiteral("default"), QStringLiteral("delete"),
        QStringLiteral("do"), QStringLiteral("else"), QStringLiteral("extends"), QStringLiteral("finally"),
        QStringLiteral("for"), QStringLiteral("function"), QStringLiteral("if"), QStringLiteral("in"),
        QStringLiteral("instanceof"), QStringLiteral("let"), QStringLiteral("new"), QStringLiteral("of"),
        QStringLiteral("return"), QStringLiteral("switch"), QStringLiteral("throw"), QStringLiteral("try"),
        QStringLiteral("typeof"), QStringLiteral("var"), QStringLiteral("void"), QStringLiteral("while"),
        QStringLiteral("yield"), QStringLiteral("await"), QStringLiteral("async"), QStringLiteral("static"),
        QStringLiteral("get"), QStringLiteral("set")};
    // Statements that are not an expression: "step(), <statement>" is not possible.
    static const QSet<QString> statementWords{
        QStringLiteral("let"), QStringLiteral("const"), QStringLiteral("var"), QStringLiteral("if"),
        QStringLiteral("for"), QStringLiteral("while"), QStringLiteral("do"), QStringLiteral("return"),
        QStringLiteral("throw"), QStringLiteral("try"), QStringLiteral("switch"), QStringLiteral("break"),
        QStringLiteral("continue"), QStringLiteral("function"), QStringLiteral("class"), QStringLiteral("async"),
        QStringLiteral("debugger"), QStringLiteral("import"), QStringLiteral("export"), QStringLiteral("with")};
    static const QSet<QString> headWords{
        QStringLiteral("if"), QStringLiteral("for"), QStringLiteral("while"), QStringLiteral("switch"),
        QStringLiteral("catch"), QStringLiteral("with")};
    static const QSet<QString> blockWords{
        QStringLiteral("else"), QStringLiteral("try"), QStringLiteral("finally"), QStringLiteral("do"),
        QStringLiteral("catch")};

    const QString source = blankLiterals(code);
    const int n = int(source.size());

    struct Brace
    {
        bool block;     // statements inside, not an object literal or a class body
        bool isDo;      // the block of do { } while (...)
        int  parens;    // ( and [ open when it opened
    };
    QVector<Brace> braces;
    QVector<bool> parens;       // per open ( or [: the head of if (...), for (...), ...
    enum Kind { Nothing, Word, Number, Quote, Punctuation };
    QString prev;
    Kind prevKind = Nothing;
    bool prevHead = false;      // prev is the ")" of such a head
    bool prevDo = false;        // prev is the "}" of do { }
    QString lineFirst, lastLineFirst;
    bool lineStart = true;
    int line = 1;

    struct Insert
    {
        int     position;
        QString text;
    };
    QVector<Insert> inserts;
    // Reads every name the script declares. Where one is not in scope, or
    // not yet initialised, reading it throws.
    QString reader = QStringLiteral("function(){var __sh_r={};");
    const QStringList names = declaredNames(code);
    for (const QString& name : names)
        reader += QStringLiteral("try{__sh_r[\"%1\"]=%1}catch(__sh_x){}").arg(name);
    reader += QStringLiteral("return __sh_r}");
    auto stepCall = [&reader](int atLine) {
        return QStringLiteral("__sh_step(%1,").arg(atLine) + reader + QLatin1Char(')');
    };

    int i = 0;
    while (i < n) {
        const QChar c = source[i];
        if (c == QLatin1Char('\n')) {
            ++line;
            lineStart = true;
            ++i;
            continue;
        }
        if (c.isSpace()) {
            ++i;
            continue;
        }

        const int start = i;
        Kind kind = Punctuation;
        if (c.isLetter() || c == QLatin1Char('_') || c == QLatin1Char('$')) {
            while (i < n && isWordChar(source[i]))
                ++i;
            kind = Word;
        } else if (c.isDigit()) {
            while (i < n && (isWordChar(source[i]) || source[i] == QLatin1Char('.')))
                ++i;
            kind = Number;
        } else if (c == QLatin1Char('\'') || c == QLatin1Char('"') || c == QLatin1Char('`')) {
            ++i;
            kind = Quote;       // the contents are blank: the next token is the closing quote
        } else if (c == QLatin1Char('=') && i + 1 < n && source[i + 1] == QLatin1Char('>')) {
            i += 2;
        } else {
            ++i;
        }
        const QString token = source.mid(start, i - start);

        if (lineStart) {
            lineStart = false;
            lastLineFirst = lineFirst;
            lineFirst = token;
            const bool statementPosition = braces.isEmpty()
                ? parens.isEmpty()
                : braces.last().block && int(parens.size()) == braces.last().parens;
            if (statementPosition && kind == Word && !neverStarts.contains(token)
                && !(prevDo && token == QLatin1String("while"))) {
                // A value ended on an earlier line and a word follows: the
                // line break ended the statement.
                const bool valueEnded = (prevKind == Word && !notAValue.contains(prev))
                    || prevKind == Number || prevKind == Quote || prev == QLatin1String("]")
                    || (prev == QLatin1String(")") && !prevHead);
                const bool fresh = prevKind == Nothing || prev == QLatin1String(";")
                    || prev == QLatin1String("{") || prev == QLatin1String("}")
                    || (prev == QLatin1String(":") && (lastLineFirst == QLatin1String("case")
                                                       || lastLineFirst == QLatin1String("default")))
                    || valueEnded;
                // The single statement of "if (x)", "else", "for (...)" without
                // braces: a statement in front would take its place, so the
                // call is put into the expression.
                const bool body = (prev == QLatin1String(")") && prevHead)
                    || prev == QLatin1String("else") || prev == QLatin1String("do");
                if (fresh) {
                    inserts.append({start, stepCall(line) + QLatin1Char(';')});
                } else if (body && (token == QLatin1String("return") || token == QLatin1String("throw"))) {
                    int j = i;
                    while (j < n && source[j] != QLatin1Char('\n') && source[j].isSpace())
                        ++j;
                    if (j < n && source[j] != QLatin1Char('\n') && source[j] != QLatin1Char(';')
                        && source[j] != QLatin1Char('}'))
                        inserts.append({i, QLatin1Char(' ') + stepCall(line) + QLatin1Char(',')});
                } else if (body && !statementWords.contains(token)) {
                    inserts.append({start, stepCall(line) + QLatin1Char(',')});
                }
            }
        }

        prevHead = false;
        prevDo = false;
        if (token == QLatin1String("{")) {
            bool block = false;
            if (prevKind == Nothing || prev == QLatin1String(";") || prev == QLatin1String("{")
                || prev == QLatin1String("}") || prev == QLatin1String(")") || prev == QLatin1String("=>")
                || blockWords.contains(prev))
                block = true;
            else if (prev == QLatin1String(":"))
                block = braces.isEmpty() || braces.last().block;    // after a case label
            braces.append({block, prev == QLatin1String("do"), int(parens.size())});
        } else if (token == QLatin1String("}")) {
            if (!braces.isEmpty()) {
                prevDo = braces.last().isDo;
                if (parens.size() > braces.last().parens)
                    parens.resize(braces.last().parens);
                braces.removeLast();
            }
        } else if (token == QLatin1String("(")) {
            parens.append(prevKind == Word && headWords.contains(prev));
        } else if (token == QLatin1String("[")) {
            parens.append(false);
        } else if (token == QLatin1String(")") || token == QLatin1String("]")) {
            const bool head = !parens.isEmpty() && parens.takeLast();
            prevHead = head && token == QLatin1String(")");
        }
        prev = token;
        prevKind = kind;
    }

    QString out = code;
    for (int k = int(inserts.size()) - 1; k >= 0; --k)
        out.insert(inserts[k].position, inserts[k].text);
    return out;
}

QStringList declaredNames(const QString& code)
{
    static const QSet<QString> reserved{
        QStringLiteral("break"), QStringLiteral("case"), QStringLiteral("catch"), QStringLiteral("class"),
        QStringLiteral("const"), QStringLiteral("continue"), QStringLiteral("debugger"), QStringLiteral("default"),
        QStringLiteral("delete"), QStringLiteral("do"), QStringLiteral("else"), QStringLiteral("export"),
        QStringLiteral("extends"), QStringLiteral("finally"), QStringLiteral("for"), QStringLiteral("function"),
        QStringLiteral("if"), QStringLiteral("import"), QStringLiteral("in"), QStringLiteral("instanceof"),
        QStringLiteral("let"), QStringLiteral("new"), QStringLiteral("return"), QStringLiteral("super"),
        QStringLiteral("switch"), QStringLiteral("this"), QStringLiteral("throw"), QStringLiteral("try"),
        QStringLiteral("typeof"), QStringLiteral("var"), QStringLiteral("void"), QStringLiteral("while"),
        QStringLiteral("with"), QStringLiteral("yield"), QStringLiteral("await"), QStringLiteral("async"),
        QStringLiteral("of"), QStringLiteral("true"), QStringLiteral("false"), QStringLiteral("null"),
        QStringLiteral("undefined"), QStringLiteral("NaN"), QStringLiteral("Infinity"),
        QStringLiteral("arguments"), QStringLiteral("eval")};
    const int maxNames = 100;       // every statement carries a reader for all of them

    const QString source = blankLiterals(code);
    const int n = int(source.size());
    QStringList names{QStringLiteral("args")};
    auto add = [&names, maxNames](const QString& name) {
        if (!name.isEmpty() && !reserved.contains(name) && !name.startsWith(QLatin1String("__sh_"))
            && !names.contains(name) && names.size() < maxNames)
            names << name;
    };

    static const QRegularExpression word(QStringLiteral("[A-Za-z_$][\\w$]*"));
    // Every name of a parameter list or a destructuring pattern. In
    // `key: name` the name is bound, not the key.
    auto addPattern = [&add](const QString& text) {
        auto words = word.globalMatch(text);
        while (words.hasNext()) {
            const QRegularExpressionMatch match = words.next();
            int after = int(match.capturedEnd());
            while (after < text.size() && text[after].isSpace())
                ++after;
            if (after < text.size() && text[after] == QLatin1Char(':'))
                continue;
            add(match.captured());
        }
    };
    auto isOpen = [](QChar c) {
        return c == QLatin1Char('(') || c == QLatin1Char('[') || c == QLatin1Char('{');
    };
    auto isClose = [](QChar c) {
        return c == QLatin1Char(')') || c == QLatin1Char(']') || c == QLatin1Char('}');
    };
    // The position of the bracket that closes the one at `open`; -1 when none does.
    auto closing = [&](int open) {
        int depth = 0;
        for (int k = open; k < n; ++k) {
            if (isOpen(source[k]))
                ++depth;
            else if (isClose(source[k]) && --depth == 0)
                return k;
        }
        return -1;
    };

    // let / const / var: one declarator after the other.
    static const QRegularExpression declaration(QStringLiteral("(?<![\\w$.])(?:let|const|var)(?![\\w$])"));
    auto declarations = declaration.globalMatch(source);
    while (declarations.hasNext()) {
        int i = int(declarations.next().capturedEnd());
        for (;;) {
            while (i < n && source[i].isSpace())
                ++i;
            if (i >= n)
                break;
            if (source[i] == QLatin1Char('{') || source[i] == QLatin1Char('[')) {
                const int close = closing(i);
                if (close < 0)
                    break;
                addPattern(source.mid(i + 1, close - i - 1));
                i = close + 1;
            } else {
                const int start = i;
                while (i < n && isWordChar(source[i]))
                    ++i;
                if (i == start)
                    break;
                add(source.mid(start, i - start));
            }
            // Over the initialiser, to the comma that starts the next declarator.
            bool more = false;
            int depth = 0;
            for (; i < n; ++i) {
                const QChar c = source[i];
                if (isOpen(c)) {
                    ++depth;
                } else if (isClose(c)) {
                    if (depth == 0)
                        break;
                    --depth;
                } else if (depth == 0 && (c == QLatin1Char(';') || c == QLatin1Char('\n'))) {
                    break;
                } else if (depth == 0 && c == QLatin1Char(',')) {
                    more = true;
                    ++i;
                    break;
                }
            }
            if (!more)
                break;
        }
    }

    // Parameters of functions, and what catch binds.
    static const QRegularExpression parameters(
        QStringLiteral("(?<![\\w$.])(?:function\\s*\\*?\\s*(?:[A-Za-z_$][\\w$]*)?|catch)\\s*\\("));
    auto lists = parameters.globalMatch(source);
    while (lists.hasNext()) {
        const int open = int(lists.next().capturedEnd()) - 1;
        const int close = closing(open);
        if (close > open)
            addPattern(source.mid(open + 1, close - open - 1));
    }

    // Arrow functions: (a, b) => and a =>
    static const QRegularExpression arrow(QStringLiteral("=>"));
    auto arrows = arrow.globalMatch(source);
    while (arrows.hasNext()) {
        int k = int(arrows.next().capturedStart()) - 1;
        while (k >= 0 && source[k].isSpace())
            --k;
        if (k < 0)
            continue;
        if (source[k] == QLatin1Char(')')) {
            const int close = k;
            int depth = 0;
            for (; k >= 0; --k) {
                if (isClose(source[k]))
                    ++depth;
                else if (isOpen(source[k]) && --depth == 0)
                    break;
            }
            if (k >= 0)
                addPattern(source.mid(k + 1, close - k - 1));
        } else if (isWordChar(source[k])) {
            const int end = k + 1;
            while (k >= 0 && isWordChar(source[k]))
                --k;
            add(source.mid(k + 1, end - k - 1));
        }
    }
    return names;
}

} // namespace ScriptSource
} // namespace ScriptHost
