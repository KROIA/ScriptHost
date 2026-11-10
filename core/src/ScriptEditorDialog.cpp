#include "ScriptEditorDialog.h"
#include "ScriptMethod.h"
#include "ScriptEngine.h"

#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSplitter>
#include <QSyntaxHighlighter>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextBlock>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <functional>
#include <map>

namespace ScriptHost
{

namespace
{

QString compactJson(const QJsonValue& value)
{
    // QJsonDocument takes only arrays and objects: wrap and unwrap the rest.
    const QString text = QString::fromUtf8(QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact));
    return text.mid(1, text.size() - 2);
}

QString prettyJson(const QJsonValue& value)
{
    if (value.isObject())
        return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Indented)).trimmed();
    if (value.isArray())
        return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Indented)).trimmed();
    return compactJson(value);
}

QString callText(const QString& method, const QJsonArray& arguments)
{
    QStringList parts;
    for (const QJsonValue& argument : arguments)
        parts << compactJson(argument);
    return QStringLiteral("%1(%2)").arg(method, parts.join(QStringLiteral(", ")));
}

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

} // namespace

// The code editor: fixed font, no wrapping, line numbers with breakpoints in
// a gutter, the debugger's line and an error line marked. Not exported - the
// dialog is the interface.
class ScriptCodeEdit : public QPlainTextEdit
{
public:
    explicit ScriptCodeEdit(QWidget* parent);

    int gutterWidth() const;
    void paintGutter(QPaintEvent* event);
    void gutterClicked(int y);

    void toggleBreakpoint(int line);
    QSet<int> breakpoints() const { return m_breakpoints; }
    void setCurrentLine(int line);
    void setErrorLine(int line);
    int errorLine() const { return m_errorLine; }

    std::function<void()> breakpointsChanged;

protected:
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void updateMarks();

    QWidget*  m_gutter = nullptr;
    QSet<int> m_breakpoints;
    int       m_currentLine = 0;
    int       m_errorLine = 0;
};

namespace
{

class ScriptGutter : public QWidget
{
public:
    explicit ScriptGutter(ScriptCodeEdit* editor)
        : QWidget(editor)
        , m_editor(editor)
    {
        setCursor(Qt::PointingHandCursor);
        setToolTip(ScriptEditorDialog::tr("Click to set or remove a breakpoint"));
    }
    QSize sizeHint() const override { return QSize(m_editor->gutterWidth(), 0); }

protected:
    void paintEvent(QPaintEvent* event) override { m_editor->paintGutter(event); }
    void mousePressEvent(QMouseEvent* event) override { m_editor->gutterClicked(event->pos().y()); }

private:
    ScriptCodeEdit* m_editor;
};

} // namespace

ScriptCodeEdit::ScriptCodeEdit(QWidget* parent)
    : QPlainTextEdit(parent)
{
    setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setTabStopDistance(4 * fontMetrics().horizontalAdvance(QLatin1Char(' ')));
    m_gutter = new ScriptGutter(this);
    new JsHighlighter(document());

    connect(this, &QPlainTextEdit::blockCountChanged, this, [this]() {
        setViewportMargins(gutterWidth(), 0, 0, 0);
    });
    connect(this, &QPlainTextEdit::updateRequest, this, [this](const QRect& rect, int dy) {
        if (dy)
            m_gutter->scroll(0, dy);
        else
            m_gutter->update(0, rect.y(), m_gutter->width(), rect.height());
    });
    setViewportMargins(gutterWidth(), 0, 0, 0);
}

int ScriptCodeEdit::gutterWidth() const
{
    const int digits = QString::number(qMax(1, blockCount())).size();
    // Room for the breakpoint dot, then the numbers.
    return 16 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * qMax(2, digits) + 6;
}

