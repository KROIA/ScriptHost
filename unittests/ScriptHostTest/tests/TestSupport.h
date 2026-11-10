#pragma once

// What the test classes share: waiting for the event loop, running a script
// to its end, and a small "database of objects" registered as methods - the
// kind of interface scripts are for.

#include "ScriptHost.h"
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QTimer>
#include <functional>

namespace TestSupport
{

// Runs the event loop until the condition holds or the time is up. Method
// calls and results reach the GUI thread through it.
inline bool spin(const std::function<bool()>& condition, int timeoutMs = 10000)
{
    QEventLoop loop;
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, [&]() {
        if (condition())
            loop.quit();
    });
    poll.start(5);
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    if (!condition())
        loop.exec();
    return condition();
}

using Options = ScriptHost::ScriptEngine::RunOptions;

inline QJsonObject runScript(ScriptHost::ScriptEngine& engine, const QString& code,
                             const Options& options = Options())
{
    QJsonObject result;
    bool done = false;
    engine.run(code, options, [&](const QJsonObject& r) {
        result = r;
        done = true;
    });
    spin([&]() { return done; });
    return result;
}

// A requested run, waited for. *finished tells whether `done` was called at all.
inline QJsonObject request(ScriptHost::ScriptEngine& engine, const ScriptHost::ScriptEngine::Request& req,
                           std::function<bool()> abandoned = nullptr, bool* finished = nullptr,
                           int timeoutMs = 10000)
{
    QJsonObject result;
    bool done = false;
    engine.requestRun(req, [&](const QJsonObject& r) {
        result = r;
        done = true;
    }, std::move(abandoned));
    spin([&]() { return done; }, timeoutMs);
    if (finished)
        *finished = done;
    return result;
}

inline ScriptHost::ScriptEngine::Request codeRequest(const QString& text, const QJsonObject& args = QJsonObject(),
                                              bool dryRun = false)
{
    ScriptHost::ScriptEngine::Request req;
    req.code = text;
    req.args = args;
    req.dryRun = dryRun;
    return req;
}

inline void writeFile(const QString& path, const QByteArray& text)
{
    QFile file(path);
    if (file.open(QIODevice::WriteOnly))
        file.write(text);
}

// Six objects o1..o6, aged 10..60.
struct Db
{
    QMap<QString, QJsonObject> objects;
    int reads = 0;
    int writes = 0;

    Db()
    {
        for (int i = 1; i <= 6; ++i) {
            const QString id = QString("o%1").arg(i);
            objects.insert(id, QJsonObject{{"id", id}, {"name", QString("Object %1").arg(i)},
                                           {"age", i * 10}, {"status", "active"}});
        }
    }
    int archived() const
    {
        int n = 0;
        for (const QJsonObject& o : objects)
            n += o["status"].toString() == "archived" ? 1 : 0;
        return n;
    }
};

// db.list and db.read only read; db.setStatus changes something.
inline void registerDb(ScriptHost::ScriptEngine& engine, Db& db)
{
    ScriptHost::ScriptMethod list("db.list");
    list.setDescription("Lists the ids of all objects.").setGroup("Database").setReadOnly(true)
        .setReturns("array", "The ids", QJsonArray{"o1", "o2"});
    engine.registerMethod(list, [&db](const QJsonObject&) -> QJsonValue {
        return QJsonArray::fromStringList(db.objects.keys());
    });

    ScriptHost::ScriptMethod read("db.read");
    read.setDescription("Reads one object.").setGroup("Database").setReadOnly(true)
        .addParameter("id", "string", "Object id")
        .setReturns("object", "The object",
                    QJsonObject{{"id", "o1"}, {"name", "Pump"}, {"age", 3}, {"status", "active"}});
    engine.registerMethod(read, [&db](const QJsonObject& args) -> QJsonValue {
        ++db.reads;
        const QString id = args["id"].toString();
        if (!db.objects.contains(id))
            throw ScriptHost::ScriptError(QString("no object '%1'").arg(id));
        return db.objects.value(id);
    });

    ScriptHost::ScriptMethod setStatus("db.setStatus");
    setStatus.setDescription("Sets the status of an object.").setGroup("Database")
        .addParameter("id", "string", "Object id")
        .addParameter("status", "string", "New status", true, {"active", "archived"})
        .setReturns("object", "", QJsonObject{{"changed", true}});
    engine.registerMethod(setStatus, [&db](const QJsonObject& args) -> QJsonValue {
        ++db.writes;
        const QString id = args["id"].toString();
        if (!db.objects.contains(id))
            throw ScriptHost::ScriptError(QString("no object '%1'").arg(id));
        db.objects[id]["status"] = args["status"].toString();
        return QJsonObject{{"changed", true}};
    });
}

// Where the documentation pictures go, when somebody asked for them.
inline void grab(QWidget* widget, const char* fileName)
{
    if (qEnvironmentVariableIsSet("SCRIPTHOST_TEST_GRABS"))
        widget->grab().save(qEnvironmentVariable("SCRIPTHOST_TEST_GRABS") + "/" + fileName);
}

} // namespace TestSupport
