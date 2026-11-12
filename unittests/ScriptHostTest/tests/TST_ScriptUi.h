#pragma once

// exposeWidgets(): scripts read a widget tree through the ui methods - the
// tree, ids, find/at/info/property, focus and tab order - and see nothing
// outside the root, nothing hidden, no ScriptHost widget and no password.
// They change it with the changing ui methods, and ui.restore() undoes what
// a run changed. They build dialogs with ui.create, run them with ui.exec
// and get widget signals as events; what a run made goes with it.

#include "UnitTest.h"
#include "TestSupport.h"
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QProgressBar>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QVBoxLayout>

class TST_ScriptUi : public UnitTest::Test
{
    TEST_CLASS(TST_ScriptUi)
public:
    TST_ScriptUi()
        : Test("TST_ScriptUi")
    {
        ADD_TEST(TST_ScriptUi::testTree);
        ADD_TEST(TST_ScriptUi::testIdsReachUnnamedWidgets);
        ADD_TEST(TST_ScriptUi::testFindAndAt);
        ADD_TEST(TST_ScriptUi::testInfoAndProperty);
        ADD_TEST(TST_ScriptUi::testFocusAndTabOrder);
        ADD_TEST(TST_ScriptUi::testHiddenAndOutsideWidgetsAreInvisible);
        ADD_TEST(TST_ScriptUi::testPasswordAndTextSwitch);
        ADD_TEST(TST_ScriptUi::testMethodsAreSwitchable);
        ADD_TEST(TST_ScriptUi::testGeometryAndVisibility);
        ADD_TEST(TST_ScriptUi::testFocusTabOrderAndCurrentIndex);
        ADD_TEST(TST_ScriptUi::testLayoutProperties);
        ADD_TEST(TST_ScriptUi::testMoveToLayoutAndReorder);
        ADD_TEST(TST_ScriptUi::testSetProperty);
        ADD_TEST(TST_ScriptUi::testRestore);
        ADD_TEST(TST_ScriptUi::testDryRunAndSwitchedOffTools);
        ADD_TEST(TST_ScriptUi::testCreateDialogTree);
        ADD_TEST(TST_ScriptUi::testCreateIntoLayoutAndClasses);
        ADD_TEST(TST_ScriptUi::testExecCloseAndDestroy);
        ADD_TEST(TST_ScriptUi::testSignals);
        ADD_TEST(TST_ScriptUi::testCreatedWidgetsGoAndSwitches);
    }

private:
    using Engine = ScriptHost::ScriptEngine;

    // window "window" (vbox, margins 10, spacing 6):
    //   nameEdit "Alice", passwordEdit "secret", statusLabel "Ready",
    //   buttons (hbox: okButton "OK", a stretch, an unnamed "Cancel" with stretch 2),
    //   internals (a group box with hiddenLabel)
    struct Window
    {
        QWidget      window;
        QLineEdit*   name = nullptr;
        QLineEdit*   password = nullptr;
        QLabel*      status = nullptr;
        QWidget*     buttons = nullptr;
        QPushButton* ok = nullptr;
        QPushButton* cancel = nullptr;
        QGroupBox*   internals = nullptr;
        QLabel*      hidden = nullptr;

        Window()
        {
            window.setObjectName("window");
            window.setWindowTitle("Main");
            auto* layout = new QVBoxLayout(&window);
            layout->setContentsMargins(10, 10, 10, 10);
            layout->setSpacing(6);
            name = new QLineEdit("Alice");
            name->setObjectName("nameEdit");
            password = new QLineEdit("secret");
            password->setObjectName("passwordEdit");
            password->setEchoMode(QLineEdit::Password);
            status = new QLabel("Ready");
            status->setObjectName("statusLabel");
            status->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            buttons = new QWidget;
            buttons->setObjectName("buttons");
            auto* row = new QHBoxLayout(buttons);
            ok = new QPushButton("OK");
            ok->setObjectName("okButton");
            ok->setToolTip("Accept");
            cancel = new QPushButton("Cancel");
            row->addWidget(ok);
            row->addStretch();
            row->addWidget(cancel, 2);
            internals = new QGroupBox("Internals");
            internals->setObjectName("internals");
            auto* inner = new QVBoxLayout(internals);
            hidden = new QLabel("hidden label");
            hidden->setObjectName("hiddenLabel");
            inner->addWidget(hidden);
            for (QWidget* w : std::initializer_list<QWidget*>{name, password, status, buttons, internals})
                layout->addWidget(w);
            window.resize(400, 300);
        }

        void show()
        {
            window.show();
            TestSupport::spin([this]() { return window.isVisible() && ok->isVisible(); });
        }
    };

    // More widgets for the changing tools, children of the window outside its
    // layout; made before Window::show(). "free" (10x10 at 20, 20), "grid"
    // (a grid layout: two labels in row 0), "split" (a splitter with two
    // widgets), "tabs" (three pages), "combo" (three items), "stack"
    // (children "a" below "b"), "colors" (a colour dialog, closed).
    struct Extras
    {
        QWidget*      free = nullptr;
        QWidget*      grid = nullptr;
        QGridLayout*  cells = nullptr;
        QSplitter*    split = nullptr;
        QTabWidget*   tabs = nullptr;
        QComboBox*    combo = nullptr;
        QWidget*      stack = nullptr;
        QWidget*      a = nullptr;
        QWidget*      b = nullptr;
        QColorDialog* colors = nullptr;

        explicit Extras(QWidget* window)
        {
            free = named(new QWidget(window), "free");
            free->setGeometry(20, 20, 10, 10);
            grid = named(new QWidget(window), "grid");
            grid->setGeometry(0, 0, 200, 60);
            cells = new QGridLayout(grid);
            cells->addWidget(new QLabel("one"), 0, 0);
            cells->addWidget(new QLabel("two"), 0, 1);
            split = named(new QSplitter(window), "split");
            split->addWidget(new QWidget);
            split->addWidget(new QWidget);
            split->setGeometry(0, 60, 300, 40);
            tabs = named(new QTabWidget(window), "tabs");
            for (const char* page : {"1", "2", "3"})
                tabs->addTab(new QWidget, page);
            tabs->setGeometry(0, 100, 200, 80);
            combo = named(new QComboBox(window), "combo");
            combo->addItems({"x", "y", "z"});
            combo->setGeometry(0, 180, 100, 20);
            stack = named(new QWidget(window), "stack");
            stack->setGeometry(0, 200, 40, 40);
            a = named(new QWidget(stack), "a");
            b = named(new QWidget(stack), "b");
            colors = new QColorDialog(window);
            colors->setObjectName("colors");
        }

        template <class T>
        static T* named(T* widget, const char* name)
        {
            widget->setObjectName(name);
            return widget;
        }
    };

    static QJsonObject run(Engine& engine, const QString& code, const TestSupport::Options& options = {})
    {
        return TestSupport::runScript(engine, code, options);
    }

    // "ok", or the error message.
    static QString done(Engine& engine, const QString& code)
    {
        const QJsonObject result = run(engine, code);
        return result["status"].toString() == "ok" ? QString("ok") : result["message"].toString();
    }

    // The message of a run that fails.
    static QString error(Engine& engine, const QString& code)
    {
        return run(engine, code)["message"].toString();
    }

    // A run that goes on after `code` - which logs "ready" at its end - until
    // the test posts "end" to it (and no other handler is left) or stops it.
    struct Live
    {
        QObject       context;
        Engine::RunId id = 0;
        bool          done = false;
        QJsonObject   result;
        QStringList   log;

        Live(Engine& engine, const QString& code)
        {
            QObject::connect(&engine, &Engine::runLog, &context, [this](Engine::RunId run, const QString& text) {
                if (run == id)
                    log << text;
            });
            id = engine.run("on('end', () => off('end'));\n" + code + "\nlog('ready');", Engine::RunOptions(),
                            [this](const QJsonObject& r) {
                                result = r;
                                done = true;
                            });
        }

        bool waitFor(const QString& line)
        {
            return TestSupport::spin([this, line]() { return log.contains(line) || done; }) && log.contains(line);
        }

        QJsonObject json(int line) const { return QJsonDocument::fromJson(log.value(line).toUtf8()).object(); }
    };

    static QJsonValue value(Engine& engine, const QString& code)
    {
        return run(engine, code)["result"];
    }

    static QJsonObject rectOf(QWidget* widget, QWidget* root)
    {
        const QPoint at = widget->mapTo(root, QPoint(0, 0));
        return QJsonObject{{"x", at.x()}, {"y", at.y()}, {"width", widget->width()}, {"height", widget->height()}};
    }

    static QStringList names(const QJsonArray& nodes)
    {
        QStringList list;
        for (const QJsonValue& node : nodes)
            list << node.toObject()["name"].toString();
        return list;
    }

    // Every name in the tree, depth first.
    static QStringList allNames(const QJsonObject& node)
    {
        QStringList list{node["name"].toString()};
        for (const QJsonValue& child : node["children"].toArray())
            list << allNames(child.toObject());
        return list;
    }

    static bool anyText(const QJsonObject& node)
    {
        if (node.contains("text"))
            return true;
        for (const QJsonValue& child : node["children"].toArray()) {
            if (anyText(child.toObject()))
                return true;
        }
        return false;
    }

