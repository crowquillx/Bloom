#pragma once

#include "models/MediaModels.h"

#include <QVariantMap>

struct PlaybackProviderContext
{
    QUrl serverUrl;
    QString accessToken;
};

/**
 * @brief Provider boundary that finalizes player-facing playback descriptors.
 *
 * The providerSource map is opaque outside the selected provider adapter.
 */
class IPlaybackProvider
{
public:
    virtual ~IPlaybackProvider() = default;

    virtual Bloom::PlaybackDescriptor createDescriptor(
        const PlaybackProviderContext &context,
        const Bloom::MediaRef &media,
        const QVariantMap &providerSource,
        int selectedAudioTrack,
        int selectedSubtitleTrack,
        qint64 startPositionMs,
        const QString &playbackSessionId = QString()) const = 0;
};
