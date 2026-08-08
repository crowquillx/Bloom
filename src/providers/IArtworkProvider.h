#pragma once

#include "models/MediaModels.h"

#include <QCoreApplication>
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
        deferRefresh(std::move(callback), resolveArtwork(artwork));
    }

protected:
    static void deferRefresh(RefreshCallback callback,
                              std::optional<QNetworkRequest> resolved = std::nullopt)
    {
        if (!callback) {
            return;
        }
        auto *context = QCoreApplication::instance();
        if (!context) {
            callback(std::move(resolved));
            return;
        }
        QTimer::singleShot(
            0,
            context,
            [callback = std::move(callback),
             resolved = std::move(resolved)]() mutable {
                callback(std::move(resolved));
            });
    }
};
