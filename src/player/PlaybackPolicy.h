#pragma once

#include <QList>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include "models/ServerTrackPreferences.h"
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
/// How closely a stream's language tag matches a preferred one (BCP 47 or
/// ISO 639, any case, 2- or 3-letter aliases): 0 the same tag, 1 the same
/// language with another region or script, -1 no match. Tags that name no
/// language (und, mul, zxx, mis, private use) never match.
[[nodiscard]] int languageMatchRank(const QString &candidate, const QString &preferred);
[[nodiscard]] int bestLanguageStreamIndex(const QVariantList &streams,
                                          const QString &language,
                                          bool subtitle);
/// A track the server's preferences pick for one file. An empty source
/// means the server has no preference this file can meet; index -1 with a
/// source is "subtitles off".
struct ServerTrackChoice
{
    int index = -1;
    QString source;
};

/// The server tier for audio: the server's resolved track, else its language
/// (with Jellyfin's "play the default track" rule).
[[nodiscard]] ServerTrackChoice serverAudioChoice(const QVariantList &audioStreams,
                                                  const Bloom::ServerTrackPreferences &server);
/// The server tier for subtitles, given the language of the audio that
/// plays. Modes: off (forced track for the audio, if forced subtitles show),
/// forced, always (remembered track, else the language) and auto (the
/// language only when the audio is in another language).
[[nodiscard]] ServerTrackChoice serverSubtitleChoice(
    const QVariantList &subtitleStreams,
    const Bloom::ServerTrackPreferences &server,
    const QString &audioLanguage);

/// A local pick of stream `streamIndex` in `mediaSource`, as saved for its
/// season or movie: the index plus what the track is (language, forced,
/// hearing impaired, title, codec, channels). Subtitle index -1 is "off";
/// any other negative index clears the pick.
[[nodiscard]] TrackSelectionPreference explicitTrackPreference(const QVariantMap &mediaSource,
                                                               int streamIndex,
                                                               bool subtitle);
/// The stream a saved local pick names in a file (`streams` of one type), or
/// -1. The saved index wins only while its track is still the picked one;
/// otherwise the track that best matches the pick does. Language, the forced
/// flag, (subtitles) signs/songs versus full dialogue and (audio) commentary
/// versus the programme's sound must match; title, then the exact language
/// tag (pt-BR over pt-PT), hearing impaired, codec and channels rank the
/// rest. A pick saved without what the track was (older Bloom) resolves by
/// index alone.
[[nodiscard]] int explicitStreamIndex(const QVariantList &streams,
                                      const TrackSelectionPreference &preference,
                                      bool subtitle);

/// Picks the initial audio and subtitle track. For each kind, first match wins:
///   1. the request's own override (a pick carried into this request);
///   2. the explicit local choice for the season or movie
///      (TrackPreferencesManager, see explicitStreamIndex), so a local pick
///      always beats the server; only a file with no matching track falls
///      through;
///   3. the server's preferences (`serverTrackPreferences` on the source),
///      unless `useServerPreferences` is off;
///   4. Bloom's global defaults (`defaultAudioSelection`,
///      `defaultSubtitleSelection`);
///   5. the server's and then the file's default flags, then the first track
///      (subtitles: forced, then off).
[[nodiscard]] ResolvedTrackSelection resolveTrackSelection(
    const QVariantMap &mediaSource,
    const ScopedTrackPreferences &preferences,
    const QString &defaultAudioSelection,
    const QString &defaultSubtitleSelection,
    int preferredAudioIndex = -2,
    int preferredSubtitleIndex = -2,
    bool useServerPreferences = true);

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
    /// Start at the request's position. Extras always start at the beginning,
    /// except when a retry, recovery or version switch continues the same
    /// session (the request then carries "continuesSession").
    bool resumesFromRequestedPosition = true;
    /// Keep the request's series and season. Without them an extra has no
    /// season track scope, adjacent-episode controls, or autoplay series.
    bool keepsSeriesContext = true;
    /// Prefetch and resolve the next episode near and at the end.
    bool allowsNextEpisode = true;
    /// Completion may lead into autoplay, the Up Next screen, or the
    /// after-movie screen.
    bool allowsUpNext = true;
    /// In-playback audio and subtitle picks are saved for the scope (and
    /// may be written back to the server).
    bool persistsTrackPreferences = true;
    /// The scope's saved subtitle delay applies at start, and in-playback
    /// subtitle timing changes are saved for the scope.
    bool persistsSubtitleDelay = true;
};

/// "extra" means Extra; anything else, including empty, means Feature.
[[nodiscard]] PlaybackContentKind playbackContentKindFromName(const QString &name);
[[nodiscard]] QString playbackContentKindName(PlaybackContentKind kind);
[[nodiscard]] PlaybackContentRules playbackContentRules(PlaybackContentKind kind);

[[nodiscard]] bool meetsCompletionThreshold(const CompletionInput &input);

/// The screen that follows a playback session once it ends.
enum class PostPlaybackScreen {
    /// Back where playback started.
    None,
    /// Up Next for the series (next episode, or the caught-up state).
    UpNext,
    /// The after-movie screen: rating and "Because you watched".
    AfterMovie
};

