#pragma once

#include <QObject>
#include <QTimer>
#include <QElapsedTimer>
#include <QHash>
#include <QMap>
#include <QMetaObject>
#include <QSet>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>
#include <QVariantList>
#include <QPointer>
#include <QPointF>
#include <QtGlobal>
#include <array>
#include <functional>
#include <memory>
#include <optional>
#include "backend/IPlayerBackend.h"
#include "PlaybackPolicy.h"
#include "TrickplayProcessor.h"
#include "LocalSeekPreview.h"
#include "../network/Types.h"
#include "../providers/IPlaybackProvider.h"
#include "../models/MediaModels.h"
#include "../utils/ConfigManager.h"
#include "../utils/TrackPreferencesManager.h"
#include "../utils/DisplayManager.h"

class PlaybackService;
class LibraryService;
class AuthenticationService;
class QNetworkReply;
class HttpRequestHandle;

/**
 * @class PlayerController
 * @brief Controls media playback with a proper state machine for handling complex playback states.
 * 
 * State Machine States:
 * - Idle: No playback active
 * - Loading: Starting mpv process, waiting for connection
 * - Buffering: Connected to mpv, waiting for media to buffer
 * - Playing: Media is actively playing
 * - Paused: Playback is paused
 * - Error: An error occurred
 * 
 * Transitions:
 * - Idle → Loading: playUrl() or playTestVideo() called
 * - Loading → Buffering: mpv connected, first position received
 * - Loading → Error: timeout or process error
 * - Buffering → Playing: playback progressing smoothly
 * - Buffering → Error: timeout or process error
 * - Playing → Paused: pause() called or mpv reports pause
 * - Playing → Buffering: rebuffering detected (stalled playback)
 * - Playing → Error: process error
 * - Playing → Idle: stop() or playback ended naturally
 * - Paused → Playing: resume() or mpv reports unpause
 * - Paused → Idle: stop() called
 * - Error → Idle: clearError() or retry()
 * - Error → Loading: retry() with pending URL
 */
