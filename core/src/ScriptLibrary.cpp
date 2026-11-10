#include "ScriptLibrary.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <algorithm>

namespace ScriptHost
{

ScriptLibrary::ScriptLibrary(QObject* parent)
    : QObject(parent)
{
}

void ScriptLibrary::setDirectory(const QString& path)
{
    if (m_directory == path)
        return;
    m_directory = path;
    emit changed();
}

void ScriptLibrary::addReadOnlyDirectory(const QString& path)
{
    if (path.isEmpty() || m_readOnly.contains(path))
        return;
    m_readOnly << path;
    emit changed();
}

QString ScriptLibrary::fileBaseName(const QString& scriptName)
{
    QString base = scriptName.toLower();
    base.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
    while (base.startsWith(QLatin1Char('-')))
        base.remove(0, 1);
    while (base.endsWith(QLatin1Char('-')))
        base.chop(1);
    return base.left(80);
}

QString ScriptLibrary::toFileText(const Script& script)
{
    // A header value is one line.
    auto line = [](const QString& text) { return text.simplified(); };

    QString text = QStringLiteral("// @name ") + line(script.name) + QLatin1Char('\n');
    if (!script.description.trimmed().isEmpty())
        text += QStringLiteral("// @description ") + line(script.description) + QLatin1Char('\n');
    for (const Parameter& parameter : script.parameters) {
        text += QStringLiteral("// @param %1 %2 %3").arg(line(parameter.name),
                    parameter.type.isEmpty() ? QStringLiteral("any") : line(parameter.type),
                    line(parameter.description)).trimmed() + QLatin1Char('\n');
    }
    if (!script.origin.isEmpty())
        text += QStringLiteral("// @origin ") + line(script.origin) + QLatin1Char('\n');
    text += QLatin1Char('\n') + script.code;
    if (!text.endsWith(QLatin1Char('\n')))
        text += QLatin1Char('\n');
    return text;
}

ScriptLibrary::Script ScriptLibrary::fromFileText(const QString& text, const QString& fallbackName)
{
    Script script;
    script.name = fallbackName;
    script.origin = QStringLiteral("user");

    static const QRegularExpression header(QStringLiteral("^//\\s*@(\\w+)\\s*(.*)$"));
    QString normalized = text;
    normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    const QStringList lines = normalized.split(QLatin1Char('\n'));
    int i = 0;
    for (; i < lines.size(); ++i) {
        const QRegularExpressionMatch match = header.match(lines[i]);
        if (!match.hasMatch())
            break;
        const QString key = match.captured(1);
        const QString value = match.captured(2).trimmed();
        if (key == QLatin1String("name")) {
            if (!value.isEmpty())
                script.name = value;
        } else if (key == QLatin1String("description")) {
            script.description = value;
        } else if (key == QLatin1String("origin")) {
            script.origin = value;
        } else if (key == QLatin1String("param")) {
            Parameter parameter;
            parameter.name = value.section(QLatin1Char(' '), 0, 0);
            parameter.type = value.section(QLatin1Char(' '), 1, 1);
            parameter.description = value.section(QLatin1Char(' '), 2);
            if (!parameter.name.isEmpty())
                script.parameters << parameter;
        }
    }
    // One empty line separates header and code.
    if (i > 0 && i < lines.size() && lines[i].trimmed().isEmpty())
        ++i;
    script.code = lines.mid(i).join(QLatin1Char('\n'));
    while (script.code.endsWith(QLatin1Char('\n')))
        script.code.chop(1);
    return script;
}

QList<ScriptLibrary::Script> ScriptLibrary::readDirectory(const QString& path, bool readOnly) const
{
    QList<Script> found;
    if (path.isEmpty())
        return found;
    const QFileInfoList files = QDir(path).entryInfoList({QStringLiteral("*.js")}, QDir::Files, QDir::Name);
    for (const QFileInfo& info : files) {
        QFile file(info.filePath());
        if (!file.open(QIODevice::ReadOnly))
            continue;
        Script script = fromFileText(QString::fromUtf8(file.readAll()), info.completeBaseName());
        script.filePath = info.filePath();
        script.readOnly = readOnly;
        if (readOnly && script.origin == QLatin1String("user"))
            script.origin = QStringLiteral("application");
        found << script;
    }
    return found;
}

QList<ScriptLibrary::Script> ScriptLibrary::scripts() const
{
    QList<Script> all = readDirectory(m_directory, false);
    auto known = [&all](const QString& name) {
        return std::any_of(all.cbegin(), all.cend(), [&name](const Script& s) {
            return s.name.compare(name, Qt::CaseInsensitive) == 0;
        });
    };
    for (const QString& path : m_readOnly) {
        const QList<Script> shipped = readDirectory(path, true);
        for (const Script& script : shipped) {
            if (!known(script.name))
                all << script;
        }
    }
    std::sort(all.begin(), all.end(), [](const Script& a, const Script& b) {
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
    return all;
}

ScriptLibrary::Script ScriptLibrary::script(const QString& name) const
{
    const QList<Script> all = scripts();
    for (const Script& script : all) {
        if (script.name.compare(name, Qt::CaseInsensitive) == 0)
            return script;
    }
    for (const Script& script : all) {
        if (QFileInfo(script.filePath).completeBaseName().compare(name, Qt::CaseInsensitive) == 0)
            return script;
    }
    return Script();
}

bool ScriptLibrary::save(const Script& script, QString* error)
{
    auto fail = [error](const QString& message) {
        if (error)
            *error = message;
        return false;
    };
    if (m_directory.isEmpty())
        return fail(QStringLiteral("no directory for scripts is set"));
    const QString base = fileBaseName(script.name);
    if (base.isEmpty())
        return fail(QStringLiteral("the script needs a name with letters or digits"));
    if (!QDir().mkpath(m_directory))
        return fail(QStringLiteral("cannot create %1").arg(m_directory));

    // The same script under another file name (renamed by hand) is replaced,
    // not doubled.
    QString path = QDir(m_directory).filePath(base + QStringLiteral(".js"));
    const QList<Script> own = readDirectory(m_directory, false);
    for (const Script& existing : own) {
        if (existing.name.compare(script.name, Qt::CaseInsensitive) == 0)
            path = existing.filePath;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return fail(QStringLiteral("cannot write %1: %2").arg(path, file.errorString()));
    file.write(toFileText(script).toUtf8());
    if (!file.commit())
        return fail(QStringLiteral("cannot write %1: %2").arg(path, file.errorString()));
    emit changed();
    return true;
}

bool ScriptLibrary::remove(const QString& name, QString* error)
{
    const QList<Script> own = readDirectory(m_directory, false);
    for (const Script& existing : own) {
        if (existing.name.compare(name, Qt::CaseInsensitive) != 0)
            continue;
        if (!QFile::remove(existing.filePath)) {
            if (error)
                *error = QStringLiteral("cannot delete %1").arg(existing.filePath);
            return false;
        }
        emit changed();
        return true;
    }
    if (error)
        *error = QStringLiteral("no script '%1' that can be deleted").arg(name);
    return false;
}

} // namespace ScriptHost
