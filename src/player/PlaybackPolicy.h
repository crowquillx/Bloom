#pragma once

#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include "utils/TrackPreferencesManager.h"

namespace Bloom::PlaybackPolicy {

enum class HdrContentKind {
    Sdr,
    Hdr,
    DolbyVisionCompatible,
    DolbyVisionUnsupported
};

struct HdrPlaybackPolicy
{
    bool toneMapToSdr = false;
    bool outputHdr = false;
    bool shouldToggleDisplayHdr = false;
};

struct ResolvedTrackSelection
{
    int audioIndex = -1;
    int subtitleIndex = -1;
    QString audioSource;
    QString subtitleSource;
};

/// How a fresh playback request picks among an item's versions.
enum class VersionPreference {
    HighestQuality,
    Smallest,
    Ask
};

struct VersionAffinity
{
    QString parentPath;
    QString name;
    QString signature;
};

struct CompletionInput
{
    bool alreadyEvaluated = false;
    QString itemId;
    QString seriesId;
    double positionSeconds = 0.0;
    double durationSeconds = 0.0;
    int thresholdPercent = 0;
};

struct PrefetchInput
{
    bool alreadyRequested = false;
    bool playbackActive = false;
    QString seriesId;
    QString itemId;
    double positionSeconds = 0.0;
    double durationSeconds = 0.0;
    double triggerPercent = 0.0;
};

struct PrefetchedEpisodeInput
{
    bool ready = false;
    QVariantMap episode;
    QString prefetchedSeriesId;
    QString prefetchedForItemId;
    QString pendingSeriesId;
    QString pendingItemId;
};

[[nodiscard]] QString normalizeLanguageCode(const QString &language);
[[nodiscard]] int bestLanguageStreamIndex(const QVariantList &streams,
                                          const QString &language,
                                          bool subtitle);
[[nodiscard]] ResolvedTrackSelection resolveTrackSelection(
    const QVariantMap &mediaSource,
    const ScopedTrackPreferences &preferences,
    const QString &defaultAudioSelection,
    const QString &defaultSubtitleSelection,
    int preferredAudioIndex = -2,
    int preferredSubtitleIndex = -2);

[[nodiscard]] HdrContentKind classifyMediaSourceHdr(const QVariantMap &mediaSource);
[[nodiscard]] bool isHdr(HdrContentKind kind);
[[nodiscard]] bool shouldToneMapToSdr(HdrContentKind kind,
                                      const QString &dolbyVisionFallbackMode);
[[nodiscard]] QString hdrContentKindName(HdrContentKind kind);
[[nodiscard]] HdrPlaybackPolicy evaluateHdrPlayback(bool contentIsHdr,
                                                    bool contentShouldToneMapToSdr,
                                                    bool hdrEnabled,
                                                    const QString &hdrOutputMode);

[[nodiscard]] QString mediaSourceParentPath(const QVariantMap &mediaSource);
[[nodiscard]] QString mediaSourceSignature(const QVariantMap &mediaSource);
[[nodiscard]] QString buildVersionSubtitle(const QVariantMap &mediaSource);
/// Parses `settings.playback.version_preference`; unknown values mean
/// HighestQuality.
[[nodiscard]] VersionPreference parseVersionPreference(const QString &value);
[[nodiscard]] QString versionPreferenceName(VersionPreference preference);
/// The source `preference` ranks first: most pixels, then HDR, bitrate, and
/// file size for HighestQuality (also the Ask fallback when no prompt is
/// allowed); smallest file size, then bitrate and pixels for Smallest. Unknown
/// (zero) metrics rank last; ties keep server order.
[[nodiscard]] QVariantMap preferredMediaSource(const QVariantList &mediaSources,
                                               VersionPreference preference);
/// A forced id wins, then (when enabled) affinity with the previous version,
/// then `preference`.
[[nodiscard]] QVariantMap selectMediaSource(const QVariantList &mediaSources,
                                            const QString &forcedMediaSourceId,
                                            bool useAffinityFallback,
                                            const VersionAffinity &affinity,
                                            VersionPreference preference);
[[nodiscard]] QVariantList primaryPresentationSources(const QVariantList &mediaSources);

/// In-playback version picker entries ({id, title, subtitle, selected}) for
/// the primary presentation sources of the playing item.
[[nodiscard]] QVariantList playbackVersionOptions(const QVariantList &primarySources,
                                                  const QString &activeSourceId);

struct AdjacentEpisodes
{
    bool currentFound = false;
    QVariantMap previous;
    QVariantMap next;
    /// The current episode opens/closes its season, so the neighbour lives
    /// in the adjacent season.
    bool previousInAdjacentSeason = false;
    bool nextInAdjacentSeason = false;
};

/// Neighbours of `currentItemId` in a season listing (provider order).
/// Non-episode rows are ignored.
[[nodiscard]] AdjacentEpisodes resolveAdjacentEpisodes(const QVariantList &seasonItems,
                                                       const QString &currentItemId);
/// The season before (`direction < 0`) or after the current one in a series
/// listing. Empty seasons are skipped, and so are specials (season 0) unless
/// the current season is the specials season.
[[nodiscard]] QString adjacentSeasonId(const QVariantList &seriesItems,
                                       const QString &currentSeasonId,
                                       int direction);
/// First (`fromEnd == false`) or last episode of a season listing.
[[nodiscard]] QVariantMap boundaryEpisode(const QVariantList &seasonItems, bool fromEnd);

/// What a playback request plays. An extra (trailer, featurette, deleted
/// scene, ...) is a standalone clip that belongs to a movie or series.
enum class PlaybackContentKind {
    Feature,
    Extra
};

/// How a playback session may affect state beyond its own item. The player
/// reports progress only for the item it plays, so an extra never reports
/// against its parent; these rules keep the client side from tying an extra
/// to the parent either.
struct PlaybackContentRules
{
    /// Start at the request's position. Extras always start at the beginning.
    bool resumesFromRequestedPosition = true;
    /// Keep the request's series and season. Without them an extra has no
    /// season track scope, adjacent-episode controls, or autoplay series.
    bool keepsSeriesContext = true;
    /// Prefetch and resolve the next episode near and at the end.
    bool allowsNextEpisode = true;
    /// Completion may lead into autoplay or the Up Next screen.
    bool allowsUpNext = true;
    /// In-playback audio and subtitle picks are saved for the scope (and
    /// may be written back to the server).
    bool persistsTrackPreferences = true;
};

/// "extra" means Extra; anything else, including empty, means Feature.
[[nodiscard]] PlaybackContentKind playbackContentKindFromName(const QString &name);
[[nodiscard]] QString playbackContentKindName(PlaybackContentKind kind);
[[nodiscard]] PlaybackContentRules playbackContentRules(PlaybackContentKind kind);

[[nodiscard]] bool meetsCompletionThreshold(const CompletionInput &input);
[[nodiscard]] bool shouldPrefetchNextEpisode(const PrefetchInput &input);
[[nodiscard]] bool isUsablePrefetchedEpisode(const PrefetchedEpisodeInput &input);
[[nodiscard]] QString nextEpisodeRequestContext(const QString &mode,
                                                const QString &seriesId,
                                                const QString &itemId);

/// Why mpv finished a playlist entry (its `end-file` reason).
enum class EndFileReason {
    Eof,
    Stop,
    Quit,
    Error,
    Redirect,
    Unknown
};

/// What the end of one mpv playlist entry means for the playback session.
enum class EndFileOutcome {
    /// mpv moves on to another entry (a redirect or a later playlist entry).
    Continue,
    /// The session ended: end of file, or stopped, quit, or replaced.
    Ended,
    /// mpv could not play the entry; the session failed.
    Failed
};

/// From the `reason` field of an mpv IPC `end-file` event.
[[nodiscard]] EndFileReason endFileReasonFromName(const QString &name);
/// From libmpv's `mpv_end_file_reason` value.
[[nodiscard]] EndFileReason endFileReasonFromLibmpv(int reason);
[[nodiscard]] QString endFileReasonName(EndFileReason reason);
/// Every backend classifies `end-file` through this so all of them treat a
/// failed file the same way.
[[nodiscard]] EndFileOutcome classifyEndFile(EndFileReason reason,
                                             bool hasRemainingPlaylistItems);

} // namespace Bloom::PlaybackPolicy
