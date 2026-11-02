#include "network/SubtitleAppearanceSync.h"

#include "network/AuthenticationService.h"
#include "network/LibraryService.h"
#include "utils/BloomLogging.h"
#include "utils/ConfigManager.h"

SubtitleAppearanceSync::SubtitleAppearanceSync(ConfigManager *config,
                                               LibraryService *library,
                                               AuthenticationService *authService,
                                               QObject *parent)
    : QObject(parent)
    , m_config(config)
    , m_library(library)
{
    m_writeTimer.setSingleShot(true);
    m_writeTimer.setInterval(kWriteDebounceMs);
    connect(&m_writeTimer, &QTimer::timeout, this, &SubtitleAppearanceSync::pump);
    // Sign-in announces itself more than once; read once per burst.
    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(0);
    connect(&m_refreshTimer, &QTimer::timeout, this, &SubtitleAppearanceSync::refresh);

    if (m_config) {
        connect(m_config, &ConfigManager::subtitleAppearanceEdited,
                this, &SubtitleAppearanceSync::onEdited);
        connect(m_config, &ConfigManager::subtitleAppearanceReset,
                this, &SubtitleAppearanceSync::onReset);
    }
    if (authService) {
        const auto scheduleRefresh = [this]() { m_refreshTimer.start(); };
        connect(authService, &AuthenticationService::connectionActivated, this, scheduleRefresh);
        connect(authService, &AuthenticationService::loginSuccess, this, scheduleRefresh);
        connect(authService, &AuthenticationService::authenticatedChanged, this, scheduleRefresh);
    }
}

void SubtitleAppearanceSync::refresh()
{
    const QString scope = m_library ? m_library->subtitleAppearanceScope() : QString();
    if (m_inFlight != Request::None && m_inFlightScope != scope) {
        // A request for a scope that is no longer live cannot race this
        // scope's override; its mark stays for when that scope is back.
        ++m_generation;
        m_inFlight = Request::None;
    }
    m_readScope = scope;
    pump();
}

void SubtitleAppearanceSync::flushPendingWrite()
{
    if (m_writeTimer.isActive()) {
        m_writeTimer.stop();
        pump();
    }
}

void SubtitleAppearanceSync::onEdited()
{
    const QString scope = m_library ? m_library->subtitleAppearanceScope() : QString();
    if (scope.isEmpty() || !m_config) {
        return;
    }
    ++m_editSerial;
    m_config->setSubtitleAppearanceWritePending(scope, true);
    m_writeTimer.start();
    // The read a first write needs can go during the debounce.
    pump();
}

void SubtitleAppearanceSync::onReset()
{
    m_writeTimer.stop();
    const QString scope = m_library ? m_library->subtitleAppearanceScope() : QString();
    if (scope.isEmpty() || !m_config) {
        return;
    }
    m_config->setSubtitleAppearanceWritePending(scope, false);
    m_config->setSubtitleAppearanceDeletePending(scope, true);
    // Waits for a write still out, so the removal reaches the server last.
    pump();
}

void SubtitleAppearanceSync::pump()
{
    if (m_inFlight != Request::None || !m_library || !m_config) {
        return;
    }
    const QString scope = m_library->subtitleAppearanceScope();
    if (scope.isEmpty()) {
        return;
    }
    if (m_config->isSubtitleAppearanceDeletePending(scope)) {
        sendRemove(scope);
        return;
    }
    if (m_config->isSubtitleAppearanceWritePending(scope)) {
        if (!m_library->hasSubtitleAppearanceDocument(scope)) {
            sendRead(scope);
        } else if (!m_writeTimer.isActive()) {
            sendWrite(scope);
        }
        return;
    }
    if (m_readScope == scope) {
        sendRead(scope);
    }
}

bool SubtitleAppearanceSync::finish(quint64 generation, const QVariantMap &result, bool ok)
{
    if (generation != m_generation) {
        return false;
    }
    m_inFlight = Request::None;
    if (!ok && result.value(QStringLiteral("stale")).toBool()) {
        // The session changed under the request; look at the live scope
        // again once the change has settled.
        m_refreshTimer.start();
    }
    return true;
}

void SubtitleAppearanceSync::sendRead(const QString &scope)
{
    m_inFlight = Request::Read;
    m_inFlightScope = scope;
    if (m_readScope == scope) {
        m_readScope.clear();
    }
    const quint64 generation = m_generation;
    m_library->getSubtitleAppearance([this, generation, scope](bool ok, const QVariantMap &result) {
        if (!finish(generation, result, ok)) {
            return;
        }
        if (!ok) {
            qCDebug(lcLibrary) << "Subtitle appearance unavailable; keeping the local one";
            return;
        }
        // A pending write means the user changed it here; never adopt then.
        if (!m_config->isSubtitleAppearanceWritePending(scope)) {
            // No value on the server means its defaults: an appearance
            // adopted from another profile does not carry over.
            const Bloom::SubtitleAppearance server =
                result.value(QStringLiteral("hasValue")).toBool()
                ? Bloom::SubtitleAppearance{}.withValues(
                      result.value(QStringLiteral("appearance")).toMap())
                : Bloom::SubtitleAppearance{};
            if (m_config->adoptSubtitleAppearance(server, scope)) {
                qCInfo(lcLibrary) << "Adopted the server's subtitle appearance";
            }
        }
        pump();
    });
}

void SubtitleAppearanceSync::sendWrite(const QString &scope)
{
    m_inFlight = Request::Write;
    m_inFlightScope = scope;
    const quint64 generation = m_generation;
    const quint64 editSerial = m_editSerial;
    m_library->setSubtitleAppearance(
        m_config->getSubtitleAppearance().toVariantMap(),
        [this, generation, editSerial, scope](bool ok, const QVariantMap &result) {
        if (!finish(generation, result, ok)) {
            return;
        }
        if (!ok) {
            // Kept marked; sent again when the scope is next live.
            qCDebug(lcLibrary) << "Could not save the subtitle appearance on the server";
            return;
        }
        if (editSerial == m_editSerial) {
            m_config->setSubtitleAppearanceWritePending(scope, false);
        }
        pump();
    });
}

void SubtitleAppearanceSync::sendRemove(const QString &scope)
{
    m_inFlight = Request::Remove;
    m_inFlightScope = scope;
    const quint64 generation = m_generation;
    m_library->resetSubtitleAppearance([this, generation, scope](bool ok, const QVariantMap &result) {
        if (!finish(generation, result, ok)) {
            return;
        }
        if (!ok) {
            // Kept marked: nothing is read or written for this scope until
            // the removal goes through.
            qCDebug(lcLibrary) << "Could not remove this device's subtitle appearance override";
            return;
        }
        m_config->setSubtitleAppearanceDeletePending(scope, false);
        // The profile-wide appearance applies again; take it as the start.
        m_readScope = scope;
        pump();
    });
}