struct PostPlaybackInput
{
    PlaybackContentKind contentKind = PlaybackContentKind::Feature;
    /// The session ended with a playback error.
    bool endedWithError = false;
    /// Completion was already evaluated for this playback attempt.
    bool alreadyEvaluated = false;
    QString itemId;
    /// Set for an episode; empty for a movie.
    QString seriesId;
    double positionSeconds = 0.0;
    double durationSeconds = 0.0;
    int thresholdPercent = 0;
};

/// A completed episode leads into Up Next and a completed movie into the
/// after-movie screen. Completion is the configured threshold, so a session
/// stopped early, an error, and an extra lead nowhere.
[[nodiscard]] PostPlaybackScreen postPlaybackScreen(const PostPlaybackInput &input);
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

/// One skip range (an intro or credits marker), in milliseconds.
struct SkipRange {
    qint64 startMs = 0;
    qint64 endMs = 0;
};

/// The ranges of one type with overlapping or touching ones joined into one,
/// ordered by start; empty ranges are dropped. Two intros back to back are
/// then one skip that lands after both.
[[nodiscard]] QList<SkipRange> mergeSkipRanges(QList<SkipRange> ranges);

/// Index of the range a skip acts on: the one holding the position, else the
/// nearest one starting at most aheadToleranceMs after it (a range that
/// begins a moment after playback starts); -1 when none is. A file can
/// repeat a range type, so skipping never jumps back to an earlier range,
/// nor far ahead to a later one.
[[nodiscard]] int skipRangeIndex(const QList<SkipRange> &ranges,
                                 qint64 positionMs,
                                 qint64 aheadToleranceMs);

/// "Still watching?" rule. The chain count is the number of episodes in a
/// row that the Up Next countdown started with no user input since. A
/// threshold of 0 turns the prompt off; other values are 2 to 5.
inline constexpr int kStillWatchingDefaultThreshold = 3;
[[nodiscard]] QList<int> stillWatchingThresholdChoices();
/// Off (0) stays off; anything else snaps into 2..5.
[[nodiscard]] int normalizeStillWatchingThreshold(int threshold);
/// Count after a playback starts: an autoplay adds one, a manual start
/// (button, Continue, a page, a remote command) starts over at zero.
[[nodiscard]] int autoplayChainCountAfterStart(int currentCount, bool startedByAutoplay);
/// Count after user input during or between episodes.
[[nodiscard]] constexpr int autoplayChainCountAfterUserInput() { return 0; }
/// Whether the Up Next countdown that would start the next episode asks
/// "Still watching?" instead.
[[nodiscard]] bool shouldAskStillWatching(int chainCount, int threshold);

/// Sleep timer. A choice is 0 (off), -1 (end of the current episode or
/// movie), or a duration in minutes from sleepTimerChoices().
inline constexpr int kSleepTimerOff = 0;
inline constexpr int kSleepTimerEndOfItem = -1;
inline constexpr int kSleepTimerGraceSeconds = 30;
enum class SleepTimerKind {
    Off,
    Duration,
    EndOfItem
};
/// Off, 15, 30, 45, 60, 90, then end of item, in menu order.
[[nodiscard]] QList<int> sleepTimerChoices();
/// Unknown values read as off.
[[nodiscard]] int normalizeSleepTimerChoice(int choice);
[[nodiscard]] SleepTimerKind sleepTimerKind(int choice);
[[nodiscard]] int sleepTimerDurationSeconds(int choice);
/// How a playback ends, for the sleep timer.
enum class SleepTimerPlaybackEnd {
    /// The item played to its end (Up Next may follow).
    Finished,
    /// The user (or the timer itself) stopped playback.
    Stopped,
    /// Playback failed; recovery may continue the same session.
    Failed
};
/// Whether an armed timer stays armed after playback ends this way. An
/// end-of-item timer fires at the end instead; a duration survives the
/// trip through Up Next; stopping cancels either.
[[nodiscard]] bool sleepTimerSurvivesPlaybackEnd(int choice, SleepTimerPlaybackEnd end);
/// Whether an end-of-item timer fires as this playback finishes.
[[nodiscard]] bool sleepTimerFiresAtPlaybackEnd(int choice, SleepTimerPlaybackEnd end);
/// How a new playback relates to the one before it, for the sleep timer.
enum class SleepTimerStart {
    /// Any start that is not continuing a viewing session.
    New,
    /// The next episode from Up Next, after the previous one ended.
    UpNext,
    /// The previous/next episode controls, replacing the playing episode.
    EpisodeSkip
};
/// The choice for a new playback. Up Next keeps a running duration (an
/// end-of-item timer already fired there); the episode controls keep a
/// duration or an end-of-item timer, which then ends after the episode the
/// user skipped to; any other start begins from the default.
[[nodiscard]] int sleepTimerChoiceForNewPlayback(int currentChoice,
                                                 int defaultChoice,
                                                 SleepTimerStart start);

} // namespace Bloom::PlaybackPolicy
