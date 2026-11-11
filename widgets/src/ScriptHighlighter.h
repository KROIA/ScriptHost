#pragma once
#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>

// Colours for script source, shared by the editor and the approval dialog.
// Private to the library.
namespace ScriptHost
{

// Keywords, strings, comments and numbers - enough to read a script, not a
// full JavaScript lexer (regular expression literals are not recognised).
class JsHighlighter : public QSyntaxHighlighter
{
public:
    explicit JsHighlighter(QTextDocument* document)
        : QSyntaxHighlighter(document)
        , m_keywords(QStringLiteral(
              "\\b(?:break|case|catch|class|const|continue|default|delete|do|else|false|finally|for|"
              "function|if|in|instanceof|let|new|null|of|return|switch|this|throw|true|try|typeof|"
              "undefined|var|void|while|args)\\b"))
        , m_numbers(QStringLiteral("\\b(?:0x[0-9a-fA-F]+|\\d+(?:\\.\\d+)?(?:[eE][+-]?\\d+)?)\\b"))
        , m_strings(QStringLiteral("\"(?:[^\"\\\\]|\\\\.)*\"?|'(?:[^'\\\\]|\\\\.)*'?|`(?:[^`\\\\]|\\\\.)*`?"))
    {
        m_keywordFormat.setForeground(QColor(0x00, 0x33, 0x99));
        m_keywordFormat.setFontWeight(QFont::Bold);
        m_numberFormat.setForeground(QColor(0x09, 0x86, 0x58));
        m_stringFormat.setForeground(QColor(0xa3, 0x15, 0x15));
        m_commentFormat.setForeground(QColor(0x6a, 0x99, 0x55));
    }

protected:
    void highlightBlock(const QString& text) override
    {
        auto paint = [this, &text](const QRegularExpression& expression, const QTextCharFormat& format) {
            QRegularExpressionMatchIterator it = expression.globalMatch(text);
            while (it.hasNext()) {
                const QRegularExpressionMatch match = it.next();
                setFormat(int(match.capturedStart()), int(match.capturedLength()), format);
            }
        };
        paint(m_keywords, m_keywordFormat);
        paint(m_numbers, m_numberFormat);

        // Strings and comments last, and in one left-to-right pass, so that a
        // quote inside a comment or "//" inside a string is taken right.
        int position = 0;
        setCurrentBlockState(0);
        if (previousBlockState() == 1) {
            const int end = int(text.indexOf(QLatin1String("*/")));
            if (end < 0) {
                setFormat(0, int(text.size()), m_commentFormat);
                setCurrentBlockState(1);
                return;
            }
            setFormat(0, end + 2, m_commentFormat);
            position = end + 2;
        }
        while (position < text.size()) {
            const QChar c = text.at(position);
            if (c == QLatin1Char('/') && position + 1 < text.size() && text.at(position + 1) == QLatin1Char('/')) {
                setFormat(position, int(text.size()) - position, m_commentFormat);
                return;
            }
            if (c == QLatin1Char('/') && position + 1 < text.size() && text.at(position + 1) == QLatin1Char('*')) {
                const int end = int(text.indexOf(QLatin1String("*/"), position + 2));
                if (end < 0) {
                    setFormat(position, int(text.size()) - position, m_commentFormat);
                    setCurrentBlockState(1);
                    return;
                }
                setFormat(position, end + 2 - position, m_commentFormat);
                position = end + 2;
                continue;
            }
            if (c == QLatin1Char('"') || c == QLatin1Char('\'') || c == QLatin1Char('`')) {
                // Every alternative may end unclosed, so the match starts at this quote.
                const QRegularExpressionMatch match = m_strings.match(text, position);
                const int length = match.hasMatch() ? int(match.capturedLength()) : 1;
                setFormat(position, length, m_stringFormat);
                position += qMax(length, 1);
                continue;
            }
            ++position;
        }
    }

private:
    QRegularExpression m_keywords, m_numbers, m_strings;
    QTextCharFormat    m_keywordFormat, m_numberFormat, m_stringFormat, m_commentFormat;
};

} // namespace ScriptHost
