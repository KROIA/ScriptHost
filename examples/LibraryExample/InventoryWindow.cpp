// What an application does to be scriptable with ScriptHost, in order:
//
//   1. Create one ScriptEngine and say who, besides the user, asks for runs.
//   2. Register the methods a script may call - here the inventory table as
//      the object `inventory`.
//   3. Decide which of them are available, and what the user may change.
//   4. Decide whether requested scripts need the user's approval.
//   5. Say where saved scripts live: shipped ones (read-only) and the user's.
//   6. Restore the user's choices from the last session.
//   7. Show what scripts do (signals), and offer the user the widgets.
//   8. Hand the user's keys and clicks to scripts that ask for them.
//   9. Let scripts draw over the window: highlights, and a small game.
//  10. Let scripts read and change the window, and build dialogs of their own.
//
// Everything else - checking a script before it runs, limits, the approval
// dialog, the Scripts window with its editor and debugger - comes from the
// library. The toolbar at the end shows the three ways a script starts:
// run() by the application, the editor by the user, requestRun() by another
// program - and scripts that run until they are stopped, beside the others.

#include "InventoryWindow.h"
#include <QDir>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QImage>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QSettings>
#include <QSplitter>
#include <QStandardPaths>
#include <QTableWidget>
#include <QToolBar>
#include <QVBoxLayout>
#include <cmath>

namespace
{

const char* const kScriptSettingsKey = "scripting/userSettings";
const char* const kRestockScript = "Restock low items";
const char* const kMonitorScript = "Stock monitor";
const char* const kInputScript = "Input logger";
const char* const kHighlightScript = "Highlight a widget";
const char* const kGameScript = "Paddle game";
const char* const kRearrangeScript = "Rearrange the window";
const char* const kLookUpScript = "Look up an item";

// ---- Helpers for the table: the application's own data, nothing ScriptHost-specific

int rowOf(const QTableWidget* table, const QString& item)
{
    for (int row = 0; row < table->rowCount(); ++row) {
        if (table->item(row, 0) && table->item(row, 0)->text().compare(item, Qt::CaseInsensitive) == 0)
            return row;
    }
    return -1;
}

// A method handler fails by throwing ScriptError: the script gets an
// exception with this message.
int existingRow(const QTableWidget* table, const QString& item)
{
    const int row = rowOf(table, item);
    if (row < 0)
        throw ScriptHost::ScriptError(QString("there is no item '%1'").arg(item));
    return row;
}

QString cell(const QTableWidget* table, int row, int column)
{
    return table->item(row, column) ? table->item(row, column)->text() : QString();
}

QJsonObject rowObject(const QTableWidget* table, int row)
{
    return {{"item", cell(table, row, 0)},
            {"quantity", cell(table, row, 1).toInt()},
            {"location", cell(table, row, 2)}};
}

void setCell(QTableWidget* table, int row, int column, const QString& text)
{
    if (!table->item(row, column))
        table->setItem(row, column, new QTableWidgetItem);
    table->item(row, column)->setText(text);
}

QString compact(const QJsonObject& object)
{
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

} // namespace

InventoryWindow::InventoryWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("ScriptHost example - Inventory");
    resize(820, 560);

    m_table = new QTableWidget(0, 3, this);
    m_table->setObjectName("inventoryTable");
    m_table->setHorizontalHeaderLabels({"Item", "Quantity", "Location"});
    m_table->horizontalHeader()->setStretchLastSection(true);
    const struct { const char* item; int quantity; const char* location; } rows[] = {
        {"Screws", 120, "Shelf A"}, {"Nuts", 30, "Shelf A"}, {"Washers", 45, "Shelf B"},
        {"Bolts", 200, "Shelf C"}, {"Hinges", 12, "Drawer 1"}};
    for (const auto& r : rows) {
        const int row = m_table->rowCount();
        m_table->insertRow(row);
        setCell(m_table, row, 0, r.item);
        setCell(m_table, row, 1, QString::number(r.quantity));
        setCell(m_table, row, 2, r.location);
    }