void ScriptCodeEdit::paintGutter(QPaintEvent* event)
{
    QPainter painter(m_gutter);
    painter.fillRect(event->rect(), QColor(0xf0, 0xf0, 0xf0));
    painter.setRenderHint(QPainter::Antialiasing);

    QTextBlock block = firstVisibleBlock();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    const int lineHeight = fontMetrics().height();
    while (block.isValid() && top <= event->rect().bottom()) {
        const int bottom = top + qRound(blockBoundingRect(block).height());
        const int line = block.blockNumber() + 1;
        if (block.isVisible() && bottom >= event->rect().top()) {
            if (m_breakpoints.contains(line)) {
                const int size = qMin(lineHeight - 4, 10);
                painter.setBrush(QColor(0xd0, 0x21, 0x21));
                painter.setPen(Qt::NoPen);
                painter.drawEllipse(3, top + (lineHeight - size) / 2, size, size);
            }
            painter.setPen(line == m_currentLine ? QColor(Qt::black) : QColor(0x88, 0x88, 0x88));
            painter.drawText(0, top, m_gutter->width() - 4, lineHeight, Qt::AlignRight, QString::number(line));
        }
        block = block.next();
        top = bottom;
    }
}

void ScriptCodeEdit::gutterClicked(int y)
{
    QTextBlock block = firstVisibleBlock();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    while (block.isValid()) {
        const int bottom = top + qRound(blockBoundingRect(block).height());
        if (y >= top && y < bottom) {
            toggleBreakpoint(block.blockNumber() + 1);
            return;
        }
        block = block.next();
        top = bottom;
    }
}

void ScriptCodeEdit::toggleBreakpoint(int line)
{
    // ponytail: breakpoints stay on their line numbers when lines are inserted
    // above them; follow the text (QTextBlock user data) if that bothers anyone.
    if (line < 1)
        return;
    if (!m_breakpoints.remove(line))
        m_breakpoints.insert(line);
    m_gutter->update();
    if (breakpointsChanged)
        breakpointsChanged();
}

void ScriptCodeEdit::setCurrentLine(int line)
{
    m_currentLine = line;
    updateMarks();
    if (line > 0) {
        const QTextBlock block = document()->findBlockByNumber(line - 1);
        if (block.isValid()) {
            setTextCursor(QTextCursor(block));
            ensureCursorVisible();
        }
    }
    m_gutter->update();
}

void ScriptCodeEdit::setErrorLine(int line)
{
    m_errorLine = line;
    updateMarks();
}

void ScriptCodeEdit::updateMarks()
{
    QList<QTextEdit::ExtraSelection> marks;
    auto mark = [this, &marks](int line, const QColor& color) {
        const QTextBlock block = document()->findBlockByNumber(line - 1);
        if (line < 1 || !block.isValid())
            return;
        QTextEdit::ExtraSelection selection;
        selection.format.setBackground(color);
        selection.format.setProperty(QTextFormat::FullWidthSelection, true);
        selection.cursor = QTextCursor(block);
        marks.append(selection);
    };
    mark(m_errorLine, QColor(0xff, 0xd6, 0xd6));
    mark(m_currentLine, QColor(0xff, 0xf1, 0x8c));    // over the error line: where the debugger is matters more
    setExtraSelections(marks);
}

void ScriptCodeEdit::resizeEvent(QResizeEvent* event)
{
    QPlainTextEdit::resizeEvent(event);
    const QRect area = contentsRect();
    m_gutter->setGeometry(QRect(area.left(), area.top(), gutterWidth(), area.height()));
}

void ScriptCodeEdit::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Tab && event->modifiers() == Qt::NoModifier && !isReadOnly()) {
        insertPlainText(QStringLiteral("    "));
        return;
    }
    QPlainTextEdit::keyPressEvent(event);
}

// ------------------------------------------------------------ dialog

