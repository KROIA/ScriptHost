#include "ScriptApprovalDialog.h"
#include "ScriptEngine.h"
#include "ScriptHighlighter.h"
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QVBoxLayout>

namespace ScriptHost
{

namespace
{

using Engine = ScriptEngine;

void askUser(Engine* engine, QWidget* parent, const Engine::ApprovalRequest& request,
             const std::function<void(const Engine::ApprovalDecision&)>& decide)
{
    QDialog dialog(parent);
    dialog.setObjectName(QStringLiteral("scripthost_scriptApprovalDialog"));
    dialog.setWindowTitle(QStringLiteral("Run script?"));
    dialog.resize(640, 520);
    auto* layout = new QVBoxLayout(&dialog);

    QString headline = request.name.isEmpty()
        ? QStringLiteral("Requested by %1: run this script?").arg(engine->requesterName())
        : QStringLiteral("Requested by %1: run the saved script \"%2\"?").arg(engine->requesterName(), request.name);
    if (request.dryRun)
        headline += QStringLiteral(" It is a dry run: nothing will be changed.");
    auto* headlineLabel = new QLabel(headline, &dialog);
    headlineLabel->setObjectName(QStringLiteral("scripthost_scriptApprovalHeadline"));
    headlineLabel->setWordWrap(true);
    layout->addWidget(headlineLabel);

    auto* code = new QPlainTextEdit(&dialog);
    code->setObjectName(QStringLiteral("scripthost_scriptApprovalCode"));
    code->setReadOnly(true);
    code->setLineWrapMode(QPlainTextEdit::NoWrap);
    code->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    new JsHighlighter(code->document());
    code->setPlainText(request.code);
    layout->addWidget(code, 1);

    QStringList uses;
    for (const QString& method : request.methods) {
        uses << (request.changingMethods.contains(method) ? method + QStringLiteral(" (changes)")
                                                          : method + QStringLiteral(" (reads)"));
    }
    QString usesText = uses.isEmpty() ? QStringLiteral("It calls no methods of the application.")
                                      : QStringLiteral("It uses: ") + uses.join(QStringLiteral(", "));
    if (request.dynamic)
        usesText += QStringLiteral("\nIt picks methods by computed names, so it may call others as well.");
    if (!request.args.isEmpty())
        usesText += QStringLiteral("\nArguments: ")
                    + QString::fromUtf8(QJsonDocument(request.args).toJson(QJsonDocument::Compact));
    auto* usesLabel = new QLabel(usesText, &dialog);
    usesLabel->setObjectName(QStringLiteral("scripthost_scriptApprovalUses"));
    usesLabel->setWordWrap(true);
    layout->addWidget(usesLabel);

    auto* output = new QPlainTextEdit(&dialog);
    output->setObjectName(QStringLiteral("scripthost_scriptApprovalOutput"));
    output->setReadOnly(true);
    output->setMaximumHeight(110);
    output->hide();
    layout->addWidget(output);

    auto* remember = new QCheckBox(QStringLiteral("Do not ask again for this script in this session"), &dialog);
    remember->setObjectName(QStringLiteral("scripthost_scriptApprovalRemember"));
    layout->addWidget(remember);

    auto* reason = new QLineEdit(&dialog);
    reason->setObjectName(QStringLiteral("scripthost_scriptApprovalReason"));
    reason->setPlaceholderText(QStringLiteral("When declining: the reason to pass on (optional)"));
    layout->addWidget(reason);

    auto* buttons = new QDialogButtonBox(&dialog);
    QPushButton* runButton = buttons->addButton(QStringLiteral("Run"), QDialogButtonBox::AcceptRole);
    runButton->setObjectName(QStringLiteral("scripthost_scriptApprovalRun"));
    QPushButton* declineButton = buttons->addButton(QStringLiteral("Decline"), QDialogButtonBox::RejectRole);
    declineButton->setObjectName(QStringLiteral("scripthost_scriptApprovalDecline"));
    QPushButton* testButton = buttons->addButton(QStringLiteral("Test run"), QDialogButtonBox::ActionRole);
    testButton->setObjectName(QStringLiteral("scripthost_scriptApprovalTest"));
    testButton->setToolTip(QStringLiteral("Runs the script without changing anything and shows what it would do"));
    testButton->setVisible(!request.dryRun);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    Engine::RunId testRun = 0;
    QObject::connect(testButton, &QPushButton::clicked, &dialog,
                     [engine, request, output, testButton, runButton, &testRun]() {
        Engine::RunOptions options;
        options.name = request.name;
        options.args = request.args;
        options.dryRun = true;
        testButton->setEnabled(false);
        runButton->setEnabled(false);
        QPointer<QPlainTextEdit> out(output);
        QPointer<QPushButton> test(testButton), runAgain(runButton);
        testRun = engine->run(request.code, options, [out, test, runAgain](const QJsonObject& result) {
            if (!out)
                return;
            out->setPlainText(QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Indented)));
            out->show();
            test->setEnabled(true);
            runAgain->setEnabled(true);
        });
    });

    Engine::ApprovalDecision decision;
    decision.approved = dialog.exec() == QDialog::Accepted;
    decision.reason = reason->text().trimmed();
    decision.rememberForSession = decision.approved && remember->isChecked();
    // A test run still going when the dialog closes must not block the real
    // one: it is stopped, and the decision takes effect once it has ended.
    if (engine->isRunning(testRun)) {
        engine->stop(testRun);
        auto* once = new QObject(engine);
        QObject::connect(engine, &Engine::runFinished, once, [once, testRun, decide, decision](Engine::RunId id) {
            if (id != testRun)
                return;
            once->deleteLater();
            decide(decision);
        });
        return;
    }
    decide(decision);
}

} // namespace

void useApprovalDialog(ScriptEngine* engine, QWidget* parent)
{
    if (!engine)
        return;
    // The engine keeps the handler, so it outlives none of its calls.
    const QPointer<QWidget> window(parent);
    engine->setApprovalHandler([engine, window](const Engine::ApprovalRequest& request,
                                                std::function<void(const Engine::ApprovalDecision&)> decide) {
        askUser(engine, window, request, decide);
    });
}

} // namespace ScriptHost
