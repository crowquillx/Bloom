#pragma once

#include "models/MediaModels.h"
#include "models/SubtitleSearch.h"
#include "network/Types.h"

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QStringList>
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

    bool replaySafe = false;

    [[nodiscard]] bool isValid() const { return !endpoint.isEmpty(); }
};

struct PlaybackStartParseResult
{
    Bloom::PlaybackDescriptor descriptor;
    bool valid = false;
    QString error;
};

struct PlaybackReplan
{
    // TrackChange asks for the selected tracks at positionMs; SeekReanchor asks
    // for a stream anchored at positionMs and keeps the current tracks.
    enum class Operation { TrackChange, SeekReanchor };
    Operation operation = Operation::TrackChange;
    int audioTrackIndex = -1;       // source audio index; -1 keeps the current one
    int subtitleTrackIndex = -1;    // source subtitle index; -1 with no id is off
    QString subtitleTrackId;        // provider track id when the source lacks the track
    qint64 positionMs = 0;          // source position
};

struct PlaybackReplanRequest
{
    QString endpoint;
    QString method = QStringLiteral("POST");
    QJsonObject body;

    bool replaySafe = false;

    [[nodiscard]] bool isValid() const { return !endpoint.isEmpty(); }
};

struct PlaybackReplanParseResult
{
    Bloom::PlaybackDescriptor descriptor;
    bool valid = false;
    QString error;
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

    bool replaySafe = false;

    [[nodiscard]] bool isValid() const { return !endpoint.isEmpty(); }
};

// How a failed progress report reflects the server-side playback session.
enum class PlaybackSessionLoss {
    None,    // Transient or unrelated failure; keep playing.
    Expired, // The session is gone; a fresh session at the same position recovers.
    Ended,   // The server ended playback; the player must leave and not restart.
};

// A subtitle search request: the server searches online subtitle providers
// for the playing file, or downloads one result for it.
struct SubtitleSearchRequest
{
    QString endpoint;
    QString method = QStringLiteral("GET");
    QJsonObject body;

    // Searches are reads; downloads are sent once (see each provider).
    bool replaySafe = false;

    [[nodiscard]] bool isValid() const { return !endpoint.isEmpty(); }
};

// The file a subtitle search is for: the playing item, its media version
// (Silo file, Jellyfin media source) and the playback session.
struct SubtitleSearchTarget
{
    Bloom::MediaRef media;
    QString mediaVersionId;
    QString playbackSessionId;
    // Subtitle stream indices the player already lists; a subtitle that
    // appears after a download is one of the others.
    QList<int> knownSubtitleIndexes;
};

struct SubtitleSearchStatus
{
    bool valid = false;
    bool available = false;
    QStringList providers;
};

struct SubtitleSearchParseResult
{
    bool valid = false;
    QString error;
    QList<Bloom::SubtitleSearchResult> results;
};

struct SubtitleDownloadParseResult
{
    bool valid = false;
    QString error;
    // Ready to load (Silo). Otherwise, with needsPlaybackInfo, the subtitle
    // becomes a stream of the media source and the caller finds it in a new
    // PlaybackInfo read (Jellyfin).
    Bloom::DownloadedSubtitle subtitle;
    bool needsPlaybackInfo = false;
};

struct PlaybackProviderContext
{
    QUrl serverUrl;
    QString accessToken;
    QString accountId;
    QString profileId;
    QString profileToken;
    QString clientName;
    QString clientVersion;
    QString deviceId;
    QString deviceName;
    QString devicePlatform;
    QString playbackAttemptId;
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

    virtual QString capabilitiesEndpoint() const { return {}; }
    virtual bool acceptCapabilities(const PlaybackProviderContext &, const QJsonObject &) const
    {
        return true;
    }

    virtual Bloom::PlaybackDescriptor createDescriptor(
        const PlaybackProviderContext &context,
        const Bloom::MediaRef &media,
        const QVariantMap &providerSource,
        int selectedAudioTrack,
        int selectedSubtitleTrack,
        qint64 startPositionMs,
        const QString &playbackSessionId = QString()) const = 0;

    virtual PlaybackInfoRequest createTrickplayInfoRequest(
        const PlaybackProviderContext &, const Bloom::MediaRef &, const QString &) const { return {}; }
    virtual TrickplayTileInfoMap parseTrickplayInfoResponse(
        const PlaybackProviderContext &, const Bloom::MediaRef &, const QJsonObject &,
        const QString &) const { return {}; }

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

    /// A new stream for an active session: a track change on a stream that
    /// carries only the selected tracks, or a seek outside what a server
    /// stream has produced. Empty when the provider has no such request.
    virtual PlaybackReplanRequest createReplanRequest(
        const PlaybackProviderContext &context,
        const QString &playbackSessionId,
        const PlaybackReplan &replan) const
    {
        Q_UNUSED(context)
        Q_UNUSED(playbackSessionId)
        Q_UNUSED(replan)
        return {};
    }

    virtual PlaybackReplanParseResult parseReplanResponse(
        const PlaybackProviderContext &context,
        const QJsonObject &wireResponse) const
    {
        Q_UNUSED(context)
        Q_UNUSED(wireResponse)
        return {};
    }

    /// Online subtitle search through the server (docs/playback.md,
    /// "Searching for subtitles"). Providers without it keep the defaults.
    virtual bool supportsSubtitleSearch() const { return false; }
    /// The capability document that gates the search. Empty when the
    /// provider has none and the search is always offered.
    virtual SubtitleSearchRequest createSubtitleSearchStatusRequest(
        const PlaybackProviderContext &context) const
    {
        Q_UNUSED(context)
        return {};
    }
    virtual SubtitleSearchStatus parseSubtitleSearchStatus(const QByteArray &body) const
    {
        Q_UNUSED(body)
        return {};
    }
    /// language is an ISO 639-2 code from the language picker ("eng").
    virtual SubtitleSearchRequest createSubtitleSearchRequest(
        const PlaybackProviderContext &context,
        const SubtitleSearchTarget &target,
        const QString &language) const
    {
        Q_UNUSED(context)
        Q_UNUSED(target)
        Q_UNUSED(language)
        return {};
    }
    virtual SubtitleSearchParseResult parseSubtitleSearchResponse(const QByteArray &body) const
    {
        Q_UNUSED(body)
        return {};
    }
    virtual SubtitleSearchRequest createSubtitleDownloadRequest(
        const PlaybackProviderContext &context,
        const SubtitleSearchTarget &target,
        const Bloom::SubtitleSearchResult &result) const
    {
        Q_UNUSED(context)
        Q_UNUSED(target)
        Q_UNUSED(result)
        return {};
    }
    virtual SubtitleDownloadParseResult parseSubtitleDownloadResponse(
        const PlaybackProviderContext &context,
        const SubtitleSearchTarget &target,
        const Bloom::SubtitleSearchResult &result,
        const QByteArray &body) const
    {
        Q_UNUSED(context)
        Q_UNUSED(target)
        Q_UNUSED(result)
        Q_UNUSED(body)
        return {};
    }

    virtual PlaybackReportRequest createReportRequest(const PlaybackReport &report) const = 0;
    /// True when a started session expires if its stream is not read soon,
    /// so later multipart segments must start their session when needed.
    virtual bool queuedSessionsExpire() const { return false; }
    virtual PlaybackSessionLoss classifyReportFailure(
        const PlaybackReport &report, int httpStatus) const
    {
        Q_UNUSED(report)
        Q_UNUSED(httpStatus)
        return PlaybackSessionLoss::None;
    }
};
