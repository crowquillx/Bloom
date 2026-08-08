#pragma once

#include "providers/IArtworkProvider.h"

#include <memory>

class AuthenticationService;
class JellyfinArtworkProvider;
class SiloArtworkProvider;

class ActiveArtworkProvider final : public IArtworkProvider
{
public:
    explicit ActiveArtworkProvider(AuthenticationService *authService);
    ~ActiveArtworkProvider() override;

    std::optional<QNetworkRequest> resolveArtwork(
        const Bloom::ArtworkRef &artwork) const override;
    void refreshArtwork(const Bloom::ArtworkRef &artwork,
                        RefreshCallback completion) const override;

private:
    IArtworkProvider *activeProvider() const;

    AuthenticationService *m_authService = nullptr;
    std::unique_ptr<JellyfinArtworkProvider> m_jellyfinProvider;
    std::unique_ptr<SiloArtworkProvider> m_siloProvider;
};
