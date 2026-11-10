#include <iostream>
#include <QApplication>
#include "ScriptHost.h"
#include "tests.h"

#ifdef _MSC_VER
#include <crtdbg.h>
#include <cstdlib>
#endif
#include <cstdio>


int main(int argc, char* argv[])
{
#ifdef _MSC_VER
    // A failed assertion or abort() must end the run with a message on stderr.
    // The default is a modal dialog, which blocks a scripted run forever and
    // waits on a desktop nobody may be looking at.
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    for (int type : { _CRT_ASSERT, _CRT_ERROR }) {
        _CrtSetReportMode(type, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
        _CrtSetReportFile(type, _CRTDBG_FILE_STDERR);
    }
#endif
    // Unbuffered, so the log up to a crash is not lost with the buffer.
    setvbuf(stdout, nullptr, _IONBF, 0);

    // Method calls reach the application through the event loop, and the
    // widget tests build ScriptsWidget and ScriptEditorDialog: both need a
    // QApplication.
    QApplication app(argc, argv);

    ScriptHost::LibraryInfo::printInfo();

    std::cout << "Running " << UnitTest::Test::getTests().size() << " tests...\n";
    UnitTest::Test::TestResults results;
    UnitTest::Test::runAllTests(results);
    UnitTest::Test::printResults(results);

    return results.getSuccess();
}
