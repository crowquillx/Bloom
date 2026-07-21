#pragma once

#include "providers/IArtworkProvider.h"

class AuthenticationService;

class JellyfinArtworkProvider final : public IArtworkProvider
{
public:
    explicit JellyfinArtworkProvider(AuthenticationService *authService);

    std::optional<QNetworkRequest> resolveArtwork(
        const Bloom::ArtworkRef &artwork) const override;

private:
    AuthenticationService *m_authService = nullptr;
};
