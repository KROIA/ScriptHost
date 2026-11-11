#pragma once

// exposeWidgets(): scripts read a widget tree through the ui methods - the
// tree, ids, find/at/info/property, focus and tab order - and see nothing
// outside the root, nothing hidden, no ScriptHost widget and no password.

#include "UnitTest.h"
#include "TestSupport.h"
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
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

    static QJsonObject run(Engine& engine, const QString& code)
    {
        return TestSupport::runScript(engine, code);
    }

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

        // Ten read-only methods in the group "UI".
        QStringList methods;
        for (const ScriptHost::ScriptMethod& m : engine.methods()) {
            if (!m.name().startsWith("ui."))
                continue;
            methods << m.name();
            TEST_ASSERT(m.isReadOnly());
            TEST_COMPARE(m.group(), QString("UI"));
        }
        TEST_COMPARE(methods, (QStringList{"ui.at", "ui.children", "ui.find", "ui.focused", "ui.info", "ui.parent",
                                           "ui.property", "ui.root", "ui.tabOrder", "ui.tree"}));
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
};

TEST_INSTANTIATE(TST_ScriptUi);
