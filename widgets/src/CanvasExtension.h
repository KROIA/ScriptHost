#pragma once
// The `gfx` extension of a ScriptCanvas and its per-run object. Internal.
#include "CanvasFrame.h"
#include "ScriptExtension.h"
#include <QElapsedTimer>
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
    ~CanvasRunObject() override;

    Q_INVOKABLE void clear();
    Q_INVOKABLE void style(const QJSValue& style);
    Q_INVOKABLE void save();
    Q_INVOKABLE void restore();
    Q_INVOKABLE void translate(double x, double y);
    Q_INVOKABLE void rotate(double radians);
    Q_INVOKABLE void scale(double x, double y);
    Q_INVOKABLE void resetTransform();
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
    Q_INVOKABLE void drawPath(const QVector<double>& commands, const QJSValue& style);
    Q_INVOKABLE void image(const QString& name, double x, double y, const QJSValue& w, const QJSValue& h,
                           const QJSValue& source, const QJSValue& style);
    Q_INVOKABLE QVariantList imageSize(const QString& name) const;
    Q_INVOKABLE void rectAt(const QString& widget, const QJSValue& style);
    Q_INVOKABLE void roundedRectAt(const QString& widget, double radius, const QJSValue& style);
    Q_INVOKABLE void ellipseAt(const QString& widget, const QJSValue& style);
    Q_INVOKABLE void textAt(const QString& widget, const QString& text, const QJSValue& style);
    Q_INVOKABLE void arrowAt(const QString& from, double x1, double y1, const QString& to, double x2, double y2,
                             const QJSValue& style);
    Q_INVOKABLE QVariantList widgetRect(const QString& widget);
    Q_INVOKABLE void onFrame(const QJSValue& handler);
    Q_INVOKABLE void offFrame();
    Q_INVOKABLE QVariantList measureText(const QString& text, const QJSValue& font);
    Q_INVOKABLE QStringList drawNames() const;
    Q_INVOKABLE void custom(const QString& name, const QJSValue& args);
    Q_INVOKABLE void present();
    Q_INVOKABLE double width() const;
    Q_INVOKABLE double height() const;

    // A frame of gfx.onFrame(), scheduled by the canvas.
    void frame();

private:
    bool fail(const char* function, const QString& message) const;
    bool finite(const char* function, std::initializer_list<double> values) const;
    bool finite(const char* function, const QVector<double>& values, int group) const;
    bool readStyle(const QJSValue& value, CanvasStyle& style, const char* function);
    bool readFont(const QJSValue& value, CanvasStyle& style, const char* function);
    bool readColour(const QJSValue& value, QRgb& colour, const char* function);
    bool readGradient(const QJSValue& value, CanvasStyle& style, const char* function);
    // The index of the style in the frame; -1 when it threw.
    int styleFor(const QJSValue& value, const char* function);
    int addStyle(const CanvasStyle& style);
    int addText(const QString& text);
    void add(quint8 op, int style, const double* values, int count, QRectF bounds, int extra = -1,
             int anchor = -1, int anchor2 = -1);
    void addEllipse(double cx, double cy, double rx, double ry, const QJSValue& style, const char* function);
    void addPath(quint8 op, const QVector<double>& points, const QJSValue& style, const char* function);
    void addAnchored(quint8 op, const QString& widget, const double* values, int count, const QJSValue& style,
                     const char* function, int extra = -1);

    struct Saved
    {
        QTransform  transform;
        CanvasStyle base;
    };

    ScriptRunContext             m_context;
    QSharedPointer<CanvasShared> m_shared;
    quint64                      m_id;
    CanvasFrame                  m_frame;               // being drawn
    CanvasStyle                  m_base;                // gfx.style()
    int                          m_baseIndex = -1;      // m_base in m_frame.styles
    QTransform                   m_transform;
    int                          m_transformIndex = -1; // m_transform in m_frame.transforms
    QVector<Saved>               m_saved;               // gfx.save()
    QHash<QString, QRgb>         m_colours;             // parsed names
    QJSValue                     m_gradientValue;       // the last gradient read, and what it made
    QSharedPointer<QGradient>    m_gradient;
    QJSValue                     m_frameHandler;        // gfx.onFrame()
    QElapsedTimer                m_frameClock;
};

} // namespace ScriptHost