    TEST_FUNCTION(testTree)
    {
        TEST_START;

        Engine engine;
        Window w;
        w.show();
        ScriptHost::exposeWidgets(&engine, &w.window);

        const QJsonObject result = run(engine, "return ui.tree();");
        TEST_COMPARE(result["status"].toString(), QString("ok"));
        // The whole tree is one call.
        TEST_COMPARE(result["calls"].toObject()["ui.tree"].toInt(), 1);
        const QJsonObject root = result["result"].toObject();
        TEST_COMPARE(root["name"].toString(), QString("window"));
        TEST_COMPARE(root["class"].toString(), QString("QWidget"));
        TEST_COMPARE(root["text"].toString(), QString("Main"));
        TEST_COMPARE(root["rect"].toObject(), (QJsonObject{{"x", 0}, {"y", 0}, {"width", 400}, {"height", 300}}));
        TEST_COMPARE(root["visible"].toBool(), true);
        const QJsonArray children = root["children"].toArray();
        TEST_COMPARE(names(children),
                     (QStringList{"nameEdit", "passwordEdit", "statusLabel", "buttons", "internals"}));

        const QJsonObject buttons = children[3].toObject();
        TEST_COMPARE(buttons["class"].toString(), QString("QWidget"));
        TEST_COMPARE(buttons["rect"].toObject(), rectOf(w.buttons, &w.window));
        const QJsonArray row = buttons["children"].toArray();
        TEST_COMPARE(names(row), (QStringList{"okButton", ""}));
        const QJsonObject ok = row[0].toObject();
        TEST_COMPARE(ok["class"].toString(), QString("QPushButton"));
        TEST_COMPARE(ok["text"].toString(), QString("OK"));
        TEST_COMPARE(ok["enabled"].toBool(), true);
        // In root coordinates, not in its parent's.
        TEST_COMPARE(ok["rect"].toObject(), rectOf(w.ok, &w.window));
        TEST_ASSERT(ok["rect"].toObject()["y"].toInt() > w.ok->y());
        TEST_COMPARE(ok["children"].toArray().size(), 0);
        TEST_COMPARE(names(children[4].toObject()["children"].toArray()), QStringList{"hiddenLabel"});

        // Depth: 0 is the node alone, 1 the children without theirs.
        const QJsonObject flat = value(engine, "return ui.tree('buttons', 0);").toObject();
        TEST_COMPARE(flat["name"].toString(), QString("buttons"));
        TEST_ASSERT(!flat.contains("children"));
        const QJsonObject one = value(engine, "return ui.tree(undefined, 1);").toObject();
        TEST_COMPARE(one["children"].toArray().size(), 5);
        TEST_ASSERT(!one["children"].toArray()[3].toObject().contains("children"));
    }

    TEST_FUNCTION(testIdsReachUnnamedWidgets)
    {
        TEST_START;

        Engine engine;
        Window w;
        ScriptHost::exposeWidgets(&engine, &w.window);

        // The unnamed button by its id, the same in every call and every run.
        const QJsonArray ids = value(engine,
            "const c = ui.children('buttons')[1];\n"
            "return [c.id, ui.info(c.id).text, ui.parent(c.id).name, ui.tree().children[3].children[1].id];")
            .toArray();
        TEST_COMPARE(ids.size(), 4);
        const int id = ids[0].toInt();
        TEST_ASSERT(id > 0);
        TEST_COMPARE(ids[1].toString(), QString("Cancel"));
        TEST_COMPARE(ids[2].toString(), QString("buttons"));
        TEST_COMPARE(ids[3].toInt(), id);
        TEST_COMPARE(value(engine, "return ui.children('buttons')[1].id;").toInt(), id);
        // Other widgets have other ids.
        const QJsonArray all = value(engine, "return ui.find({}).map(n => n.id);").toArray();
        QSet<int> distinct;
        for (const QJsonValue& v : all)
            distinct.insert(v.toInt());
        TEST_COMPARE(distinct.size(), all.size());
        TEST_COMPARE(all.size(), 9);

        // The id goes with its widget.
        delete w.cancel;
        const QJsonObject gone = run(engine, QString("return ui.info(%1);").arg(id));
        TEST_COMPARE(gone["status"].toString(), QString("error"));
        TEST_COMPARE(gone["message"].toString(), QString("ui.info: no widget with the id %1").arg(id));
        const QJsonObject unknown = run(engine, "return ui.info('nope');");
        TEST_COMPARE(unknown["message"].toString(), QString("ui.info: no widget 'nope'"));
    }

    TEST_FUNCTION(testFindAndAt)
    {
        TEST_START;

        Engine engine;
        Window w;
        w.show();
        ScriptHost::exposeWidgets(&engine, &w.window);

        auto found = [&](const QString& query) {
            return names(value(engine, "return ui.find(" + query + ");").toArray());
        };
        // Class by inheritance, in tree order.
        TEST_COMPARE(found("{class: 'QAbstractButton'}"), (QStringList{"okButton", ""}));
        TEST_COMPARE(found("{class: 'QLineEdit'}"), (QStringList{"nameEdit", "passwordEdit"}));
        // Name and text: a substring ignoring case, or exact.
        TEST_COMPARE(found("{name: 'edit'}"), (QStringList{"nameEdit", "passwordEdit"}));
        TEST_COMPARE(found("{name: 'edit', exact: true}"), QStringList());
        TEST_COMPARE(found("{name: 'nameEdit', exact: true}"), QStringList{"nameEdit"});
        TEST_COMPARE(found("{text: 'ok'}"), QStringList{"okButton"});
        TEST_COMPARE(found("{text: 'ok', exact: true}"), QStringList());
        TEST_COMPARE(found("{text: 'Cancel', class: 'QPushButton', exact: true}"), QStringList{""});
        // All criteria must match.
        TEST_COMPARE(found("{text: 'Ready', class: 'QPushButton'}"), QStringList());

        // At a point in root coordinates: the widget there.
        const QPoint okCenter = w.ok->mapTo(&w.window, w.ok->rect().center());
        TEST_COMPARE(value(engine, QString("return ui.at(%1, %2).name;").arg(okCenter.x()).arg(okCenter.y()))
                         .toString(), QString("okButton"));
        TEST_COMPARE(value(engine, "return ui.at(1, 1).name;").toString(), QString("window"));
        TEST_ASSERT(value(engine, "return ui.at(-1, 5);").isNull());
        TEST_ASSERT(value(engine, "return ui.at(400, 5);").isNull());
    }

    TEST_FUNCTION(testInfoAndProperty)
    {
        TEST_START;

        Engine engine;
        Window w;
        w.show();
        ScriptHost::exposeWidgets(&engine, &w.window);

        const QJsonArray ids = value(engine, "return [ui.find({name: 'buttons'})[0].id, "
                                             "ui.find({name: 'okButton'})[0].id, ui.children('buttons')[1].id];")
                                   .toArray();
        const int buttonsId = ids[0].toInt();
        const int okId = ids[1].toInt();
        const int cancelId = ids[2].toInt();

        const QJsonObject ok = value(engine, "return ui.info('okButton');").toObject();
        TEST_COMPARE(ok["name"].toString(), QString("okButton"));
        TEST_COMPARE(ok["geometry"].toObject(), (QJsonObject{{"x", w.ok->x()}, {"y", w.ok->y()},
                                                             {"width", w.ok->width()}, {"height", w.ok->height()}}));
        TEST_COMPARE(ok["sizeHint"].toObject(),
                     (QJsonObject{{"width", w.ok->sizeHint().width()}, {"height", w.ok->sizeHint().height()}}));
        TEST_COMPARE(ok["minimumSize"].toObject(), (QJsonObject{{"width", 0}, {"height", 0}}));
        TEST_COMPARE(ok["maximumSize"].toObject()["width"].toInt(), QWIDGETSIZE_MAX);
        TEST_COMPARE(ok["sizePolicy"].toObject(), (QJsonObject{{"horizontal", "Minimum"}, {"vertical", "Fixed"},
                                                               {"horizontalStretch", 0}, {"verticalStretch", 0}}));
        TEST_COMPARE(ok["toolTip"].toString(), QString("Accept"));
        TEST_COMPARE(ok["parentLayout"].toObject(), (QJsonObject{{"owner", buttonsId}, {"type", "QHBoxLayout"},
                                                                 {"index", 0}, {"stretch", 0}}));
        TEST_ASSERT(ok["layout"].isNull());

        const QJsonObject row = value(engine, "return ui.info('buttons').layout;").toObject();
        TEST_COMPARE(row["type"].toString(), QString("QHBoxLayout"));
        const QJsonArray items = row["items"].toArray();
        TEST_COMPARE(items.size(), 3);
        TEST_COMPARE(items[0].toObject(), (QJsonObject{{"index", 0}, {"stretch", 0}, {"widget", okId},
                                                       {"name", "okButton"}}));
        TEST_COMPARE(items[1].toObject()["index"].toInt(), 1);
        TEST_ASSERT(items[1].toObject().contains("spacer"));
        TEST_COMPARE(items[2].toObject(), (QJsonObject{{"index", 2}, {"stretch", 2}, {"widget", cancelId},
                                                       {"name", ""}}));

        const QJsonObject window = value(engine, "return ui.info('window');").toObject();
        TEST_ASSERT(window["parentLayout"].isNull());
        const QJsonObject column = window["layout"].toObject();
        TEST_COMPARE(column["type"].toString(), QString("QVBoxLayout"));
        TEST_COMPARE(column["spacing"].toInt(), 6);
        TEST_COMPARE(column["margins"].toObject(),
                     (QJsonObject{{"left", 10}, {"top", 10}, {"right", 10}, {"bottom", 10}}));
        TEST_COMPARE(column["items"].toArray().size(), 5);

        // Properties as JSON.
        auto property = [&](const QString& widget, const QString& name) {
            return value(engine, "return ui.property('" + widget + "', '" + name + "');");
        };
        TEST_COMPARE(property("okButton", "text").toString(), QString("OK"));
        TEST_COMPARE(property("okButton", "enabled").toBool(), true);
        TEST_COMPARE(property("okButton", "geometry").toObject(), ok["geometry"].toObject());
        TEST_COMPARE(property("okButton", "focusPolicy").toString(), QString("StrongFocus"));
        TEST_COMPARE(property("nameEdit", "echoMode").toString(), QString("Normal"));
        TEST_COMPARE(property("nameEdit", "maxLength").toInt(), 32767);
        const QString alignment = property("statusLabel", "alignment").toString();
        // Qt names the bit of AlignRight by its alias AlignTrailing.
        TEST_ASSERT((alignment.contains("AlignRight") || alignment.contains("AlignTrailing"))
                    && alignment.contains("AlignVCenter"));
        TEST_COMPARE(property("okButton", "font").toObject()["family"].toString(), w.ok->font().family());
        TEST_COMPARE(property("okButton", "sizePolicy").toObject(), ok["sizePolicy"].toObject());
        const QJsonObject missing = run(engine, "return ui.property('okButton', 'nope');");
        TEST_COMPARE(missing["message"].toString(), QString("ui.property: a QPushButton has no property 'nope'"));
    }

