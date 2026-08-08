#pragma once

#include "models/MediaModels.h"
#include "network/Types.h"

#include <QJsonObject>
#include <QVariantMap>
#include <QUrl>

struct PlaybackProviderContext;

struct PlaybackInfoRequest
{
    QString endpoint;
    QString method = QStringLiteral("GET");
    QJsonObject body;

    [[nodiscard]] bool isValid() const { return !endpoint.isEmpty(); }
};

struct PlaybackInfoParseResult
{
    PlaybackInfoResponse response;
    bool valid = false;
    QString error;
};

struct PlaybackStartRequest
{
    QString endpoint;
    QString method = QStringLiteral("POST");
    QJsonObject body;

    [[nodiscard]] bool isValid() const { return !endpoint.isEmpty(); }
};

struct PlaybackStartParseResult
{
    Bloom::PlaybackDescriptor descriptor;
    bool valid = false;
    QString error;
};

struct PlaybackAudioSwitchRequest
{
    QString endpoint;
    QString method = QStringLiteral("PATCH");
    QJsonObject body;

    [[nodiscard]] bool isValid() const { return !endpoint.isEmpty(); }
};

struct PlaybackAudioSwitchParseResult
{
    QUrl reloadUrl;
    bool valid = false;
    QString error;
};

struct PlaybackRecoveryRequest
{
    QString endpoint;
    QString method = QStringLiteral("POST");
    QJsonObject body;

    [[nodiscard]] bool isValid() const { return !endpoint.isEmpty(); }
};

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
    QString method = QStringLiteral("POST");
    QString endpoint;
    QJsonObject body;
    bool deferSessionExpiry = true;

    [[nodiscard]] bool isValid() const { return !endpoint.isEmpty(); }
};

struct PlaybackProviderContext
{
    QUrl serverUrl;
    QString accessToken;
    QString profileId;
    QString profileToken;
    QString clientName;
    QString clientVersion;
    QString deviceId;
    QString deviceName;
    QString devicePlatform;
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

    virtual QUrl createTrickplayTileUrl(
        const PlaybackProviderContext &context,
        const QString &itemId,
        int width,
        int tileIndex) const = 0;
    virtual PlaybackInfoRequest createPlaybackInfoRequest(
        const PlaybackProviderContext &context,
        const Bloom::MediaRef &media,
        const QVariantMap &providerSource) const
    {
        Q_UNUSED(context)
        Q_UNUSED(media)
        Q_UNUSED(providerSource)
        return {};
    }

    virtual PlaybackInfoParseResult parsePlaybackInfoResponse(
        const PlaybackProviderContext &context,
        const Bloom::MediaRef &media,
        const QJsonObject &wireResponse,
        const QVariantMap &providerSource = {}) const
    {
        Q_UNUSED(context)
        Q_UNUSED(media)
        Q_UNUSED(wireResponse)
        Q_UNUSED(providerSource)
        return {};
    }

    virtual PlaybackStartRequest createPlaybackStartRequest(
        const PlaybackProviderContext &context,
        const Bloom::MediaRef &media,
        const QVariantMap &providerSource,
        int selectedAudioTrack,
        int selectedSubtitleTrack,
        qint64 startPositionMs) const
    {
        Q_UNUSED(context)
        Q_UNUSED(media)
        Q_UNUSED(providerSource)
        Q_UNUSED(selectedAudioTrack)
        Q_UNUSED(selectedSubtitleTrack)
        Q_UNUSED(startPositionMs)
        return {};
    }

    virtual PlaybackStartParseResult parsePlaybackStartResponse(
        const PlaybackProviderContext &context,
        const Bloom::MediaRef &media,
        const QJsonObject &wireResponse,
        const QVariantMap &providerSource = {}) const
    {
        Q_UNUSED(context)
        Q_UNUSED(media)
        Q_UNUSED(wireResponse)
        Q_UNUSED(providerSource)
        return {};
    }

    virtual PlaybackAudioSwitchRequest createAudioSwitchRequest(
        const PlaybackProviderContext &context,
        const QString &playbackSessionId,
        int audioTrackIndex,
        qint64 positionMs) const
    {
        Q_UNUSED(context)
        Q_UNUSED(playbackSessionId)
        Q_UNUSED(audioTrackIndex)
        Q_UNUSED(positionMs)
        return {};
    }

    virtual PlaybackAudioSwitchParseResult parseAudioSwitchResponse(
        const PlaybackProviderContext &context,
        const QJsonObject &wireResponse) const
    {
        Q_UNUSED(context)
        Q_UNUSED(wireResponse)
        return {};
    }

    virtual PlaybackRecoveryRequest createPlaybackRecoveryRequest(
        const PlaybackProviderContext &context,
        const Bloom::MediaRef &media,
        const QVariantMap &providerSource,
        qint64 startPositionMs) const
    {
        Q_UNUSED(context)
        Q_UNUSED(media)
        Q_UNUSED(providerSource)
        Q_UNUSED(startPositionMs)
        return {};
    }

    virtual PlaybackStartParseResult parsePlaybackRecoveryResponse(
        const PlaybackProviderContext &context,
        const Bloom::MediaRef &media,
        const QJsonObject &wireResponse,
        const QVariantMap &providerSource = {}) const
    {
        return parsePlaybackStartResponse(context, media, wireResponse, providerSource);
    }
    virtual PlaybackReportRequest createReportRequest(const PlaybackReport &report) const = 0;
};