ScriptEditorDialog::ScriptEditorDialog(ScriptEngine* scripting, QWidget* parent)
    : QDialog(parent)
    , m_scripting(scripting)
{
    setObjectName(QStringLiteral("scripthost_scriptEditorDialog"));
    setWindowTitle(tr("Script editor"));
    setWindowFlags(windowFlags() | Qt::WindowMaximizeButtonHint | Qt::WindowMinimizeButtonHint);
    setSizeGripEnabled(true);
    resize(1000, 700);
    const QFont fixed = QFontDatabase::systemFont(QFontDatabase::FixedFont);

    auto* layout = new QVBoxLayout(this);

    // ---- name, description, parameters
    auto* form = new QFormLayout();
    m_name = new QLineEdit(this);
    m_name->setObjectName(QStringLiteral("scripthost_scriptEditorName"));
    form->addRow(tr("Name:"), m_name);
    m_description = new QLineEdit(this);
    m_description->setObjectName(QStringLiteral("scripthost_scriptEditorDescription"));
    form->addRow(tr("Description:"), m_description);

    m_parameters = new QTableWidget(0, 3, this);
    m_parameters->setObjectName(QStringLiteral("scripthost_scriptEditorParameters"));
    m_parameters->setHorizontalHeaderLabels({tr("Name"), tr("Type"), tr("Description")});
    m_parameters->verticalHeader()->setVisible(false);
    m_parameters->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_parameters->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_parameters->setMaximumHeight(100);
    m_parameters->setToolTip(tr("What the script expects in `args`. Types: string, number, boolean, array, object."));
    m_parameterAdd = new QPushButton(tr("+"), this);
    m_parameterAdd->setObjectName(QStringLiteral("scripthost_scriptEditorParameterAdd"));
    m_parameterRemove = new QPushButton(tr("-"), this);
    m_parameterRemove->setObjectName(QStringLiteral("scripthost_scriptEditorParameterRemove"));
    auto* parameterButtons = new QVBoxLayout();
    parameterButtons->addWidget(m_parameterAdd);
    parameterButtons->addWidget(m_parameterRemove);
    parameterButtons->addStretch();
    auto* parameterRow = new QHBoxLayout();
    parameterRow->addWidget(m_parameters, 1);
    parameterRow->addLayout(parameterButtons);
    form->addRow(tr("Parameters:"), parameterRow);
    layout->addLayout(form);

    connect(m_parameterAdd, &QPushButton::clicked, this, [this]() {
        const int row = m_parameters->rowCount();
        m_parameters->insertRow(row);
        m_parameters->setItem(row, 1, new QTableWidgetItem(QStringLiteral("string")));
        m_parameters->editItem(m_parameters->item(row, 1));
    });
    connect(m_parameterRemove, &QPushButton::clicked, this, [this]() {
        if (m_parameters->currentRow() >= 0)
            m_parameters->removeRow(m_parameters->currentRow());
    });

    // ---- code and output | reference
    auto* columns = new QSplitter(Qt::Horizontal, this);
    auto* rows = new QSplitter(Qt::Vertical, columns);

    m_code = new ScriptCodeEdit(rows);
    m_code->setObjectName(QStringLiteral("scripthost_scriptEditorCode"));
    m_code->breakpointsChanged = [this]() {
        if (m_ownRun && m_scripting)
            m_scripting->setBreakpoints(m_code->breakpoints());
    };

    auto* lower = new QWidget(rows);
    auto* lowerLayout = new QVBoxLayout(lower);
    lowerLayout->setContentsMargins(0, 0, 0, 0);
    auto* argsRow = new QHBoxLayout();
    argsRow->addWidget(new QLabel(tr("Arguments (JSON):"), lower));
    m_args = new QLineEdit(lower);
    m_args->setObjectName(QStringLiteral("scripthost_scriptEditorArgs"));
    m_args->setFont(fixed);
    m_args->setPlaceholderText(QStringLiteral("{\"days\": 30}"));
    argsRow->addWidget(m_args, 1);
    lowerLayout->addLayout(argsRow);

    m_status = new QLabel(lower);
    m_status->setObjectName(QStringLiteral("scripthost_scriptEditorStatus"));
    m_status->setWordWrap(true);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    lowerLayout->addWidget(m_status);

    m_outputTabs = new QTabWidget(lower);
    m_outputTabs->setObjectName(QStringLiteral("scripthost_scriptEditorOutputTabs"));
    m_output = new QPlainTextEdit(m_outputTabs);
    m_output->setObjectName(QStringLiteral("scripthost_scriptEditorOutput"));
    m_output->setReadOnly(true);
    m_output->setFont(fixed);
    m_output->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_outputTabs->addTab(m_output, tr("Output"));
    m_calls = new QTableWidget(0, 3, m_outputTabs);
    m_calls->setObjectName(QStringLiteral("scripthost_scriptEditorCalls"));
    m_calls->setHorizontalHeaderLabels({tr("Line"), tr("Call"), tr("Returned")});
    m_calls->verticalHeader()->setVisible(false);
    m_calls->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_calls->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_calls->setWordWrap(false);
    m_calls->setFont(fixed);
    m_calls->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_calls->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Interactive);
    m_calls->horizontalHeader()->setStretchLastSection(true);
    m_calls->setColumnWidth(1, 320);
    m_outputTabs->addTab(m_calls, tr("Calls"));
    lowerLayout->addWidget(m_outputTabs, 1);

    rows->addWidget(m_code);
    rows->addWidget(lower);
    rows->setStretchFactor(0, 3);
    rows->setStretchFactor(1, 2);

    auto* referencePane = new QWidget(columns);
    auto* referenceLayout = new QVBoxLayout(referencePane);
    referenceLayout->setContentsMargins(0, 0, 0, 0);
    referenceLayout->addWidget(new QLabel(tr("Methods a script can call:"), referencePane));
    m_search = new QLineEdit(referencePane);
    m_search->setObjectName(QStringLiteral("scripthost_scriptEditorReferenceSearch"));
    m_search->setPlaceholderText(tr("Search"));
    m_search->setClearButtonEnabled(true);
    referenceLayout->addWidget(m_search);
    auto* referenceSplit = new QSplitter(Qt::Vertical, referencePane);
    m_reference = new QTreeWidget(referenceSplit);
    m_reference->setObjectName(QStringLiteral("scripthost_scriptEditorReference"));
    m_reference->setHeaderHidden(true);
    m_reference->setToolTip(tr("Double click inserts a call at the cursor."));
    m_referenceText = new QPlainTextEdit(referenceSplit);
    m_referenceText->setObjectName(QStringLiteral("scripthost_scriptEditorReferenceText"));
    m_referenceText->setReadOnly(true);
    m_referenceText->setFont(fixed);
    m_referenceText->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    referenceSplit->addWidget(m_reference);
    referenceSplit->addWidget(m_referenceText);
    referenceLayout->addWidget(referenceSplit, 1);

    columns->addWidget(rows);
    columns->addWidget(referencePane);
    columns->setStretchFactor(0, 3);
    columns->setStretchFactor(1, 1);
    layout->addWidget(columns, 1);

    connect(m_search, &QLineEdit::textChanged, this, &ScriptEditorDialog::fillReference);
    connect(m_reference, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* current) {
        m_referenceText->setPlainText(current ? current->data(0, Qt::UserRole + 1).toString() : QString());
    });
    connect(m_reference, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item) {
        const QString snippet = item->data(0, Qt::UserRole + 2).toString();
        if (snippet.isEmpty() || m_code->isReadOnly())
            return;
        m_code->insertPlainText(snippet);
        m_code->setFocus();
    });

    // ---- buttons
    auto* buttons = new QHBoxLayout();
    auto button = [this, buttons](const QString& text, const char* name, const QString& tip) {
        auto* b = new QPushButton(text, this);
        b->setObjectName(QString::fromLatin1(name));
        b->setToolTip(tip);
        b->setAutoDefault(false);
        buttons->addWidget(b);
        return b;
    };
    m_check    = button(tr("Check"), "scripthost_scriptEditorCheck", tr("Does it compile, and does it only call methods there are?"));
    m_run      = button(tr("Run"), "scripthost_scriptEditorRun", tr("Runs the script for real."));
    m_dryRun   = button(tr("Dry run"), "scripthost_scriptEditorDryRun",
                        tr("Methods that change something are recorded, not called."));
    m_debug    = button(tr("Debug"), "scripthost_scriptEditorDebug",
                        tr("Runs the script and stops before its first call and at breakpoints."));
    m_step     = button(tr("Step"), "scripthost_scriptEditorStep", tr("Makes the call and stops before the next one."));
    m_continue = button(tr("Continue"), "scripthost_scriptEditorContinue", tr("Runs on to the next breakpoint."));
    m_stop     = button(tr("Stop"), "scripthost_scriptEditorStop", tr("Ends the run."));
    buttons->addStretch();
    m_save = button(tr("Save"), "scripthost_scriptEditorSave", tr("Saves the script into the library."));
    QPushButton* closeButton = button(tr("Close"), "scripthost_scriptEditorClose", QString());
    layout->addLayout(buttons);

    connect(m_check, &QPushButton::clicked, this, &ScriptEditorDialog::checkScript);
    connect(m_run, &QPushButton::clicked, this, [this]() { runScript(false); });
    connect(m_dryRun, &QPushButton::clicked, this, [this]() { runScript(true); });
    connect(m_debug, &QPushButton::clicked, this, &ScriptEditorDialog::debugScript);
    connect(m_step, &QPushButton::clicked, this, &ScriptEditorDialog::step);
    connect(m_continue, &QPushButton::clicked, this, &ScriptEditorDialog::continueRun);
    connect(m_stop, &QPushButton::clicked, this, &ScriptEditorDialog::stopRun);
    connect(m_save, &QPushButton::clicked, this, [this]() { save(true); });
    connect(closeButton, &QPushButton::clicked, this, &QDialog::reject);

    if (m_scripting) {
        connect(m_scripting, &ScriptEngine::runPaused, this, &ScriptEditorDialog::onPaused);
        connect(m_scripting, &ScriptEngine::runCallFinished, this, &ScriptEditorDialog::onCallFinished);
        connect(m_scripting, &ScriptEngine::runLog, this, [this](const QString& text) {
            if (m_ownRun)
                m_output->appendPlainText(text);
        });
        connect(m_scripting, &ScriptEngine::runStarted, this, &ScriptEditorDialog::updateButtons);
        connect(m_scripting, &ScriptEngine::runFinished, this, &ScriptEditorDialog::updateButtons);
        connect(m_scripting, &ScriptEngine::settingsChanged, this, &ScriptEditorDialog::updateButtons);
        connect(m_scripting, &ScriptEngine::methodsChanged, this, &ScriptEditorDialog::fillReference);
    }
    fillReference();
    updateButtons();
}

