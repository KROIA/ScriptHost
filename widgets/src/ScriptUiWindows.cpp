#include "ScriptUi.h"
#include "ItemGeometry.h"
#include "ScriptEngine.h"
#include "ScriptUiSupport.h"
#include <QApplication>
#include <QJsonArray>
#include <QScreen>
#include <QSet>
#include <QWidget>
#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

// ui.map, ui.window and what ui.info says of other windows on top (T-027).

namespace ScriptHost
{

using namespace ScriptUiSupport;
using ItemGeometry::rectJson;

namespace
{

const QStringList kSpaces{"widget", "root", "window", "frame", "screen"};

// The top left of a coordinate space in screen coordinates. Every space is
// the screen moved, so mapping is adding and subtracting these.
QPoint origin(const QString& space, const QWidget* widget, const QWidget* root)
{
    if (space == QLatin1String("widget"))
        return widget->mapToGlobal(QPoint());
    if (space == QLatin1String("root"))
        return root->mapToGlobal(QPoint());
    if (space == QLatin1String("window"))
        return widget->window()->mapToGlobal(QPoint());
    if (space == QLatin1String("frame"))
        return widget->window()->frameGeometry().topLeft();
    return QPoint();
}

QJsonObject windowJson(const QWidget* window)
{
    QJsonObject result{{"geometry", rectJson(window->geometry())},
                       {"frameGeometry", rectJson(window->frameGeometry())},
                       {"active", window->isActiveWindow()},
                       {"minimized", window->isMinimized()},
                       {"maximized", window->isMaximized()},
                       {"fullScreen", window->isFullScreen()},
                       {"modal", window->isModal()}};
    if (const QScreen* screen = window->screen()) {
        result.insert("screen", QJsonObject{{"name", screen->name()},
                                            {"geometry", rectJson(screen->geometry())},
                                            {"availableGeometry", rectJson(screen->availableGeometry())},
                                            {"devicePixelRatio", screen->devicePixelRatio()},
                                            {"logicalDpi", screen->logicalDotsPerInch()}});
    }
    return result;
}

// Is the top-level window `other` above `window`?
bool isAbove(const QWidget* other, const QWidget* window)
{
#ifdef Q_OS_WIN
    // The z-order of the desktop's top-level windows, from `window` up.
    const HWND target = reinterpret_cast<HWND>(other->effectiveWinId());
    for (HWND h = GetWindow(reinterpret_cast<HWND>(window->effectiveWinId()), GW_HWNDPREV); h;
         h = GetWindow(h, GW_HWNDPREV)) {
        if (h == target)
            return true;
    }
    return false;
#else
    // ponytail: Qt has no z-order of top-level windows; elsewhere a guess
    // from what is always on top. Ask the window system (X11, Cocoa) if
    // that matters there.
    return other->isActiveWindow() || other->windowType() == Qt::Popup || other->windowType() == Qt::ToolTip
        || (other->windowFlags() & Qt::WindowStaysOnTopHint)
        || (other->parentWidget() && other->parentWidget()->window() == window);
#endif
}

} // namespace

void ScriptUi::addCoverage(QJsonObject* info, QWidget* widget) const
{
    const QRect visible = ItemGeometry::visiblePart(widget, widget->rect());
    const QRect global(widget->mapToGlobal(visible.topLeft()), visible.size());
    QWidget* own = widget->window();
    bool covered = false;
    QJsonArray by;
    for (QWidget* other : QApplication::topLevelWidgets()) {
        // Overlays that let the mouse through do not count as covering.
        if (visible.isEmpty() || other == own || !other->isVisible() || other->isMinimized()
            || other->windowType() == Qt::Desktop || other->testAttribute(Qt::WA_TransparentForMouseEvents)
            || (other->windowFlags() & Qt::WindowTransparentForInput)
            || !other->frameGeometry().intersects(global) || !isAbove(other, own))
            continue;
        covered = true;
        // Windows outside what scripts see cover too, but have no id.
        if (isVisible(other))
            by.append(widgetId(other));
    }
    info->insert("covered", covered);
    if (covered)
        info->insert("coveredBy", by);
}

void ScriptUi::registerWindowMethods()
{
    const QJsonObject coordinate{{"type", "integer"}};
    const QJsonObject pointOrRect{
        {"type", "object"},
        {"description", "A point {x, y} or a rect {x, y, width, height}"},
        {"properties", QJsonObject{{"x", coordinate}, {"y", coordinate}, {"width", coordinate},
                                   {"height", coordinate}}},
        {"required", QJsonArray{"x", "y"}}};
    const QString spaces = QStringLiteral(
        "widget (the widget given), root, window (the widget's window - default the root's - without its "
        "frame), frame (that window with its frame) or screen (logical pixels on the desktop)");

    add(ScriptMethod(QStringLiteral("ui.map"))
            .setDescription(QStringLiteral("A point or rect from one coordinate space to another; returns the "
                                           "same shape. Spaces: ") + spaces + ".")
            .addRawParameter(QStringLiteral("widget"), widgetSchema(), false)
            .addRawParameter(QStringLiteral("pointOrRect"), pointOrRect)
            .addParameter(QStringLiteral("from"), QStringLiteral("string"), QStringLiteral("Its space"), true,
                          kSpaces)
            .addParameter(QStringLiteral("to"), QStringLiteral("string"), QStringLiteral("The space wanted"), true,
                          kSpaces)
            .setReturns(QStringLiteral("object"), QString(), QJsonObject{{"x", 120}, {"y", 340}}),
        [this](const QJsonObject& args) -> QJsonValue {
            QWidget* root = resolveRoot();
            const bool given = args.contains("widget");
            QWidget* widget = given ? resolve(args.value("widget")) : root;
            const QString from = args.value("from").toString();
            const QString to = args.value("to").toString();
            if (!given && (from == QLatin1String("widget") || to == QLatin1String("widget")))
                throw ScriptError(QStringLiteral("the space 'widget' needs a widget"));
            QJsonObject value = args.value("pointOrRect").toObject();
            if (value.contains("width") != value.contains("height"))
                throw ScriptError(QStringLiteral("a rect needs width and height"));
            const QPoint mapped = QPoint(value.value("x").toInt(), value.value("y").toInt())
                                + origin(from, widget, root) - origin(to, widget, root);
            value.insert("x", mapped.x());
            value.insert("y", mapped.y());
            return value;
        });

    const QJsonObject rect{{"x", 0}, {"y", 0}, {"width", 800}, {"height", 600}};
    add(ScriptMethod(QStringLiteral("ui.window"))
            .setDescription(QStringLiteral(
                "The top-level window of the widget (default: the root's): {id (null when scripts do not see "
                "it), title, geometry and frameGeometry in screen coordinates, active, minimized, maximized, "
                "fullScreen, modal, screen: {name, geometry, availableGeometry, devicePixelRatio, logicalDpi}}. "
                "Nodes of widgets in another window than the root's carry window: its id."))
            .addRawParameter(QStringLiteral("widget"), widgetSchema(), false)
            .setReturns(QStringLiteral("object"), QString(),
                        QJsonObject{{"id", -1}, {"title", "Main"}, {"geometry", rect}, {"frameGeometry", rect},
                                    {"active", true}, {"minimized", false}, {"maximized", false},
                                    {"fullScreen", false}, {"modal", false},
                                    {"screen", QJsonObject{{"name", "DISPLAY1"}, {"geometry", rect},
                                                           {"availableGeometry", rect},
                                                           {"devicePixelRatio", 1}, {"logicalDpi", 96}}}}),
        [this](const QJsonObject& args) -> QJsonValue {
            QWidget* window = (args.contains("widget") ? resolve(args.value("widget")) : resolveRoot())->window();
            QJsonObject result = windowJson(window);
            result.insert("id", isVisible(window) ? QJsonValue(widgetId(window)) : QJsonValue(QJsonValue::Null));
            if (m_textReadable)
                result.insert("title", window->windowTitle());
            return result;
        });
}

} // namespace ScriptHost
