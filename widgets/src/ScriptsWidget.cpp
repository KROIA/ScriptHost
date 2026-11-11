#include "ScriptsWidget.h"
#include "ScriptEditorDialog.h"
#include "ScriptLibrary.h"
#include "ScriptMethod.h"
#include "ScriptEngine.h"

#include <QCheckBox>
#include <QDesktopServices>
#include <QDir>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <map>

namespace ScriptHost
{

namespace
{

QString prettyJson(const QJsonValue& value)
{
    if (value.isObject())
        return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Indented)).trimmed();
    if (value.isArray())
        return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Indented)).trimmed();
    const QString text = QString::fromUtf8(QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact));
    return text.mid(1, text.size() - 2);
}

QTableWidget* makeTable(const QStringList& headers, const char* objectName, int stretchColumn, QWidget* parent)
{
    auto* table = new QTableWidget(0, int(headers.size()), parent);
    table->setObjectName(QString::fromLatin1(objectName));
    table->setHorizontalHeaderLabels(headers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setWordWrap(false);
    table->verticalHeader()->setVisible(false);
    QHeaderView* header = table->horizontalHeader();
    header->setStretchLastSection(false);
    header->setSectionResizeMode(QHeaderView::ResizeToContents);
    header->setSectionResizeMode(stretchColumn, QHeaderView::Stretch);
    return table;
}

QLabel* hintLabel(const char* objectName, QWidget* parent)
{
    auto* label = new QLabel(ScriptsWidget::tr("Set by the application."), parent);
    label->setObjectName(QString::fromLatin1(objectName));
    label->setStyleSheet(QStringLiteral("color: #888;"));
    return label;
}

} // namespace