ScriptEditorDialog::~ScriptEditorDialog()
{
    if (m_ownRun && m_scripting)
        m_scripting->stop();
}

void ScriptEditorDialog::setScript(const ScriptLibrary::Script& script)
{
    m_name->setText(script.name);
    m_description->setText(script.description);
    m_parameters->setRowCount(0);
    for (const ScriptLibrary::Parameter& parameter : script.parameters) {
        const int row = m_parameters->rowCount();
        m_parameters->insertRow(row);
        m_parameters->setItem(row, 0, new QTableWidgetItem(parameter.name));
        m_parameters->setItem(row, 1, new QTableWidgetItem(parameter.type));
        m_parameters->setItem(row, 2, new QTableWidgetItem(parameter.description));
    }
    m_code->setPlainText(script.code);
    m_code->setErrorLine(0);
    m_origin = script.origin;
    m_filePath = script.filePath;
    // Only a script that came from the library has a name to be renamed from.
    m_savedName = script.filePath.isEmpty() ? QString() : script.name;
    setWindowTitle(script.name.isEmpty() ? tr("New script") : tr("Script: %1").arg(script.name));
    showStatus(QString(), false);
}

ScriptLibrary::Script ScriptEditorDialog::script() const
{
    ScriptLibrary::Script result;
    result.name = m_name->text().trimmed();
    result.description = m_description->text().trimmed();
    auto cell = [this](int row, int column) {
        const QTableWidgetItem* item = m_parameters->item(row, column);
        return item ? item->text().trimmed() : QString();
    };
    for (int row = 0; row < m_parameters->rowCount(); ++row) {
        if (!cell(row, 0).isEmpty())
            result.parameters.append({cell(row, 0), cell(row, 1), cell(row, 2)});
    }
    result.code = m_code->toPlainText();
    result.origin = m_origin;
    result.filePath = m_filePath;
    result.readOnly = m_readOnly;
    return result;
}

