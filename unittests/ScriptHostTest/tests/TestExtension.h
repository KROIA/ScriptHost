#pragma once

// The extension TST_ScriptExtensions uses: `ext`, whose per-run object
// (ExtensionProbe) adds, throws, logs, holds the run and keeps a "tick"
// handler. It records what it was asked and on which thread, and hands the
// tests each run's context and object so that they can schedule into it.
// A header of its own: AUTOMOC compiles a Q_OBJECT header once more, and a
// test header's TEST_INSTANTIATE may exist only once.

#include "ScriptHost.h"
#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QJSValue>
#include <QList>
#include <QMap>
#include <QMutex>
#include <QMutexLocker>
#include <QObject>
#include <QThread>
#include <QVector>
#include <atomic>
#include <functional>

class TestExtension;

// `first`: methods that no other test calls, so that
// TST_ScriptExtensions::testFirstCallsAtOnce makes their first calls in the
// process (I-016).
class FirstCallProbe : public QObject
{
    Q_OBJECT
public:
    // Waits until `expected` runs are here, so that they call at once.
    Q_INVOKABLE void together() const
    {
        ++arrived;
        QDeadlineTimer deadline(5000);
        while (arrived < expected && !deadline.hasExpired())
            QThread::yieldCurrentThread();
    }
    Q_INVOKABLE double s0(const QVector<double>& v) const { return v.value(0); }
    Q_INVOKABLE double s1(const QVector<double>& v) const { return v.value(0); }
    Q_INVOKABLE double s2(const QVector<double>& v) const { return v.value(0); }
    Q_INVOKABLE double s3(const QVector<double>& v) const { return v.value(0); }
    Q_INVOKABLE double s4(const QVector<double>& v) const { return v.value(0); }
    Q_INVOKABLE double s5(const QVector<double>& v) const { return v.value(0); }
    Q_INVOKABLE double s6(const QVector<double>& v) const { return v.value(0); }
    Q_INVOKABLE double s7(const QVector<double>& v) const { return v.value(0); }
    Q_INVOKABLE double s8(const QVector<double>& v) const { return v.value(0); }
    Q_INVOKABLE double s9(const QVector<double>& v) const { return v.value(0); }
    Q_INVOKABLE double s10(const QVector<double>& v) const { return v.value(0); }
    Q_INVOKABLE double s11(const QVector<double>& v) const { return v.value(0); }
    Q_INVOKABLE double s12(const QVector<double>& v) const { return v.value(0); }
    Q_INVOKABLE double s13(const QVector<double>& v) const { return v.value(0); }
    Q_INVOKABLE double s14(const QVector<double>& v) const { return v.value(0); }
    Q_INVOKABLE double s15(const QVector<double>& v) const { return v.value(0); }
    Q_INVOKABLE double s16(const QVector<double>& v) const { return v.value(0); }
    Q_INVOKABLE double s17(const QVector<double>& v) const { return v.value(0); }
    Q_INVOKABLE double s18(const QVector<double>& v) const { return v.value(0); }
    Q_INVOKABLE double s19(const QVector<double>& v) const { return v.value(0); }
    Q_INVOKABLE double s20(const QVector<double>& v) const { return v.value(0); }
    Q_INVOKABLE double s21(const QVector<double>& v) const { return v.value(0); }
    Q_INVOKABLE double s22(const QVector<double>& v) const { return v.value(0); }
    Q_INVOKABLE double s23(const QVector<double>& v) const { return v.value(0); }
    Q_INVOKABLE double s24(const QVector<double>& v) const { return v.value(0); }
    Q_INVOKABLE double s25(const QVector<double>& v) const { return v.value(0); }
    Q_INVOKABLE double s26(const QVector<double>& v) const { return v.value(0); }
    Q_INVOKABLE double s27(const QVector<double>& v) const { return v.value(0); }
    Q_INVOKABLE double s28(const QVector<double>& v) const { return v.value(0); }
    Q_INVOKABLE double s29(const QVector<double>& v) const { return v.value(0); }
    Q_INVOKABLE double s30(const QVector<double>& v) const { return v.value(0); }
    Q_INVOKABLE double s31(const QVector<double>& v) const { return v.value(0); }

    static inline std::atomic<int> arrived{0};
    static inline int              expected = 0;
};

class FirstCallExtension : public ScriptHost::ScriptExtension
{
public:
    FirstCallExtension()
        : ScriptExtension("first")
    {
    }
    QStringList members() const override
    {
        QStringList names{"together"};
        for (int i = 0; i < 32; ++i)
            names << QString("s%1").arg(i);
        return names;
    }
    QString declaration() const override { return "declare namespace first {}\n"; }
    QObject* createRunObject(const ScriptHost::ScriptRunContext&) override { return new FirstCallProbe; }
};