    TEST_FUNCTION(testFocusAndTabOrder)
    {
        TEST_START;

        Engine engine;
        Window w;
        w.show();
        ScriptHost::exposeWidgets(&engine, &w.window);

        w.name->setFocus();
        TestSupport::spin([&]() { return w.window.focusWidget() == w.name; }, 1000);
        TEST_COMPARE(w.window.focusWidget(), static_cast<QWidget*>(w.name));
        const QJsonObject focused = value(engine, "return ui.focused();").toObject();
        TEST_COMPARE(focused["name"].toString(), QString("nameEdit"));
        TEST_COMPARE(focused["focus"].toBool(), true);

        // The widgets that take the focus by Tab, in tab order; labels do not.
        QWidget::setTabOrder(w.cancel, w.ok);
        const QJsonArray order = value(engine, "return ui.tabOrder();").toArray();
        const QStringList tabbed = names(order);
        TEST_ASSERT(tabbed.contains("nameEdit") && tabbed.contains("passwordEdit") && tabbed.contains("okButton"));
        TEST_ASSERT(!tabbed.contains("statusLabel") && !tabbed.contains("hiddenLabel"));
        const int okAt = tabbed.indexOf("okButton");
        TEST_ASSERT(okAt > 0);
        TEST_COMPARE(order[okAt - 1].toObject()["text"].toString(), QString("Cancel"));
        // Of the window of a widget given.
        TEST_COMPARE(names(value(engine, "return ui.tabOrder('okButton');").toArray()), tabbed);
    }

    TEST_FUNCTION(testHiddenAndOutsideWidgetsAreInvisible)
    {
        TEST_START;

        Engine engine;
        Window w;
        w.show();
        ScriptHost::attachCanvas(&engine, &w.window);
        ScriptHost::exposeWidgets(&engine, &w.window);

        // The canvas overlay is a child of the root, but not for scripts.
        const QJsonObject before = value(engine, "return ui.tree();").toObject();
        TEST_COMPARE(names(before["children"].toArray()),
                     (QStringList{"nameEdit", "passwordEdit", "statusLabel", "buttons", "internals"}));
        const QJsonArray classes = value(engine, "return ui.find({class: 'QWidget'}).map(n => n.class);").toArray();
        TEST_COMPARE(classes.size(), 9);
        for (const QJsonValue& c : classes)
            TEST_ASSERT(!c.toString().startsWith("ScriptHost::"));

        // Hidden by the application, with what is below: by hideWidget() or
        // by the property.
        const QPoint label = w.hidden->mapTo(&w.window, w.hidden->rect().center());
        ScriptHost::ScriptUi::hideWidget(w.internals);
        w.status->setProperty("scripthost_hidden", true);
        const QStringList seen = allNames(value(engine, "return ui.tree();").toObject());
        TEST_COMPARE(seen, (QStringList{"window", "nameEdit", "passwordEdit", "buttons", "okButton", ""}));
        TEST_COMPARE(names(value(engine, "return ui.find({name: 'label'});").toArray()), QStringList());
        TEST_COMPARE(run(engine, "return ui.info('hiddenLabel');")["message"].toString(),
                     QString("ui.info: no widget 'hiddenLabel'"));
        // At a point over it: the widget above it.
        TEST_COMPARE(value(engine, QString("return ui.at(%1, %2).name;").arg(label.x()).arg(label.y())).toString(),
                     QString("window"));
        // Its layout item is left out; the others keep their index.
        const QJsonArray items = value(engine, "return ui.info('window').layout.items;").toArray();
        QList<int> indexes;
        for (const QJsonValue& item : items)
            indexes << item.toObject()["index"].toInt();
        TEST_COMPARE(indexes, (QList<int>{0, 1, 3}));
        // And back.
        ScriptHost::ScriptUi::hideWidget(w.internals, false);
        TEST_COMPARE(value(engine, "return ui.find({name: 'hiddenLabel'}).length;").toInt(), 1);

        // Another window: invisible, by name and by id - even the id it has
        // from another engine's ui.
        QWidget other;
        auto* outside = new QPushButton("Outside", &other);
        outside->setObjectName("outsideButton");
        Engine second;
        ScriptHost::exposeWidgets(&second, &other);
        const int outsideId = value(second, "return ui.find({name: 'outsideButton'})[0].id;").toInt();
        TEST_ASSERT(outsideId > 0);
        TEST_COMPARE(names(value(engine, "return ui.find({name: 'outside'});").toArray()), QStringList());
        TEST_COMPARE(run(engine, "return ui.info('outsideButton');")["message"].toString(),
                     QString("ui.info: no widget 'outsideButton'"));
        TEST_COMPARE(run(engine, QString("return ui.info(%1);").arg(outsideId))["message"].toString(),
                     QString("ui.info: no widget with the id %1").arg(outsideId));
        // The root has no parent for scripts.
        TEST_ASSERT(value(engine, "return ui.parent('window');").isNull());
    }

    TEST_FUNCTION(testPasswordAndTextSwitch)
    {
        TEST_START;

        Engine engine;
        Window w;
        ScriptHost::ScriptUi* ui = ScriptHost::exposeWidgets(&engine, &w.window);
        TEST_ASSERT(ui->isTextReadable());

        // A password's text is never returned.
        const QJsonArray fields = value(engine, "return ui.tree().children.slice(0, 2);").toArray();
        TEST_COMPARE(fields[0].toObject()["text"].toString(), QString("Alice"));
        TEST_ASSERT(!fields[1].toObject().contains("text"));
        TEST_ASSERT(!value(engine, "return ui.info('passwordEdit');").toObject().contains("text"));
        TEST_COMPARE(value(engine, "return ui.find({text: 'secret'}).length;").toInt(), 0);
        for (const QString& name : {"text", "displayText", "selectedText"}) {
            TEST_COMPARE(run(engine, "return ui.property('passwordEdit', '" + name + "');")["message"].toString(),
                         QString("ui.property: the application does not let scripts read '%1' of this widget")
                             .arg(name));
        }
        TEST_COMPARE(value(engine, "return ui.property('passwordEdit', 'echoMode');").toString(),
                     QString("Password"));
        // Another echo mode is a password too.
        w.password->setEchoMode(QLineEdit::PasswordEchoOnEdit);
        TEST_ASSERT(run(engine, "return ui.property('passwordEdit', 'text');")["status"].toString() == "error");

        // The switch: no text at all.
        ui->setTextReadable(false);
        TEST_ASSERT(!anyText(value(engine, "return ui.tree();").toObject()));
        const QJsonObject ok = value(engine, "return ui.info('okButton');").toObject();
        TEST_ASSERT(!ok.contains("text") && !ok.contains("toolTip"));
        TEST_COMPARE(run(engine, "return ui.property('okButton', 'text');")["message"].toString(),
                     QString("ui.property: the application does not let scripts read 'text' of this widget"));
        TEST_COMPARE(run(engine, "return ui.property('window', 'windowTitle');")["status"].toString(),
                     QString("error"));
        TEST_COMPARE(run(engine, "return ui.find({text: 'OK'});")["message"].toString(),
                     QString("ui.find: the application does not let scripts read widget text"));
        // Names and other properties stay readable.
        TEST_COMPARE(value(engine, "return ui.property('okButton', 'objectName');").toString(), QString("okButton"));
        TEST_COMPARE(value(engine, "return ui.property('okButton', 'enabled');").toBool(), true);
        TEST_COMPARE(names(value(engine, "return ui.find({name: 'ok'});").toArray()), QStringList{"okButton"});

        ui->setTextReadable(true);
        TEST_COMPARE(value(engine, "return ui.property('okButton', 'text');").toString(), QString("OK"));
    }

