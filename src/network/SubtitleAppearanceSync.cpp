#include "network/SubtitleAppearanceSync.h"

#include "network/AuthenticationService.h"
#include "network/LibraryService.h"
#include "utils/BloomLogging.h"
#include "utils/ConfigManager.h"

#include <utility>

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
    connect(&m_writeTimer, &QTimer::timeout, this, &SubtitleAppearanceSync::sendWrite);
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
    if (scope != m_scope) {
        m_document.clear();
    }
    if (scope != m_writeScope) {
        m_writeTimer.stop();
        m_writeQueued = false;
    } else if (std::exchange(m_writeQueued, false)) {
        // The write in flight is forgotten below; send the queued one anew.
        m_writeTimer.start();
    }
    // Answers to requests for the previous state are ignored from here on;
    // a session change may have dropped them without an answer.
    const quint64 generation = ++m_generation;
    m_writeInFlight = false;
    m_scope = scope;
    if (scope.isEmpty()) {
        return;
    }
    m_library->getSubtitleAppearance([this, generation, scope](bool ok, const QVariantMap &result) {
        if (generation != m_generation) {
            return;
        }
        if (!ok) {
            qCDebug(lcLibrary) << "Subtitle appearance unavailable; keeping the local one";
            return;
        }
        m_document = result.value(QStringLiteral("document")).toString();
        if (!result.value(QStringLiteral("hasValue")).toBool() || !m_config) {
            return;
        }
        const Bloom::SubtitleAppearance server = Bloom::SubtitleAppearance{}.withValues(
            result.value(QStringLiteral("appearance")).toMap());
        if (m_config->adoptSubtitleAppearance(server)) {
            qCInfo(lcLibrary) << "Adopted the server's subtitle appearance";
        }
    });
}

void SubtitleAppearanceSync::flushPendingWrite()
{
    if (m_writeTimer.isActive()) {
        m_writeTimer.stop();
        sendWrite();
    }
}

void SubtitleAppearanceSync::onEdited()
{
    const QString scope = m_library ? m_library->subtitleAppearanceScope() : QString();
    if (scope.isEmpty()) {
        return;
    }
    m_writeScope = scope;
    m_writeTimer.start();
}

void SubtitleAppearanceSync::onReset()
{
    m_writeTimer.stop();
    m_writeQueued = false;
    const QString scope = m_library ? m_library->subtitleAppearanceScope() : QString();
    if (scope.isEmpty()) {
        return;
    }
    const quint64 generation = m_generation;
    m_library->resetSubtitleAppearance([this, generation](bool ok, const QVariantMap &) {
        if (generation != m_generation) {
            return;
        }
        if (!ok) {
            qCDebug(lcLibrary) << "Could not remove this device's subtitle appearance override";
            return;
        }
        // The profile-wide appearance applies again; take it as the start.
        refresh();
    });
}

void SubtitleAppearanceSync::sendWrite()
{
    const QString scope = m_library ? m_library->subtitleAppearanceScope() : QString();
    if (scope.isEmpty() || scope != m_writeScope || !m_config) {
        // The change was made on another connection or profile.
        return;
    }
    if (m_writeInFlight) {
        m_writeQueued = true;
        return;
    }
    m_writeInFlight = true;
    const quint64 generation = m_generation;
    m_library->setSubtitleAppearance(
        m_config->getSubtitleAppearance().toVariantMap(),
        scope == m_scope ? m_document : QString(),
        [this, generation, scope](bool ok, const QVariantMap &result) {
        if (generation != m_generation) {
            return;
        }
        m_writeInFlight = false;
        if (ok) {
            m_scope = scope;
            const QString document = result.value(QStringLiteral("document")).toString();
            if (!document.isEmpty()) {
                m_document = document;
            }
        } else {
            qCDebug(lcLibrary) << "Could not save the subtitle appearance on the server";
        }
        if (std::exchange(m_writeQueued, false)) {
            sendWrite();
        }
    });
}
