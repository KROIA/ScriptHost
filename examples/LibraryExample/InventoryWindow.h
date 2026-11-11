#pragma once
#include "ScriptHostWidgets.h"
#include <QMainWindow>
#include <QPointer>

class QPlainTextEdit;
class QTableWidget;

// A small inventory application that lets its user - and other programs -
// script it. InventoryWindow.cpp shows, in order, what an application has to
// do for that.
class InventoryWindow : public QMainWindow
{
public:
    explicit InventoryWindow(QWidget* parent = nullptr);

private:
    void setUpScripting();
    void registerMethods();
    void buildToolBar();

    void openScripts();
    void newScript();
    void runRestock(bool dryRun);
    void startMonitor();
    void requestScript();

    void saveScriptSettings();
    void appendLog(const QString& text);

    QTableWidget*   m_table = nullptr;
    QPlainTextEdit* m_log = nullptr;

    ScriptHost::ScriptEngine*  m_engine = nullptr;
    ScriptHost::ScriptLibrary* m_library = nullptr;
    QPointer<QWidget>          m_scriptsWindow;
};
