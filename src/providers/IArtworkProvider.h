#pragma once

#include "models/MediaModels.h"

#include <QNetworkRequest>
#include <optional>

/**
 * @brief Resolves a token-free artwork identity into an authenticated fetch.
 *
 * Implementations must not use the resolved URL as a persistent cache key.
 */
class IArtworkProvider
{
public:
    virtual ~IArtworkProvider() = default;

    virtual std::optional<QNetworkRequest> resolveArtwork(
        const Bloom::ArtworkRef &artwork) const = 0;
};