    m_log = new QPlainTextEdit(this);
    m_log->setObjectName("scriptLog");
    m_log->setReadOnly(true);
    m_log->setPlaceholderText("Script runs and their log lines appear here.");

    // Where the user's keys and clicks go to scripts (step 8). A plain widget
    // of its own: in the table, typing would edit cells, and a scroll area's
    // clicks land on its viewport.
    m_input = new QLabel("Input for scripts: click here, then type", this);
    m_input->setObjectName("scriptInput");
    m_input->setAlignment(Qt::AlignCenter);
    m_input->setFrameShape(QFrame::StyledPanel);
    m_input->setMinimumHeight(48);
    m_input->setFocusPolicy(Qt::StrongFocus);

    // A plain widget for the game script to play on (step 9). It takes the
    // keys once clicked, like the input area.
    m_field = new QWidget(this);
    m_field->setObjectName("gameField");
    m_field->setMinimumHeight(160);
    m_field->setFocusPolicy(Qt::StrongFocus);
    m_field->setAutoFillBackground(true);
    QPalette fieldPalette = m_field->palette();
    fieldPalette.setColor(QPalette::Window, QColor(240, 244, 250));
    m_field->setPalette(fieldPalette);

    auto* split = new QSplitter(Qt::Vertical, this);
    split->addWidget(m_table);
    split->addWidget(m_input);
    split->addWidget(m_field);
    split->addWidget(m_log);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 0);
    split->setStretchFactor(2, 2);
    split->setStretchFactor(3, 2);
    setCentralWidget(split);

    setUpScripting();
    buildToolBar();
}