ScriptsWidget::ScriptsWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* outer = new QVBoxLayout(this);
    m_noScripting = new QLabel(tr("No scripting bound - call setScripting() to manage scripts here."), this);
    m_noScripting->setWordWrap(true);
    m_noScripting->setStyleSheet(QStringLiteral("color: #888;"));
    outer->addWidget(m_noScripting);

    m_content = new QWidget(this);
    auto* layout = new QVBoxLayout(m_content);
    layout->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(m_content, 1);

    // ---- switches
    auto* form = new QFormLayout();
    auto* enabledRow = new QHBoxLayout();
    m_enabled = new QCheckBox(tr("Allow requested scripts"), m_content);
    m_enabled->setObjectName(QStringLiteral("scripthost_scriptsEnabled"));
    m_enabledHint = hintLabel("scripthost_scriptsEnabledHint", m_content);
    enabledRow->addWidget(m_enabled);
    enabledRow->addWidget(m_enabledHint);
    enabledRow->addStretch();
    form->addRow(enabledRow);

    // One check box per kind of script: new ones left, saved ones right.
    struct KindRow
    {
        ScriptEngine::ScriptKind kind;
        QString text, tip;
        int row, column;
    };
    const QString newTip = tr("A script written for the request, or one the requester saved itself.");
    const QString savedTip = tr("A script from the list below that you saved or that came with the "
                                "application, run by its name.");
    const KindRow kindRows[] = {     // in the order of the enum
        {ScriptEngine::ScriptKind::NewReading, tr("New scripts that only read"), newTip, 0, 0},
        {ScriptEngine::ScriptKind::NewChanging, tr("New scripts that change something"), newTip, 1, 0},
        {ScriptEngine::ScriptKind::SavedReading, tr("Saved scripts that only read"), savedTip, 0, 1},
        {ScriptEngine::ScriptKind::SavedChanging, tr("Saved scripts that change something"), savedTip, 1, 1},
        {ScriptEngine::ScriptKind::DryRun, tr("Dry runs"),
         tr("A run in which methods that change something are only recorded, not called."), 2, 0},
    };
    auto* approvalGrid = new QGridLayout();
    for (const KindRow& kindRow : kindRows) {
        auto* box = new QCheckBox(kindRow.text, m_content);
        box->setObjectName(QStringLiteral("scripthost_scriptsAsk_") + ScriptEngine::scriptKindKey(kindRow.kind));
        box->setToolTip(kindRow.tip);
        approvalGrid->addWidget(box, kindRow.row, kindRow.column);
        m_approval.append(box);
        const int kind = int(kindRow.kind);
        connect(box, &QCheckBox::toggled, this, [this, kind](bool on) {
            m_pendingApproval.insert(kind, on);
            userChanged();
        });
    }
    m_approvalHint = hintLabel("scripthost_scriptsApprovalHint", m_content);
    approvalGrid->addWidget(m_approvalHint, 2, 1);
    approvalGrid->setColumnStretch(2, 1);
    form->addRow(tr("Ask before running:"), approvalGrid);
    layout->addLayout(form);

    connect(m_enabled, &QCheckBox::toggled, this, [this](bool on) {
        m_pendingEnabled = on ? 1 : 0;
        userChanged();
    });

    auto* tabs = new QTabWidget(m_content);
    tabs->setObjectName(QStringLiteral("scripthost_scriptsTabs"));
    layout->addWidget(tabs, 1);

    // ---- saved scripts
    auto* scriptsPage = new QWidget(tabs);
    auto* scriptsLayout = new QVBoxLayout(scriptsPage);
    m_noLibrary = new QLabel(tr("The application keeps no saved scripts (no script library is set)."), scriptsPage);
    m_noLibrary->setObjectName(QStringLiteral("scripthost_scriptsNoLibrary"));
    m_noLibrary->setWordWrap(true);
    m_noLibrary->setStyleSheet(QStringLiteral("color: #888;"));
    scriptsLayout->addWidget(m_noLibrary);
    m_scripts = makeTable({tr("Name"), tr("Description"), tr("Origin")}, "scripthost_scriptsTable", 1, scriptsPage);
    scriptsLayout->addWidget(m_scripts, 1);
    auto* scriptButtons = new QHBoxLayout();
    auto button = [scriptsPage, scriptButtons](const QString& text, const char* name) {
        auto* b = new QPushButton(text, scriptsPage);
        b->setObjectName(QString::fromLatin1(name));
        scriptButtons->addWidget(b);
        return b;
    };
    m_new = button(tr("New"), "scripthost_scriptsNew");
    m_edit = button(tr("Edit"), "scripthost_scriptsEdit");
    m_duplicate = button(tr("Duplicate"), "scripthost_scriptsDuplicate");
    m_delete = button(tr("Delete"), "scripthost_scriptsDelete");
    scriptButtons->addStretch();
    m_openFolder = button(tr("Open folder"), "scripthost_scriptsOpenFolder");
    scriptsLayout->addLayout(scriptButtons);
    tabs->addTab(scriptsPage, tr("Scripts"));

    connect(m_scripts, &QTableWidget::itemSelectionChanged, this, &ScriptsWidget::updateScriptButtons);
    connect(m_scripts, &QTableWidget::cellDoubleClicked, this, [this]() { editScript(selectedScript()); });
    connect(m_new, &QPushButton::clicked, this, &ScriptsWidget::newScript);
    connect(m_edit, &QPushButton::clicked, this, [this]() { editScript(selectedScript()); });
    connect(m_duplicate, &QPushButton::clicked, this, [this]() { duplicateScript(selectedScript()); });
    connect(m_delete, &QPushButton::clicked, this, [this]() { deleteScript(selectedScript()); });
    connect(m_openFolder, &QPushButton::clicked, this, &ScriptsWidget::openFolder);

    // ---- methods
    auto* scroll = new QScrollArea(tabs);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    m_methodsPage = new QWidget(scroll);
    m_methodsLayout = new QVBoxLayout(m_methodsPage);
    m_methodsLayout->setSpacing(4);
    m_methodsLayout->addStretch();
    scroll->setWidget(m_methodsPage);
    tabs->addTab(scroll, tr("Methods"));

    // ---- runs going on now and runs of this session
    auto* runsPage = new QWidget(tabs);
    auto* runsLayout = new QVBoxLayout(runsPage);
    auto* split = new QSplitter(Qt::Vertical, runsPage);
    split->setChildrenCollapsible(false);
    m_activeRuns = makeTable({tr("Running script"), tr("By"), tr("Started"), tr("Running for"), QString()},
                             "scripthost_scriptsActiveRuns", 0, split);
    m_activeRuns->setSelectionMode(QAbstractItemView::SingleSelection);
    m_activeRuns->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tick = new QTimer(this);
    m_tick->setInterval(1000);
    connect(m_tick, &QTimer::timeout, this, &ScriptsWidget::updateElapsed);
    m_runs = makeTable({tr("Time"), tr("Script"), tr("By"), tr("Dry run"), tr("Status"), tr("Duration")},
                       "scripthost_scriptsRuns", 1, split);
    m_runDetails = new QPlainTextEdit(split);
    m_runDetails->setObjectName(QStringLiteral("scripthost_scriptsRunDetails"));
    m_runDetails->setReadOnly(true);
    m_runDetails->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_runDetails->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    split->addWidget(m_activeRuns);
    split->addWidget(m_runs);
    split->addWidget(m_runDetails);
    runsLayout->addWidget(split, 1);
    auto* runsButtons = new QHBoxLayout();
    runsButtons->addStretch();
    auto* clear = new QPushButton(tr("Clear"), runsPage);
    clear->setObjectName(QStringLiteral("scripthost_scriptsRunsClear"));
    runsButtons->addWidget(clear);
    runsLayout->addLayout(runsButtons);
    tabs->addTab(runsPage, tr("Runs"));

    // One run is shown at a time: an active one or one of the session.
    connect(m_runs, &QTableWidget::itemSelectionChanged, this, [this]() {
        const QList<QTableWidgetItem*> selected = m_runs->selectedItems();
        m_selectedRun = selected.isEmpty() ? QDateTime() : selected.first()->data(Qt::UserRole).toDateTime();
        if (!selected.isEmpty()) {
            m_shownActive = 0;
            const QSignalBlocker blocker(m_activeRuns);
            m_activeRuns->clearSelection();
        }
        showRunDetails();
    });
    connect(m_activeRuns, &QTableWidget::itemSelectionChanged, this, [this]() {
        const QList<QTableWidgetItem*> selected = m_activeRuns->selectedItems();
        m_shownActive = selected.isEmpty() ? 0 : m_activeRuns->item(selected.first()->row(), 0)
                                                     ->data(Qt::UserRole).toULongLong();
        if (m_shownActive != 0) {
            m_selectedRun = QDateTime();
            const QSignalBlocker blocker(m_runs);
            m_runs->clearSelection();
        }
        showRunDetails();
    });
    connect(clear, &QPushButton::clicked, this, [this]() {
        if (m_scripting)
            m_scripting->clearRuns();
    });

    refresh();
}