    TEST_FUNCTION(testMethodsAreSwitchable)
    {
        TEST_START;

        Engine engine;
        auto* w = new Window;
        QPointer<ScriptHost::ScriptUi> ui = ScriptHost::exposeWidgets(&engine, &w->window);

        // The read-only methods and the changing ones, all in the group "UI".
        // One line per task, so that parallel work merges.
        QStringList expectedReading{"ui.at", "ui.children", "ui.connect", "ui.disconnect", "ui.find",
                                    "ui.focused", "ui.info", "ui.parent", "ui.property", "ui.root",
                                    "ui.tabOrder", "ui.tree"};
        expectedReading << "ui.items" << "ui.itemAt";   // T-025
        expectedReading << "ui.map" << "ui.window";     // T-027
        expectedReading.sort();
        QStringList reading, changing;
        for (const ScriptHost::ScriptMethod& m : engine.methods()) {
            if (!m.name().startsWith("ui."))
                continue;
            (m.isReadOnly() ? reading : changing) << m.name();
            TEST_COMPARE(m.group(), QString("UI"));
        }
        reading.sort();
        TEST_COMPARE(reading, expectedReading);
        TEST_COMPARE(changing, (QStringList{"ui.close", "ui.create", "ui.destroy", "ui.exec", "ui.hide", "ui.lower",
                                            "ui.move", "ui.moveToLayout", "ui.raise",
                                            "ui.reorder", "ui.resize", "ui.restore", "ui.setCurrentIndex",
                                            "ui.setEnabled", "ui.setFocus", "ui.setGeometry", "ui.setLayout",
                                            "ui.setProperty", "ui.setSplitterSizes", "ui.setStretch",
                                            "ui.setTabOrder", "ui.show"}));
        TEST_ASSERT(engine.apiDefinition("ui.tree").contains("tree(widget?: number | string, depth?: number)"));
        TEST_ASSERT(engine.apiDefinition().contains("ui.at - "));

        // One switched off; the others work.
        engine.setMethodEnabled("ui.at", false);
        const Engine::Check check = engine.check("return ui.at(1, 1);");
        TEST_ASSERT(!check.ok && check.message.contains("disabled"));
        TEST_COMPARE(run(engine, "return ui.at(1, 1);")["status"].toString(), QString("error"));
        TEST_ASSERT(!engine.apiDefinition().contains("ui.at - "));
        TEST_COMPARE(value(engine, "return ui.root().name;").toString(), QString("window"));
        engine.setMethodEnabled("ui.at", true);
        TEST_COMPARE(run(engine, "return ui.at(1, 1).name;")["status"].toString(), QString("ok"));

        // A requested run reads like with any read-only method.
        const QJsonObject requested = TestSupport::request(engine,
                                                           TestSupport::codeRequest("return ui.root().name;"));
        TEST_COMPARE(requested["result"].toString(), QString("window"));

        // Deleting the ScriptUi, or the root, ends it.
        delete ui;
        TEST_ASSERT(!engine.hasMethod("ui.tree"));
        ui = ScriptHost::exposeWidgets(&engine, &w->window);
        TEST_ASSERT(engine.hasMethod("ui.tree"));
        delete w;
        TEST_ASSERT(ui.isNull());
        TEST_ASSERT(!engine.hasMethod("ui.tree"));
    }

    TEST_FUNCTION(testGeometryAndVisibility)
    {
        TEST_START;

        Engine engine;
        Window w;
        Extras x(&w.window);
        w.show();
        ScriptHost::exposeWidgets(&engine, &w.window);
        const QString ok("ok");

        // In the parent's coordinates; changes stay after the run.
        const QJsonObject moved = value(engine, "return ui.move('free', 5, 6);").toObject();
        TEST_COMPARE(x.free->pos(), QPoint(5, 6));
        TEST_COMPARE(moved["name"].toString(), QString("free"));
        TEST_COMPARE(moved["rect"].toObject()["x"].toInt(), 5);
        TEST_ASSERT(moved.contains("managedByLayout") && !moved["managedByLayout"].toBool());
        TEST_COMPARE(done(engine, "ui.resize('free', 30, 40);"), ok);
        TEST_COMPARE(x.free->size(), QSize(30, 40));
        TEST_COMPARE(done(engine, "ui.setGeometry('free', 1, 2, 3, 4);"), ok);
        TEST_COMPARE(x.free->geometry(), QRect(1, 2, 3, 4));
        // A widget a layout places says so: the layout may put it back.
        TEST_ASSERT(value(engine, "return ui.move('okButton', 0, 0).managedByLayout;").toBool());
        TEST_ASSERT(value(engine, "return ui.setGeometry('okButton', 0, 0, 5, 5).managedByLayout;").toBool());

        TEST_COMPARE(done(engine, "ui.hide('okButton'); ui.setEnabled('buttons', false);"), ok);
        TEST_ASSERT(w.ok->isHidden() && !w.cancel->isEnabled());
        // A hidden widget is still there for scripts.
        const QJsonObject shown = value(engine, "ui.setEnabled('buttons', true); return ui.show('okButton');")
                                      .toObject();
        TEST_ASSERT(!w.ok->isHidden() && w.cancel->isEnabled());
        TEST_COMPARE(shown["visible"].toBool(), true);

        TEST_COMPARE(done(engine, "ui.raise('a');"), ok);
        TEST_COMPARE(x.stack->children(), (QObjectList{x.b, x.a}));
        TEST_COMPARE(done(engine, "ui.lower('a');"), ok);
        TEST_COMPARE(x.stack->children(), (QObjectList{x.a, x.b}));
    }

    TEST_FUNCTION(testFocusTabOrderAndCurrentIndex)
    {
        TEST_START;

        Engine engine;
        Window w;
        Extras x(&w.window);
        w.show();
        ScriptHost::exposeWidgets(&engine, &w.window);
        const QString ok("ok");

        TEST_COMPARE(done(engine, "ui.setFocus('passwordEdit');"), ok);
        TestSupport::spin([&]() { return w.window.focusWidget() == w.password; }, 1000);
        TEST_COMPARE(w.window.focusWidget(), static_cast<QWidget*>(w.password));
        TEST_COMPARE(value(engine, "return ui.focused().name;").toString(), QString("passwordEdit"));

        // Tab goes from each to the next; returns the new tab order.
        const QJsonArray order = value(engine, "const cancel = ui.find({text: 'Cancel'})[0].id;"
                                               "return ui.setTabOrder(['passwordEdit', 'okButton', cancel, 'nameEdit']);")
                                     .toArray();
        const QStringList tabbed = names(order);
        const int from = tabbed.indexOf("passwordEdit");
        TEST_ASSERT(from >= 0);
        QStringList cycle;
        for (int i = 0; i < 4; ++i)
            cycle << tabbed.value((from + i) % tabbed.size());
        TEST_COMPARE(cycle, (QStringList{"passwordEdit", "okButton", "", "nameEdit"}));
        TEST_COMPARE(error(engine, "ui.setTabOrder(['okButton', 'okButton']);"),
                     QString("ui.setTabOrder: a widget is in the list twice"));
        TEST_COMPARE(error(engine, "ui.setTabOrder(['okButton', 'colors']);"),
                     QString("ui.setTabOrder: the widgets are not in one window"));
        TEST_COMPARE(error(engine, "ui.setTabOrder(['okButton']);"),
                     QString("ui.setTabOrder: takes two widgets or more"));

        TEST_COMPARE(done(engine, "ui.setCurrentIndex('tabs', 2); ui.setCurrentIndex('combo', 1);"), ok);
        TEST_COMPARE(x.tabs->currentIndex(), 2);
        TEST_COMPARE(x.combo->currentIndex(), 1);
        TEST_COMPARE(value(engine, "return ui.setCurrentIndex('combo', 2).text;").toString(), QString("z"));
        TEST_COMPARE(error(engine, "ui.setCurrentIndex('tabs', 3);"),
                     QString("ui.setCurrentIndex: the index 3 is not between 0 and 2"));
        TEST_COMPARE(error(engine, "ui.setCurrentIndex('okButton', 0);"),
                     QString("ui.setCurrentIndex: a QPushButton has no current index"));

        // All three undone.
        const QJsonValue before = value(engine, "return ui.tabOrder().map(n => n.id);");
        const QJsonObject back = value(engine, "ui.setFocus('okButton'); ui.setTabOrder(['nameEdit', 'okButton']);"
                                               "ui.setCurrentIndex('tabs', 0); return ui.restore();")
                                     .toObject();
        TEST_COMPARE(back["restored"].toInt(), 3);
        TestSupport::spin([&]() { return w.window.focusWidget() == w.password; }, 1000);
        TEST_COMPARE(w.window.focusWidget(), static_cast<QWidget*>(w.password));
        TEST_COMPARE(value(engine, "return ui.tabOrder().map(n => n.id);"), before);
        TEST_COMPARE(x.tabs->currentIndex(), 2);
    }