void ScriptEditorDialog::setReadOnly(bool readOnly)
{
    m_readOnly = readOnly;
    m_name->setReadOnly(readOnly);
    m_description->setReadOnly(readOnly);
    m_parameters->setEditTriggers(readOnly ? QAbstractItemView::NoEditTriggers
                                           : QAbstractItemView::AllEditTriggers);
    m_parameterAdd->setVisible(!readOnly);
    m_parameterRemove->setVisible(!readOnly);
    m_code->setReadOnly(readOnly);
    updateButtons();
}

QSet<int> ScriptEditorDialog::breakpoints() const
{
    return m_code->breakpoints();
}

void ScriptEditorDialog::toggleBreakpoint(int line)
{
    m_code->toggleBreakpoint(line);
}

int ScriptEditorDialog::errorLine() const
{
    return m_code->errorLine();
}

void ScriptEditorDialog::showStatus(const QString& text, bool error)
{
    m_status->setText(text);
    m_status->setStyleSheet(error ? QStringLiteral("color: #c0392b;") : QStringLiteral("color: #1e8449;"));
}

bool ScriptEditorDialog::checkScript()
{
    if (!m_scripting)
        return false;
    const ScriptEngine::Check check = m_scripting->check(m_code->toPlainText());
    if (check.ok) {
        m_code->setErrorLine(0);
        showStatus(check.methods.isEmpty()
                       ? QString::fromUtf8("OK – calls no methods")
                       : QString::fromUtf8("OK – uses: %1").arg(check.methods.join(QStringLiteral(", "))),
                   false);
        return true;
    }
    QString text = check.line > 0 ? tr("%1 error in line %2: %3").arg(check.phase, QString::number(check.line), check.message)
                                   : tr("%1 error: %2").arg(check.phase, check.message);
    if (!check.didYouMean.isEmpty())
        text += tr(" - did you mean %1?").arg(check.didYouMean);
    m_code->setErrorLine(check.line);
    showStatus(text, true);
    return false;
}