class PlayerController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(PlaybackState playbackState READ playbackState NOTIFY playbackStateChanged)
    Q_PROPERTY(bool isPlaybackActive READ isPlaybackActive NOTIFY isPlaybackActiveChanged)
    Q_PROPERTY(bool awaitingNextEpisodeResolution READ awaitingNextEpisodeResolution NOTIFY awaitingNextEpisodeResolutionChanged)
    Q_PROPERTY(QString stateName READ stateName NOTIFY stateChanged)
    Q_PROPERTY(bool isBuffering READ isBuffering NOTIFY isBufferingChanged)
    Q_PROPERTY(bool isLoading READ isLoading NOTIFY isLoadingChanged)
    Q_PROPERTY(bool isStartupBuffering READ isStartupBuffering NOTIFY isStartupBufferingChanged)
    Q_PROPERTY(bool isPaused READ isPaused NOTIFY playbackStateChanged)
    Q_PROPERTY(bool hasError READ hasError NOTIFY hasErrorChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
    Q_PROPERTY(bool isRecovering READ isRecovering NOTIFY isRecoveringChanged)
    Q_PROPERTY(int recoveryAttemptCount READ recoveryAttemptCount NOTIFY recoveryAttemptCountChanged)
    Q_PROPERTY(int bufferingProgress READ bufferingProgress NOTIFY bufferingProgressChanged)
    Q_PROPERTY(int audioDelay READ audioDelay WRITE setAudioDelay NOTIFY audioDelayChanged)
    Q_PROPERTY(bool supportsEmbeddedVideo READ supportsEmbeddedVideo NOTIFY supportsEmbeddedVideoChanged)
    Q_PROPERTY(bool embeddedVideoShrinkEnabled READ embeddedVideoShrinkEnabled WRITE setEmbeddedVideoShrinkEnabled NOTIFY embeddedVideoShrinkEnabledChanged)
    Q_PROPERTY(int volume READ volume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ muted NOTIFY mutedChanged)
    Q_PROPERTY(double currentPositionSeconds READ currentPositionSeconds NOTIFY timelineChanged)
    Q_PROPERTY(double durationSeconds READ durationSeconds NOTIFY timelineChanged)
    Q_PROPERTY(double progressRatio READ progressRatio NOTIFY timelineChanged)
    Q_PROPERTY(double cacheEndSeconds READ cacheEndSeconds NOTIFY cacheEndChanged)
    Q_PROPERTY(double cacheProgressRatio READ cacheProgressRatio NOTIFY cacheEndChanged)
    Q_PROPERTY(bool isInIntroSegment READ isInIntroSegment NOTIFY skipSegmentsChanged)
    Q_PROPERTY(bool isInOutroSegment READ isInOutroSegment NOTIFY skipSegmentsChanged)
    Q_PROPERTY(bool hasActiveSkipSegment READ hasActiveSkipSegment NOTIFY skipSegmentsChanged)
    Q_PROPERTY(bool hasTrickplay READ hasTrickplay NOTIFY trickplayStateChanged)
    Q_PROPERTY(int trickplayIntervalMs READ trickplayIntervalMs NOTIFY trickplayStateChanged)
    Q_PROPERTY(QString trickplayPreviewUrl READ trickplayPreviewUrl NOTIFY trickplayPreviewChanged)
    Q_PROPERTY(quint64 trickplayFrameRevision READ trickplayFrameRevision NOTIFY trickplayFramesChanged)
    Q_PROPERTY(QString currentItemId READ currentItemId NOTIFY currentItemIdChanged)
    Q_PROPERTY(QString overlayTitle READ overlayTitle NOTIFY overlayMetadataChanged)
    Q_PROPERTY(QString overlaySubtitle READ overlaySubtitle NOTIFY overlayMetadataChanged)
    Q_PROPERTY(QString overlayBackdropUrl READ overlayBackdropUrl NOTIFY overlayMetadataChanged)
    Q_PROPERTY(QString overlayLogoUrl READ overlayLogoUrl NOTIFY overlayMetadataChanged)
    Q_PROPERTY(QVariantList playbackChapters READ playbackChapters NOTIFY playbackChaptersChanged)
    Q_PROPERTY(bool hasPlaybackChapters READ hasPlaybackChapters NOTIFY playbackChaptersChanged)
    Q_PROPERTY(int currentPlaybackChapterIndex READ currentPlaybackChapterIndex NOTIFY currentPlaybackChapterIndexChanged)
    // Versions of the current item for the in-playback quality picker. Each entry
    // is {id, title, subtitle, selected}; fewer than two entries means no choice.
    Q_PROPERTY(QVariantList playbackVersions READ playbackVersions NOTIFY playbackVersionsChanged)
    // Previous/next episode of the playing episode, resolved from its season
    // listing (crossing into adjacent seasons at the boundaries).
    Q_PROPERTY(bool hasEpisodeNavigation READ hasEpisodeNavigation NOTIFY adjacentEpisodesChanged)
    Q_PROPERTY(bool hasPreviousEpisode READ hasPreviousEpisode NOTIFY adjacentEpisodesChanged)
    Q_PROPERTY(bool hasNextEpisode READ hasNextEpisode NOTIFY adjacentEpisodesChanged)
    
    // Track selection properties
    Q_PROPERTY(int selectedAudioTrack READ selectedAudioTrack WRITE setSelectedAudioTrack NOTIFY selectedAudioTrackChanged)
    Q_PROPERTY(int selectedSubtitleTrack READ selectedSubtitleTrack WRITE setSelectedSubtitleTrack NOTIFY selectedSubtitleTrackChanged)
    Q_PROPERTY(int subtitleDelayMs READ subtitleDelayMs WRITE setSubtitleDelayMs NOTIFY subtitleDelayChanged)
    Q_PROPERTY(int subtitleDelayStepMs READ subtitleDelayStepMs NOTIFY subtitleDelayStepChanged)
    Q_PROPERTY(QString mediaSourceId READ mediaSourceId NOTIFY mediaSourceIdChanged)
    Q_PROPERTY(QString playSessionId READ playSessionId NOTIFY playSessionIdChanged)
    Q_PROPERTY(QVariantList availableAudioTracks READ availableAudioTracks NOTIFY availableTracksChanged)
    Q_PROPERTY(QVariantList availableSubtitleTracks READ availableSubtitleTracks NOTIFY availableTracksChanged)
    Q_PROPERTY(QVariantMap audioDiagnostics READ audioDiagnostics NOTIFY audioDiagnosticsChanged)
    Q_PROPERTY(QVariantList availableAudioDevices READ availableAudioDevices NOTIFY availableAudioDevicesChanged)
    // Output selector entries (System Default plus the active driver's endpoints)
    // and the resolved device / exclusive-mode state. See docs/audio.md.
    Q_PROPERTY(QVariantList audioOutputOptions READ audioOutputOptions NOTIFY audioOutputStatusChanged)
    Q_PROPERTY(QVariantMap audioOutputStatus READ audioOutputStatus NOTIFY audioOutputStatusChanged)
    // Up Next asks "Still watching?" instead of counting down (see
    // Bloom::PlaybackPolicy::shouldAskStillWatching).
    Q_PROPERTY(bool stillWatchingPromptActive READ stillWatchingPromptActive NOTIFY stillWatchingPromptActiveChanged)
    // Sleep timer: 0 off, -1 end of the current item, otherwise minutes.
    Q_PROPERTY(int sleepTimerChoice READ sleepTimerChoice NOTIFY sleepTimerChanged)
    Q_PROPERTY(QVariantList sleepTimerChoices READ sleepTimerChoices CONSTANT)
    Q_PROPERTY(int sleepTimerRemainingSeconds READ sleepTimerRemainingSeconds NOTIFY sleepTimerChanged)
    Q_PROPERTY(bool sleepTimerGraceActive READ sleepTimerGraceActive NOTIFY sleepTimerChanged)
    Q_PROPERTY(int sleepTimerGraceRemainingSeconds READ sleepTimerGraceRemainingSeconds NOTIFY sleepTimerChanged)
    // "End of this episode", "End of this movie" or "End of this video" (an extra).
    Q_PROPERTY(QString sleepTimerEndOfItemLabel READ sleepTimerEndOfItemLabel NOTIFY sleepTimerChanged)

public:
    /// Playback states for the state machine
    enum PlaybackState {
        Idle,
        Loading,
        Buffering,
        Playing,
        Paused,
        Error
    };
    Q_ENUM(PlaybackState)

    /// Events that trigger state transitions
    enum class Event {
        Play,           // User requests playback
        LoadComplete,   // Media loading finished (connected to mpv)
        BufferComplete, // Buffering finished, ready to play
        BufferStart,    // Rebuffering started during playback
        Pause,          // User or mpv requests pause
        Resume,         // User or mpv requests resume
        Stop,           // User requests stop
        PlaybackEnd,    // Playback reached end of media
        ErrorOccurred,  // An error happened
        Recover         // User requests error recovery
    };
    Q_ENUM(Event)

    explicit PlayerController(IPlayerBackend *playerBackend, ConfigManager *config, TrackPreferencesManager *trackPrefs, DisplayManager *displayManager, PlaybackService *playbackService, LibraryService *libraryService, AuthenticationService *authService, QObject *parent = nullptr);
    ~PlayerController();

    /**
     * Returns the current playback state.
     * @returns The current PlaybackState.
     */
    PlaybackState playbackState() const;
    /**
     * Indicates whether playback is currently active.
     * @returns `true` if playback is active, `false` otherwise.
     */
    bool isPlaybackActive() const;
    /**
     * Indicates whether the controller is awaiting resolution of the next episode (prefetch/navigation).
     * @returns `true` if awaiting next-episode resolution, `false` otherwise.
     */
    bool awaitingNextEpisodeResolution() const { return m_awaitingNextEpisodeResolution; }
    /**
     * Returns a human-readable name for the current playback state.
     * @returns The playback state's name as a QString.
     */
    QString stateName() const;
    /**
     * Indicates whether the player is currently buffering.
     * @returns `true` if buffering, `false` otherwise.
     */
    bool isBuffering() const;
    /**
     * Indicates whether the player is currently loading content.
     * @returns `true` if loading, `false` otherwise.
     */
    bool isLoading() const;
    /**
     * Indicates whether the player should show the startup buffering experience.
     * @returns `true` while loading or waiting for remote-mount initial cache, `false` otherwise.
     */
    bool isStartupBuffering() const;
    /**
     * Indicates whether playback is currently paused.
     * @returns `true` if paused, `false` otherwise.
     */
    bool isPaused() const { return m_playbackState == Paused; }
    bool hasError() const;
    QString errorMessage() const;
    bool isRecovering() const { return m_isRecovering; }
    int recoveryAttemptCount() const { return m_recoveryAttemptCount; }
    int bufferingProgress() const;
    
    // Track selection getters
    int selectedAudioTrack() const { return m_selectedAudioTrack; }
    int selectedSubtitleTrack() const { return m_selectedSubtitleTrack; }
    int subtitleDelayMs() const { return m_subtitleDelayMs; }
    int subtitleDelayStepMs() const;
    QString mediaSourceId() const { return m_mediaSourceId; }
    QString playSessionId() const { return m_playSessionId; }
    QVariantList availableAudioTracks() const { return m_availableAudioTracks; }
    QVariantList availableSubtitleTracks() const { return m_availableSubtitleTracks; }
    QVariantMap audioDiagnostics() const { return m_audioDiagnostics; }
    QVariantList availableAudioDevices() const { return m_availableAudioDevices; }
    QVariantList audioOutputOptions() const { return m_audioOutputOptions; }
    QVariantMap audioOutputStatus() const { return m_audioOutputStatus; }
    
    // Audio Delay (ms)
    int audioDelay() const { return m_config->getAudioDelay(); }
    bool supportsEmbeddedVideo() const;
    bool embeddedVideoShrinkEnabled() const { return m_embeddedVideoShrinkEnabled; }
    int volume() const { return m_volume; }
    bool muted() const { return m_muted; }
    double currentPositionSeconds() const { return m_currentPosition; }
    double durationSeconds() const { return m_duration; }
    double progressRatio() const { return m_duration > 0.0 ? qBound(0.0, m_currentPosition / m_duration, 1.0) : 0.0; }
    // mpv reports the cache on the player clock; the timeline is source time.
    double cacheEndSeconds() const { return m_cacheEndSeconds + m_streamTimelineOffsetSeconds; }
    double cacheProgressRatio() const { return m_duration > 0.0 ? qBound(0.0, cacheEndSeconds() / m_duration, 1.0) : 0.0; }
    bool isInIntroSegment() const { return m_isInIntroSegment; }
    bool isInOutroSegment() const { return m_isInOutroSegment; }
    bool hasActiveSkipSegment() const { return m_isInIntroSegment || m_isInOutroSegment; }
    bool hasTrickplay() const { return m_hasTrickplayInfo || m_useLocalTrickplay; }
    int trickplayIntervalMs() const { return m_hasTrickplayInfo ? m_currentTrickplayInfo.interval : (m_useLocalTrickplay ? LocalSeekPreview::intervalMs : 0); }
    QString trickplayPreviewUrl() const { return m_trickplayPreviewUrl; }
    quint64 trickplayFrameRevision() const { return m_trickplayFrameRevision; }
    QString currentItemId() const { return m_currentItemId; }
    QString overlayTitle() const { return m_overlayTitle; }
    QString overlaySubtitle() const { return m_overlaySubtitle; }
    QString overlayBackdropUrl() const { return m_overlayBackdropUrl; }
    QString overlayLogoUrl() const { return m_overlayLogoUrl; }
    QVariantList playbackChapters() const { return m_playbackChapters; }
    bool hasPlaybackChapters() const { return !m_playbackChapters.isEmpty(); }
    int currentPlaybackChapterIndex() const { return m_currentPlaybackChapterIndex; }
    QVariantList playbackVersions() const;
    bool hasEpisodeNavigation() const;
    bool hasPreviousEpisode() const;
    bool hasNextEpisode() const;
    Q_INVOKABLE void setAudioDelay(int ms);
    Q_INVOKABLE bool attachEmbeddedVideoTarget(QObject *target);
    Q_INVOKABLE void detachEmbeddedVideoTarget(QObject *target = nullptr);
    Q_INVOKABLE void setEmbeddedVideoViewport(qreal x, qreal y, qreal width, qreal height);
    Q_INVOKABLE void setEmbeddedVideoShrinkEnabled(bool enabled);

    Q_INVOKABLE void playTestVideo();
    Q_INVOKABLE void playUrl(const QString &url, const QString &itemId = "", qint64 startPositionMs = 0, const QString &seriesId = "", const QString &seasonId = "", const QString &libraryId = "", double framerate = 0.0, bool isHDR = false, bool toneMapToSdr = false);
    Q_INVOKABLE void requestPlayback(const QVariantMap &request);
    Q_INVOKABLE void confirmPlaybackVersion(const QString &requestId, const QString &mediaSourceId);
    Q_INVOKABLE void cancelPendingPlaybackRequest(const QString &requestId);
    
    /// Play the next episode from the Up Next screen.
    /// @param episodeData Canonical episode model with camelCase fields and millisecond timing.
    /// @param seriesId The series this episode belongs to.
    /// @param startedByAutoplay True when the Up Next countdown ran out; any
    /// button press (Play, Continue, a card) is a manual start.
    Q_INVOKABLE void playNextEpisode(const QVariantMap &episodeData, const QString &seriesId,
                                     bool startedByAutoplay = false);
    /**
     * @brief Clears any stored pending autoplay context.
     *
     * Resets stashed identifiers and playback hints used by Up Next/autoplay flows and
     * marks next-episode resolution as no longer pending. Used when navigation is canceled
     * or playback context is intentionally replaced.
     */
    Q_INVOKABLE void clearPendingAutoplayContext();
    /// Up Next closed without starting playback: releases a parked display
    /// restore and ends a running sleep timer.
    Q_INVOKABLE void releaseDeferredPostPlaybackDisplayRestore();

    /**
     * @brief Resolves the effective startup audio/subtitle selection for a media source.
     *
     * `preferredAudioIndex` and `preferredSubtitleIndex` use provider source-track indices
     * with sentinel values: `-2` means "unset/no override", `-1` means "off" (subtitle only),
     * and `>= 0` means an explicit stream index. `carryTrackPreferencesFromSeasonId`
     * (episodes) names a season whose picks apply for kinds `scopeId` has none of.
     */
    Q_INVOKABLE QVariantMap resolveTrackSelectionForMediaSource(
        const QVariantMap &mediaSource,
        const QString &scopeId,
        bool isMovie,
        int preferredAudioIndex = -2,
        int preferredSubtitleIndex = -2,
        const QString &carryTrackPreferencesFromSeasonId = QString()) const;

    // Extended playUrl with track selection.
    Q_INVOKABLE void playUrlWithTracks(const QString &url, const QString &itemId, qint64 startPositionMs,
                                       const QString &seriesId, const QString &seasonId, const QString &libraryId,
                                       const QString &mediaSourceId, const QString &playSessionId,
                                       const QVariantMap &mediaSource,
                                       int audioStreamIndex, int subtitleStreamIndex,
                                       const QVariantList &availableAudioTracks = {},
                                       const QVariantList &availableSubtitleTracks = {},
                                       qint64 durationMs = 0, double framerate = 0.0,
                                       bool isHDR = false, bool toneMapToSdr = false);
    
    Q_INVOKABLE void stop();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void resume();
    Q_INVOKABLE void togglePause();

    // ---- Still watching / sleep timer ----
    bool stillWatchingPromptActive() const { return m_stillWatchingPromptActive; }
    int autoplayChainCount() const { return m_autoplayChainCount; }
    int sleepTimerChoice() const { return m_sleepTimerChoice; }
    QVariantList sleepTimerChoices() const;
    int sleepTimerRemainingSeconds() const { return m_sleepTimerRemainingSeconds; }
    bool sleepTimerGraceActive() const { return m_sleepTimerGraceActive; }
    int sleepTimerGraceRemainingSeconds() const { return m_sleepTimerGraceRemainingSeconds; }
    QString sleepTimerEndOfItemLabel() const;
    /// User input (key, button, click, wheel, pointer movement, controller
    /// action, or a pause/resume or bloom-* script message from the external
    /// mpv window). Restarts
    /// the "Still watching?" episode count and cancels a sleep-timer grace
    /// countdown.
    Q_INVOKABLE void notePlaybackUserInput();
    /// Arms (or with 0, turns off) the sleep timer for the current playback.
    Q_INVOKABLE void setSleepTimer(int choice);
    /// Moves to the next sleep-timer choice (the remote action without a menu).
    Q_INVOKABLE void cycleSleepTimer();
    /// Ends the grace countdown and keeps playback paused; the timer is off.
    Q_INVOKABLE void cancelSleepTimerGrace();
    Q_INVOKABLE void seek(double seconds);
    Q_INVOKABLE void seekRelative(double seconds);
    Q_INVOKABLE void skipIntro();
    Q_INVOKABLE void skipOutro();
    Q_INVOKABLE void skipActiveSegment();
    Q_INVOKABLE void retry();
    Q_INVOKABLE void clearError();
    
    // Track selection methods - change tracks during playback via mpv
    Q_INVOKABLE void setSelectedAudioTrack(int index);
    Q_INVOKABLE void setSelectedSubtitleTrack(int index);
    // Saves the output choice and reopens audio on the running session only.
    Q_INVOKABLE void selectAudioOutputDevice(const QString &device);
    Q_INVOKABLE void refreshAudioOutputDevices();
    Q_INVOKABLE void setSubtitleDelayMs(int delayMs);
    Q_INVOKABLE void adjustSubtitleDelayMs(int deltaMs);
    Q_INVOKABLE void resetSubtitleDelay();
    Q_INVOKABLE void addExternalSubtitleTrack(const QString &subtitleUrl,
                                              const QString &displayTitle = QString(),
                                              const QString &language = QString(),
                                              int sourceStreamIndexHint = -1);
    Q_INVOKABLE void cycleAudioTrack();
    Q_INVOKABLE void cycleSubtitleTrack();
    Q_INVOKABLE void previousChapter();
    Q_INVOKABLE void nextChapter();
    Q_INVOKABLE void seekToPlaybackChapter(int index);
    /// Restarts the current item on another version at the current position.
    Q_INVOKABLE void switchPlaybackVersion(const QString &mediaSourceId);
    /// Replaces the playing episode with its previous/next episode.
    Q_INVOKABLE void skipToPreviousEpisode();
    Q_INVOKABLE void skipToNextEpisode();
    Q_INVOKABLE void toggleMute();
    Q_INVOKABLE void setMuted(bool muted);
    Q_INVOKABLE void setVolume(int volume);
    Q_INVOKABLE void adjustVolume(int delta);
    Q_INVOKABLE void showMpvStatsOnce();
    Q_INVOKABLE void toggleMpvStats();
    Q_INVOKABLE void showMpvStatsPage(int page);
    Q_INVOKABLE void toggleSubtitleAssOverride();
    Q_INVOKABLE void toggleDeband();
    Q_INVOKABLE void sendMpvKeypress(const QString &key);
    Q_INVOKABLE void setOverlayMetadata(const QString &title,
                                        const QString &subtitle = QString(),
                                        const QString &backdropUrl = QString(),
                                        const QString &logoUrl = QString());
    Q_INVOKABLE void clearOverlayMetadata();
    Q_INVOKABLE void setTrickplayPreviewPositionSeconds(double seconds);
    Q_INVOKABLE void clearTrickplayPreviewPositionOverride();
    /// Trickplay frame URL for an arbitrary position (filmstrip neighbours);
    /// empty when trickplay is unavailable. Does not move the preview cursor.
    Q_INVOKABLE QString trickplayPreviewUrlAt(double seconds, quint64 revision = 0);
    
    // Legacy convenience getters retained for existing QML/tests.
    Q_INVOKABLE int getLastAudioTrackForSeason(const QString &seasonId) const;
    Q_INVOKABLE int getLastSubtitleTrackForSeason(const QString &seasonId) const;
    Q_INVOKABLE int getLastAudioTrackForMovie(const QString &movieId) const;
    Q_INVOKABLE int getLastSubtitleTrackForMovie(const QString &movieId) const;

    Q_INVOKABLE void saveAudioTrackPreference(const QString &seasonId, int index);
    Q_INVOKABLE void saveSubtitleTrackPreference(const QString &seasonId, int index);
    Q_INVOKABLE void saveMovieAudioTrackPreference(const QString &movieId, int index);
    Q_INVOKABLE void saveMovieSubtitleTrackPreference(const QString &movieId, int index);
    Q_INVOKABLE void setExplicitSeasonAudioPreference(const QString &seasonId, int index);
    Q_INVOKABLE void setExplicitSeasonSubtitlePreference(const QString &seasonId, int index);
    // A pick the user made for an episode before playback (the season view's
    // track menu): saved for the season, then written back to the server for
    // the series so other devices follow (best-effort).
    Q_INVOKABLE void chooseSeasonAudioTrack(const QString &seasonId, const QString &seriesId,
                                            const QVariantMap &mediaSource, int index);
    Q_INVOKABLE void chooseSeasonSubtitleTrack(const QString &seasonId, const QString &seriesId,
                                               const QVariantMap &mediaSource, int index);
    // The same for a movie: saved for the movie and written back for it.
    Q_INVOKABLE void chooseMovieAudioTrack(const QString &movieId, const QVariantMap &mediaSource,
                                           int index);
    Q_INVOKABLE void chooseMovieSubtitleTrack(const QString &movieId,
                                              const QVariantMap &mediaSource, int index);
    Q_INVOKABLE void clearSeasonAudioPreference(const QString &seasonId);
    Q_INVOKABLE void clearSeasonSubtitlePreference(const QString &seasonId);
    Q_INVOKABLE void setExplicitMovieAudioPreference(const QString &movieId, int index);
    Q_INVOKABLE void setExplicitMovieSubtitlePreference(const QString &movieId, int index);
    Q_INVOKABLE void clearMovieAudioPreference(const QString &movieId);
    Q_INVOKABLE void clearMovieSubtitlePreference(const QString &movieId);

signals:
    void playbackStateChanged();
    void stateChanged(const QString &state);
    void isBufferingChanged();
    void isLoadingChanged();
    void isStartupBufferingChanged();
    void hasErrorChanged();
    void errorMessageChanged();
    void isRecoveringChanged();
    void recoveryAttemptCountChanged();
    void bufferingProgressChanged();
    void audioDelayChanged();
    void isPlaybackActiveChanged();
    void awaitingNextEpisodeResolutionChanged();
    void supportsEmbeddedVideoChanged();
    void embeddedVideoShrinkEnabledChanged();
    void volumeChanged();
    void mutedChanged();
    void timelineChanged();
    void cacheEndChanged();
    void skipSegmentsChanged();
    void trickplayStateChanged();
    void trickplayPreviewChanged();
    void trickplayFramesChanged();
    void currentItemIdChanged();
    void overlayMetadataChanged();
    void playbackChaptersChanged();
    void currentPlaybackChapterIndexChanged();
    void playbackVersionsChanged();
    void adjacentEpisodesChanged();

    // Track selection signals
    void selectedAudioTrackChanged();
    void selectedSubtitleTrackChanged();
    void subtitleDelayChanged();
    void subtitleDelayStepChanged();
    void mediaSourceIdChanged();
    void playSessionIdChanged();
    void availableTracksChanged();
    void availableAudioDevicesChanged();
    void audioDiagnosticsChanged();
    void audioOutputStatusChanged();
    // Short user-facing message about an output fallback.
    void audioOutputNotice(const QString &message);
    
    /// Emitted when playback has stopped (user stop, playback end, or error)
    /// Use this to refresh UI elements that depend on playback state (e.g., watch progress)
    void playbackStopped();
    void stillWatchingPromptActiveChanged();
    void sleepTimerChanged();
    
    /// Emitted when autoplay is about to start the next episode
    /// @param episodeName The name of the next episode
    /// @param seriesName The name of the series
    void autoplayingNextEpisode(const QString &episodeName, const QString &seriesName);
    
    /// Emitted when playback ends with threshold met and navigation to next episode is requested
    /// @param episodeData The JSON object containing the next episode data
    /// @param seriesId The series ID
    /// @param carryTrackPreferencesFromSeasonId The season whose track picks the next
    /// episode takes for kinds its own season has none of, or empty (see
    /// resolveTrackSelectionForMediaSource and the request's carryTrackPreferencesFromSeasonId)
    void navigateToNextEpisode(const QVariantMap &episodeData, const QString &seriesId,
                               const QString &carryTrackPreferencesFromSeasonId, bool autoplay);
    void playbackVersionSelectionRequested(const QString &requestId, const QVariantMap &dialogModel,
                                           QObject *restoreFocusTarget);

protected:
    // Installed on the application (ApplicationInitializer) to observe user
    // input for the still-watching count and the sleep-timer grace period.
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onProcessStateChanged(bool running);
    void onProcessError(const QString &error);
    void onPositionChanged(double seconds);
    void onDurationChanged(double seconds);
    void onPauseChanged(bool paused);
    void onPausedForCacheChanged(bool pausedForCache);
    void onCacheEndChanged(double seconds);
    void onPlaybackEnded(
        Bloom::PlaybackPolicy::EndFileReason reason = Bloom::PlaybackPolicy::EndFileReason::Eof,
        const QString &detail = QString());
    void onPlaylistPositionChanged(int playlistIndex);
    void onNextEpisodeLoaded(const QString &connectionId,
                             const QString &seriesId,
                             const QVariantMap &episodeData,
                             const QString &requestContext);
    void onNextEpisodeFailed(const QString &seriesId,
                             const QString &error,
                             const QString &requestContext);
    void onPlaybackInfoLoaded(const QString &itemId, const PlaybackInfoResponse &playbackInfo);
    void onPlaybackInfoLoadedForRequest(const QString &itemId,
                                        const PlaybackInfoResponse &playbackInfo,
                                        const QString &requestContext);
    void onPlaybackInfoFailedForRequest(const QString &itemId,
                                        const QString &error,
                                        const QString &requestContext);
    void onPlaybackDescriptorLoadedForRequest(const QString &itemId,
                                              const Bloom::PlaybackDescriptor &descriptor,
                                              const QString &requestContext);
    void onPlaybackDescriptorFailedForRequest(const QString &itemId,
                                              const QString &error,
                                              const QString &requestContext);
    void onPlaybackSessionLost(const QString &playbackSessionId, bool endedByServer);
    void requestDeferredSegmentDescriptor();
    void dispatchQueuedReplan(double fallbackSourceSeconds);
    void onPlaybackReplannedForRequest(const QString &sessionId,
                                       const Bloom::PlaybackDescriptor &descriptor,
                                       const QString &requestContext);
    void onPlaybackReplanFailedForRequest(const QString &sessionId,
                                          const QString &error,
                                          const QString &requestContext);
    void onBackendFileLoaded();
    void onAdditionalPartsLoaded(const QString &itemId, const QVariantList &parts);
    void onAdditionalPartsLoadedForRequest(const QString &itemId,
                                           const QVariantList &parts,
                                           const QString &requestContext);
    void onAdditionalPartsFailedForRequest(const QString &itemId,
                                           const QString &error,
                                           const QString &requestContext);
    void onPlaybackServiceErrorOccurred(const QString &endpoint, const QString &error);
    void onPlaybackServiceNetworkError(const NetworkError &error);
    void onAutoplayPlaybackInfoTimeout();
    void onItemLibraryResolved(const QString &itemId, const QString &libraryId);
    void onItemLibraryResolutionFailed(const QString &itemId, const QString &error);
    void onAdjacentEpisodeItemsLoaded(const QString &connectionId,
                                      const QString &parentId,
                                      const QString &queryKey,
                                      const QVariantList &items,
                                      int totalRecordCount);
    void onRecoveryTick();
    void resumeFromRecoveryContext();
    
    // Timeout handlers
    void onLoadingTimeout();
    void onBufferingTimeout();
    
    // OSC and trickplay handlers
    void onScriptMessage(const QString &messageName, const QStringList &args);
    void onMediaSegmentsLoaded(const QString &itemId, const QList<MediaSegmentInfo> &segments);
    void onTrickplayInfoLoaded(const QString &itemId, const QMap<int, TrickplayTileInfo> &trickplayInfo);
    void onChaptersLoaded(const QString &connectionId,
                          const QString &itemId,
                          const QVariantList &chapters);
    void onChaptersFailed(const QString &connectionId,
                          const QString &itemId,
                          const QString &error);
    
    // TrickplayProcessor handlers
    void onTrickplayProcessingComplete(const QString &itemId, int count, int intervalMs,
                                       int width, int height, const QString &filePath);
    void onTrickplayProcessingFailed(const QString &itemId, const QString &error);

private:
#ifdef BLOOM_TESTING
    friend class PlayerControllerAutoplayContextTest;
#endif
    enum class TerminalReason {
        Stop,
        PlaybackEnd,
        Error
    };

    // ---- Still watching / sleep timer helpers ----
    void setStillWatchingPromptActive(bool active);
    /// Whether the Up Next screen about to open asks "Still watching?"
    /// instead of counting down.
    bool shouldAskStillWatchingForUpNext() const;
    void armSleepTimer(int choice, bool keepRemaining);
    void clearSleepTimer();
    void onSleepTimerTick();
    void fireSleepTimer();
    void finishSleepTimerGrace();
    void updateSleepTimerTickTimer();
    /// Sleep-timer bookkeeping for a terminal transition; true when the
    /// timer ended this playback and Up Next must not open.
    bool applySleepTimerAtTerminal(TerminalReason reason);
    void showSleepTimerOsd(const QString &text, int durationMs);

    struct PendingPlaybackRequest;
    using HdrPlaybackPolicy = Bloom::PlaybackPolicy::HdrPlaybackPolicy;
    // State machine
    void setupStateMachine();
    bool processEvent(Event event);
    void enterState(PlaybackState newState);
    void exitState(PlaybackState oldState);
    
    // State entry handlers
    void onEnterIdleState();
    void onEnterLoadingState();
    void onEnterBufferingState();
    void onEnterPlayingState();
    void onEnterPausedState();
    void onEnterErrorState();
    
    // State exit handlers
    void onExitIdleState();
    void onExitLoadingState();
    void onExitBufferingState();
    void onExitPlayingState();
    void onExitPausedState();
    void onExitErrorState();
    
    // Helpers
    void setPlaybackState(PlaybackState state);
    void setErrorMessage(const QString &message);
    void setBufferingProgress(int progress);
    void setWaitingForRemoteMountInitialCache(bool waiting);
    void completeRemoteMountStartupBuffering(double cacheSeconds, const char *reason);
    void beginRecovery();
    void cancelRecovery();
    void reportPlaybackStart();
    void reportPlaybackProgress();
    void reportPlaybackProgressNow();
    void reportPlaybackStop();
    struct PlaybackStopReportSnapshot
    {
        QString itemId;
        QString mediaSourceId;
        QString playSessionId;
        int audioStreamIndex = -1;
        int subtitleStreamIndex = -1;
        qint64 positionMs = 0;
        bool canSeek = false;
        bool isPaused = false;
        bool isMuted = false;
        QString playMethod;
        double aggregatePositionSeconds = 0.0;
        double durationSeconds = 0.0;
        bool valid = false;
    };
    PlaybackStopReportSnapshot capturePlaybackStopReportSnapshot() const;
    bool wouldMeetCompletionThreshold() const;
    void checkCompletionThreshold();
    bool checkCompletionThresholdAndAutoplay();  // Returns true if threshold was met (for autoplay)
    void prepareTerminalTransition(TerminalReason reason);
    void requestTerminalTransition(TerminalReason reason);
    void queueTerminalFinalization();
    void finalizeTerminalTransition();
    void resetTerminalTransitionState(bool invalidateQueuedWork = false);
    void updatePendingTerminalReason(TerminalReason reason);
    bool stashRecoveryContextForCurrentPlayback();
    [[nodiscard]] Event eventForTerminalReason(TerminalReason reason) const;
    [[nodiscard]] static int terminalReasonPriority(TerminalReason reason);
    void maybeTriggerNextEpisodePrefetch();
    /**
     * Returns whether a prefetched "next episode" payload is available and usable for navigation.
     * @returns `true` if a prefetched next-episode item is ready and applicable to consume, `false` otherwise.
     */
    bool hasUsablePrefetchedNextEpisode() const;
    QString expectedPrefetchRequestContext() const;
    QString expectedAutoplayResolutionRequestContext() const;
    bool matchesPlaybackRequestContext(const QString &requestContext, bool awaitingPlaybackEnd) const;
    /**
     * Consume any prefetched next-episode payload and navigate to it using the stored autoplay/context.
     */
    void consumePrefetchedNextEpisodeAndNavigate();
    /**
     * Clear any stored state related to next-episode prefetching and resolution.
     */
    void clearNextEpisodePrefetchState();
    /**
     * Preserve the current autoplay/navigation context so it can be restored after teardown or state transitions.
     */
    void stashPendingAutoplayContext();
    void emitNavigateToNextEpisodeQueued(const QVariantMap &episodeData, const QString &seriesId,
                                         const QString &carryTrackPreferencesFromSeasonId,
                                         bool autoplay, bool stillWatchingPrompt = false);
    /**
     * Set whether the controller is currently awaiting resolution of the next-episode decision.
     * @param awaiting `true` when awaiting next-episode resolution, `false` otherwise.
     */
    void setAwaitingNextEpisodeResolution(bool awaiting);
    /**
     * Load runtime configuration values from the ConfigManager into cached controller state.
     */
    void loadConfig();
    /**
     * Begin playback of the given URL using the current playback configuration and state.
     * @param url Media resource URL to start playback for.
     */
    void startPlayback(const QString &url);
    /**
     * Apply framerate-matching decisions (if any) to the pending playback context and start playback.
     */
    void applyFramerateMatchingAndStart();
    /**
     * Initiate mpv (or configured internal player) startup sequence according to the pending playback context.
     */
    void initiateMpvStart();
    HdrPlaybackPolicy computeEffectiveHdrPlaybackPolicy() const;
    // Dolby Vision filter handling for the backend's runtime; see
    // docs/playback.md ("Dolby Vision processing and Profile 7 FEL").
    QStringList applyDolbyVisionPlaybackArgs(const QStringList &args);
    void onBackendRuntimeInfoChanged();
    void onPlaybackDiagnosticsAvailable(const QString &stage, const QVariantMap &properties);
    /**
     * Update the trickplay preview image/URL for the specified playback position.
     * @param seconds Position in seconds for which to update the trickplay preview.
     */
    void updateTrickplayPreviewForPosition(double seconds);
    /**
     * Clear any active trickplay preview override and associated preview URL/state.
     */
    void clearTrickplayPreview();
    /// Cached data URL for one trickplay frame; empty on failure.
    QString cachedTrickplayFrameUrl(int frameIndex);
    void enableLocalTrickplay();
    void requestTrickplayInfo(const QString &itemId);
    void updateLocalSeekPreviewSuspension();
    /// Requests the season/series listings used for previous/next episode.
    void refreshAdjacentEpisodes();
    void resolveAdjacentEpisodes();
    void requestAdjacentEpisodeListing(const QString &parentId, const QString &kind);
    void requestAdjacentEpisodePlayback(const QVariantMap &episode);
    void setPlaybackVersions(const QString &itemId, const QVariantList &versions);
    static QString episodeOverlaySubtitle(const QVariantMap &episode);
    /**
     * Re-evaluate and update flags that indicate whether the player is currently inside intro/outro or other skip segments.
     */
    void updateSkipSegmentState();
    void updateCurrentPlaybackChapterIndex();
    void clearPlaybackChapters();
    /**
     * Seek to the end boundary of the specified media segment type (e.g., intro or outro).
     * @param segmentType Type of media segment whose end should be sought.
     * @returns `true` if a seek was performed to the segment end, `false` if no applicable segment was found.
     */
    bool seekToSegmentEnd(MediaSegmentType segmentType);
    // The current item's ranges of one type, touching or overlapping ones
    // merged (PlaybackPolicy::mergeSkipRanges).
    QList<Bloom::PlaybackPolicy::SkipRange> skipRanges(MediaSegmentType type) const;
    // Identifies one merged skip range for m_autoSkippedSegments.
    static QString segmentSkipKey(MediaSegmentType type, const Bloom::PlaybackPolicy::SkipRange &range);
    // How far ahead of the position a skip may reach for a range that has
    // not begun yet (one starting a moment after playback starts).
    static constexpr qint64 kSkipAheadGraceMs = 2000;
    /**
     * Schedule a debounced persistence of the current playback volume state to configuration/storage.
     */
    void schedulePersistPlaybackVolumeState();
    /**
     * Persist the current playback volume and mute state immediately.
     */
    void persistPlaybackVolumeState();
    /**
     * Build a data URL referencing a trickplay preview frame stored in a binary file.
     * @param binaryPath Filesystem path to the trickplay binary data.
     * @param frameIndex Zero-based index of the frame within the binary.
     * @param width Frame width in pixels.
     * @param height Frame height in pixels.
     * @returns A QString containing the data URL suitable for use as an image source, or an empty string on failure.
     */
    static QString buildTrickplayPreviewDataUrl(const QString &binaryPath, int frameIndex, int width, int height);
    /**
     * Connect required signals from the provided player backend into this controller.
     * @param backend Backend instance whose signals should be connected.
     */
    void connectBackendSignals(IPlayerBackend *backend);
    void enterIdleStateImmediate();
    void startDeferredDisplayRestore(bool needsHdrRestore, bool needsRefreshRestore);
    void maybeStartDeferredPostPlaybackDisplayRestore();
    void scheduleDeferredRefreshRestore(quint64 generation, int delayMs);
    void cancelPendingDisplayRestore(bool clearParkedPostPlaybackRestore = true);
    void cancelPendingTerminalTransition();
    void scheduleReplacementPlayback(const std::function<void()> &action);
    void finalizeReplacementPlaybackStop();
    bool runPendingReplacementPlaybackAction();
    void queueBackendFallbackFinalization();
    void finalizeBackendFallback();
    /**
     * Attempt to fall back from the internal player backend to an external backend for the current attempt.
     * @param reason Human-readable reason for attempting the fallback.
     * @returns `true` if a fallback was initiated, `false` otherwise.
     */
    bool tryFallbackToExternalBackend(const QString &reason);
    using ResolvedTrackSelection =
        Bloom::PlaybackPolicy::ResolvedTrackSelection;

    void updateTrackMappings(const QVariantMap &mediaSource);
    /**
     * Map a provider source-track index to the corresponding mpv audio track number.
     * @param sourceStreamIndex Provider source-track index.
     * @returns 1-based mpv audio track number, or -1 when auto/none is intended.
     */
    int mpvAudioTrackForSourceIndex(int sourceStreamIndex) const;
    int sourceAudioTrackForMpvTrack(int mpvTrackId) const;
    /**
     * Map a provider source-track index to the corresponding mpv subtitle track number.
     * @param sourceStreamIndex Provider source-track index.
     * @returns 1-based mpv subtitle track number, or -1 when subtitles are disabled.
     */
    int mpvSubtitleTrackForSourceIndex(int sourceStreamIndex) const;
    int sourceSubtitleTrackForMpvTrack(int mpvTrackId) const;
    ResolvedTrackSelection resolveTrackSelection(const QVariantMap &mediaSource,
                                                 const ScopedTrackPreferences &preferences,
                                                 int preferredAudioIndex = -2,
                                                 int preferredSubtitleIndex = -2) const;
    void syncBackendAudioTrack(int mpvTrackId);
    void syncBackendSubtitleTrack(int mpvTrackId);
    void onAudioDeviceListChanged(const QVariantList &devices);
    void applyAudioOutputDevice();
    void updateAudioPlan();
    void applyAudioPlanOptions();
    Bloom::AudioOutputPlan computeAudioPlan();
    void onAudioReloadTimeout();
    void updateAudioOutputStatus();
    void reportAudioOutputNotice(const QString &message);
    QString audioDeviceLabel(const QString &device) const;
    void applySubtitleDelay();
    void persistSubtitleDelayForCurrentScope(int delayMs);
    int subtitleDelayForScope(const QString &seasonId, const QString &itemId, bool isMovie) const;
    // How a track saved for the current season or movie came about, which
    // decides its write-back to the server.
    enum class TrackPick {
        User,               // a pick that already plays: written after a pause
        UserAwaitingReplan, // a pick that needs a new stream: held, written once it
                            // plays; `previousIndex` is the track it replaces
        Restore,            // a held pick undone, `index` playing instead: see
                            // restoreHeldTrackPick
    };
    void persistAudioPreferenceForCurrentScope(int index, TrackPick pick = TrackPick::User,
                                               int previousIndex = -2);
    void persistSubtitlePreferenceForCurrentScope(int index, TrackPick pick = TrackPick::User,
                                                  int previousIndex = -2);
    void persistTrackPreferenceForCurrentScope(TrackPreferenceWrite::Kind kind, int index,
                                               TrackPick pick, int previousIndex);
    void saveCurrentScopeTrackPreference(TrackPreferenceWrite::Kind kind,
                                         const TrackSelectionPreference &preference);
    // Undoes the held pick of `kind`. If `playingIndex` is a track the pick
    // replaced, the choice that picked it comes back (a pick, or nothing
    // saved when the server or Bloom's defaults chose it) with its unsent
    // write; any other delivered track is saved as the choice. The undone
    // pick is never written, and a server already holding an undone pick
    // gets the restored track. Nothing happens when no pick is held: an
    // undone pick that was never saved changed nothing.
    void restoreHeldTrackPick(TrackPreferenceWrite::Kind kind, const QString &contentId,
                              int playingIndex);
    // Held picks the answered stream change asked for but did not deliver.
    void rollBackUngrantedTrackPicks(int inFlightAudio, int inFlightSubtitle);
    void saveSeasonTrackPreference(const QString &seasonId, TrackPreferenceWrite::Kind kind,
                                   const TrackSelectionPreference &preference);
    void saveMovieTrackPreference(const QString &movieId, TrackPreferenceWrite::Kind kind,
                                  const TrackSelectionPreference &preference);
    // Writes a user's pick back to the server for the series (episodes) or
    // the movie after a short pause, so browsing through tracks sends only
    // the last one. Skipped when server preferences are off. A write keeps
    // the account and profile it was picked under and is dropped if either
    // changes or the setting turns off before it is sent.
    void scheduleTrackPreferenceWrite(TrackPreferenceWrite::Kind kind, const QString &contentId,
                                      const QVariantMap &mediaSource, int index,
                                      bool awaitingReplan = false);
    void cancelTrackPreferenceWrite(TrackPreferenceWrite::Kind kind, const QString &contentId);
    [[nodiscard]] std::optional<TrackPreferenceWrite> takePendingTrackPreferenceWrite(
        TrackPreferenceWrite::Kind kind, const QString &contentId);
    // Queues a write that is due after the usual pause.
    void queueTrackPreferenceWrite(const TrackPreferenceWrite &write);
    // Writes held for a stream change become due once no change is pending.
    void releaseHeldTrackPreferenceWrites();
    void dropHeldTrackPreferenceWrites();
    void dropStaleTrackPreferenceWrites();
    void flushTrackPreferenceWrites();
    [[nodiscard]] QString trackPreferenceWriteKey(const TrackPreferenceWrite &write) const;
    void updateSeasonPreference(const QString &seasonId,
                                std::function<void(ScopedTrackPreferences &preferences)> updater);
    void updateMoviePreference(const QString &movieId,
                               std::function<void(ScopedTrackPreferences &preferences)> updater);
    QVariantList buildAvailableTrackOptions(const QVariantMap &mediaSource, const QString &streamType) const;
    double videoFramerateForMediaSource(const QVariantMap &mediaSource) const;
    bool mediaSourceIsHdr(const QVariantMap &mediaSource) const;
    void maybeResolvePendingRequestLibraryId(PendingPlaybackRequest &pending);
    void maybeFinalizePendingPlaybackRequest(const QString &requestId);
    void launchResolvedPlaybackRequest(const QString &requestId);
    void failPendingPlaybackRequest(const QString &requestId, const QString &message);
    void requestPlaybackDescriptors(const QString &requestId);
    void maybeFinalizePlaybackDescriptors(const QString &requestId);
    void applyPlaybackDescriptorToSegment(QVariantMap &segment,
                                          const Bloom::PlaybackDescriptor &descriptor) const;
    void loadActiveStreamLayout(const QVariantMap &segment);
    bool requestStreamReplan(bool seek, double sourceSeconds);
    void seekToSourcePosition(double sourceSeconds);
    bool canSeekLocally(double playerSeconds) const;
    bool subtitleOptionRequiresReplan(int index) const;
    bool selectedSubtitleIsSidecar() const;
    void appendDescriptorExternalSubtitleTracks(QVariantList &availableSubtitleTracks,
                                                const Bloom::PlaybackDescriptor &descriptor,
                                                int *selectedSubtitleIndex) const;
    void queueActiveSegmentExternalSubtitleTracks();
    void applyPendingExternalSubtitleTracks();
    void addExternalSubtitleTrackInternal(const QString &subtitleUrl,
                                          const QString &displayTitle,
                                          const QString &language,
                                          int sourceStreamIndexHint,
                                          bool select,
                                          bool loadInMpv = true);
    QVariantMap buildPlaybackVersionDialogModel(const QString &requestId) const;
    QString buildVersionSubtitle(const QVariantMap &mediaSource) const;
    /**
     * Convert a playback state enum value to a human-readable string.
     * @param state PlaybackState value to convert.
     * @returns A QString representation of `state`.
     */
    static QString stateToString(PlaybackState state);
    /**
     * Convert an event enum value to a human-readable string.
     * @param event Event value to convert.
     * @returns A QString representation of `event`.
     */
    static QString eventToString(Event event);
    [[nodiscard]] bool isRecoverableBackendPlaybackError(const QString &error) const;
    void fallbackToPendingAutoplayPlayback();
    // The season whose track picks the finished episode carries into the
    // next one, or empty: only into another season, and only for a kind
    // (audio, subtitles) that has an explicit pick there and none in the
    // next season (in the same season the saved pick resolves by itself).
    [[nodiscard]] QString pendingAutoplayCarrySeasonId(const QString &targetSeasonId) const;
    // A season's saved picks, with each kind it has none of taken from
    // `carryFromSeasonId`. The carried picks are matched by what the track
    // is, not its index, like any saved pick.
    [[nodiscard]] ScopedTrackPreferences seasonTrackPreferencesWithCarry(
        const QString &seasonId, const QString &carryFromSeasonId) const;
    // Saves the picks carried into the season that now plays as its own, so
    // its next episodes keep them rather than follow the server.
    void saveCarriedTrackPreferences();
    void stopAutoplayPlaybackInfoWait();
    void armAutoplayPlaybackInfoWait();
    static qint64 resumePositionMs(const QVariantMap &item);
    QVariantMap selectMediaSourceForRequest(const QVariantList &mediaSources,
                                            const QString &forcedMediaSourceId,
                                            bool useAffinityFallback) const;
    QVariantMap resolveSegmentPlaybackContext(const ScopedTrackPreferences &trackPreferences,
                                              const PlaybackInfoResponse &playbackInfo,
                                              const QVariantList &mediaSources,
                                              const QVariantMap &preferredMediaSource,
                                              int preferredAudioIndex,
                                              int preferredSubtitleIndex,
                                              bool useAffinityFallback) const;
    QVariantList buildMultipartSegments(const QVariantMap &request,
                                        const QVariantMap &primarySource,
                                        const PlaybackInfoResponse &primaryPlaybackInfo);
    QVariantMap activePlaybackSegment() const;
    QString activePlaybackSegmentItemId() const;
    void applyPlaybackSegment(int index, bool reportSegmentStart);
    void clearPlaybackSegments();
    qint64 currentReportingPositionMs() const;
    void reportPlaybackStartForSegment(const QVariantMap &segment);
    void reportPlaybackStopForSegment(const QVariantMap &segment, qint64 positionMs);
    void updateVersionAffinityFromMediaSource(const QVariantMap &mediaSource);
    [[nodiscard]] QString mediaSourceParentPath(const QVariantMap &mediaSource) const;
    [[nodiscard]] QString mediaSourceSignature(const QVariantMap &mediaSource) const;
    struct PendingPlaybackRequest
    {
        QString requestId;
        QString itemId;
        QString seriesId;
        QString seasonId;
        QString libraryId;
        QString overlayTitle;
        QString overlaySubtitle;
        QString overlayBackdropUrl;
        QString overlayLogoUrl;
        qint64 startPositionMs = 0;
        int preferredAudioIndex = -2;
        int preferredSubtitleIndex = -2;
        // The season whose picks this episode takes for kinds its own season
        // has none of (see seasonTrackPreferencesWithCarry).
        QString carryTrackPreferencesFromSeasonId;
        bool isMovie = false;
        Bloom::PlaybackPolicy::PlaybackContentKind contentKind =
            Bloom::PlaybackPolicy::PlaybackContentKind::Feature;
        bool allowVersionPrompt = true;
        bool useAffinityFallback = false;
        bool awaitingLibraryResolution = false;
        QObject *restoreFocusTarget = nullptr;
        bool additionalPartsLoaded = false;
        bool versionSelectionRequested = false;
        QHash<QString, PlaybackInfoResponse> playbackInfos;
        QVariantList descriptorSegments;
        QHash<QString, Bloom::PlaybackDescriptor> descriptors;
        int pendingDescriptorCount = 0;
        bool descriptorsRequested = false;
        // Only the first segment's session starts up front; see
        // requestDeferredSegmentDescriptor.
        bool deferLaterSegments = false;
        QString chosenMediaSourceId;
        QVariantList additionalParts;
        QSet<QString> awaitedPlaybackInfoIds;
        QSet<QString> failedPlaybackInfoIds;
        bool primaryPlaybackInfoFailed = false;
        QString failureMessage;
    };

    IPlayerBackend *m_playerBackend;
    std::unique_ptr<IPlayerBackend> m_ownedBackend;
    QPointer<IPlayerBackend> m_backendFallbackSource;
    bool m_backendFallbackFinalizationQueued = false;
    ConfigManager *m_config;
    TrackPreferencesManager *m_trackPrefs;
    DisplayManager *m_displayManager;
    PlaybackService *m_playbackService;
    LibraryService *m_libraryService;
    AuthenticationService *m_authService;
    TrickplayProcessor *m_trickplayProcessor;
    
    // State machine transition table
    // Maps (CurrentState, Event) → NextState
    using StateTransition = QPair<PlaybackState, Event>;
    QHash<StateTransition, PlaybackState> m_transitions;
    
    // Timeouts
    QTimer *m_loadingTimeoutTimer;
    QTimer *m_bufferingTimeoutTimer;
    QTimer *m_startDelayTimer;  // Trackable timer for deferred mpv start
    static constexpr int kLoadingTimeoutMs = 30000;  // 30 seconds
    static constexpr int kRemoteMountLoadingTimeoutMs = 300000; // 5 minutes
    static constexpr double kRemoteMountInitialCacheSeconds = 60.0;
    static constexpr int kBufferingTimeoutMs = 60000; // 60 seconds
    
    // Progress reporting timer
    QTimer *m_progressReportTimer;
    QTimer *m_volumePersistTimer;
    QTimer *m_autoplayPlaybackInfoTimeoutTimer;
    QTimer *m_trackPreferenceWriteTimer = nullptr;
    struct PendingTrackPreferenceWrite
    {
        TrackPreferenceWrite write;
        bool awaitingReplan = false;
    };
    // At most one per kind and content.
    QList<PendingTrackPreferenceWrite> m_pendingTrackPreferenceWrites;
    // What a held pick replaced: the track that played, the choice saved for
    // it then, and its write if one was still waiting.
    struct ReplacedTrackPick
    {
        int streamIndex = -2;
        TrackSelectionPreference preference;
        std::optional<TrackPreferenceWrite> write;
    };
    // A pick of the current season or movie waiting on a stream change.
    struct HeldTrackPick
    {
        int streamIndex = -2;
        // Newest first: a pick that replaces a held one keeps its history.
        QList<ReplacedTrackPick> replaced;
    };
    // Audio, then subtitles.
    std::array<std::optional<HeldTrackPick>, 2> m_heldTrackPicks;
    // The stream index last sent per account, kind and content, so undoing a
    // pick that already reached the server writes the restored track.
    QHash<QString, int> m_sentTrackPreferenceIndexes;

    // Audio output device hotplug handling
    Bloom::AudioOutputPlan m_audioPlan;
    QVariantMap m_audioDiagnostics;
    QVariantMap m_preparedAudioPlan;
    bool m_forcePcmAudio = false;
    // Session fallback after mpv failed to open the endpoint exclusively.
    bool m_exclusiveFallback = false;
    // Next debounced reload must reopen audio even if the plan is unchanged.
    bool m_audioReloadForced = false;
    QString m_audioPlanEndpointId;
    QVariantList m_audioOutputOptions;
    QVariantMap m_audioOutputStatus;
    QString m_reportedMissingAudioDevice;
    QTimer *m_audioDeviceRefreshTimer = nullptr;
    bool m_reopeningAudio = false;
    quint64 m_audioPlanGeneration = 0;
    qint64 m_nextAudioRetryMs = 0;
    QVariantList m_availableAudioDevices;
    QTimer *m_audioDeviceReloadTimer = nullptr;
    bool m_audioDeviceListInitialized = false;
    static constexpr int kAudioDeviceReloadDebounceMs = 500;
    static constexpr int kProgressReportIntervalMs = 10000; // 10 seconds
    static constexpr int kPausedProgressEveryTicks = 6;     // one paused sample per minute
    // A deferred multipart part's session starts this close to the handoff.
    static constexpr double kDeferredSegmentLeadSeconds = 20.0;
    int m_pausedProgressTicks = 0;
    // Request context of the in-flight deferred multipart segment descriptor.
    QString m_deferredSegmentContext;
    static constexpr int kVolumePersistDebounceMs = 250;
    static constexpr int kAutoplayPlaybackInfoTimeoutMs = 8000;
    static constexpr int kTrackPreferenceWriteDelayMs = 1500;
    static constexpr double kNextEpisodePrefetchTriggerPercent = 70.0;
    static constexpr int kMaxSubtitleDelayMs = 600000;

    // Recovery from transient server loss
    struct RecoveryContext {
        QString url;
        QString itemId;
        qint64 startPositionMs = 0;
        QString seriesId;
        QString seasonId;
        QString libraryId;
        Bloom::PlaybackPolicy::PlaybackContentKind contentKind =
            Bloom::PlaybackPolicy::PlaybackContentKind::Feature;
        QString mediaSourceId;
        QString playSessionId;
        QVariantMap mediaSource;
        int audioStreamIndex = -1;
        int subtitleStreamIndex = -1;
        QVariantList availableAudioTracks;
        QVariantList availableSubtitleTracks;
        double framerate = 0.0;
        bool isHDR = false;
        QList<QVariantMap> playbackSegments;
        int activePlaybackSegmentIndex = -1;
        qint64 activePlaybackSegmentOffsetMs = 0;
        double segmentRelativePosition = 0.0;
        bool valid = false;
    };
    RecoveryContext m_recoveryContext;
    QTimer *m_recoveryTimer;
    QPointer<HttpRequestHandle> m_recoveryPing;
    int m_recoveryAttemptCount = 0;
    bool m_isRecovering = false;
    bool m_lastErrorWasNetworkRecoverable = false;
    bool m_waitingForRemoteMountInitialCache = false;
    bool m_completingRemoteMountStartupBuffering = false;
    static constexpr int kRecoveryPingIntervalMs = 5000;
    
    // State
    PlaybackState m_playbackState = Idle;
    QString m_errorMessage;
    int m_bufferingProgress = 0;
    
    // Playback Tracking
    QString m_currentItemId;
    QString m_currentSeriesId;  // For autoplay next episode
    // An extra never leads into autoplay/Up Next or saves track picks.
    Bloom::PlaybackPolicy::PlaybackContentKind m_currentContentKind =
        Bloom::PlaybackPolicy::PlaybackContentKind::Feature;
    Bloom::PlaybackPolicy::PlaybackContentKind m_nextContentKind =
        Bloom::PlaybackPolicy::PlaybackContentKind::Feature;
    QString m_currentSeasonId;  // For per-season track preferences
    QString m_currentLibraryId; // For profile resolution
    QString m_pendingUrl;
    double m_currentPosition = 0;
    double m_duration = 0;
    double m_cacheEndSeconds = 0;
    bool m_hasReportedStart = false;
    double m_seekTargetWhileBuffering = -1;
    bool m_reportProgressOnNextPositionUpdate = false;
    qint64 m_startPositionMs = 0;
    bool m_shouldAutoplay = false;  // Flag to trigger autoplay on next episode loaded
    // Episodes the Up Next countdown started in a row without user input.
    int m_autoplayChainCount = 0;
    bool m_stillWatchingPromptActive = false;
    // Pointer travel from the reference point that counts as movement, in
    // logical pixels; smaller moves are sensor jitter or resent hovers.
    static constexpr qreal kPointerMovementThreshold = 8.0;
    bool m_lastPointerPositionKnown = false;
    QPointF m_lastPointerGlobalPosition;
    // Sleep timer. Duration timers count playing time only.
    int m_sleepTimerChoice = 0;
    int m_sleepTimerRemainingSeconds = 0;
    bool m_sleepTimerGraceActive = false;
    int m_sleepTimerGraceRemainingSeconds = 0;
    // The pause/stop the timer itself sent; neither counts as user input
    // nor (for the stop) leads into Up Next.
    bool m_sleepTimerPauseRequested = false;
    bool m_sleepTimerStopRequested = false;
    QTimer *m_sleepTimerTickTimer = nullptr;
    // QML-visible/property-facing flag for waiting on next-episode resolution callbacks.
    bool m_awaitingNextEpisodeResolution = false;
    // Internal playback-end flow gate used to act only on expected post-end next-episode events.
    bool m_waitingForNextEpisodeAtPlaybackEnd = false;
    bool m_nextEpisodePrefetchRequestedForAttempt = false;
    bool m_nextEpisodePrefetchReady = false;
    QVariantMap m_prefetchedNextEpisodeData;
    QString m_prefetchedNextEpisodeSeriesId;
    QString m_prefetchedForItemId;

    // Persisted autoplay context across state teardown/idle transition
    QString m_pendingAutoplayItemId;
    QString m_pendingAutoplaySeriesId;
    QString m_pendingAutoplaySeasonId;
    QString m_pendingAutoplayLibraryId;
    int m_pendingAutoplayAudioTrack = -1;
    int m_pendingAutoplaySubtitleTrack = -1;
    // The carry season of the stream about to start, then of the stream that
    // plays until it reaches Playing (see saveCarriedTrackPreferences).
    QString m_nextCarryTrackPreferencesFromSeasonId;
    QString m_carryTrackPreferencesFromSeasonId;
    double m_pendingAutoplayFramerate = 0.0;
    bool m_pendingAutoplayIsHDR = false;
    bool m_pendingAutoplayToneMapToSdr = false;
    
    // Track selection state
    int m_selectedAudioTrack = -1;      // Jellyfin audio stream index (for API reporting)
    int m_selectedSubtitleTrack = -1;   // Jellyfin subtitle stream index (-1 = none)
    int m_subtitleDelayMs = 0;          // mpv sub-delay in milliseconds, scoped like subtitle preferences
    int m_mpvAudioTrack = -1;           // mpv audio track number (1-based, -1 = auto)
    int m_mpvSubtitleTrack = -1;        // mpv subtitle track number (1-based, -1 = disabled)
    QHash<int, int> m_audioTrackMap;    // Provider source index -> mpv aid track ID (1-based)
    QHash<int, int> m_subtitleTrackMap; // Provider source index -> mpv sid track ID (1-based)
    QHash<int, int> m_externalSubtitleTrackMap; // Synthetic external index -> mpv sid track ID (1-based)
    int m_pendingExternalSubtitleIndex = -1;     // Synthetic index awaiting sid resolution from mpv
    QHash<int, int> m_audioTrackReverseMap;    // mpv aid track ID (1-based) -> provider source index
    QHash<int, int> m_subtitleTrackReverseMap; // mpv sid track ID (1-based) -> provider source index
    QString m_mediaSourceId;            // Current media source ID
    QVariantList m_pendingExternalSubtitleTracks;
    QString m_pendingAutoplayDescriptorContext;
    QString m_pendingAutoplayDescriptorItemId;
    QVariantMap m_pendingAutoplayDescriptorSource;
    int m_pendingAutoplayDescriptorAudioIndex = -1;
    int m_pendingAutoplayDescriptorSubtitleIndex = -1;
    qint64 m_pendingAutoplayDescriptorStartPositionMs = 0;
    QString m_pendingAutoplayDescriptorSessionId;
    QVariantList m_pendingAutoplayDescriptorAudioTracks;
    QVariantList m_pendingAutoplayDescriptorSubtitleTracks;
    QString m_pendingAudioSwitchContext;   // in-flight replan request context
    int m_pendingAudioSwitchPreviousTrack = -2;
    int m_pendingSubtitleReplanPreviousTrack = -2;
    bool m_pendingReplanIsSeek = false;
    // A replan asked for while another is in flight runs when that one ends;
    // the server rejects replans of a plan it has already replaced.
    bool m_replanQueued = false;
    // Tracks the in-flight replan asked for, and choices made since then that
    // the queued replan must carry (kNoQueuedTrack: none; -1 is subtitles off).
    static constexpr int kNoQueuedTrack = -2;
    int m_inFlightAudioTrack = kNoQueuedTrack;
    int m_inFlightSubtitleTrack = kNoQueuedTrack;
    int m_queuedAudioTrack = kNoQueuedTrack;
    int m_queuedSubtitleTrack = kNoQueuedTrack;
    // Segment index of mpv playlist entry 0 (non-zero after an in-place reload).
    int m_playlistSegmentOffset = 0;
    bool m_queuedReplanIsSeek = false;
    double m_queuedReplanSourceSeconds = 0.0;
    // Active stream timeline and track layout (see Bloom::StreamRequest).
    double m_streamTimelineOffsetSeconds = 0.0;
    double m_streamPlayerStartSeconds = 0.0;
    bool m_streamSeekRequiresReanchor = false;
    bool m_streamCarriesSourceTracks = true;
    bool m_streamSubtitleBurnedIn = false;
    QList<int> m_streamDeliveredAudioTracks;
    QList<int> m_streamDeliveredSubtitleTracks;
    // loadfile replace is in flight: the old file's end is not playback end,
    // and its positions are on the previous timeline.
    bool m_awaitingInPlaceReload = false;
    bool m_ignoreReplacedFileEnd = false;
    QString m_playSessionId;            // Playback session ID for reporting
    QVariantList m_availableAudioTracks;
    QVariantList m_availableSubtitleTracks;
    QVariantMap m_activeMediaSource;
    Bloom::PlaybackPolicy::DolbyVisionSourceInfo m_activeDolbyVisionSource;
    Bloom::PlaybackPolicy::RendererKind m_activeRendererKind = Bloom::PlaybackPolicy::RendererKind::Unknown;
    QString m_loggedRuntimeIdentity;
    bool m_applyingInitialTracks = false;
    bool m_pendingAudioTrackPersistenceFromBackend = false;
    bool m_pendingSubtitleTrackPersistenceFromBackend = false;
    bool m_waitingForAutoplayPlaybackInfo = false;
    QVariantMap m_pendingAutoplayEpisodeData;
    QHash<QString, PendingPlaybackRequest> m_pendingPlaybackRequests;
    QHash<QString, QString> m_seriesLibraryIdCache;
    QSet<QString> m_seriesLibraryResolutionInFlight;
    QList<QVariantMap> m_playbackSegments;
    QVariantList m_nextPlaybackSegments;
    QStringList m_nextPlaylistAppendUrls;
    int m_activePlaybackSegmentIndex = -1;
    qint64 m_activePlaybackSegmentOffsetMs = 0;
    double m_segmentRelativePosition = 0.0;
    double m_aggregatePlaybackDuration = 0.0;
    QStringList m_pendingPlaylistAppendUrls;
    QString m_lastVersionName;
    QString m_lastVersionParentPath;
    QString m_lastVersionSignature;
    
    // Content framerate for display refresh rate matching
    double m_contentFramerate = 0.0;
    double m_mpvDisplayFpsOverride = 0.0;
    
    // Content HDR flag for display HDR switching
    bool m_contentIsHDR = false;
    bool m_contentShouldToneMapToSdr = false;
    quint64 m_playbackAttemptId = 0;
    bool m_hasReportedStopForAttempt = false;
    bool m_hasEvaluatedCompletionForAttempt = false;
    bool m_terminalTransitionActive = false;
    TerminalReason m_pendingTerminalReason = TerminalReason::Stop;
    bool m_backendStopRequested = false;
    bool m_terminalFinalizationQueued = false;
    bool m_terminalPrefetchedReady = false;
    quint64 m_terminalTransitionGeneration = 0;
    bool m_suppressBackendStopHandling = false;
    std::function<void()> m_pendingReplacementPlaybackAction;
    quint64 m_displayRestoreGeneration = 0;
    QMetaObject::Connection m_hdrRestoreFinishedConnection;
    QMetaObject::Connection m_refreshRestoreFinishedConnection;
    QMetaObject::Connection m_displayPreparationConnection;
    bool m_deferredPostPlaybackDisplayRestorePending = false;
    bool m_deferredPostPlaybackNeedsHdrRestore = false;
    bool m_deferredPostPlaybackNeedsRefreshRestore = false;
    
    // Buffering detection
    QElapsedTimer m_lastPositionUpdateTime;
    double m_lastPosition = 0;
    bool m_isWaitingForPosition = false;
    
    // Config cache
    QString m_mpvBin;
    QString m_testVideoUrl;

    bool m_embeddedVideoShrinkEnabled = false;
    int m_volume = 100;
    bool m_muted = false;
    QString m_playMethod = QStringLiteral("directPlay");
    QString m_nextPlaybackMethod;
    bool m_streamPinsAudioTrack = false;
    bool m_streamPinsSubtitleTrack = false;
    bool m_nextStreamPinsAudioTrack = false;
    bool m_nextStreamPinsSubtitleTrack = false;
    int m_pinnedAudioTrack = -1;
    int m_pinnedSubtitleTrack = -1;
    int m_nextPinnedAudioTrack = -1;
    int m_nextPinnedSubtitleTrack = -1;
    bool m_attemptedLinuxEmbeddedFallback = false;
    QString m_overlayTitle;
    QString m_overlaySubtitle;
    QString m_overlayBackdropUrl;
    QString m_overlayLogoUrl;
    QVariantList m_playbackChapters;
    int m_currentPlaybackChapterIndex = -1;
    
    // OSC and trickplay data
    QList<MediaSegmentInfo> m_currentSegments;
    bool m_isInIntroSegment = false;
    bool m_isInOutroSegment = false;
    // Intro and credits ranges auto-skip already skipped for this item
    // (segmentSkipKey), so a seek back into one does not skip it again
    // while a later range of the same type still skips.
    QSet<QString> m_autoSkippedSegments;
    // The merged intro and credits ranges holding the position (empty when
    // none), so entering a new range of the same type is noticed.
    QString m_activeIntroSkipKey;
    QString m_activeOutroSkipKey;
    TrickplayTileInfo m_currentTrickplayInfo;
    bool m_hasTrickplayInfo = false;
    LocalSeekPreview *m_localSeekPreview = nullptr;
    // Local previews wait for playback to settle so they never compete with it.
    QTimer *m_localSeekPreviewResumeTimer = nullptr;
    static constexpr int kLocalSeekPreviewSettleMs = 5000;
    bool m_useLocalTrickplay = false;
    quint64 m_trickplayFrameRevision = 0;
    QString m_trickplayBinaryPath;
    int m_currentTrickplayFrameIndex = -1;
    QString m_trickplayPreviewUrl;
    bool m_hasTrickplayPreviewPositionOverride = false;
    double m_trickplayPreviewPositionOverrideSeconds = 0.0;
    // Encoded trickplay frames keyed by frame index, valid for m_trickplayFrameCachePath.
    QHash<int, QString> m_trickplayFrameUrlCache;
    QList<int> m_trickplayFrameUrlOrder;
    QString m_trickplayFrameCachePath;

    // In-playback version picker state, scoped to the item it was built for.
    QString m_playbackVersionsItemId;
    QVariantList m_playbackVersions;

    // Previous/next episode lookup, scoped to m_adjacentEpisodesItemId.
    QString m_adjacentEpisodesItemId;
    QString m_adjacentEpisodesConnectionId;
    QHash<QString, QVariantList> m_adjacentListings;   // kind -> items
    QSet<QString> m_adjacentListingsRequested;          // kind|parentId
    QVariantMap m_previousEpisode;
    QVariantMap m_nextEpisode;
};

// Hash functions required for QHash with enum types
inline size_t qHash(PlayerController::PlaybackState key, size_t seed = 0) noexcept
{
    return qHash(static_cast<int>(key), seed);
}

inline size_t qHash(PlayerController::Event key, size_t seed = 0) noexcept
{
    return qHash(static_cast<int>(key), seed);
}
