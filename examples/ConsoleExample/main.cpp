// The engine without any window: a QCoreApplication that links only Qt Core,
// Qt Qml and ScriptHost (not ScriptHostWidgets). It registers one method,
// runs a script and prints the result as JSON; the script's log lines go to
// stdout as they come (setOutput).
#include "ScriptHost.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <cstdio>

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    QFile out;
    out.open(stdout, QIODevice::WriteOnly);

    ScriptHost::ScriptEngine engine;
    engine.setOutput(&out);

    ScriptHost::ScriptMethod add("math.add");
    add.setDescription("Adds two numbers.").setReadOnly(true)
       .addParameter("a", "number", "The first number")
       .addParameter("b", "number", "The second number")
       .setReturns("number", "The sum");
    engine.registerMethod(add, [](const QJsonObject& args) {
        return QJsonValue(args["a"].toDouble() + args["b"].toDouble());
    });

    const QString code = "const sum = math.add(2, 3);\n"
                         "print('2 + 3 =', sum);\n"
                         "return { sum: sum };";
    engine.run(code, ScriptHost::ScriptEngine::RunOptions(), [&](const QJsonObject& result) {
        out.write(QJsonDocument(result).toJson(QJsonDocument::Indented));
        out.flush();
        app.exit(result["status"].toString() == "ok" ? 0 : 1);
    });
    return app.exec();
}