ScriptsWidget::~ScriptsWidget() = default;

void ScriptsWidget::setScripting(ScriptEngine* scripting)
{
    if (m_scripting)
        m_scripting->disconnect(this);
    m_scripting = scripting;
    m_pendingEnabled = -1;
    m_pendingApproval.clear();
    m_pendingMethods.clear();
    if (m_scripting) {
        connect(m_scripting, &ScriptEngine::enabledChanged, this, &ScriptsWidget::refreshSwitches);
        connect(m_scripting, &ScriptEngine::settingsChanged, this, [this]() {
            refreshSwitches();
            refreshScripts();
        });
        connect(m_scripting, &ScriptEngine::methodsChanged, this, &ScriptsWidget::refreshMethods);
        connect(m_scripting, &ScriptEngine::runsChanged, this, &ScriptsWidget::refreshRuns);
        connect(m_scripting, &ScriptEngine::runStarted, this, &ScriptsWidget::refreshActiveRuns);
        connect(m_scripting, &ScriptEngine::runFinished, this, &ScriptsWidget::onRunFinished);
        connect(m_scripting, &ScriptEngine::runLog, this, &ScriptsWidget::onRunLog);
    }
    m_shownActive = 0;
    m_liveLogs.clear();
    refresh();
}

ScriptEngine* ScriptsWidget::scripting() const
{
    return m_scripting;
}