    TEST_FUNCTION(testLayoutProperties)
    {
        TEST_START;

        Engine engine;
        Window w;
        Extras x(&w.window);
        w.show();
        ScriptHost::exposeWidgets(&engine, &w.window);
        const QString ok("ok");
        QLayout* outer = w.window.layout();
        auto* row = static_cast<QBoxLayout*>(w.buttons->layout());

        const QJsonObject layout = value(engine, "return ui.setLayout('window', "
                                                 "{spacing: 12, margins: {left: 3}, alignment: 'AlignTop'});")
                                       .toObject();
        TEST_COMPARE(outer->spacing(), 12);
        TEST_ASSERT(outer->contentsMargins() == QMargins(3, 10, 10, 10));
        TEST_ASSERT(outer->alignment() == Qt::AlignTop);
        TEST_COMPARE(layout["spacing"].toInt(), 12);
        TEST_COMPARE(done(engine, "ui.setLayout('buttons', {margins: 0, direction: 'RightToLeft'});"), ok);
        TEST_ASSERT(row->direction() == QBoxLayout::RightToLeft && row->contentsMargins() == QMargins());
        TEST_COMPARE(error(engine, "ui.setLayout('okButton', {spacing: 1});"),
                     QString("ui.setLayout: the widget has no layout"));
        TEST_COMPARE(error(engine, "ui.setLayout('grid', {direction: 'LeftToRight'});"),
                     QString("ui.setLayout: only a box layout has a direction"));
        TEST_COMPARE(error(engine, "ui.setLayout('window', {alignment: 'AlignSideways'});"),
                     QString("ui.setLayout: 'AlignSideways' is not an alignment"));

        // A box item's stretch; a grid's row or column stretch.
        TEST_ASSERT(value(engine, "return ui.setStretch('okButton', 3).managedByLayout;").toBool());
        TEST_COMPARE(row->stretch(row->indexOf(w.ok)), 3);
        TEST_COMPARE(done(engine, "ui.setStretch('grid', 4, {column: 1});"), ok);
        TEST_COMPARE(x.cells->columnStretch(1), 4);
        TEST_ASSERT(error(engine, "ui.setStretch('free', 1);").startsWith("ui.setStretch: the widget is not in a box layout"));
        TEST_COMPARE(error(engine, "ui.setStretch('grid', 1, {row: 0, column: 0});"),
                     QString("ui.setStretch: 'at' takes {row} or {column}"));

        const QJsonArray taken = value(engine, "return ui.setSplitterSizes('split', [50, 150]);").toArray();
        TEST_COMPARE(taken.size(), 2);
        TEST_ASSERT(taken[0].toInt() < taken[1].toInt());
        const QList<int> sizes = x.split->sizes();
        TEST_COMPARE(sizes, (QList<int>{taken[0].toInt(), taken[1].toInt()}));
        TEST_COMPARE(error(engine, "ui.setSplitterSizes('split', [1, 2, 3]);"),
                     QString("ui.setSplitterSizes: the splitter has 2 widgets"));
        TEST_COMPARE(error(engine, "ui.setSplitterSizes('free', [1]);"),
                     QString("ui.setSplitterSizes: the widget is not a QSplitter"));

        // All undone: back to what the runs before left.
        const QJsonObject back = value(engine,
            "ui.setLayout('window', {spacing: 0, margins: 1, alignment: 'AlignBottom'});"
            "ui.setLayout('buttons', {direction: 'TopToBottom'}); ui.setStretch('okButton', 0);"
            "ui.setStretch('grid', 0, {column: 1}); ui.setSplitterSizes('split', [150, 50]);"
            "return ui.restore();").toObject();
        TEST_COMPARE(back["restored"].toInt(), 5);
        TEST_ASSERT(outer->spacing() == 12 && outer->contentsMargins() == QMargins(3, 10, 10, 10)
                    && outer->alignment() == Qt::AlignTop);
        TEST_ASSERT(row->direction() == QBoxLayout::RightToLeft);
        TEST_COMPARE(row->stretch(row->indexOf(w.ok)), 3);
        TEST_COMPARE(x.cells->columnStretch(1), 4);
        TEST_COMPARE(x.split->sizes(), sizes);
    }

    TEST_FUNCTION(testMoveToLayoutAndReorder)
    {
        TEST_START;

        Engine engine;
        Window w;
        Extras x(&w.window);
        w.show();
        ScriptHost::exposeWidgets(&engine, &w.window);
        const QString ok("ok");
        auto* outer = static_cast<QBoxLayout*>(w.window.layout());
        auto* row = static_cast<QBoxLayout*>(w.buttons->layout());

        // Into a grid cell, reparented.
        const QJsonObject moved = value(engine, "return ui.moveToLayout('nameEdit', 'grid', "
                                                "{row: 1, column: 0, columnSpan: 2});").toObject();
        TEST_COMPARE(w.name->parentWidget(), x.grid);
        TEST_ASSERT(x.cells->itemAtPosition(1, 1) && x.cells->itemAtPosition(1, 1)->widget() == w.name);
        TEST_COMPARE(outer->indexOf(w.name), -1);
        TEST_ASSERT(moved["managedByLayout"].toBool());
        // Into a box at an index, or at the end; a widget no layout placed too.
        TEST_COMPARE(done(engine, "ui.moveToLayout('statusLabel', 'buttons', {index: 0});"
                                  "ui.moveToLayout('free', 'buttons');"), ok);
        TEST_COMPARE(row->indexOf(w.status), 0);
        TEST_COMPARE(row->indexOf(x.free), row->count() - 1);
        TEST_COMPARE(x.free->parentWidget(), w.buttons);
        TEST_COMPARE(error(engine, "ui.moveToLayout('window', 'buttons');"),
                     QString("ui.moveToLayout: the root or a window cannot move into a layout"));
        TEST_COMPARE(error(engine, "ui.moveToLayout('okButton', 'passwordEdit');"),
                     QString("ui.moveToLayout: the target has no box, grid or form layout"));
        TEST_COMPARE(error(engine, "ui.moveToLayout('buttons', 'buttons');"),
                     QString("ui.moveToLayout: a widget cannot move into itself"));
        TEST_COMPARE(error(engine, "ui.moveToLayout('okButton', 'grid', {index: 0});"),
                     QString("ui.moveToLayout: a grid layout takes {row, column, rowSpan?, columnSpan?}"));
        TEST_COMPARE(error(engine, "ui.moveToLayout('okButton', 'window', {index: 9});"),
                     QString("ui.moveToLayout: the index 9 is not between 0 and %1").arg(outer->count()));

        // Reordered in its box layout: [Cancel, statusLabel, okButton, stretch, free]; the stretch goes along.
        TEST_COMPARE(done(engine, "ui.reorder(ui.find({text: 'Cancel'})[0].id, 0);"), ok);
        TEST_COMPARE(row->indexOf(w.cancel), 0);
        TEST_COMPARE(row->stretch(0), 2);
        TEST_COMPARE(row->indexOf(w.status), 1);
        TEST_COMPARE(error(engine, "ui.reorder('okButton', 5);"),
                     QString("ui.reorder: the index 5 is not between 0 and 4"));
        TEST_COMPARE(error(engine, "ui.reorder('nameEdit', 0);"),
                     QString("ui.reorder: the widget is not in a box layout"));

        // All undone: each back in its layout and place, with its stretch and parent.
        const QJsonObject back = value(engine,
            "ui.moveToLayout('nameEdit', 'window', {index: 0}); ui.moveToLayout('free', 'grid', {row: 2, column: 0});"
            "ui.reorder('okButton', 0); ui.moveToLayout(ui.find({text: 'Cancel'})[0].id, 'window');"
            "return ui.restore();").toObject();
        TEST_COMPARE(back["restored"].toInt(), 4);
        TEST_COMPARE(w.name->parentWidget(), x.grid);
        TEST_ASSERT(x.cells->itemAtPosition(1, 1) && x.cells->itemAtPosition(1, 1)->widget() == w.name);
        TEST_COMPARE(outer->indexOf(w.name), -1);
        TEST_COMPARE(x.free->parentWidget(), w.buttons);
        TEST_COMPARE(x.cells->indexOf(x.free), -1);
        TEST_COMPARE(w.cancel->parentWidget(), w.buttons);
        TEST_COMPARE(row->indexOf(w.cancel), 0);
        TEST_COMPARE(row->stretch(0), 2);
        TEST_COMPARE(row->indexOf(w.status), 1);
        TEST_COMPARE(row->indexOf(w.ok), 2);
        TEST_COMPARE(row->indexOf(x.free), 4);
    }

    TEST_FUNCTION(testSetProperty)
    {
        TEST_START;

        Engine engine;
        Window w;
        Extras x(&w.window);
        w.show();
        ScriptHost::exposeWidgets(&engine, &w.window);
        const QString ok("ok");
        const auto set = [&engine](const QString& widget, const QString& name, const QString& json) {
            return done(engine, QString("ui.setProperty('%1', '%2', %3);").arg(widget, name, json));
        };

        TEST_COMPARE(set("nameEdit", "placeholderText", "'Your name'"), ok);
        TEST_COMPARE(w.name->placeholderText(), QString("Your name"));
        TEST_COMPARE(set("nameEdit", "readOnly", "true"), ok);
        TEST_ASSERT(w.name->isReadOnly());
        TEST_COMPARE(set("nameEdit", "maxLength", "12"), ok);
        TEST_COMPARE(w.name->maxLength(), 12);
        TEST_COMPARE(set("nameEdit", "echoMode", "'Password'"), ok);
        TEST_ASSERT(w.name->echoMode() == QLineEdit::Password);
        TEST_COMPARE(set("passwordEdit", "echoMode", "0"), ok);
        TEST_ASSERT(w.password->echoMode() == QLineEdit::Normal);
        TEST_COMPARE(set("statusLabel", "alignment", "'AlignLeft|AlignTop'"), ok);
        TEST_ASSERT(w.status->alignment() == (Qt::AlignLeft | Qt::AlignTop));
        TEST_COMPARE(set("free", "minimumSize", "{width: 40, height: 25}"), ok);
        TEST_COMPARE(x.free->minimumSize(), QSize(40, 25));
        TEST_COMPARE(set("free", "maximumSize", "'50x30'"), ok);
        TEST_COMPARE(x.free->maximumSize(), QSize(50, 30));
        TEST_COMPARE(set("free", "pos", "{x: 7, y: 8}"), ok);
        TEST_COMPARE(x.free->pos(), QPoint(7, 8));
        TEST_COMPARE(set("free", "geometry", "'1, 2, 45, 28'"), ok);
        TEST_COMPARE(x.free->geometry(), QRect(1, 2, 45, 28));
        TEST_COMPARE(set("colors", "currentColor", "'#336699'"), ok);
        TEST_COMPARE(x.colors->currentColor(), QColor("#336699"));
        TEST_COMPARE(set("colors", "currentColor", "{r: 10, g: 20, b: 30}"), ok);
        TEST_COMPARE(x.colors->currentColor(), QColor(10, 20, 30));
        TEST_COMPARE(set("okButton", "font", "{bold: true, pointSize: 15}"), ok);
        TEST_ASSERT(w.ok->font().bold() && w.ok->font().pointSize() == 15);
        const QJsonObject node = value(engine, "return ui.setProperty('okButton', 'text', 'Apply');").toObject();
        TEST_COMPARE(w.ok->text(), QString("Apply"));
        TEST_COMPARE(node["text"].toString(), QString("Apply"));

        TEST_COMPARE(set("nameEdit", "maxLength", "'long'"), QString("ui.setProperty: 'maxLength' takes a number"));
        TEST_COMPARE(set("free", "minimumSize", "'big'"),
                     QString("ui.setProperty: 'minimumSize' takes {width, height} or \"width x height\""));
        TEST_COMPARE(set("colors", "currentColor", "'no colour'"),
                     QString("ui.setProperty: 'currentColor' takes a colour: \"#rrggbb\", \"#aarrggbb\", a name or "
                             "{r, g, b, a?}"));
        TEST_COMPARE(set("nameEdit", "echoMode", "'Loud'"), QString("ui.setProperty: 'Loud' is not a value of EchoMode"));
        TEST_COMPARE(set("nameEdit", "width", "5"), QString("ui.setProperty: 'width' of a QLineEdit cannot be set"));
        TEST_COMPARE(set("nameEdit", "nope", "5"), QString("ui.setProperty: a QLineEdit has no property 'nope'"));
        TEST_COMPARE(w.name->maxLength(), 12);
        // Hidden from scripts: no such widget.
        ScriptHost::ScriptUi::hideWidget(w.internals);
        TEST_COMPARE(set("hiddenLabel", "text", "'x'"), QString("ui.setProperty: no widget 'hiddenLabel'"));
        TEST_COMPARE(w.hidden->text(), QString("hidden label"));

        // Undone, the latest first: the first value of a property comes back.
        const QJsonObject back = value(engine,
            "ui.setProperty('okButton', 'text', 'Go'); ui.setProperty('okButton', 'text', 'Stop');"
            "ui.setProperty('statusLabel', 'alignment', 'AlignCenter'); ui.setProperty('colors', 'currentColor', 'red');"
            "ui.setProperty('nameEdit', 'echoMode', 'Normal'); return ui.restore();").toObject();
        TEST_COMPARE(back["restored"].toInt(), 5);
        TEST_COMPARE(w.ok->text(), QString("Apply"));
        TEST_ASSERT(w.status->alignment() == (Qt::AlignLeft | Qt::AlignTop));
        TEST_COMPARE(x.colors->currentColor(), QColor(10, 20, 30));
        TEST_ASSERT(w.name->echoMode() == QLineEdit::Password);
    }

