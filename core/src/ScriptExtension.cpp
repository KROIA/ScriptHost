#include "ScriptExtension.h"
#include "ScriptRunner.h"
#include <QMutexLocker>
#include <QThread>
#include <utility>

namespace ScriptHost
{

namespace
{

// The runner, when called on its worker thread; else nullptr and a warning.
// Compared before it is used: on its own thread the runner exists.
ScriptRunner* workerRunner(const QSharedPointer<ScriptRunContext::Data>& d, const char* what)
{
    if (d && QThread::currentThread() == d->runner)
        return d->runner;
    qWarning("ScriptHost::ScriptRunContext: %s works only on the run's worker thread", what);
    return nullptr;
}

} // namespace

// ------------------------------------------------------------ extension

ScriptExtension::ScriptExtension(const QString& name)
    : m_name(name)
{
}

ScriptExtension::~ScriptExtension() = default;

// ------------------------------------------------------------ run context

ScriptEngine::RunId ScriptRunContext::runId() const
{
    return d ? d->runId : 0;
}

bool ScriptRunContext::isRequested() const
{
    return d && d->requested;
}

bool ScriptRunContext::isDryRun() const
{
    return d && d->dryRun;
}

bool ScriptRunContext::isActive() const
{
    if (!d)
        return false;
    QMutexLocker lock(&d->shared->mutex);
    return !d->shared->ended && !d->shared->stop;
}

void ScriptRunContext::throwError(const QString& message) const
{
    if (ScriptRunner* runner = workerRunner(d, "throwError()"))
        runner->throwError(message);
}

void ScriptRunContext::log(const QString& text) const
{
    if (ScriptRunner* runner = workerRunner(d, "log()"))
        runner->writeLog(text);
}

bool ScriptRunContext::call(const QJSValue& fn, const QJSValueList& args) const
{
    ScriptRunner* runner = workerRunner(d, "call()");
    return runner && runner->callScript(fn, args);
}

bool ScriptRunContext::schedule(const QString& key, std::function<void()> callback) const
{
    if (!d || !callback)
        return false;
    const QString full = key.isEmpty() ? QString() : d->keyPrefix + key;
    {
        QMutexLocker lock(&d->shared->mutex);
        if (d->shared->ended || d->shared->stop)
            return false;
        bool replaced = false;
        if (!full.isEmpty()) {
            for (auto& pending : d->shared->scheduled) {
                if (pending.first == full) {
                    // The old one leaves with `callback`, outside the lock.
                    std::swap(pending.second, callback);
                    replaced = true;
                    break;
                }
            }
        }
        if (!replaced)
            d->shared->scheduled.append({full, std::move(callback)});
        d->shared->wake.wakeAll();
    }
    return true;
}

void ScriptRunContext::setKeepAlive(const QString& key, bool keep) const
{
    if (!d)
        return;
    QMutexLocker lock(&d->shared->mutex);
    if (keep)
        d->shared->keepAlive.insert(d->keyPrefix + key);
    else
        d->shared->keepAlive.remove(d->keyPrefix + key);
    d->shared->wake.wakeAll();
}

} // namespace ScriptHost
