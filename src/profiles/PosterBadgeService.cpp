#include "PosterBadgeService.h"

#include "network/AuthenticationService.h"
#include "network/ConnectionSessionPool.h"
#include "profiles/BloomProfileRepository.h"
#include "profiles/MergedCatalogService.h"
#include "providers/IProviderAdapter.h"
#include "utils/BloomLogging.h"
#include "utils/ConfigManager.h"

#include <QDateTime>

PosterBadgeService::PosterBadgeService(AuthenticationService *authService,
                                       BloomProfileRepository *repository,
                                       ConfigManager *configManager,
                                       MergedCatalogService *mergedCatalog,
                                       QObject *parent)
    : QObject(parent)
    , m_authService(authService)
    , m_repository(repository)
    , m_configManager(configManager)
    , m_mergedCatalog(mergedCatalog)
{
    if (m_repository) {
        connect(m_repository, &BloomProfileRepository::profilesChanged,
                this, &PosterBadgeService::reconcile);
        connect(m_repository, &BloomProfileRepository::activeProfileChanged,
                this, &PosterBadgeService::reconcile);
    }
    if (m_configManager) {
        connect(m_configManager, &ConfigManager::connectionsChanged, this, [this]() {
            updateHasBadgeConnections();
            reconcile();
        });
        connect(m_configManager, &ConfigManager::serverPosterBadgesEnabledChanged, this, [this]() {
            reconcile();
            bumpRevision();
        });
    }
    if (m_authService) {
        // The live session and pooled member sessions become readable after
        // startup restore, sign-in, or a space switch.
        connect(m_authService, &AuthenticationService::connectionActivated,
                this, &PosterBadgeService::reconcile);
        connect(m_authService, &AuthenticationService::authenticatedChanged,
                this, &PosterBadgeService::reconcile);
        if (m_authService->sessionPool()) {
            connect(m_authService->sessionPool(), &ConnectionSessionPool::connectionReady,
                    this, [this](const QString &, bool) { reconcile(); });
        }
    }
    updateHasBadgeConnections();
    reconcile();
}

QVariantList PosterBadgeService::badgesFor(const QVariantMap &item) const
{
    if (!featureEnabled()) {
        return {};
    }
    const auto layout = m_layouts.constFind(item.value(QStringLiteral("connectionId")).toString());
    if (layout == m_layouts.cend()) {
        return {};
    }
    return Bloom::resolvePosterBadges(*layout, item);
}

Bloom::PosterBadgeLayout PosterBadgeService::layoutFor(const QString &connectionId) const
{
    return m_layouts.value(connectionId);
}

