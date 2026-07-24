#pragma once

#include "models/MediaModels.h"

#include <QJsonObject>
#include <QVariantMap>

enum class PlaybackReportEvent {
    Start,
    Progress,
    Pause,
    Resume,
    Stop
};

struct PlaybackReport
{
    PlaybackReportEvent event = PlaybackReportEvent::Progress;
    Bloom::MediaRef media;
    qint64 positionMs = -1;
    QString mediaVersionId;
    QString audioTrackId;
    QString subtitleTrackId;
    QString playbackSessionId;
    bool canSeek = true;
    bool isPaused = false;
    bool isMuted = false;
    QString playbackMethod;
    QString repeatMode;
    QString playbackOrder;
};

struct PlaybackReportRequest
{
    QString endpoint;
    QJsonObject body;
    bool deferSessionExpiry = true;

    [[nodiscard]] bool isValid() const { return !endpoint.isEmpty(); }
};

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

    virtual PlaybackReportRequest createReportRequest(const PlaybackReport &report) const = 0;
};
