#include "ActiveArtworkProvider.h"

#include "network/AuthenticationService.h"
#include "providers/jellyfin/JellyfinArtworkProvider.h"
#include "providers/silo/SiloArtworkProvider.h"

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
    IArtworkProvider *provider = activeProvider();
    return provider ? provider->resolveArtwork(artwork) : std::nullopt;
}

void ActiveArtworkProvider::refreshArtwork(
    const Bloom::ArtworkRef &artwork,
    RefreshCallback completion) const
{
    if (!completion) {
        return;
    }

    IArtworkProvider *provider = activeProvider();
    if (!provider) {
        completion(std::nullopt);
        return;
    }
    provider->refreshArtwork(artwork, std::move(completion));
}

IArtworkProvider *ActiveArtworkProvider::activeProvider() const
{
    if (!m_authService) {
        return nullptr;
    }

    switch (m_authService->activeProviderKind()) {
    case ProviderKind::Jellyfin:
        return m_jellyfinProvider.get();
    case ProviderKind::Silo:
        return m_siloProvider.get();
    }
    return nullptr;
}
