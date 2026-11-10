#include "InventoryWindow.h"
#include <QApplication>

int main(int argc, char* argv[])
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    // Qt 6 scales for high-DPI screens by default.
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif
    QApplication app(argc, argv);
    // QSettings and QStandardPaths::AppDataLocation are named after these:
    // the user's scripts and settings end up in their own folder.
    QApplication::setOrganizationName("ScriptHost");
    QApplication::setApplicationName("ScriptHostExample");

    ScriptHost::LibraryInfo::printInfo();

    InventoryWindow window;
    window.show();
    return app.exec();
}
