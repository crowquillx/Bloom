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

[[nodiscard]] bool meetsCompletionThreshold(const CompletionInput &input);
[[nodiscard]] bool shouldPrefetchNextEpisode(const PrefetchInput &input);
[[nodiscard]] bool isUsablePrefetchedEpisode(const PrefetchedEpisodeInput &input);
[[nodiscard]] QString nextEpisodeRequestContext(const QString &mode,
                                                const QString &seriesId,
                                                const QString &itemId);

} // namespace Bloom::PlaybackPolicy
