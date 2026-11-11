#pragma once

// The extension TST_ScriptExtensions uses: `ext`, whose per-run object
// (ExtensionProbe) adds, throws, logs, holds the run and keeps a "tick"
// handler. It records what it was asked and on which thread, and hands the
// tests each run's context and object so that they can schedule into it.
// A header of its own: AUTOMOC compiles a Q_OBJECT header once more, and a
// test header's TEST_INSTANTIATE may exist only once.

#include "ScriptHost.h"
#include <QCoreApplication>
#include <QJSValue>
#include <QList>
#include <QMap>
#include <QMutex>
#include <QMutexLocker>
#include <QObject>
#include <QThread>

class TestExtension;

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