void ScriptsWidget::setLockedVisible(bool visible)
{
    m_lockedVisible = visible;
    refreshMethods();
}

void ScriptsWidget::setApplyImmediately(bool immediately)
{
    m_immediate = immediately;
    if (m_immediate)
        apply();
}

void ScriptsWidget::userChanged()
{
    emit changed();
    if (m_immediate)
        apply();
}

void ScriptsWidget::apply()
{
    if (!m_scripting)
        return;
    // Every change on the ScriptEngine object asks for a refresh; the method
    // list is not rebuilt under the check box that is being clicked.
    m_applying = true;
    if (m_pendingEnabled >= 0)
        m_scripting->setEnabledByUser(m_pendingEnabled == 1);
    const QMap<int, bool> pendingApproval = m_pendingApproval;
    const QMap<QString, bool> pending = m_pendingMethods;
    m_pendingEnabled = -1;
    m_pendingApproval.clear();
    m_pendingMethods.clear();
    for (auto it = pendingApproval.constBegin(); it != pendingApproval.constEnd(); ++it)
        m_scripting->setApprovalRequiredByUser(static_cast<ScriptEngine::ScriptKind>(it.key()), it.value());
    for (auto it = pending.constBegin(); it != pending.constEnd(); ++it)
        m_scripting->setMethodEnabledByUser(it.key(), it.value());
    m_applying = false;
    refreshSwitches();
}

void ScriptsWidget::revert()
{
    m_pendingEnabled = -1;
    m_pendingApproval.clear();
    m_pendingMethods.clear();
    refreshSwitches();
    refreshMethods();
}

void ScriptsWidget::refresh()
{
    m_noScripting->setVisible(!m_scripting);
    m_content->setEnabled(m_scripting != nullptr);
    refreshSwitches();
    refreshScripts();
    refreshMethods();
    refreshRuns();
    refreshActiveRuns();
}

void ScriptsWidget::refreshSwitches()
{
    const QSignalBlocker blockEnabled(m_enabled);
    if (!m_scripting) {
        m_enabledHint->hide();
        m_approvalHint->hide();
        return;
    }
    // Who asks is the application's to say: "the assistant", "the plugin".
    m_enabled->setText(tr("Allow %1 to run scripts").arg(m_scripting->requesterName()));
    const bool enabledLocked = !m_scripting->isEnabledUserConfigurable();
    m_enabled->setChecked(!enabledLocked && m_pendingEnabled >= 0 ? m_pendingEnabled == 1
                                                                  : m_scripting->isEnabled());
    m_enabled->setEnabled(!enabledLocked);
    m_enabledHint->setVisible(enabledLocked);
    bool anyApprovalLocked = false;
    for (int kind = 0; kind < m_approval.size(); ++kind) {
        QCheckBox* box = m_approval[kind];
        const QSignalBlocker blockBox(box);
        const auto scriptKind = static_cast<ScriptEngine::ScriptKind>(kind);
        const bool locked = !m_scripting->isApprovalUserConfigurable(scriptKind);
        box->setChecked(!locked && m_pendingApproval.contains(kind) ? m_pendingApproval.value(kind)
                                                                    : m_scripting->isApprovalRequired(scriptKind));
        box->setEnabled(!locked);
        anyApprovalLocked = anyApprovalLocked || locked;
    }
    m_approvalHint->setVisible(anyApprovalLocked);
}