    TEST_FUNCTION(testRestore)
    {
        TEST_START;

        Engine engine;
        Window w;
        Extras x(&w.window);
        w.show();
        ScriptHost::exposeWidgets(&engine, &w.window);
        QPointer<QWidget> dropped = x.free;
        engine.registerMethod(ScriptHost::ScriptMethod("test.drop"), [&dropped](const QJsonObject&) -> QJsonValue {
            delete dropped;
            return true;
        });
        auto* row = static_cast<QBoxLayout*>(w.buttons->layout());

        // A mix, undone the latest first; the widget deleted meanwhile is skipped.
        const QJsonObject result = value(engine,
            "ui.move('free', 1, 1); ui.hide('okButton'); ui.setEnabled('nameEdit', false);"
            "ui.setProperty('statusLabel', 'text', 'Busy'); ui.setCurrentIndex('tabs', 1); ui.raise('a');"
            "ui.setLayout('window', {spacing: 20}); ui.reorder('okButton', 2);"
            "test.drop(); return ui.restore();").toObject();
        TEST_COMPARE(result["restored"].toInt(), 7);
        TEST_COMPARE(result["skipped"].toInt(), 1);
        TEST_ASSERT(dropped.isNull());
        TEST_ASSERT(!w.ok->isHidden() && w.ok->isVisible() && w.name->isEnabled());
        TEST_COMPARE(w.status->text(), QString("Ready"));
        TEST_COMPARE(x.tabs->currentIndex(), 0);
        TEST_COMPARE(x.stack->children(), (QObjectList{x.a, x.b}));
        TEST_COMPARE(w.window.layout()->spacing(), 6);
        TEST_COMPARE(row->indexOf(w.ok), 0);
        TEST_COMPARE(row->stretch(row->indexOf(w.cancel)), 2);

        // What a run does not restore stays; a later run cannot restore it.
        TEST_COMPARE(done(engine, "ui.setProperty('statusLabel', 'text', 'Busy');"), QString("ok"));
        const QJsonObject nothing = value(engine, "return ui.restore();").toObject();
        TEST_COMPARE(nothing, (QJsonObject{{"restored", 0}, {"skipped", 0}}));
        TEST_COMPARE(w.status->text(), QString("Busy"));
        // Nor a failed one. Restored once, the journal is empty.
        TEST_COMPARE(run(engine, "ui.setProperty('statusLabel', 'text', 'Failed'); throw new Error('no');")["status"]
                         .toString(), QString("error"));
        TEST_COMPARE(w.status->text(), QString("Failed"));
        const QJsonObject twice = value(engine, "ui.setProperty('statusLabel', 'text', 'Idle'); ui.restore();"
                                                "return ui.restore();").toObject();
        TEST_COMPARE(twice["restored"].toInt(), 0);
        TEST_COMPARE(w.status->text(), QString("Failed"));
    }

    TEST_FUNCTION(testDryRunAndSwitchedOffTools)
    {
        TEST_START;

        Engine engine;
        Window w;
        Extras x(&w.window);
        w.show();
        ScriptHost::exposeWidgets(&engine, &w.window);
        const QPoint freeAt = x.free->pos();

        // A dry run records the changing tools and changes nothing; reading works.
        TestSupport::Options options;
        options.dryRun = true;
        const QJsonObject dry = run(engine,
            "ui.move('free', 9, 9); ui.hide('okButton'); ui.setProperty('statusLabel', 'text', 'Busy');"
            "ui.moveToLayout('free', 'buttons'); ui.restore(); return ui.root().name;", options);
        TEST_COMPARE(dry["status"].toString(), QString("ok"));
        TEST_ASSERT(dry["dry_run"].toBool());
        TEST_COMPARE(dry["result"].toString(), QString("window"));
        TEST_COMPARE(dry["would_call"].toObject().keys(),
                     (QStringList{"ui.hide", "ui.move", "ui.moveToLayout", "ui.restore", "ui.setProperty"}));
        TEST_COMPARE(x.free->pos(), freeAt);
        TEST_ASSERT(!w.ok->isHidden() && x.free->parentWidget() == &w.window);
        TEST_COMPARE(w.status->text(), QString("Ready"));

        // What a dry run answers names no widget, so the reading tools that
        // really run cannot reach one of the application's by chance.
        const QJsonObject made = run(engine,
            "const d = ui.create({class: 'QDialog', name: 'ask'});"
            "return [d.id, d.ids.ok, ui.move('free', 1, 1).id, ui.exec(d.id).accepted];", options);
        TEST_COMPARE(made["result"].toArray(), (QJsonArray{-1, -1, -1, true}));
        TEST_COMPARE(run(engine, "const d = ui.create({class: 'QLineEdit', name: 'name'});"
                                 "return ui.property(d.id, 'text');", options)["message"].toString(),
                     QString("ui.property: no widget with the id -1"));
        const QJsonObject lookUp = run(engine, "const d = ui.create({class: 'QLineEdit', name: 'name'});"
                                               "return ui.property(d.ids.name, 'text');", options);
        // A name the example does not have: the call misses its argument.
        TEST_ASSERT(lookUp["message"].toString().startsWith("ui.property: 'widget' is missing"));
        // Ids start at 1: 0 is none, not a widget that has no id yet.
        TEST_COMPARE(error(engine, "ui.info(0);"), QString("ui.info: no widget with the id 0"));

        // One tool switched off; the others work.
        engine.setMethodEnabled("ui.hide", false);
        const Engine::Check check = engine.check("ui.hide('okButton');");
        TEST_ASSERT(!check.ok && check.message.contains("disabled"));
        TEST_COMPARE(run(engine, "ui.hide('okButton');")["status"].toString(), QString("error"));
        TEST_ASSERT(!w.ok->isHidden());
        TEST_COMPARE(done(engine, "ui.setEnabled('okButton', false);"), QString("ok"));
        TEST_ASSERT(!w.ok->isEnabled());
        engine.setMethodEnabled("ui.hide", true);
        TEST_COMPARE(done(engine, "ui.hide('okButton');"), QString("ok"));
        TEST_ASSERT(w.ok->isHidden());
    }

