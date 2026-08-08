#pragma once

#include "providers/IArtworkProvider.h"

#include <QPointer>

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
    QPointer<AuthenticationService> m_authService;
};