bool ScriptEditorDialog::parseArgs(QJsonObject& args)
{
    const QString text = m_args->text().trimmed();
    if (text.isEmpty()) {
        args = QJsonObject();
        return true;
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        showStatus(error.error != QJsonParseError::NoError
                       ? tr("Arguments are not valid JSON: %1 (at character %2)").arg(error.errorString()).arg(error.offset)
                       : tr("Arguments must be a JSON object, like {\"days\": 30}."),
                   true);
        return false;
    }
    args = document.object();
    return true;
}

bool ScriptEditorDialog::runScript(bool dryRun)
{
    return startRun(dryRun, false);
}

bool ScriptEditorDialog::debugScript()
{
    if (!m_scripting || !m_scripting->isDebuggerEnabled())
        return false;
    return startRun(false, true);
}

bool ScriptEditorDialog::startRun(bool dryRun, bool debug)
{
    if (!m_scripting || m_scripting->isRunning())
        return false;
    ScriptEngine::RunOptions options;
    if (!parseArgs(options.args))
        return false;
    options.name = m_name->text().trimmed();
    options.dryRun = dryRun;
    options.debug = debug;
    options.stepFromStart = debug;
    options.breakpoints = m_code->breakpoints();

    m_output->clear();
    m_calls->setRowCount(0);
    m_pendingRow = -1;
    m_paused = false;
    m_pausedLine = 0;
    m_code->setCurrentLine(0);
    m_code->setErrorLine(0);
    m_outputTabs->setCurrentWidget(debug ? static_cast<QWidget*>(m_calls) : m_output);
    showStatus(debug ? tr("Debugging...") : dryRun ? tr("Dry run...") : tr("Running..."), false);

    m_ownRun = true;
    // The callback may come after the window is gone (closing stops the run,
    // the result arrives later).
    QPointer<ScriptEditorDialog> self(this);
    const bool started = m_scripting->run(m_code->toPlainText(), options, [self](const QJsonObject& result) {
        if (self)
            self->onOwnRunFinished(result);
    });
    updateButtons();
    return started;
}

void ScriptEditorDialog::onPaused(int line, const QString& method, const QJsonArray& arguments)
{
    if (!m_ownRun)
        return;
    settlePendingCall();
    m_paused = true;
    m_pausedLine = line;
    m_pendingRow = m_calls->rowCount();
    m_calls->insertRow(m_pendingRow);
    m_calls->setItem(m_pendingRow, 0, new QTableWidgetItem(QString::number(line)));
    auto* call = new QTableWidgetItem(QString::fromUtf8("→ about to call %1").arg(callText(method, arguments)));
    call->setData(Qt::UserRole, callText(method, arguments));
    m_calls->setItem(m_pendingRow, 1, call);
    m_calls->setItem(m_pendingRow, 2, new QTableWidgetItem());
    m_calls->scrollToItem(call);
    m_code->setCurrentLine(line);
    showStatus(tr("Paused in line %1 before %2").arg(QString::number(line), method), false);
    updateButtons();
}