void ScriptsWidget::refreshScripts()
{
    ScriptLibrary* library = m_scripting ? m_scripting->library() : nullptr;
    if (library != m_library) {
        if (m_library)
            m_library->disconnect(this);
        m_library = library;
        if (m_library)
            connect(m_library, &ScriptLibrary::changed, this, &ScriptsWidget::refreshScripts);
    }

    const QString selected = selectedScript();
    const QSignalBlocker blocker(m_scripts);
    m_scripts->setRowCount(0);
    m_noLibrary->setVisible(m_scripting && !library);
    m_scripts->setVisible(library != nullptr || !m_scripting);
    if (library) {
        for (const ScriptLibrary::Script& entry : library->scripts()) {
            const int row = m_scripts->rowCount();
            m_scripts->insertRow(row);
            auto* name = new QTableWidgetItem(entry.name);
            name->setData(Qt::UserRole, entry.name);
            name->setToolTip(entry.filePath);
            m_scripts->setItem(row, 0, name);
            auto* description = new QTableWidgetItem(entry.description.simplified());
            description->setToolTip(entry.description);
            m_scripts->setItem(row, 1, description);
            m_scripts->setItem(row, 2, new QTableWidgetItem(entry.readOnly ? tr("%1 (read-only)").arg(entry.origin)
                                                                           : entry.origin));
            if (entry.readOnly) {
                for (int column = 0; column < 3; ++column)
                    m_scripts->item(row, column)->setForeground(QColor(0x88, 0x88, 0x88));
            }
            if (entry.name == selected)
                m_scripts->selectRow(row);
        }
    }
    updateScriptButtons();
}

void ScriptsWidget::updateScriptButtons()
{
    ScriptLibrary* library = m_scripting ? m_scripting->library() : nullptr;
    const bool mayEdit = library && m_scripting->userMayEditScripts();
    const bool writable = library && !library->directory().isEmpty();
    const QString name = selectedScript();
    const bool readOnly = !name.isEmpty() && library && library->script(name).readOnly;
    m_new->setEnabled(mayEdit && writable);
    m_edit->setEnabled(!name.isEmpty());
    m_edit->setText(mayEdit && !readOnly ? tr("Edit") : tr("View"));
    m_duplicate->setEnabled(mayEdit && writable && !name.isEmpty());
    m_delete->setEnabled(mayEdit && !name.isEmpty() && !readOnly);
    m_openFolder->setEnabled(writable);
    for (QPushButton* b : {m_new, m_edit, m_duplicate, m_delete, m_openFolder})
        b->setVisible(library != nullptr);
}

QString ScriptsWidget::selectedScript() const
{
    const QList<QTableWidgetItem*> selected = m_scripts->selectedItems();
    if (selected.isEmpty())
        return QString();
    const QTableWidgetItem* item = m_scripts->item(selected.first()->row(), 0);
    return item ? item->data(Qt::UserRole).toString() : QString();
}

void ScriptsWidget::refreshMethods()
{
    if (m_applying)
        return;

    // Deleted synchronously: deleteLater() posted
    // before a modal dialog's exec() would wait until the dialog closes.
    while (m_methodsLayout->count() > 1) {
        QLayoutItem* item = m_methodsLayout->takeAt(0);
        if (item->widget()) {
            item->widget()->hide();
            delete item->widget();
        }
        delete item;
    }
    auto addLabel = [this](const QString& text, const QString& style) {
        auto* label = new QLabel(text, m_methodsPage);
        label->setWordWrap(true);
        label->setStyleSheet(style);
        m_methodsLayout->insertWidget(m_methodsLayout->count() - 1, label);
    };
    if (!m_scripting)
        return;

    std::map<QString, QList<ScriptMethod>> groups;
    for (const ScriptMethod& method : m_scripting->methods()) {
        if (m_lockedVisible || m_scripting->isMethodUserConfigurable(method.name()))
            groups[method.group()].append(method);
    }
    if (groups.empty()) {
        addLabel(tr("No methods registered."), QStringLiteral("color: #888;"));
        return;
    }

    auto addGroup = [&](const QString& title, const QList<ScriptMethod>& methods) {
        addLabel(title, QStringLiteral("font-weight: bold; margin-top: 8px;"));
        for (const ScriptMethod& method : methods) {
            const QString name = method.name();
            const bool locked = !m_scripting->isMethodUserConfigurable(name);
            QString text = name;
            if (method.isReadOnly())
                text += tr("  (reads only)");
            if (locked)
                text += tr("  - set by the application");
            auto* check = new QCheckBox(text, m_methodsPage);
            check->setObjectName(QStringLiteral("scripthost_scriptsMethod_") + name);
            check->setChecked(!locked && m_pendingMethods.contains(name) ? m_pendingMethods.value(name)
                                                                          : m_scripting->isMethodEnabled(name));
            check->setEnabled(!locked);
            QString tip = method.signature();
            if (!method.description().isEmpty())
                tip += QStringLiteral("\n\n") + method.description();
            check->setToolTip(tip);
            connect(check, &QCheckBox::toggled, this, [this, name](bool on) {
                m_pendingMethods[name] = on;
                userChanged();
            });
            m_methodsLayout->insertWidget(m_methodsLayout->count() - 1, check);
        }
    };
    for (const auto& group : groups) {
        if (!group.first.isEmpty())
            addGroup(group.first, group.second);
    }
    const auto ungrouped = groups.find(QString());
    if (ungrouped != groups.end())
        addGroup(tr("Other"), ungrouped->second);
}

