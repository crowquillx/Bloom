#pragma once

#include "providers/IArtworkProvider.h"

class AuthenticationService;

class SiloArtworkProvider final : public IArtworkProvider
{
public:
    explicit SiloArtworkProvider(AuthenticationService *authService);

    std::optional<QNetworkRequest> resolveArtwork(
        const Bloom::ArtworkRef &artwork) const override;
    void refreshArtwork(const Bloom::ArtworkRef &artwork,
                        RefreshCallback callback) const override;

private:
    AuthenticationService *m_authService = nullptr;
};