void ScriptEditorDialog::settlePendingCall()
{
    // The arrow goes once the call is no longer about to happen.
    if (m_pendingRow < 0 || m_pendingRow >= m_calls->rowCount())
        return;
    QTableWidgetItem* call = m_calls->item(m_pendingRow, 1);
    call->setText(call->data(Qt::UserRole).toString());
}

void ScriptEditorDialog::onCallFinished(int line, const QString& method, const QJsonArray& arguments,
                                        const QJsonValue& value, const QString& error)
{
    if (!m_ownRun)
        return;
    const QString text = callText(method, arguments);
    int row = -1;
    if (m_pendingRow >= 0 && m_pendingRow < m_calls->rowCount()
        && m_calls->item(m_pendingRow, 1)->data(Qt::UserRole).toString() == text)
        row = m_pendingRow;
    else
        settlePendingCall();
    m_pendingRow = -1;
    if (row < 0) {
        row = m_calls->rowCount();
        m_calls->insertRow(row);
        m_calls->setItem(row, 0, new QTableWidgetItem(QString::number(line)));
        m_calls->setItem(row, 1, new QTableWidgetItem());
        m_calls->setItem(row, 2, new QTableWidgetItem());
    }
    m_calls->item(row, 1)->setText(text);
    QTableWidgetItem* returned = m_calls->item(row, 2);
    if (error.isEmpty()) {
        returned->setText(compactJson(value).left(300));
        returned->setToolTip(prettyJson(value).left(4000));
    } else {
        returned->setText(tr("error: %1").arg(error));
        returned->setForeground(QColor(0xc0, 0x39, 0x2b));
    }
    m_calls->scrollToItem(returned);
}

void ScriptEditorDialog::step()
{
    if (!m_ownRun || !m_paused || !m_scripting)
        return;
    m_paused = false;
    m_pausedLine = 0;
    settlePendingCall();
    m_code->setCurrentLine(0);
    m_scripting->stepRun();
    updateButtons();
}

void ScriptEditorDialog::continueRun()
{
    if (!m_ownRun || !m_paused || !m_scripting)
        return;
    m_paused = false;
    m_pausedLine = 0;
    settlePendingCall();
    m_code->setCurrentLine(0);
    m_scripting->continueRun();
    updateButtons();
}

void ScriptEditorDialog::stopRun()
{
    if (m_ownRun && m_scripting)
        m_scripting->stop();
}

void ScriptEditorDialog::onOwnRunFinished(const QJsonObject& result)
{
    m_ownRun = false;
    m_paused = false;
    m_pausedLine = 0;
    settlePendingCall();
    m_pendingRow = -1;
    m_code->setCurrentLine(0);

    // The log lines are already in the output, as they came.
    QJsonObject shown = result;
    shown.remove(QStringLiteral("log"));
    m_output->appendPlainText(prettyJson(shown));

    if (result.value(QStringLiteral("status")).toString() == QLatin1String("ok")) {
        showStatus(tr("Finished in %1 ms").arg(result.value(QStringLiteral("duration_ms")).toInt()), false);
    } else {
        const int line = result.value(QStringLiteral("line")).toInt();
        m_code->setErrorLine(line);
        const QString phase = result.value(QStringLiteral("phase")).toString();
        const QString message = result.value(QStringLiteral("message")).toString();
        showStatus(line > 0 ? tr("%1 error in line %2: %3").arg(phase, QString::number(line), message)
                            : tr("%1 error: %2").arg(phase, message),
                   true);
        m_outputTabs->setCurrentWidget(m_output);
    }
    updateButtons();
}

