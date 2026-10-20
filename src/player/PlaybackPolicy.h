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
[[nodiscard]] QVariantMap selectMediaSource(const QVariantList &mediaSources,
                                            const QString &forcedMediaSourceId,
                                            bool useAffinityFallback,
                                            const VersionAffinity &affinity);
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

[[nodiscard]] bool meetsCompletionThreshold(const CompletionInput &input);
[[nodiscard]] bool shouldPrefetchNextEpisode(const PrefetchInput &input);
[[nodiscard]] bool isUsablePrefetchedEpisode(const PrefetchedEpisodeInput &input);
[[nodiscard]] QString nextEpisodeRequestContext(const QString &mode,
                                                const QString &seriesId,
                                                const QString &itemId);

} // namespace Bloom::PlaybackPolicy