void PosterBadgeService::refresh()
{
    if (!featureEnabled()) {
        return;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (const QString &connectionId : badgeConnections()) {
        if (!inFlight(connectionId)
            && now - m_attemptedAtMs.value(connectionId, 0) >= kRefreshAfterMs) {
            fetch(connectionId);
        }
    }
}

void PosterBadgeService::reconcile()
{
    const QStringList targets = badgeConnections();
    bool changed = false;
    for (auto it = m_layouts.begin(); it != m_layouts.end();) {
        if (!targets.contains(it.key())) {
            m_attemptedAtMs.remove(it.key());
            it = m_layouts.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }
    m_fetches.removeIf([&targets](const QHash<QString, Fetch>::iterator &it) {
        return !targets.contains(it.key());
    });
    if (featureEnabled()) {
        for (const QString &connectionId : targets) {
            if (!m_layouts.contains(connectionId) && !inFlight(connectionId)) {
                fetch(connectionId);
            }
        }
    }
    if (changed) {
        bumpRevision();
    }
}

bool PosterBadgeService::inFlight(const QString &connectionId) const
{
    // A space or membership change drops merged reads without completing
    // them, so only a read from the current epoch is still coming.
    const auto it = m_fetches.constFind(connectionId);
    return it != m_fetches.cend() && m_mergedCatalog
        && it->epoch == m_mergedCatalog->epoch();
}

void PosterBadgeService::fetch(const QString &connectionId)
{
    if (!m_mergedCatalog) {
        return;
    }
    m_attemptedAtMs.insert(connectionId, QDateTime::currentMSecsSinceEpoch());
    Fetch state;
    state.generation = ++m_generation;
    state.epoch = m_mergedCatalog->epoch();
    state.pending = 2;
    m_fetches.insert(connectionId, state);
    const quint64 generation = state.generation;
    for (const ProviderCatalogOperation operation : {ProviderCatalogOperation::PosterBadgeSettings,
                                                     ProviderCatalogOperation::PosterBadgeDefaults}) {
        m_mergedCatalog->readConnection(
            connectionId, operation, ProviderCatalogQuery{},
            [this, connectionId, generation, operation](
                const std::optional<ProviderCatalogResponse> &response) {
            const auto it = m_fetches.find(connectionId);
            if (it == m_fetches.end() || it->generation != generation) {
                return;
            }
            if (operation == ProviderCatalogOperation::PosterBadgeSettings) {
                if (response.has_value()) {
                    it->settings = response->rawItem;
                } else {
                    it->settingsFailed = true;
                }
            } else if (response.has_value()) {
                // Without the server-wide defaults the shipped defaults apply.
                it->defaults = response->rawItem;
            }
            if (--it->pending == 0) {
                finishFetch(connectionId, generation);
            }
        });
    }
}

void PosterBadgeService::finishFetch(const QString &connectionId, quint64 generation)
{
    const auto it = m_fetches.constFind(connectionId);
    if (it == m_fetches.cend() || it->generation != generation) {
        return;
    }
    const Fetch result = *it;
    m_fetches.remove(connectionId);
    if (result.settingsFailed) {
        // Keep the previous layout; the next refresh tries again.
        qCDebug(lcLibrary) << "Poster badge preferences unavailable for" << connectionId;
        return;
    }
    IProviderAdapter *adapter = m_authService
        ? m_authService->providerForConnection(connectionId) : nullptr;
    const std::optional<Bloom::PosterBadgeLayout> layout = adapter
        ? adapter->mapPosterBadgeLayout(result.settings, result.defaults) : std::nullopt;
    if (!layout.has_value()) {
        return;
    }
    const auto current = m_layouts.constFind(connectionId);
    if (current != m_layouts.cend() && *current == *layout) {
        return;
    }
    m_layouts.insert(connectionId, *layout);
    bumpRevision();
}

QStringList PosterBadgeService::badgeConnections() const
{
    QStringList candidates;
    const std::optional<BloomProfile> profile =
        m_repository ? m_repository->activeProfile() : std::nullopt;
    if (profile.has_value()) {
        for (const BloomProfileMember &member : profile->members) {
            if (member.enabled) {
                candidates.append(member.connectionId);
            }
        }
    } else if (m_authService && !m_authService->activeConnectionId().isEmpty()) {
        candidates.append(m_authService->activeConnectionId());
    }
    QStringList result;
    for (const QString &connectionId : std::as_const(candidates)) {
        const auto connection = m_configManager
            ? m_configManager->getConnection(connectionId) : std::nullopt;
        if (connection.has_value() && connection->providerKind == ProviderKind::Silo
            && !result.contains(connectionId)) {
            result.append(connectionId);
        }
    }
    return result;
}

void PosterBadgeService::updateHasBadgeConnections()
{
    bool any = false;
    if (m_configManager) {
        for (const ServerConnection &connection : m_configManager->getConnections()) {
            if (connection.providerKind == ProviderKind::Silo) {
                any = true;
                break;
            }
        }
    }
    if (any != m_hasBadgeConnections) {
        m_hasBadgeConnections = any;
        emit hasBadgeConnectionsChanged();
    }
}

bool PosterBadgeService::featureEnabled() const
{
    return !m_configManager || m_configManager->getServerPosterBadgesEnabled();
}

void PosterBadgeService::bumpRevision()
{
    ++m_revision;
    emit badgesChanged();
}