void ScriptEditorDialog::updateButtons()
{
    const bool available = m_scripting != nullptr;
    const bool running = available && m_scripting->isRunning();
    const bool debugger = available && m_scripting->isDebuggerEnabled();
    m_check->setEnabled(available);
    m_run->setEnabled(available && !running);
    m_dryRun->setEnabled(available && !running);
    m_debug->setEnabled(available && !running);
    m_debug->setVisible(debugger);
    m_step->setVisible(debugger);
    m_continue->setVisible(debugger);
    m_step->setEnabled(m_ownRun && m_paused);
    m_continue->setEnabled(m_ownRun && m_paused);
    m_stop->setEnabled(m_ownRun && running);
    m_save->setEnabled(!m_readOnly && m_scripting && m_scripting->userMayEditScripts() && m_scripting->library());
}

void ScriptEditorDialog::fillReference()
{
    const QString current = m_reference->currentItem() ? m_reference->currentItem()->text(0) : QString();
    m_reference->clear();
    m_referenceText->clear();
    if (!m_scripting)
        return;

    const QString query = m_search->text().trimmed();
    std::map<QString, QList<ScriptMethod>> spaces;
    for (const ScriptMethod& method : m_scripting->methods()) {
        if (!m_scripting->isMethodEnabled(method.name()))
            continue;
        if (!query.isEmpty() && !method.name().contains(query, Qt::CaseInsensitive)
            && !method.description().contains(query, Qt::CaseInsensitive))
            continue;
        spaces[method.nameSpace()].append(method);
    }

    for (const auto& space : spaces) {
        auto* top = new QTreeWidgetItem(m_reference, {space.first.isEmpty() ? tr("(global functions)") : space.first});
        for (const ScriptMethod& method : space.second) {
            auto* item = new QTreeWidgetItem(top, {method.name()});
            item->setToolTip(0, method.signature());
            item->setData(0, Qt::UserRole + 1, method.declaration());
            QString snippet;
            if (method.takesObject()) {
                QStringList fields;
                for (const QString& parameter : method.parameterNames())
                    fields << parameter + QStringLiteral(": ");
                snippet = QStringLiteral("%1({%2})").arg(method.name(), fields.join(QStringLiteral(", ")));
            } else {
                snippet = QStringLiteral("%1(%2)").arg(method.name(), method.parameterNames().join(QStringLiteral(", ")));
            }
            item->setData(0, Qt::UserRole + 2, snippet);
            if (item->text(0) == current)
                m_reference->setCurrentItem(item);
        }
    }
    m_reference->expandAll();
}

void ScriptEditorDialog::done(int result)
{
    // Covers Close, Esc and the window's close button alike.
    stopRun();
    QDialog::done(result);
}

bool ScriptEditorDialog::save(bool askOnCheckFailure)
{
    ScriptLibrary* library = m_scripting ? m_scripting->library() : nullptr;
    if (!library || m_readOnly || !m_scripting->userMayEditScripts())
        return false;
    if (m_name->text().trimmed().isEmpty()) {
        showStatus(tr("Give the script a name before saving it."), true);
        m_name->setFocus();
        return false;
    }
    if (!checkScript() && askOnCheckFailure) {
        const auto answer = QMessageBox::question(this, tr("Save script"),
            tr("The script does not pass the check:\n%1\n\nSave it anyway?").arg(m_status->text()));
        if (answer != QMessageBox::Yes)
            return false;
    }

    ScriptLibrary::Script toSave = script();
    if (toSave.origin.isEmpty())
        toSave.origin = QStringLiteral("user");
    toSave.readOnly = false;
    QString error;
    if (!library->save(toSave, &error)) {
        showStatus(tr("Not saved: %1").arg(error), true);
        return false;
    }
    // Renamed: the file under the old name would otherwise stay as a second script.
    if (!m_savedName.isEmpty() && m_savedName.compare(toSave.name, Qt::CaseInsensitive) != 0)
        library->remove(m_savedName);
    m_savedName = toSave.name;
    m_origin = toSave.origin;
    m_filePath = library->script(toSave.name).filePath;
    setWindowTitle(tr("Script: %1").arg(toSave.name));
    if (m_code->errorLine() == 0)
        showStatus(tr("Saved."), false);
    emit saved(toSave.name);
    return true;
}

} // namespace ScriptHost
