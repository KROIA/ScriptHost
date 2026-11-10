#pragma once

// Saved scripts are plain .js files with a header of comment lines.

#include "UnitTest.h"
#include "TestSupport.h"
#include <QFile>
#include <QTemporaryDir>

class TST_ScriptLibrary : public UnitTest::Test
{
    TEST_CLASS(TST_ScriptLibrary)
public:
    TST_ScriptLibrary()
        : Test("TST_ScriptLibrary")
    {
        ADD_TEST(TST_ScriptLibrary::testFileFormat);
        ADD_TEST(TST_ScriptLibrary::testDirectories);
    }

private:
    using Library = ScriptHost::ScriptLibrary;

    TEST_FUNCTION(testFileFormat)
    {
        TEST_START;

        Library::Script script;
        script.name = "Archive old objects";
        script.description = "Archives every object\nolder than `days`.";
        script.parameters = {{"days", "number", "Age in days"}, {"dryRun", "boolean", ""}};
        script.code = "for (const id of db.list()) {\n    // ...\n}\nreturn 1;";
        script.origin = "user";

        const QString text = Library::toFileText(script);
        TEST_ASSERT(text.startsWith("// @name Archive old objects\n"
                                    "// @description Archives every object older than `days`.\n"
                                    "// @param days number Age in days\n"
                                    "// @param dryRun boolean\n"
                                    "// @origin user\n\n"
                                    "for (const id of db.list()) {\n"));
        TEST_ASSERT(text.endsWith("return 1;\n"));
        const Library::Script parsed = Library::fromFileText(text, "fallback");
        TEST_COMPARE(parsed.name, script.name);
        TEST_COMPARE(parsed.description, QString("Archives every object older than `days`."));
        TEST_COMPARE(parsed.parameters.size(), 2);
        if (parsed.parameters.size() == 2) {
            TEST_COMPARE(parsed.parameters[0].name, QString("days"));
            TEST_COMPARE(parsed.parameters[0].type, QString("number"));
            TEST_COMPARE(parsed.parameters[0].description, QString("Age in days"));
            TEST_COMPARE(parsed.parameters[1].name, QString("dryRun"));
            TEST_ASSERT(parsed.parameters[1].description.isEmpty());
        }
        TEST_COMPARE(parsed.code, script.code);
        TEST_COMPARE(parsed.origin, QString("user"));

        // A file without a header is a script named after the file.
        const Library::Script bare = Library::fromFileText("return 1;\r\n", "quick-count");
        TEST_COMPARE(bare.name, QString("quick-count"));
        TEST_COMPARE(bare.code, QString("return 1;"));
        TEST_ASSERT(bare.isValid());
        TEST_ASSERT(!Library::Script().isValid());
        TEST_COMPARE(Library::fileBaseName("  Archive: old Objects! "), QString("archive-old-objects"));
    }

    TEST_FUNCTION(testDirectories)
    {
        TEST_START;

        QTemporaryDir userDir, shippedDir;
        TEST_ASSERT(userDir.isValid() && shippedDir.isValid());

        Library::Script script;
        script.name = "Archive old objects";
        script.code = "return 1;";
        script.origin = "user";

        Library library;
        int changes = 0;
        QObject::connect(&library, &Library::changed, [&]() { ++changes; });
        QString error;
        TEST_ASSERT(!library.save(script, &error));                 // nowhere to save
        TEST_COMPARE(error, QString("no directory for scripts is set"));

        library.setDirectory(userDir.path() + "/scripts");          // created on demand
        TEST_COMPARE(library.directory(), userDir.path() + "/scripts");
        Library::Script nameless;
        nameless.name = "!!!";
        TEST_ASSERT(!library.save(nameless, &error));
        TEST_ASSERT(error.contains("needs a name"));
        TEST_ASSERT(library.save(script, &error));
        TEST_ASSERT(QFile::exists(userDir.path() + "/scripts/archive-old-objects.js"));
        TEST_COMPARE(library.scripts().size(), 1);
        TEST_ASSERT(library.contains("archive OLD objects"));       // by name, any case
        TEST_ASSERT(library.contains("archive-old-objects"));       // by file name
        TEST_COMPARE(library.script("Archive old objects").code, script.code);
        TEST_ASSERT(!library.script("Archive old objects").readOnly);
        TEST_ASSERT(library.script("Archive old objects").filePath.endsWith("archive-old-objects.js"));
        TEST_ASSERT(!library.script("nothing").isValid());

        // Saved again: replaced, not doubled.
        script.code = "return 2;";
        TEST_ASSERT(library.save(script));
        TEST_COMPARE(library.scripts().size(), 1);
        TEST_COMPARE(library.script("Archive old objects").code, QString("return 2;"));

        // Shipped with the application: listed, never changed.
        TestSupport::writeFile(shippedDir.path() + "/count.js",
                               "// @name Count objects\n// @description Counts.\n\nreturn db.list().length;\n");
        TestSupport::writeFile(shippedDir.path() + "/archive.js",
                               "// @name Archive old objects\n\nreturn 'shipped';\n");
        library.addReadOnlyDirectory(shippedDir.path());
        TEST_COMPARE(library.readOnlyDirectories(), QStringList{shippedDir.path()});
        TEST_COMPARE(library.scripts().size(), 2);
        TEST_ASSERT(library.script("Count objects").readOnly);
        TEST_COMPARE(library.script("Count objects").origin, QString("application"));
        TEST_COMPARE(library.script("Archive old objects").code, QString("return 2;"));    // the user's wins
        TEST_ASSERT(!library.remove("Count objects", &error));
        TEST_ASSERT(QFile::exists(shippedDir.path() + "/count.js"));

        TEST_ASSERT(library.remove("Archive old objects"));
        TEST_COMPARE(library.script("Archive old objects").code, QString("return 'shipped';"));
        TEST_ASSERT(changes >= 4);
    }
};

TEST_INSTANTIATE(TST_ScriptLibrary);