class ExtensionProbe : public QObject
{
    Q_OBJECT
public:
    ExtensionProbe(const ScriptHost::ScriptRunContext& context, TestExtension* extension)
        : m_context(context)
        , m_extension(extension)
    {
    }
    ~ExtensionProbe() override;

    Q_INVOKABLE double add(double a, double b) const { return a + b; }
    Q_INVOKABLE void fail(const QString& message) const { m_context.throwError(message); }
    Q_INVOKABLE void note(const QString& text) const { m_context.log(text); }
    Q_INVOKABLE void hold(bool keep) const { m_context.setKeepAlive("hold", keep); }
    Q_INVOKABLE void onTick(const QJSValue& fn)
    {
        m_tick = fn.isCallable() ? fn : QJSValue();
        m_context.setKeepAlive("tick", fn.isCallable());
    }
    Q_INVOKABLE bool isRequested() const { return m_context.isRequested(); }
    Q_INVOKABLE bool isDryRun() const { return m_context.isDryRun(); }
    Q_INVOKABLE double runId() const { return double(m_context.runId()); }

    // Worker thread, from a scheduled callback: calls the "tick" handler.
    void tick(int n) const
    {
        if (m_tick.isCallable())
            m_context.call(m_tick, {QJSValue(n)});
    }

private:
    ScriptHost::ScriptRunContext m_context;
    TestExtension*               m_extension;
    QJSValue                     m_tick;
};

class TestExtension : public ScriptHost::ScriptExtension
{
    Q_OBJECT
public:
    using RunId = ScriptHost::ScriptEngine::RunId;

    explicit TestExtension(const QString& name = "ext", const QString& glue = QString())
        : ScriptExtension(name)
        , m_glue(glue)
    {
    }

    QStringList members() const override
    {
        return {"add", "fail", "note", "hold", "onTick", "isRequested", "isDryRun", "runId",
                "info.version", "info.build.date", "twice"};
    }
    QString declaration() const override
    {
        return "declare namespace " + name() + " {\n"
               "  /** Adds two numbers in the extension. */\n"
               "  function add(a: number, b: number): number;\n"
               "}\n";
    }
    QObject* createRunObject(const ScriptHost::ScriptRunContext& context) override
    {
        if (beforeCreate)
            beforeCreate();
        auto* probe = new ExtensionProbe(context, this);
        QMutexLocker lock(&m_mutex);
        ++created;
        createdOnWorker = createdOnWorker && QThread::currentThread() != qApp->thread();
        Slot& slot = m_slots[context.runId()];
        slot.context = context;
        slot.probe = probe;
        return probe;
    }
    QString glue() const override { return m_glue; }
    void runEnded(RunId id) override
    {
        QMutexLocker lock(&m_mutex);
        ended << id;
        endedOnGui = endedOnGui && QThread::currentThread() == qApp->thread();
        // The per-run object is gone by now, if there was one.
        probeGoneAtEnd = probeGoneAtEnd && !m_slots.value(id).probe;
    }

    ScriptHost::ScriptRunContext context(RunId id) const
    {
        QMutexLocker lock(&m_mutex);
        return m_slots.value(id).context;
    }
    ExtensionProbe* probe(RunId id) const
    {
        QMutexLocker lock(&m_mutex);
        return m_slots.value(id).probe;
    }
    bool hasEnded(RunId id) const
    {
        QMutexLocker lock(&m_mutex);
        return ended.contains(id);
    }
    void probeDeleted(const ExtensionProbe* probe)
    {
        QMutexLocker lock(&m_mutex);
        for (Slot& slot : m_slots) {
            if (slot.probe == probe)
                slot.probe = nullptr;
        }
    }

    // Called on the worker thread as a run installs the extension.
    std::function<void()> beforeCreate;

    // Under the mutex, or read when no run is active.
    int          created = 0;
    bool         createdOnWorker = true;
    bool         endedOnGui = true;
    bool         probeGoneAtEnd = true;
    QList<RunId> ended;

private:
    struct Slot
    {
        ScriptHost::ScriptRunContext context;
        ExtensionProbe*              probe = nullptr;
    };
    mutable QMutex       m_mutex;
    QMap<RunId, Slot>    m_slots;
    QString              m_glue;
};

inline ExtensionProbe::~ExtensionProbe()
{
    m_extension->probeDeleted(this);
}