    TEST_FUNCTION(testCreateDialogTree)
    {
        TEST_START;

        Engine engine;
        Window w;
        w.show();
        ScriptHost::exposeWidgets(&engine, &w.window);

        // One call, the whole tree.
        Live live(engine,
            "const d = ui.create({class: 'QDialog', name: 'ask', title: 'Question', layout: {type: 'vbox', "
            "    spacing: 4, items: ["
            "  {class: 'QLabel', name: 'prompt', text: 'Your name?', wordWrap: true},"
            "  {class: 'QLineEdit', name: 'answer', placeholderText: 'Name', maxLength: 20},"
            "  {class: 'QWidget', layout: {type: 'grid', items: ["
            "    {class: 'QCheckBox', name: 'remember', text: 'Remember', checked: true},"
            "    {class: 'QSpinBox', name: 'age', value: 120, maximum: 150, row: 0, column: 1}]}},"
            "  {class: 'QComboBox', name: 'colour', items: ['red', 'green', 'blue'], currentIndex: 2},"
            "  {class: 'QTabWidget', name: 'pages', children: [{class: 'QWidget', title: 'One'},"
            "                                                  {class: 'QWidget', title: 'Two'}]},"
            "  {class: 'QWidget', layout: {type: 'form', items: [{class: 'QLineEdit', name: 'city', label: 'City'}]}},"
            "  {class: 'QWidget', layout: {type: 'hbox', items: [{stretch: 1},"
            "    {class: 'QPushButton', name: 'ok', text: 'OK', role: 'accept'}]}}]}});"
            "log(JSON.stringify(d));"
            "log(ui.property(d.ids.age, 'value') + ' ' + ui.info(d.id).text);");
        TEST_ASSERT(live.waitFor("ready"));
        TEST_COMPARE(live.log.value(1), QString("120 Question"));
        const QJsonObject made = live.json(0);
        auto* dialog = w.window.findChild<QDialog*>("ask");
        TEST_ASSERT(dialog);
        TEST_ASSERT(dialog->isWindow() && dialog->parentWidget() == &w.window && !dialog->isVisible());
        TEST_COMPARE(dialog->windowTitle(), QString("Question"));
        TEST_COMPARE(made["id"].toInt(), dialog->property("scripthost_id").toInt());
        const QJsonObject ids = made["ids"].toObject();
        TEST_COMPARE(ids.keys(), (QStringList{"age", "answer", "ask", "city", "colour", "ok", "pages", "prompt",
                                              "remember"}));
        TEST_COMPARE(dialog->layout()->spacing(), 4);
        auto* answer = dialog->findChild<QLineEdit*>("answer");
        TEST_ASSERT(answer && answer->placeholderText() == "Name" && answer->maxLength() == 20);
        TEST_COMPARE(ids["answer"].toInt(), answer->property("scripthost_id").toInt());
        TEST_ASSERT(dialog->findChild<QLabel*>("prompt")->wordWrap());
        // A grid: row from the item's index unless given, column 0 unless given.
        auto* age = dialog->findChild<QSpinBox*>("age");
        TEST_ASSERT(age && age->maximum() == 150 && age->value() == 120);
        auto* grid = qobject_cast<QGridLayout*>(age->parentWidget()->layout());
        TEST_ASSERT(grid && grid->itemAtPosition(0, 1)->widget() == age);
        TEST_ASSERT(grid->itemAtPosition(0, 0)->widget() == dialog->findChild<QCheckBox*>("remember"));
        TEST_ASSERT(dialog->findChild<QCheckBox*>("remember")->isChecked());
        auto* colour = dialog->findChild<QComboBox*>("colour");
        TEST_ASSERT(colour->count() == 3 && colour->currentText() == "blue");
        auto* pages = dialog->findChild<QTabWidget*>("pages");
        TEST_ASSERT(pages->count() == 2 && pages->tabText(1) == "Two");
        auto* city = dialog->findChild<QLineEdit*>("city");
        auto* form = qobject_cast<QFormLayout*>(city->parentWidget()->layout());
        TEST_ASSERT(form && qobject_cast<QLabel*>(form->labelForField(city))->text() == "City");
        // A button with a role closes its dialog.
        dialog->show();
        dialog->findChild<QPushButton*>("ok")->click();
        TEST_ASSERT(!dialog->isVisible() && dialog->result() == QDialog::Accepted);

        // Made by the run: gone when it ends.
        const QPointer<QDialog> gone(dialog);
        engine.postEvent(live.id, "end", QJsonObject());
        TEST_ASSERT(TestSupport::spin([&]() { return live.done && gone.isNull(); }));
        TEST_COMPARE(live.result["status"].toString(), QString("ok"));
    }

    TEST_FUNCTION(testCreateIntoLayoutAndClasses)
    {
        TEST_START;

        Engine engine;
        Window w;
        w.show();
        ScriptHost::ScriptUi* ui = ScriptHost::exposeWidgets(&engine, &w.window);
        auto* row = static_cast<QBoxLayout*>(w.buttons->layout());

        // Into the application's layout, at a place; an app factory's class.
        ui->addWidgetFactory("Gauge", []() -> QWidget* { return new QProgressBar; });
        TEST_ASSERT(ui->widgetClasses().contains("Gauge") && ui->widgetClasses().contains("QDialog"));
        Live live(engine,
            "const help = ui.create({class: 'QPushButton', name: 'help', text: 'Help'}, "
            "                       {parent: 'buttons', at: {index: 1}});"
            "ui.create({class: 'Gauge', name: 'gauge', value: 40}, {parent: 'window'});"
            "log(help.id);");
        TEST_ASSERT(live.waitFor("ready"));
        auto* help = w.buttons->findChild<QPushButton*>("help");
        TEST_ASSERT(help && help->parentWidget() == w.buttons);
        TEST_COMPARE(row->indexOf(help), 1);
        TEST_COMPARE(live.log.value(0).toInt(), help->property("scripthost_id").toInt());
        TEST_ASSERT(TestSupport::spin([&]() { return help->isVisible(); }));
        auto* gauge = w.window.findChild<QProgressBar*>("gauge");
        TEST_ASSERT(gauge && gauge->value() == 40);
        TEST_COMPARE(w.window.layout()->indexOf(gauge), w.window.layout()->count() - 1);
        const QPointer<QWidget> gone(help);
        engine.postEvent(live.id, "end", QJsonObject());
        TEST_ASSERT(TestSupport::spin([&]() { return live.done && gone.isNull(); }));
        TEST_COMPARE(row->count(), 3);

        // Only the classes allowed; nothing is made when any part is wrong.
        const int widgets = w.window.findChildren<QWidget*>().size();
        TEST_COMPARE(error(engine, "ui.create({class: 'QFileDialog'});"),
                     QString("ui.create: the class 'QFileDialog' is not allowed"));
        ui->removeWidgetClass("QLineEdit");
        TEST_ASSERT(!ui->widgetClasses().contains("QLineEdit"));
        TEST_COMPARE(error(engine, "ui.create({class: 'QDialog', layout: {type: 'vbox', items: ["
                                   "{class: 'QLabel', text: 'a'}, {class: 'QLineEdit'}]}});"),
                     QString("ui.create: the class 'QLineEdit' is not allowed"));
        TEST_COMPARE(error(engine, "ui.create({class: 'QLabel', nope: 1});"),
                     QString("ui.create: a QLabel has no property 'nope'"));
        TEST_COMPARE(error(engine, "ui.create({class: 'QSpinBox', value: 'x'});"),
                     QString("ui.create: 'value' takes a number"));
        TEST_COMPARE(error(engine, "ui.create({class: 'QLabel', items: ['a']});"),
                     QString("ui.create: a QLabel takes no items"));
        TEST_COMPARE(error(engine, "ui.create({class: 'QPushButton', role: 'accept'});"),
                     QString("ui.create: a button with a role must be in a dialog"));
        TEST_COMPARE(error(engine, "ui.create({class: 'QWidget', layout: {type: 'diagonal'}});"),
                     QString("ui.create: a layout's type is \"vbox\", \"hbox\", \"grid\" or \"form\", not 'diagonal'"));
        TEST_COMPARE(error(engine, "ui.create({class: 'QLabel'}, {parent: 'buttons', at: {index: 9}});"),
                     QString("ui.create: the index 9 is not between 0 and 3"));
        TestSupport::spin([]() { return false; }, 50);
        TEST_COMPARE(w.window.findChildren<QWidget*>().size(), widgets);
    }

    TEST_FUNCTION(testExecCloseAndDestroy)
    {
        TEST_START;

        Engine engine;
        Window w;
        w.show();
        ScriptHost::exposeWidgets(&engine, &w.window);

        // The script waits for the dialog; the application goes on.
        Live live(engine,
            "const d = ui.create({class: 'QDialog', name: 'ask', layout: {type: 'vbox', items: ["
            "  {class: 'QLineEdit', name: 'answer'},"
            "  {class: 'QDialogButtonBox', name: 'box', standardButtons: 'Ok|Cancel'}]}});"
            "const first = ui.exec(d.id);"
            "log(JSON.stringify(first) + ' ' + ui.property('answer', 'text'));"
            "log(JSON.stringify(ui.exec(d.id)));");
        QPointer<QDialog> dialog;
        TEST_ASSERT(TestSupport::spin([&]() {
            dialog = w.window.findChild<QDialog*>("ask");
            return dialog && dialog->isVisible();
        }));
        TEST_ASSERT(dialog->isModal() && dialog->isWindow());
        auto* box = dialog->findChild<QDialogButtonBox*>("box");
        dialog->findChild<QLineEdit*>("answer")->setText("Bob");
        box->button(QDialogButtonBox::Ok)->click();
        TEST_ASSERT(live.waitFor("{\"accepted\":true,\"button\":\"OK\"} Bob"));
        TEST_ASSERT(TestSupport::spin([&]() { return dialog->isVisible(); }));
        box->button(QDialogButtonBox::Cancel)->click();
        TEST_ASSERT(live.waitFor("{\"accepted\":false,\"button\":\"Cancel\"}"));
        TEST_ASSERT(live.waitFor("ready"));
        TEST_ASSERT(!w.window.findChild<QDialog*>("ask")->isVisible());
        engine.postEvent(live.id, "end", QJsonObject());
        TEST_ASSERT(TestSupport::spin([&]() { return live.done && dialog.isNull(); }));
        TEST_COMPARE(error(engine, "ui.exec('okButton');"), QString("ui.exec: the widget is not a QDialog"));

        // Every ui.exec of a dialog leaves no connection to it behind (the
        // first shows it, and Qt keeps one of its own from then on).
        struct Counted : QDialog
        {
            using QDialog::QDialog;
            int destroyedReceivers() const { return receivers(SIGNAL(destroyed(QObject*))); }
        };
        auto* counted = new Counted(&w.window);
        counted->setObjectName("counted");
        int receivers = 0;
        for (int i = 0; i < 4; ++i) {
            bool answered = false;
            engine.run("return ui.exec('counted').accepted;", Engine::RunOptions(),
                       [&](const QJsonObject&) { answered = true; });
            TEST_ASSERT(TestSupport::spin([&]() { return counted->isVisible(); }));
            counted->accept();
            TEST_ASSERT(TestSupport::spin([&]() { return answered; }));
            if (i == 0)
                receivers = counted->destroyedReceivers();
        }
        TestSupport::spin([]() { return false; }, 50);
        TEST_COMPARE(counted->destroyedReceivers(), receivers);
        delete counted;

        // Closing; destroying only what the run made.
        const QJsonObject closed = value(engine,
            "const d = ui.create({class: 'QDialog', name: 'note', layout: {type: 'vbox', items: ["
            "  {class: 'QLabel', name: 'line', text: 'x'}]}});"
            "const shown = ui.show(d.id).visible;"
            "const after = ui.close(d.id).visible;"
            "ui.destroy('line');"
            "const lines = ui.find({name: 'line'}).length;"
            "ui.destroy(d.id);"
            "return {shown, after, lines, notes: ui.find({name: 'note'}).length};").toObject();
        TEST_COMPARE(closed, (QJsonObject{{"shown", true}, {"after", false}, {"lines", 0}, {"notes", 0}}));
        TEST_COMPARE(error(engine, "ui.destroy('okButton');"),
                     QString("ui.destroy: only a widget this run created can be destroyed"));
        TEST_COMPARE(error(engine, "ui.close('window');"), QString("ui.close: the root cannot be closed"));
        TEST_ASSERT(w.window.isVisible() && w.ok);
    }