void ScriptsWidget::refreshRuns()
{
    const QSignalBlocker blocker(m_runs);
    m_runs->setRowCount(0);
    const QList<ScriptEngine::RunRecord> records = m_scripting ? m_scripting->runs() : QList<ScriptEngine::RunRecord>();
    int selectedRow = -1;
    for (int i = int(records.size()) - 1; i >= 0; --i) {
        const ScriptEngine::RunRecord& record = records.at(i);
        const QString status = record.result.value(QStringLiteral("status")).toString();
        const QString phase = record.result.value(QStringLiteral("phase")).toString();
        const QJsonValue duration = record.result.value(QStringLiteral("duration_ms"));
        const QStringList cells = {
            record.started.toString(QStringLiteral("HH:mm:ss")),
            record.name.isEmpty() ? tr("(unsaved)") : record.name,
            record.external ? m_scripting->requesterName() : tr("the user"),
            record.dryRun ? tr("yes") : QString(),
            phase.isEmpty() ? status : QStringLiteral("%1 (%2)").arg(status, phase),
            duration.isUndefined() ? QString() : tr("%1 ms").arg(duration.toInt())
        };
        const int row = m_runs->rowCount();
        m_runs->insertRow(row);
        for (int column = 0; column < cells.size(); ++column)
            m_runs->setItem(row, column, new QTableWidgetItem(cells.at(column)));
        m_runs->item(row, 0)->setData(Qt::UserRole, record.started);
        if (record.started == m_selectedRun)
            selectedRow = row;
    }
    if (selectedRow >= 0)
        m_runs->selectRow(selectedRow);
    else
        m_selectedRun = QDateTime();
    showRunDetails();
}

