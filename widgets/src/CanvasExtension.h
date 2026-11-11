#pragma once
// The `gfx` extension of a ScriptCanvas and its per-run object. Internal.
#include "CanvasFrame.h"
#include "ScriptExtension.h"
#include <QHash>
#include <QJSValue>
#include <QObject>
#include <QSharedPointer>
#include <QVector>
#include <initializer_list>

namespace ScriptHost
{

class CanvasExtension : public ScriptExtension
{
public:
    explicit CanvasExtension(const QSharedPointer<CanvasShared>& shared);

    QStringList members() const override;
    QString declaration() const override;
    QObject* createRunObject(const ScriptRunContext& context) override;
    QString glue() const override;
    void runEnded(ScriptEngine::RunId id) override;

private:
    QSharedPointer<CanvasShared> m_shared;
};

// What a run's `gfx` calls, on the run's worker thread: records into the
// frame being drawn; present() hands it over. The glue passes every argument,
// so a missing one arrives as NaN or undefined.
class CanvasRunObject : public QObject
{
    Q_OBJECT
public:
    CanvasRunObject(const ScriptRunContext& context, const QSharedPointer<CanvasShared>& shared);

    Q_INVOKABLE void clear();
    Q_INVOKABLE void style(const QJSValue& style);
    Q_INVOKABLE void rect(double x, double y, double w, double h, const QJSValue& style);
    Q_INVOKABLE void roundedRect(double x, double y, double w, double h, double radius, const QJSValue& style);
    Q_INVOKABLE void circle(double cx, double cy, double r, const QJSValue& style);
    Q_INVOKABLE void ellipse(double cx, double cy, double rx, double ry, const QJSValue& style);
    Q_INVOKABLE void line(double x1, double y1, double x2, double y2, const QJSValue& style);
    Q_INVOKABLE void arrow(double x1, double y1, double x2, double y2, const QJSValue& style);
    Q_INVOKABLE void polyline(const QVector<double>& points, const QJSValue& style);
    Q_INVOKABLE void polygon(const QVector<double>& points, const QJSValue& style);
    Q_INVOKABLE void text(double x, double y, const QString& text, const QJSValue& style);
    Q_INVOKABLE void rects(const QVector<double>& values, const QJSValue& style);
    Q_INVOKABLE void circles(const QVector<double>& values, const QJSValue& style);
    Q_INVOKABLE void lines(const QVector<double>& values, const QJSValue& style);
    Q_INVOKABLE void present();
    Q_INVOKABLE double width() const;
    Q_INVOKABLE double height() const;

private:
    bool fail(const char* function, const QString& message) const;
    bool finite(const char* function, std::initializer_list<double> values) const;
    bool finite(const char* function, const QVector<double>& values, int group) const;
    bool readStyle(const QJSValue& value, CanvasStyle& style, const char* function);
    bool readColour(const QJSValue& value, QRgb& colour, const char* function);
    // The index of the style in the frame; -1 when it threw.
    int styleFor(const QJSValue& value, const char* function);
    int addStyle(const CanvasStyle& style);
    void add(quint8 op, int style, const double* values, int count, const QRectF& bounds, int text = -1);
    void addEllipse(double cx, double cy, double rx, double ry, const QJSValue& style, const char* function);
    void addPath(quint8 op, const QVector<double>& points, const QJSValue& style, const char* function);

    ScriptRunContext             m_context;
    QSharedPointer<CanvasShared> m_shared;
    quint64                      m_id;
    CanvasFrame                  m_frame;           // being drawn
    CanvasStyle                  m_base;            // gfx.style()
    int                          m_baseIndex = -1;  // m_base in m_frame.styles
    QHash<QString, QRgb>         m_colours;         // parsed names
};

} // namespace ScriptHost