void InventoryWindow::setUpScripting()
{
    // ---- 1. The engine ----------------------------------------------------
    // One per application. Scripts run on a thread of their own; method
    // handlers run on this (the GUI) thread.
    m_engine = new ScriptHost::ScriptEngine(this);
    // Requested runs come from somebody other than the user. The name is what
    // the user reads: "Allow the remote client to run scripts", "Requested by
    // the remote client: run this script?".
    m_engine->setRequesterName("the remote client");
    // Requested runs that need approval ask the user in the built-in dialog
    // (ScriptHostWidgets), over this window.
    ScriptHost::useApprovalDialog(m_engine, this);

    // ---- 2. The methods ---------------------------------------------------
    registerMethods();

    // ---- 3. What is available, and who decides --------------------------
    // Removing items is not for scripts, and not the user's to switch on in
    // the Scripts window: the application decides that alone.
    m_engine->setMethodEnabled("inventory.remove", false);
    m_engine->setMethodUserConfigurable("inventory.remove", false);

    // ---- 4. Approval of requested runs ------------------------------------
    // A requested script that changes something is shown to the user first;
    // one that only reads runs at once. setApprovalRequired() decides per
    // kind of script instead. The user may change it in the Scripts window;
    // setApprovalUserConfigurable(false) would fix it.
    m_engine->setApproval(ScriptHost::ScriptEngine::Approval::ChangingScripts);

    // ---- 5. Saved scripts -------------------------------------------------
    // The user's scripts go to a folder of the application's choice. Scripts
    // shipped with the application come from a second folder and cannot be
    // changed - here the resource folder ":/scripts" (see scripts.qrc).
    m_library = new ScriptHost::ScriptLibrary(this);
    m_library->setDirectory(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/scripts");
    m_library->addReadOnlyDirectory(":/scripts");
    m_engine->setLibrary(m_library);

    // ---- 6. The user's choices from earlier sessions ----------------------
    // What the application locked (step 3) is skipped.
    m_engine->applyUserSettings(
        QJsonDocument::fromJson(QSettings().value(kScriptSettingsKey).toByteArray()).object());

    // ---- 7. What scripts do -----------------------------------------------
    connect(m_engine, &ScriptHost::ScriptEngine::runStarted, this,
            [this](ScriptHost::ScriptEngine::RunId, const QString& name, bool external) {
        appendLog(QString("[run] %1 started by %2")
                      .arg(name.isEmpty() ? QString("a script") : "'" + name + "'",
                           external ? m_engine->requesterName() : QString("the user")));
    });
    connect(m_engine, &ScriptHost::ScriptEngine::runLog, this,
            [this](ScriptHost::ScriptEngine::RunId, const QString& text) {
        appendLog("[log] " + text);
    });
    connect(m_engine, &ScriptHost::ScriptEngine::runFinished, this,
            [this](ScriptHost::ScriptEngine::RunId, const QJsonObject& result) {
        appendLog("[run] finished: " + compact(result).left(400));
    });

    // ---- 8. Keys and clicks -----------------------------------------------
    // What the input area receives reaches scripts as "key" and "mouse"
    // events (on('key', fn)), and input.mouse() tells them where the pointer
    // is. Keys arrive only while the area has the focus - a click into it
    // gives it. Requested runs get none of it unless allowed:
    // m_engine->allowEventForRequestedRuns("key") - not done here, as a
    // program that may read the user's keys could log them.
    // The game field's keys go to scripts too. input.mouse() follows the
    // latest forwarder: the input area.
    ScriptHost::forwardInput(m_engine, m_field);
    ScriptHost::forwardInput(m_engine, m_input);

    // ---- 9. Drawing over the window ---------------------------------------
    setUpCanvas();

    // ---- 10. The window's widgets -----------------------------------------
    // Scripts get the `ui` methods (group "UI"): they read the widget tree,
    // change it (ui.restore() undoes what a run changed), build dialogs and
    // get their signals as events. They see this window and what lies below
    // it, and never ScriptHost's own widgets: the canvas, the editor, the
    // Scripts panel and the approval dialog. Each tool is a method like any
    // other, switched off with setMethodEnabled("ui.create", false).
    ScriptHost::exposeWidgets(m_engine, this);
}

// A canvas over the whole window: scripts get the global `gfx` and draw on
// it - shapes, text, images - while the window works as before (the canvas
// takes no clicks). Shapes drawn on gfx.widget("objectName") follow that
// widget when it moves or resizes.
void InventoryWindow::setUpCanvas()
{
    ScriptHost::ScriptCanvas* canvas = ScriptHost::attachCanvas(m_engine, this);

    // Scripts read no files: images come from the application, by name.
    QImage ball(12, 12, QImage::Format_ARGB32_Premultiplied);
    ball.fill(Qt::transparent);
    {
        QPainter painter(&ball);
        painter.setRenderHint(QPainter::Antialiasing);
        QRadialGradient shade(4, 4, 8);
        shade.setColorAt(0, QColor(255, 160, 160));
        shade.setColorAt(1, QColor(190, 20, 20));
        painter.setPen(Qt::NoPen);
        painter.setBrush(shade);
        painter.drawEllipse(QRectF(0, 0, 12, 12));
    }
    canvas->addImage("ball", ball);

    // A drawing function of the application: gfx.draw.star(x, y, r) in
    // scripts, painted with the script's style and transform. The bounds
    // function says where it paints, so that only that area is repainted.
    auto starBounds = [](const QVariantList& args) {
        const double r = args.value(2).toDouble();
        return QRectF(args.value(0).toDouble() - r, args.value(1).toDouble() - r, 2 * r, 2 * r);
    };
    canvas->addDrawFunction("star", [](QPainter& painter, const QVariantList& args) {
        const QPointF centre(args.value(0).toDouble(), args.value(1).toDouble());
        const double r = args.value(2).toDouble();
        QPainterPath star;
        for (int i = 0; i < 10; ++i) {
            const double angle = (i * 0.2 - 0.5) * 3.141592653589793;
            const double reach = i % 2 ? r * 0.45 : r;
            const QPointF point = centre + QPointF(std::cos(angle), std::sin(angle)) * reach;
            if (i == 0)
                star.moveTo(point);
            else
                star.lineTo(point);
        }
        star.closeSubpath();
        painter.drawPath(star);
    }, "/** A five-pointed star around (x, y), r its outer radius. */\n"
       "function star(x: number, y: number, r: number): void;", starBounds);
}

void InventoryWindow::registerMethods()
{
    // A definition (name, description, parameters, return value) is written
    // once. It is checked against on every call, shown in the editor's
    // reference and returned by apiDefinition() for whoever writes scripts.
    QTableWidget* table = m_table;
    const QString group = "Inventory";
    // The example return value gives the return type its shape, and is what
    // a dry run answers for a method that changes something.
    const QJsonObject exampleRow{{"item", "Screws"}, {"quantity", 120}, {"location", "Shelf A"}};

    ScriptHost::ScriptMethod list("inventory.list");
    list.setDescription("All inventory items.").setGroup(group).setReadOnly(true)
        .setReturns("array", "One object per item", QJsonArray{exampleRow});
    m_engine->registerMethod(list, [table](const QJsonObject&) -> QJsonValue {
        QJsonArray rows;
        for (int row = 0; row < table->rowCount(); ++row)
            rows.append(rowObject(table, row));
        return rows;
    });

    ScriptHost::ScriptMethod get("inventory.get");
    get.setDescription("One inventory item, by name.").setGroup(group).setReadOnly(true)
       .addParameter("item", "string", "Name of the item")
       .setReturns("object", "The item", exampleRow);
    m_engine->registerMethod(get, [table](const QJsonObject& args) -> QJsonValue {
        return rowObject(table, existingRow(table, args["item"].toString()));
    });

    // Not read-only: needs approval when requested, and is only recorded in
    // a dry run.
    ScriptHost::ScriptMethod setQuantity("inventory.setQuantity");
    setQuantity.setDescription("Sets the quantity of an item.").setGroup(group)
               .addParameter("item", "string", "Name of the item")
               .addParameter("quantity", "integer", "The new quantity")
               .setRange("quantity", 0, 1000000)
               .setReturns("object", "The item as it is now", exampleRow);
    m_engine->registerMethod(setQuantity, [table](const QJsonObject& args) -> QJsonValue {
        // The arguments are checked already: "quantity" is an integer in range.
        const int row = existingRow(table, args["item"].toString());
        setCell(table, row, 1, QString::number(args["quantity"].toInt()));
        return rowObject(table, row);
    });

    ScriptHost::ScriptMethod setLocation("inventory.setLocation");
    setLocation.setDescription("Moves an item to another location.").setGroup(group)
               .addParameter("item", "string", "Name of the item")
               .addParameter("location", "string", "Where it is kept now")
               .setReturns("object", "The item as it is now", exampleRow);
    m_engine->registerMethod(setLocation, [table](const QJsonObject& args) -> QJsonValue {
        const int row = existingRow(table, args["item"].toString());
        setCell(table, row, 2, args["location"].toString());
        return rowObject(table, row);
    });

    ScriptHost::ScriptMethod add("inventory.add");
    add.setDescription("Adds a new item.").setGroup(group)
       .addParameter("item", "string", "Name of the item")
       .addParameter("quantity", "integer", "How many there are")
       .addParameter("location", "string", "Where it is kept", false)
       .setDefault("location", "Unsorted")
       .setReturns("object", "The new item", exampleRow);
    m_engine->registerMethod(add, [table](const QJsonObject& args) -> QJsonValue {
        const QString item = args["item"].toString().trimmed();
        if (item.isEmpty())
            throw ScriptHost::ScriptError("the item needs a name");
        if (rowOf(table, item) >= 0)
            throw ScriptHost::ScriptError(QString("there is an item '%1' already").arg(item));
        const int row = table->rowCount();
        table->insertRow(row);
        setCell(table, row, 0, item);
        setCell(table, row, 1, QString::number(args["quantity"].toInt()));
        setCell(table, row, 2, args["location"].toString());    // the default applied if left out
        return rowObject(table, row);
    });

    ScriptHost::ScriptMethod remove("inventory.remove");
    remove.setDescription("Removes an item.").setGroup(group)
          .addParameter("item", "string", "Name of the item");
    m_engine->registerMethod(remove, [table](const QJsonObject& args) -> QJsonValue {
        table->removeRow(existingRow(table, args["item"].toString()));
        return QJsonValue();
    });
}

void InventoryWindow::buildToolBar()
{
    QToolBar* bar = addToolBar("Scripts");
    bar->setObjectName("scriptsToolBar");
    bar->setMovable(false);
    bar->addAction(QString::fromUtf8("Scripts…"), this, [this]() { openScripts(); });
    bar->addAction(QString::fromUtf8("New script…"), this, [this]() { newScript(); });
    bar->addSeparator();
    bar->addAction(QString("Run '%1'").arg(kRestockScript), this, [this]() { runRestock(false); });
    bar->addAction(QString("Dry run '%1'").arg(kRestockScript), this, [this]() { runRestock(true); });
    bar->addSeparator();
    bar->addAction(QString("Start '%1'").arg(kMonitorScript), this, [this]() {
        runUntilStopped(kMonitorScript, QJsonObject{{"threshold", 50}, {"seconds", 5}});
    });
    bar->addAction(QString("Start '%1'").arg(kInputScript), this, [this]() { runUntilStopped(kInputScript); });
    bar->addSeparator();
    bar->addAction("Highlight the input area", this, [this]() {
        runUntilStopped(kHighlightScript, QJsonObject{{"widget", "scriptInput"}});
    });
    bar->addAction(QString("Start '%1'").arg(kGameScript), this, [this]() {
        runUntilStopped(kGameScript);
        m_field->setFocus();    // the keys go to the field
    });
    bar->addSeparator();
    // Both wait - for a timer, for the user - so they run until stopped.
    bar->addAction(QString("Run '%1'").arg(kRearrangeScript), this, [this]() { runUntilStopped(kRearrangeScript); });
    bar->addAction(QString("Run '%1'").arg(kLookUpScript), this, [this]() { runUntilStopped(kLookUpScript); });
    bar->addSeparator();
    bar->addAction("Request a script", this, [this]() { requestScript(); });
}

// The Scripts window: the switches, the saved scripts (with the editor), the
// methods and the runs of this session. ScriptsWidget is a plain widget; it
// fits into a settings dialog just as well.
void InventoryWindow::openScripts()
{
    if (!m_scriptsWindow) {
        m_scriptsWindow = new QWidget(this, Qt::Window);
        m_scriptsWindow->setObjectName("scriptsWindow");
        m_scriptsWindow->setWindowTitle("Scripts");
        m_scriptsWindow->resize(760, 520);
        auto* layout = new QVBoxLayout(m_scriptsWindow);
        auto* scripts = new ScriptHost::ScriptsWidget(m_scriptsWindow);
        // No OK / Cancel here: a click takes effect at once.
        scripts->setApplyImmediately(true);
        scripts->setScripting(m_engine);
        // The user's choices outlive the session (see step 6).
        connect(scripts, &ScriptHost::ScriptsWidget::changed, this, [this]() { saveScriptSettings(); });
        layout->addWidget(scripts);
        // The panel in it is hidden from scripts as ScriptHost's own; its
        // frame is the application's. Hidden too, so that scripts do not see
        // an empty window.
        ScriptHost::ScriptUi::hideWidget(m_scriptsWindow);
    }
    m_scriptsWindow->show();
    m_scriptsWindow->raise();
    m_scriptsWindow->activateWindow();
}

// The editor on its own, for an application that wants a "New script" command
// without the Scripts window. Save writes into the engine's library.
void InventoryWindow::newScript()
{
    auto* editor = new ScriptHost::ScriptEditorDialog(m_engine, this);
    editor->setAttribute(Qt::WA_DeleteOnClose);
    ScriptHost::ScriptLibrary::Script blank;
    blank.origin = "user";
    blank.code = "// The reference on the right lists what a script can call.\n"
                 "const items = inventory.list();\n"
                 "log('items: ' + items.length);\n"
                 "return items.filter(entry => entry.quantity < 50).map(entry => entry.item);\n";
    editor->setScript(blank);
    editor->show();
}

// The application runs a script itself: run(). No approval, the switch of
// requested runs does not apply - it is the application's own decision.
void InventoryWindow::runRestock(bool dryRun)
{
    const ScriptHost::ScriptLibrary::Script script = m_library->script(kRestockScript);
    if (!script.isValid()) {
        appendLog(QString("[run] there is no script '%1'").arg(kRestockScript));
        return;
    }
    ScriptHost::ScriptEngine::RunOptions options;
    options.name = script.name;
    options.args = QJsonObject{{"threshold", 50}, {"amount", 100}};
    // A dry run: inventory.list runs, inventory.setQuantity is only recorded
    // ("would_call" in the result).
    options.dryRun = dryRun;
    m_engine->run(script.code, options, [this](const QJsonObject& result) {
        if (result["status"].toString() != "ok")
            appendLog("[run] did not work: " + result["message"].toString());
    });
}

// Scripts that run until they are stopped: the stock monitor sets an
// interval timer and checks the stock on every tick; the input logger
// subscribes to key and mouse events with on(). Several runs go on at once,
// so the toolbar and the editor stay usable beside them; every run has an id
// (RunId), and the Scripts window lists the active ones, each with a Stop
// button.
void InventoryWindow::runUntilStopped(const QString& name, const QJsonObject& args)
{
    const ScriptHost::ScriptLibrary::Script script = m_library->script(name);
    if (!script.isValid()) {
        appendLog(QString("[run] there is no script '%1'").arg(name));
        return;
    }
    ScriptHost::ScriptEngine::RunOptions options;
    options.name = script.name;
    options.args = args;
    // The engine's Limits (5 minutes, 5000 calls) would end it. The
    // application lets its own script run on: 0 = no limit, -1 (the
    // default) = the engine's. requestRun() cannot do this.
    options.timeoutMs = 0;
    options.maxCalls = 0;
    const ScriptHost::ScriptEngine::RunId id = m_engine->run(script.code, options, [this, name](const QJsonObject& result) {
        if (result["status"].toString() != "ok")
            appendLog(QString("[run] '%1' ended: %2").arg(name, result["message"].toString()));
    });
    if (id != 0)
        appendLog(QString("[run] '%1' is run #%2; stop it in Scripts..., tab Runs").arg(name).arg(id));
}

// Another program asks: requestRun(). A real application would do this when
// a message arrives from a plugin, a remote connection or an assistant. The
// script is checked, then - it changes something, and such scripts need
// approval - shown to the user, then run. `done` always gets a result,
// which goes back to whoever asked.
void InventoryWindow::requestScript()
{
    ScriptHost::ScriptEngine::Request request;
    request.code = "const nuts = inventory.get('Nuts');\n"
                   "inventory.setQuantity('Nuts', nuts.quantity + 10);\n"
                   "return inventory.get('Nuts');\n";
    m_engine->requestRun(request, [this](const QJsonObject& result) {
        appendLog("[request] answer for the remote client: " + compact(result).left(400));
    });
}

void InventoryWindow::saveScriptSettings()
{
    // Only what the user may change is in there.
    QSettings().setValue(kScriptSettingsKey,
                         QJsonDocument(m_engine->userSettings()).toJson(QJsonDocument::Compact));
}

void InventoryWindow::appendLog(const QString& text)
{
    m_log->appendPlainText(text);
}
