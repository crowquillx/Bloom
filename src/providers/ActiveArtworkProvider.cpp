#include "ActiveArtworkProvider.h"

#include "network/AuthenticationService.h"
#include "providers/jellyfin/JellyfinArtworkProvider.h"
#include "providers/silo/SiloArtworkProvider.h"
#include "utils/ConfigManager.h"

#include <utility>

ActiveArtworkProvider::ActiveArtworkProvider(AuthenticationService *authService)
    : m_authService(authService)
    , m_jellyfinProvider(std::make_unique<JellyfinArtworkProvider>(authService))
    , m_siloProvider(std::make_unique<SiloArtworkProvider>(authService))
{
}

ActiveArtworkProvider::~ActiveArtworkProvider() = default;

std::optional<QNetworkRequest> ActiveArtworkProvider::resolveArtwork(
    const Bloom::ArtworkRef &artwork) const
{
    IArtworkProvider *provider = providerFor(artwork);
    return provider ? provider->resolveArtwork(artwork) : std::nullopt;
}

void ActiveArtworkProvider::refreshArtwork(
    const Bloom::ArtworkRef &artwork,
    RefreshCallback completion) const
{
    if (!completion) {
        return;
    }

    IArtworkProvider *provider = providerFor(artwork);
    if (!provider) {
        deferRefresh(std::move(completion));
        return;
    }
    provider->refreshArtwork(artwork, std::move(completion));
}

IArtworkProvider *ActiveArtworkProvider::providerFor(const Bloom::ArtworkRef &artwork) const
{
    if (!m_authService) {
        return nullptr;
    }

    // Merged Bloom profiles show artwork from several connections at once, so
    // the owning connection, not the active session, selects the provider.
    ConfigManager *config = m_authService->configManager();
    const auto connection = config ? config->getConnection(artwork.connectionId) : std::nullopt;
    const ProviderKind kind = connection.has_value()
        ? connection->providerKind : m_authService->activeProviderKind();
    switch (kind) {
    case ProviderKind::Jellyfin:
        return m_jellyfinProvider.get();
    case ProviderKind::Silo:
        return m_siloProvider.get();
    }
    return nullptr;
}
