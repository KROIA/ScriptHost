#pragma once
#include "ScriptHost_base.h"
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

namespace ScriptHost
{

// Saved scripts, one .js file each. The application says where:
//
//   library.setDirectory(appData + "/scripts");        // the user's, writable
//   library.addReadOnlyDirectory(":/scripts");         // shipped with the application
//
// A file is the script's code under a header of comment lines:
//
//   // @name Archive outdated objects
//   // @description Sets the status of every object older than `days` to archived.
//   // @param days number Age in days
//   // @origin user
//
//   for (const id of db.listObjects()) { ... }
//
// so it stays a plain JavaScript file that any editor opens and any version
// control diffs. Files are read when asked for; nothing is cached.
class SCRIPT_HOST_API ScriptLibrary : public QObject
{
    Q_OBJECT
public:
    struct Parameter
    {
        QString name;
        QString type;           // "string", "number", "boolean", "array", "object"
        QString description;
    };

    struct Script
    {
        QString          name;
        QString          description;
        QList<Parameter> parameters;    // what the script expects in `args`
        QString          code;          // without the header
        QString          origin;        // "user", "application", "assistant"
        QString          filePath;      // where it was read from; empty for a new one
        bool             readOnly = false;

        bool isValid() const { return !name.isEmpty(); }
    };

    explicit ScriptLibrary(QObject* parent = nullptr);

    // Where scripts are saved. Created when the first script is saved.
    void setDirectory(const QString& path);
    QString directory() const { return m_directory; }
    // Scripts from here are listed and run but never changed.
    void addReadOnlyDirectory(const QString& path);
    QStringList readOnlyDirectories() const { return m_readOnly; }

    // All scripts, sorted by name. With the same name in several places the
    // writable directory wins.
    QList<Script> scripts() const;
    // By name (case-insensitive) or by file name without extension; invalid
    // when there is none.
    Script script(const QString& name) const;
    bool contains(const QString& name) const { return script(name).isValid(); }

    // Writes the script into the writable directory, replacing one of the
    // same name there. False with *error when there is no directory, the name
    // is empty or the file cannot be written.
    bool save(const Script& script, QString* error = nullptr);
    // Deletes a script of the writable directory.
    bool remove(const QString& name, QString* error = nullptr);

    // The file format.
    static QString toFileText(const Script& script);
    static Script fromFileText(const QString& text, const QString& fallbackName);
    // "Archive outdated objects" -> "archive-outdated-objects"
    static QString fileBaseName(const QString& scriptName);

signals:
    void changed();

private:
    QList<Script> readDirectory(const QString& path, bool readOnly) const;

    QString     m_directory;
    QStringList m_readOnly;
};

} // namespace ScriptHost