    TEST_FUNCTION(testSignals)
    {
        TEST_START;

        Engine engine;
        Window w;
        w.show();
        ScriptHost::exposeWidgets(&engine, &w.window);

        Live live(engine,
            "const d = ui.create({class: 'QDialog', name: 'form', layout: {type: 'vbox', items: ["
            "  {class: 'QPushButton', name: 'go', text: 'Go', checkable: true},"
            "  {class: 'QLineEdit', name: 'field'},"
            "  {class: 'QSpinBox', name: 'count'},"
            "  {class: 'QComboBox', name: 'pick', items: ['a', 'b', 'c']},"
            "  {class: 'QLineEdit', name: 'secret', echoMode: 'Password'}]}});"
            "on(ui.connect('go', 'clicked'), e => log('clicked ' + e.checked + ' ' + e.widget + ' ' + e.signal));"
            "log('name ' + ui.connect('go', 'clicked'));"
            "on(ui.connect('field', 'textChanged'), e => log('text ' + e.text));"
            "on(ui.connect('count', 'valueChanged(int)'), e => log('value ' + e.value + ' ' + e.args[0]));"
            "on(ui.connect('pick', 'currentIndexChanged(int)'), e => log('index ' + e.index));"
            "on(ui.connect('secret', 'textChanged'), e => log('secret ' + e.text));"
            "on(ui.connect('okButton', 'clicked'), e => log('app ' + e.checked));"
            "on('off', () => log('off ' + ui.disconnect('field', 'textChanged') + ' '"
            "                    + ui.disconnect('field', 'textChanged')));");
        TEST_ASSERT(live.waitFor("ready"));
        auto* dialog = w.window.findChild<QDialog*>("form");
        TEST_ASSERT(dialog);
        auto* go = dialog->findChild<QPushButton*>("go");
        const int goId = go->property("scripthost_id").toInt();
        TEST_ASSERT(live.log.contains(QString("name ui:%1:clicked").arg(goId)));

        // Created widgets and the application's, with the arguments by name.
        go->click();
        TEST_ASSERT(live.waitFor(QString("clicked true %1 clicked").arg(goId)));
        dialog->findChild<QLineEdit*>("field")->setText("hi");
        TEST_ASSERT(live.waitFor("text hi"));
        dialog->findChild<QSpinBox*>("count")->setValue(5);
        TEST_ASSERT(live.waitFor("value 5 5"));
        dialog->findChild<QComboBox*>("pick")->setCurrentIndex(2);
        TEST_ASSERT(live.waitFor("index 2"));
        dialog->findChild<QLineEdit*>("secret")->setText("pw");
        TEST_ASSERT(live.waitFor("secret null"));
        w.ok->click();
        TEST_ASSERT(live.waitFor("app false"));

        // Disconnected: no more events of it.
        engine.postEvent(live.id, "off", QJsonObject());
        TEST_ASSERT(live.waitFor("off true false"));
        dialog->findChild<QLineEdit*>("field")->setText("again");
        go->click();
        TEST_ASSERT(live.waitFor(QString("clicked false %1 clicked").arg(goId)));
        TEST_ASSERT(!live.log.contains("text again"));

        // Stopped: its widgets and connections go.
        const QPointer<QDialog> gone(dialog);
        engine.stop(live.id);
        TEST_ASSERT(TestSupport::spin([&]() { return live.done && gone.isNull(); }));
        TEST_COMPARE(live.result["phase"].toString(), QString("cancelled"));
        const int lines = live.log.size();
        w.ok->click();
        TestSupport::spin([]() { return false; }, 50);
        TEST_COMPARE(live.log.size(), lines);

        // Requested runs: with the "ui" opt-in only.
        const QString code("const n = ui.connect('okButton', 'clicked'); on(n, () => {}); off(n); return n;");
        const QJsonObject blocked = TestSupport::request(engine, TestSupport::codeRequest(code));
        TEST_COMPARE(blocked["message"].toString(),
                     QString("ui.connect: the application does not let requested scripts receive widget signals"));
        engine.allowEventForRequestedRuns("ui");
        const QJsonObject allowed = TestSupport::request(engine, TestSupport::codeRequest(code));
        TEST_COMPARE(allowed["result"].toString(),
                     QString("ui:%1:clicked").arg(w.ok->property("scripthost_id").toInt()));

        // What a requested run started with counts, as for on(): the opt-in
        // changed while it runs makes no difference to it.
        engine.allowEventForRequestedRuns("go");
        const auto connectLater = [&](bool allowedAtStart) {
            engine.allowEventForRequestedRuns("ui", allowedAtStart);
            Engine::RunOptions options;
            options.external = true;
            QStringList log;
            bool finished = false;
            Engine::RunId id = 0;
            QObject context;
            QObject::connect(&engine, &Engine::runLog, &context, [&](Engine::RunId run, const QString& text) {
                if (run == id)
                    log << text;
            });
            id = engine.run("on('go', () => { off('go');"
                            "  try { log(ui.connect('okButton', 'clicked')); } catch (e) { log(e.message); } });"
                            "log('ready');", options, [&](const QJsonObject&) { finished = true; });
            TestSupport::spin([&]() { return log.contains("ready"); });
            engine.allowEventForRequestedRuns("ui", !allowedAtStart);
            engine.postEvent(id, "go", QJsonObject());
            TestSupport::spin([&]() { return finished; });
            return log.value(1);
        };
        TEST_COMPARE(connectLater(true), QString("ui:%1:clicked").arg(w.ok->property("scripthost_id").toInt()));
        TEST_ASSERT(connectLater(false).contains("does not let requested scripts receive widget signals"));
    }

    TEST_FUNCTION(testCreatedWidgetsGoAndSwitches)
    {
        TEST_START;

        Engine engine;
        Window w;
        w.show();
        ScriptHost::exposeWidgets(&engine, &w.window);

        // Stopped while a dialog is open: it goes.
        Live live(engine, "ui.show(ui.create({class: 'QDialog', name: 'open'}).id);");
        TEST_ASSERT(live.waitFor("ready"));
        const QPointer<QDialog> dialog = w.window.findChild<QDialog*>("open");
        TEST_ASSERT(dialog && dialog->isVisible());
        engine.stop(live.id);
        TEST_ASSERT(TestSupport::spin([&]() { return live.done && dialog.isNull(); }));

        // ui.restore() takes it away at once.
        TEST_COMPARE(value(engine, "ui.create({class: 'QLabel', name: 'tmp'}, {parent: 'window'});"
                                   "const before = ui.find({name: 'tmp'}).length; ui.restore();"
                                   "return before;").toInt(), 1);

        // A dry run makes nothing.
        TestSupport::Options options;
        options.dryRun = true;
        const QJsonObject dry = run(engine, "return ui.create({class: 'QDialog', name: 'dry'}).ids;", options);
        TEST_COMPARE(dry["status"].toString(), QString("ok"));
        TEST_ASSERT(!w.window.findChild<QDialog*>("dry"));

        // Each tool switched off alone.
        for (const QString& name : {"ui.create", "ui.exec", "ui.close", "ui.destroy", "ui.connect",
                                    "ui.disconnect"}) {
            engine.setMethodEnabled(name, false);
            const Engine::Check check = engine.check(name + "('okButton');");
            TEST_ASSERT(!check.ok && check.message.contains("disabled"));
            engine.setMethodEnabled(name, true);
        }
        engine.setMethodEnabled("ui.create", false);
        TEST_COMPARE(run(engine, "ui.create({class: 'QLabel'});")["status"].toString(), QString("error"));
        TEST_COMPARE(value(engine, "const n = ui.connect('okButton', 'clicked'); return n.startsWith('ui:');")
                         .toBool(), true);
        TestSupport::spin([]() { return false; }, 50);
        TEST_ASSERT(!w.window.findChild<QLabel*>("tmp"));
    }
};

TEST_INSTANTIATE(TST_ScriptUi);
