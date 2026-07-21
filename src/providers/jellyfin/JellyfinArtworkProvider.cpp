#include "JellyfinArtworkProvider.h"

#include "network/AuthenticationService.h"
#include "providers/jellyfin/JellyfinModelMapper.h"
#include "utils/ConfigManager.h"

JellyfinArtworkProvider::JellyfinArtworkProvider(AuthenticationService *authService)
    : m_authService(authService)
{
}

std::optional<QNetworkRequest> JellyfinArtworkProvider::resolveArtwork(
    const Bloom::ArtworkRef &artwork) const
{
    if (!m_authService || !artwork.isValid() || !m_authService->isAuthenticated()) {
        return std::nullopt;
    }

    ConfigManager *config = m_authService->configManager();
    const auto connection = config ? config->getActiveConnection() : std::nullopt;
    if (!connection.has_value() || connection->connectionId != artwork.connectionId
        || connection->providerKind != ProviderKind::Jellyfin) {
        return std::nullopt;
    }

    const QString endpoint = JellyfinModelMapper::artworkEndpoint(artwork);
    if (endpoint.isEmpty()) {
        return std::nullopt;
    }
    return m_authService->createRequest(endpoint);
}
