#include "ScriptInput.h"
#include "ScriptEngine.h"
#include <QCursor>
#include <QGuiApplication>
#include <QJsonArray>
#include <QKeyEvent>
#include <QMetaEnum>
#include <QMouseEvent>
#include <QPointer>
#include <QWheelEvent>
#include <QWidget>

namespace ScriptHost
{

namespace
{

const char* const kMouseMethod = "input.mouse";
// Engine property: the forwarder whose input.mouse the engine has.
const char* const kOwner = "scripthost_inputForwarder";

QJsonArray modifierNames(Qt::KeyboardModifiers modifiers)
{
    QJsonArray names;
    if (modifiers & Qt::ShiftModifier)
        names.append(QStringLiteral("shift"));
    if (modifiers & Qt::ControlModifier)
        names.append(QStringLiteral("control"));
    if (modifiers & Qt::AltModifier)
        names.append(QStringLiteral("alt"));
    if (modifiers & Qt::MetaModifier)
        names.append(QStringLiteral("meta"));
    return names;
}

QString buttonName(Qt::MouseButton button)
{
    switch (button) {
    case Qt::NoButton:      return QStringLiteral("none");
    case Qt::LeftButton:    return QStringLiteral("left");
    case Qt::RightButton:   return QStringLiteral("right");
    case Qt::MiddleButton:  return QStringLiteral("middle");
    case Qt::BackButton:    return QStringLiteral("back");
    case Qt::ForwardButton: return QStringLiteral("forward");
    default:                return QStringLiteral("other");
    }
}

QJsonArray buttonNames(Qt::MouseButtons buttons)
{
    QJsonArray names;
    for (Qt::MouseButton button : {Qt::LeftButton, Qt::RightButton, Qt::MiddleButton, Qt::BackButton,
                                   Qt::ForwardButton}) {
        if (buttons & button)
            names.append(buttonName(button));
    }
    return names;
}

// Qt::Key_PageUp -> "PageUp"; empty for a value without a name.
QString keyName(int key)
{
    return QString::fromLatin1(QMetaEnum::fromType<Qt::Key>().valueToKey(key)).mid(4);
}

class InputForwarder : public QObject
{
public:
    InputForwarder(ScriptEngine* engine, QWidget* source)
        : QObject(source)
        , m_engine(engine)
    {
        source->installEventFilter(this);
    }

    ~InputForwarder() override
    {
        if (m_engine && m_engine->property(kOwner).toULongLong() == qulonglong(quintptr(this))) {
            m_engine->unregisterMethod(QString::fromLatin1(kMouseMethod));
            m_engine->setProperty(kOwner, QVariant());
        }
    }

    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (m_engine)
            post(event);
        return QObject::eventFilter(watched, event);
    }

private:
    void post(QEvent* event)
    {
        switch (event->type()) {
        case QEvent::KeyPress:
        case QEvent::KeyRelease: {
            const auto* key = static_cast<QKeyEvent*>(event);
            m_engine->postEvent(QStringLiteral("key"), QJsonObject{
                {"type", event->type() == QEvent::KeyPress ? "down" : "up"},
                {"key", keyName(key->key())},
                {"code", key->key()},
                {"text", key->text()},
                {"modifiers", modifierNames(key->modifiers())},
                {"repeat", key->isAutoRepeat()}});
            break;
        }
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonRelease:
        case QEvent::MouseButtonDblClick: {
            const auto* mouse = static_cast<QMouseEvent*>(event);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
            const QPoint at = mouse->position().toPoint();
#else
            const QPoint at = mouse->pos();
#endif
            const QString type = event->type() == QEvent::MouseButtonPress     ? QStringLiteral("down")
                                 : event->type() == QEvent::MouseButtonRelease ? QStringLiteral("up")
                                                                               : QStringLiteral("dblclick");
            m_engine->postEvent(QStringLiteral("mouse"), QJsonObject{
                {"type", type},
                {"button", buttonName(mouse->button())},
                {"buttons", buttonNames(mouse->buttons())},
                {"x", at.x()},
                {"y", at.y()},
                {"modifiers", modifierNames(mouse->modifiers())}});
            break;
        }
        case QEvent::Wheel: {
            const auto* wheel = static_cast<QWheelEvent*>(event);
            const QPoint at = wheel->position().toPoint();
            m_engine->postEvent(QStringLiteral("mouse"), QJsonObject{
                {"type", "wheel"},
                {"button", "none"},
                {"buttons", buttonNames(wheel->buttons())},
                {"x", at.x()},
                {"y", at.y()},
                {"modifiers", modifierNames(wheel->modifiers())},
                {"deltaX", wheel->angleDelta().x()},
                {"deltaY", wheel->angleDelta().y()}});
            break;
        }
        default:
            break;
        }
    }

    QPointer<ScriptEngine> m_engine;
};

} // namespace

QObject* forwardInput(ScriptEngine* engine, QWidget* source)
{
    auto* forwarder = new InputForwarder(engine, source);

    ScriptMethod mouse(QString::fromLatin1(kMouseMethod));
    mouse.setDescription(QStringLiteral("Where the mouse pointer is, relative to the application's input area, "
                                        "and which buttons are held."))
         .setGroup(QStringLiteral("Input")).setReadOnly(true)
         .setReturns(QStringLiteral("object"), QStringLiteral("{x, y, inside, buttons}"),
                     QJsonObject{{"x", 10}, {"y", 20}, {"inside", true}, {"buttons", QJsonArray{"left"}}});
    const QPointer<QWidget> area(source);
    engine->registerMethod(mouse, [engine, area](const QJsonObject&) -> QJsonValue {
        // Requested runs: the same opt-in as the "mouse" events.
        if (!engine->isEventAllowedForRun(engine->callingRun(), QStringLiteral("mouse")))
            throw ScriptError(QStringLiteral("the application does not let requested scripts read the mouse"));
        if (!area)
            throw ScriptError(QStringLiteral("the input area is gone"));
        const QPoint at = area->mapFromGlobal(QCursor::pos());
        return QJsonObject{{"x", at.x()}, {"y", at.y()}, {"inside", area->rect().contains(at)},
                           {"buttons", buttonNames(QGuiApplication::mouseButtons())}};
    });
    engine->setProperty(kOwner, qulonglong(quintptr(forwarder)));
    return forwarder;
}

} // namespace ScriptHost
