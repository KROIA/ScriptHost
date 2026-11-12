#pragma once
#include <QJsonObject>
#include <QRectF>
#include <QString>

class QObject;
class QWidget;

namespace ScriptHost
{

// What the ScriptUi*.cpp files share; defined in ScriptUi.cpp.
namespace ScriptUiSupport
{

// The schema of a `widget` parameter: an id or an objectName.
QJsonObject widgetSchema();
// A node for the examples of dry runs, with an id no widget has.
QJsonObject exampleNode();
// The id a script knows the widget by; given on first sight.
int widgetId(QWidget* widget);
// ScriptHost's own widgets and the ones the application hid (not their
// children: ScriptUi::isVisible() checks the ancestors too).
bool isExcluded(const QWidget* widget);
// A QLineEdit whose echo mode is not Normal.
bool isPassword(const QWidget* widget);
QString className(const QObject* object);
QJsonObject rectJson(const QRectF& rect);
// For a menu without a parent: the widget it was opened from (its menu
// action's menubar, menu, ...) that has a parent; else nullptr. Defined in
// ScriptUiText.cpp.
QWidget* popupOpener(const QWidget* widget);

} // namespace ScriptUiSupport
} // namespace ScriptHost