void ScriptsWidget::refreshActiveRuns()
{
    const QSignalBlocker blocker(m_activeRuns);
    m_activeRuns->setRowCount(0);
    const QList<ScriptEngine::RunId> ids = m_scripting ? m_scripting->activeRuns() : QList<ScriptEngine::RunId>();
    const QDate today = QDate::currentDate();
    for (const ScriptEngine::RunId id : ids) {
        const ScriptEngine::RunRecord record = m_scripting->activeRun(id);
        // A run of an earlier day says which.
        const QString started = record.started.toString(record.started.date() == today
                                                            ? QStringLiteral("HH:mm:ss")
                                                            : QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        const QStringList cells = {
            record.name.isEmpty() ? tr("(unnamed)") : record.name,
            record.external ? tr("requested by %1").arg(m_scripting->requesterName()) : tr("the user"),
            started,
            QString()
        };
        const int row = m_activeRuns->rowCount();
        m_activeRuns->insertRow(row);
        for (int column = 0; column < cells.size(); ++column)
            m_activeRuns->setItem(row, column, new QTableWidgetItem(cells.at(column)));
        m_activeRuns->item(row, 0)->setData(Qt::UserRole, QVariant::fromValue<quint64>(id));
        m_activeRuns->item(row, 0)->setToolTip(tr("Run #%1 - select it to see its code and log").arg(id));
        m_activeRuns->item(row, 2)->setData(Qt::UserRole, record.started);
        if (id == m_shownActive)
            m_activeRuns->selectRow(row);
        // Stops at once; the row goes when the run has finished.
        auto* stop = new QPushButton(tr("Stop"), m_activeRuns);
        stop->setObjectName(QStringLiteral("scripthost_scriptsActiveRunStop"));
        connect(stop, &QPushButton::clicked, this, [this, stop, id]() {
            stop->setEnabled(false);
            stop->setText(tr("Stopping..."));
            if (m_scripting)
                m_scripting->stop(id);
        });
        m_activeRuns->setCellWidget(row, 4, stop);
    }
    updateElapsed();
    if (ids.isEmpty())
        m_tick->stop();
    else if (!m_tick->isActive())
        m_tick->start();
}

void ScriptsWidget::onRunFinished(quint64 id)
{
    m_liveLogs.remove(id);
    if (id == m_shownActive) {
        // It goes on being shown, now with its result, in the runs of the session.
        m_shownActive = 0;
        const QList<ScriptEngine::RunRecord> records = m_scripting ? m_scripting->runs()
                                                                   : QList<ScriptEngine::RunRecord>();
        for (const ScriptEngine::RunRecord& record : records) {
            if (record.id == id)
                m_selectedRun = record.started;
        }
        refreshRuns();
    }
    refreshActiveRuns();
}

void ScriptsWidget::onRunLog(quint64 id, const QString& text)
{
    QStringList& lines = m_liveLogs[id];
    lines << text;
    const int keep = m_scripting ? m_scripting->limits().maxLogLines : 200;
    if (keep > 0 && lines.size() > keep)
        lines.removeFirst();
    // The log is the end of the details: a line is added there, keeping the
    // user's scroll position.
    if (id == m_shownActive)
        m_runDetails->appendPlainText(text);
}

void ScriptsWidget::updateElapsed()
{
    const QDateTime now = QDateTime::currentDateTime();
    for (int row = 0; row < m_activeRuns->rowCount(); ++row) {
        const QDateTime started = m_activeRuns->item(row, 2)->data(Qt::UserRole).toDateTime();
        const qint64 seconds = started.isValid() ? qMax<qint64>(0, started.secsTo(now)) : 0;
        m_activeRuns->item(row, 3)->setText(QStringLiteral("%1:%2:%3")
                                                .arg(seconds / 3600)
                                                .arg((seconds / 60) % 60, 2, 10, QLatin1Char('0'))
                                                .arg(seconds % 60, 2, 10, QLatin1Char('0')));
    }
}

void ScriptsWidget::showRunDetails()
{
    if (m_shownActive != 0 && m_scripting && m_scripting->isRunning(m_shownActive)) {
        const ScriptEngine::RunRecord record = m_scripting->activeRun(m_shownActive);
        QString text;
        text += tr("Run #%1: %2 - running\n").arg(record.id).arg(record.name.isEmpty() ? tr("(unsaved)") : record.name);
        text += tr("Started: %1 by %2%3\n\n")
                    .arg(record.started.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")),
                         record.external ? m_scripting->requesterName() : tr("the user"),
                         record.dryRun ? tr(", dry run") : QString());
        text += tr("Arguments:\n%1\n\n").arg(prettyJson(record.args));
        text += tr("Code:\n%1\n\n").arg(record.code);
        text += tr("Log:");
        const QStringList lines = m_liveLogs.value(m_shownActive);
        if (!lines.isEmpty())
            text += QLatin1Char('\n') + lines.join(QLatin1Char('\n'));
        m_runDetails->setPlainText(text);
        return;
    }
    // Rows are newest first; by position, as two runs may start in the same millisecond.
    const QList<QTableWidgetItem*> selected = m_runs->selectedItems();
    const QList<ScriptEngine::RunRecord> records = m_scripting ? m_scripting->runs() : QList<ScriptEngine::RunRecord>();
    const int index = selected.isEmpty() ? -1 : int(records.size()) - 1 - selected.first()->row();
    if (index >= 0 && index < records.size()) {
        const ScriptEngine::RunRecord& record = records.at(index);
        QJsonObject result = record.result;
        const QJsonArray logged = result.take(QStringLiteral("log")).toArray();
        QStringList lines;
        for (const QJsonValue& line : logged)
            lines << (line.isString() ? line.toString() : prettyJson(line));
        QString text;
        text += tr("Run #%1: %2\n").arg(record.id).arg(record.name.isEmpty() ? tr("(unsaved)") : record.name);
        text += tr("Started: %1 by %2%3\n\n")
                    .arg(record.started.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")),
                         record.external ? m_scripting->requesterName() : tr("the user"),
                         record.dryRun ? tr(", dry run") : QString());
        text += tr("Arguments:\n%1\n\n").arg(prettyJson(record.args));
        text += tr("Code:\n%1\n\n").arg(record.code);
        text += tr("Result:\n%1\n\n").arg(prettyJson(result));
        text += tr("Log:\n%1\n").arg(lines.isEmpty() ? tr("(empty)") : lines.join(QLatin1Char('\n')));
        m_runDetails->setPlainText(text);
        return;
    }
    m_runDetails->clear();
}

// ------------------------------------------------------------ scripts

ScriptEditorDialog* ScriptsWidget::openEditor()
{
    auto* editor = new ScriptEditorDialog(m_scripting, this);
    editor->setAttribute(Qt::WA_DeleteOnClose);
    editor->setModal(false);
    return editor;
}

ScriptEditorDialog* ScriptsWidget::newScript()
{
    ScriptLibrary* library = m_scripting ? m_scripting->library() : nullptr;
    if (!library || library->directory().isEmpty() || !m_scripting->userMayEditScripts())
        return nullptr;
    ScriptLibrary::Script fresh;
    fresh.origin = QStringLiteral("user");
    ScriptEditorDialog* editor = openEditor();
    editor->setScript(fresh);
    editor->show();
    return editor;
}

ScriptEditorDialog* ScriptsWidget::editScript(const QString& name)
{
    ScriptLibrary* library = m_scripting ? m_scripting->library() : nullptr;
    if (!library || name.isEmpty())
        return nullptr;
    const ScriptLibrary::Script found = library->script(name);
    if (!found.isValid())
        return nullptr;
    ScriptEditorDialog* editor = openEditor();
    editor->setScript(found);
    editor->setReadOnly(found.readOnly || !m_scripting->userMayEditScripts());
    editor->show();
    return editor;
}

ScriptEditorDialog* ScriptsWidget::duplicateScript(const QString& name)
{
    ScriptLibrary* library = m_scripting ? m_scripting->library() : nullptr;
    if (!library || library->directory().isEmpty() || !m_scripting->userMayEditScripts())
        return nullptr;
    ScriptLibrary::Script copy = library->script(name);
    if (!copy.isValid())
        return nullptr;
    copy.name = tr("%1 (copy)").arg(copy.name);
    copy.origin = QStringLiteral("user");
    copy.filePath.clear();
    copy.readOnly = false;
    ScriptEditorDialog* editor = openEditor();
    editor->setScript(copy);
    editor->show();
    return editor;
}

bool ScriptsWidget::deleteScript(const QString& name, bool confirm)
{
    ScriptLibrary* library = m_scripting ? m_scripting->library() : nullptr;
    if (!library || name.isEmpty() || !m_scripting->userMayEditScripts())
        return false;
    const ScriptLibrary::Script found = library->script(name);
    if (!found.isValid() || found.readOnly)
        return false;
    if (confirm && QMessageBox::question(this, tr("Delete script"),
                                         tr("Delete the script \"%1\"?").arg(found.name)) != QMessageBox::Yes)
        return false;
    QString error;
    if (!library->remove(found.name, &error)) {
        QMessageBox::warning(this, tr("Delete script"), tr("The script could not be deleted: %1").arg(error));
        return false;
    }
    return true;
}

void ScriptsWidget::openFolder()
{
    ScriptLibrary* library = m_scripting ? m_scripting->library() : nullptr;
    if (!library || library->directory().isEmpty())
        return;
    // The directory is only created with the first saved script.
    QDir().mkpath(library->directory());
    QDesktopServices::openUrl(QUrl::fromLocalFile(library->directory()));
}

} // namespace ScriptHost
