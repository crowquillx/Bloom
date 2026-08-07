#pragma once

#include "models/MediaModels.h"

#include <QNetworkRequest>
#include <QTimer>
#include <functional>
#include <optional>
#include <utility>

/**
 * @brief Resolves artwork identity and its optional transient source into a fetch.
 *
 * Implementations must never use sourceUrl or a resolved URL as a persistent
 * cache key.
 */
class IArtworkProvider
{
public:
    virtual ~IArtworkProvider() = default;
    using RefreshCallback =
        std::function<void(std::optional<QNetworkRequest>)>;

    virtual std::optional<QNetworkRequest> resolveArtwork(
        const Bloom::ArtworkRef &artwork) const = 0;

    virtual void refreshArtwork(const Bloom::ArtworkRef &artwork,
                                RefreshCallback callback) const
    {
        if (!callback) {
            return;
        }
        auto resolved = resolveArtwork(artwork);
        QTimer::singleShot(
            0,
            [callback = std::move(callback),
             resolved = std::move(resolved)]() mutable {
                callback(std::move(resolved));
            });
    }
};
