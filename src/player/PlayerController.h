#pragma once

#include <QObject>
#include <QTimer>
#include <QElapsedTimer>
#include <QHash>
#include <QMap>
#include <QVariantList>
#include <QtGlobal>
#include <memory>
#include "backend/IPlayerBackend.h"
#include "TrickplayProcessor.h"
#include "../utils/ConfigManager.h"
#include "../utils/TrackPreferencesManager.h"
#include "../utils/DisplayManager.h"

class PlaybackService;
class LibraryService;
class AuthenticationService;

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
    Q_PROPERTY(bool isPaused READ isPaused NOTIFY playbackStateChanged)
    Q_PROPERTY(bool hasError READ hasError NOTIFY hasErrorChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
    Q_PROPERTY(int bufferingProgress READ bufferingProgress NOTIFY bufferingProgressChanged)
    Q_PROPERTY(int audioDelay READ audioDelay WRITE setAudioDelay NOTIFY audioDelayChanged)
    Q_PROPERTY(bool supportsEmbeddedVideo READ supportsEmbeddedVideo NOTIFY supportsEmbeddedVideoChanged)
    Q_PROPERTY(bool embeddedVideoShrinkEnabled READ embeddedVideoShrinkEnabled WRITE setEmbeddedVideoShrinkEnabled NOTIFY embeddedVideoShrinkEnabledChanged)
    Q_PROPERTY(int volume READ volume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ muted NOTIFY mutedChanged)
    Q_PROPERTY(double currentPositionSeconds READ currentPositionSeconds NOTIFY timelineChanged)
    Q_PROPERTY(double durationSeconds READ durationSeconds NOTIFY timelineChanged)
    Q_PROPERTY(double progressRatio READ progressRatio NOTIFY timelineChanged)
    Q_PROPERTY(bool isInIntroSegment READ isInIntroSegment NOTIFY skipSegmentsChanged)
    Q_PROPERTY(bool isInOutroSegment READ isInOutroSegment NOTIFY skipSegmentsChanged)
    Q_PROPERTY(bool hasActiveSkipSegment READ hasActiveSkipSegment NOTIFY skipSegmentsChanged)
    Q_PROPERTY(bool hasTrickplay READ hasTrickplay NOTIFY trickplayStateChanged)
    Q_PROPERTY(int trickplayIntervalMs READ trickplayIntervalMs NOTIFY trickplayStateChanged)
    Q_PROPERTY(QString trickplayPreviewUrl READ trickplayPreviewUrl NOTIFY trickplayPreviewChanged)
    Q_PROPERTY(QString currentItemId READ currentItemId NOTIFY currentItemIdChanged)
    Q_PROPERTY(QString overlayTitle READ overlayTitle NOTIFY overlayMetadataChanged)
    Q_PROPERTY(QString overlaySubtitle READ overlaySubtitle NOTIFY overlayMetadataChanged)
    Q_PROPERTY(QString overlayBackdropUrl READ overlayBackdropUrl NOTIFY overlayMetadataChanged)
    
    // Track selection properties
    Q_PROPERTY(int selectedAudioTrack READ selectedAudioTrack WRITE setSelectedAudioTrack NOTIFY selectedAudioTrackChanged)
    Q_PROPERTY(int selectedSubtitleTrack READ selectedSubtitleTrack WRITE setSelectedSubtitleTrack NOTIFY selectedSubtitleTrackChanged)
    Q_PROPERTY(QString mediaSourceId READ mediaSourceId NOTIFY mediaSourceIdChanged)
    Q_PROPERTY(QString playSessionId READ playSessionId NOTIFY playSessionIdChanged)
    Q_PROPERTY(QVariantList availableAudioTracks READ availableAudioTracks NOTIFY availableTracksChanged)
    Q_PROPERTY(QVariantList availableSubtitleTracks READ availableSubtitleTracks NOTIFY availableTracksChanged)

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
    /**
     * Indicates whether playback is currently active.
     * @returns `true` if playback is active, `false` otherwise.
     */
    /**
     * Indicates whether the controller is awaiting resolution of the next episode (prefetch/navigation).
     * @returns `true` if awaiting next-episode resolution, `false` otherwise.
     */
    /**
     * Returns a human-readable name for the current playback state.
     * @returns The playback state's name as a QString.
     */
    /**
     * Indicates whether the player is currently buffering.
     * @returns `true` if buffering, `false` otherwise.
     */
    /**
     * Indicates whether the player is currently loading content.
     * @returns `true` if loading, `false` otherwise.
     */
    /**
     * Indicates whether playback is currently paused.
     * @returns `true` if paused, `false` otherwise.
     */
    PlaybackState playbackState() const;
    bool isPlaybackActive() const;
    bool awaitingNextEpisodeResolution() const { return m_awaitingNextEpisodeResolution; }
    QString stateName() const;
    bool isBuffering() const;
    bool isLoading() const;
    bool isPaused() const { return m_playbackState == Paused; }
    bool hasError() const;
    QString errorMessage() const;
    int bufferingProgress() const;
    
    // Track selection getters
    int selectedAudioTrack() const { return m_selectedAudioTrack; }
    int selectedSubtitleTrack() const { return m_selectedSubtitleTrack; }
    QString mediaSourceId() const { return m_mediaSourceId; }
    QString playSessionId() const { return m_playSessionId; }
    QVariantList availableAudioTracks() const { return m_availableAudioTracks; }
    QVariantList availableSubtitleTracks() const { return m_availableSubtitleTracks; }
    
    // Audio Delay (ms)
    int audioDelay() const { return m_config->getAudioDelay(); }
    bool supportsEmbeddedVideo() const;
    bool embeddedVideoShrinkEnabled() const { return m_embeddedVideoShrinkEnabled; }
    int volume() const { return m_volume; }
    bool muted() const { return m_muted; }
    double currentPositionSeconds() const { return m_currentPosition; }
    double durationSeconds() const { return m_duration; }
    double progressRatio() const { return m_duration > 0.0 ? qBound(0.0, m_currentPosition / m_duration, 1.0) : 0.0; }
    bool isInIntroSegment() const { return m_isInIntroSegment; }
    bool isInOutroSegment() const { return m_isInOutroSegment; }
    bool hasActiveSkipSegment() const { return m_isInIntroSegment || m_isInOutroSegment; }
    bool hasTrickplay() const { return m_hasTrickplayInfo; }
    int trickplayIntervalMs() const { return m_hasTrickplayInfo ? m_currentTrickplayInfo.interval : 0; }
    QString trickplayPreviewUrl() const { return m_trickplayPreviewUrl; }
    QString currentItemId() const { return m_currentItemId; }
    QString overlayTitle() const { return m_overlayTitle; }
    QString overlaySubtitle() const { return m_overlaySubtitle; }
    QString overlayBackdropUrl() const { return m_overlayBackdropUrl; }
    Q_INVOKABLE void setAudioDelay(int ms);
    Q_INVOKABLE bool attachEmbeddedVideoTarget(QObject *target);
    Q_INVOKABLE void detachEmbeddedVideoTarget(QObject *target = nullptr);
    Q_INVOKABLE void setEmbeddedVideoViewport(qreal x, qreal y, qreal width, qreal height);
    Q_INVOKABLE void setEmbeddedVideoShrinkEnabled(bool enabled);

    Q_INVOKABLE void playTestVideo();
    Q_INVOKABLE void playUrl(const QString &url, const QString &itemId = "", qint64 startPositionTicks = 0, const QString &seriesId = "", const QString &seasonId = "", const QString &libraryId = "", double framerate = 0.0, bool isHDR = false);
    
    /// Play the next episode from the Up Next screen
    /// @param episodeData JSON object with episode details (Id, Name, SeriesName, etc.)
    /// @param seriesId The series this episode belongs to
    Q_INVOKABLE void playNextEpisode(const QJsonObject &episodeData, const QString &seriesId);
    Q_INVOKABLE void clearPendingAutoplayContext();
    
    // Extended playUrl with track selection
    // audioStreamIndex/subtitleStreamIndex: Jellyfin unified stream indices (for API reporting)
    // mpvAudioTrack/mpvSubtitleTrack: mpv 1-based track IDs (for mpv commands)
    Q_INVOKABLE void playUrlWithTracks(const QString &url, const QString &itemId, qint64 startPositionTicks,
                                       const QString &seriesId, const QString &seasonId, const QString &libraryId,
                                       const QString &mediaSourceId, const QString &playSessionId,
                                       int audioStreamIndex, int subtitleStreamIndex,
                                       int mpvAudioTrack, int mpvSubtitleTrack,
                                       const QVariantList &audioTrackMap = {},
                                       const QVariantList &subtitleTrackMap = {},
                                       const QVariantList &availableAudioTracks = {},
                                       const QVariantList &availableSubtitleTracks = {},
                                       double framerate = 0.0, bool isHDR = false);
    
    Q_INVOKABLE void stop();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void resume();
    Q_INVOKABLE void togglePause();
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
    Q_INVOKABLE void cycleAudioTrack();
    Q_INVOKABLE void cycleSubtitleTrack();
    Q_INVOKABLE void previousChapter();
    Q_INVOKABLE void nextChapter();
    Q_INVOKABLE void toggleMute();
    Q_INVOKABLE void setMuted(bool muted);
    Q_INVOKABLE void setVolume(int volume);
    Q_INVOKABLE void adjustVolume(int delta);
    Q_INVOKABLE void showMpvStatsOnce();
    Q_INVOKABLE void toggleMpvStats();
    Q_INVOKABLE void showMpvStatsPage(int page);
    Q_INVOKABLE void sendMpvKeypress(const QString &key);
    Q_INVOKABLE void setOverlayMetadata(const QString &title, const QString &subtitle = QString(), const QString &backdropUrl = QString());
    Q_INVOKABLE void clearOverlayMetadata();
    Q_INVOKABLE void setTrickplayPreviewPositionSeconds(double seconds);
    Q_INVOKABLE void clearTrickplayPreviewPositionOverride();
    
    // Get last used track preferences for a season (for episode continuity)
    Q_INVOKABLE int getLastAudioTrackForSeason(const QString &seasonId) const;
    Q_INVOKABLE int getLastSubtitleTrackForSeason(const QString &seasonId) const;
    
    // Save track preferences from UI (before playback starts)
    // This allows preserving user's track selection when navigating away and back
    Q_INVOKABLE void saveAudioTrackPreference(const QString &seasonId, int index);
    Q_INVOKABLE void saveSubtitleTrackPreference(const QString &seasonId, int index);
    
    // Get last used track preferences for a movie
    Q_INVOKABLE int getLastAudioTrackForMovie(const QString &movieId) const;
    Q_INVOKABLE int getLastSubtitleTrackForMovie(const QString &movieId) const;
    
    // Save track preferences for a movie
    Q_INVOKABLE void saveMovieAudioTrackPreference(const QString &movieId, int index);
    Q_INVOKABLE void saveMovieSubtitleTrackPreference(const QString &movieId, int index);

signals:
    void playbackStateChanged();
    void stateChanged(const QString &state);
    void isBufferingChanged();
    void isLoadingChanged();
    void hasErrorChanged();
    void errorMessageChanged();
    void bufferingProgressChanged();
    void audioDelayChanged();
    void isPlaybackActiveChanged();
    void awaitingNextEpisodeResolutionChanged();
    void supportsEmbeddedVideoChanged();
    void embeddedVideoShrinkEnabledChanged();
    void volumeChanged();
    void mutedChanged();
    void timelineChanged();
    void skipSegmentsChanged();
    void trickplayStateChanged();
    void trickplayPreviewChanged();
    void currentItemIdChanged();
    void overlayMetadataChanged();
    
    // Track selection signals
    void selectedAudioTrackChanged();
    void selectedSubtitleTrackChanged();
    void mediaSourceIdChanged();
    void playSessionIdChanged();
    void availableTracksChanged();
    
    /// Emitted when playback has stopped (user stop, playback end, or error)
    /// Use this to refresh UI elements that depend on playback state (e.g., watch progress)
    void playbackStopped();
    
    /// Emitted when autoplay is about to start the next episode
    /// @param episodeName The name of the next episode
    /// @param seriesName The name of the series
    void autoplayingNextEpisode(const QString &episodeName, const QString &seriesName);
    
    /// Emitted when playback ends with threshold met and navigation to next episode is requested
    /// @param episodeData The JSON object containing the next episode data
    /// @param seriesId The series ID
    /// @param lastAudioIndex The audio track index used in the completed episode
    /// @param lastSubtitleIndex The subtitle track index used in the completed episode
    void navigateToNextEpisode(const QJsonObject &episodeData, const QString &seriesId,
                                int lastAudioIndex, int lastSubtitleIndex, bool autoplay);

private slots:
    void onProcessStateChanged(bool running);
    void onProcessError(const QString &error);
    void onPositionChanged(double seconds);
    void onDurationChanged(double seconds);
    void onPauseChanged(bool paused);
    void onPausedForCacheChanged(bool pausedForCache);
    void onPlaybackEnded();
    void onNextEpisodeLoaded(const QString &seriesId, const QJsonObject &episodeData);
    
    // Item marked as played handler
    void onItemMarkedPlayed(const QString &itemId);
    
    // Timeout handlers
    void onLoadingTimeout();
    void onBufferingTimeout();
    
    // OSC and trickplay handlers
    void onScriptMessage(const QString &messageName, const QStringList &args);
    void onMediaSegmentsLoaded(const QString &itemId, const QList<MediaSegmentInfo> &segments);
    void onTrickplayInfoLoaded(const QString &itemId, const QMap<int, TrickplayTileInfo> &trickplayInfo);
    
    // TrickplayProcessor handlers
    void onTrickplayProcessingComplete(const QString &itemId, int count, int intervalMs,
                                       int width, int height, const QString &filePath);
    void onTrickplayProcessingFailed(const QString &itemId, const QString &error);

private:
#ifdef BLOOM_TESTING
    friend class PlayerControllerAutoplayContextTest;
#endif
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
    void reportPlaybackStart();
    void reportPlaybackProgress();
    void reportPlaybackStop();
    void checkCompletionThreshold();
    bool checkCompletionThresholdAndAutoplay();  // Returns true if threshold was met (for autoplay)
    void handlePlaybackStopAndAutoplay(Event stopEvent);
    void maybeTriggerNextEpisodePrefetch();
    /**
     * Returns whether a prefetched "next episode" payload is available and usable for navigation.
     * @returns `true` if a prefetched next-episode item is ready and applicable to consume, `false` otherwise.
     */
    bool hasUsablePrefetchedNextEpisode() const;
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
    /**
     * Update the trickplay preview image/URL for the specified playback position.
     * @param seconds Position in seconds for which to update the trickplay preview.
     */
    void updateTrickplayPreviewForPosition(double seconds);
    /**
     * Clear any active trickplay preview override and associated preview URL/state.
     */
    void clearTrickplayPreview();
    /**
     * Re-evaluate and update flags that indicate whether the player is currently inside intro/outro or other skip segments.
     */
    void updateSkipSegmentState();
    /**
     * Seek to the end boundary of the specified media segment type (e.g., intro or outro).
     * @param segmentType Type of media segment whose end should be sought.
     * @returns `true` if a seek was performed to the segment end, `false` if no applicable segment was found.
     */
    bool seekToSegmentEnd(MediaSegmentType segmentType);
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
    /**
     * Attempt to fall back from the internal player backend to an external backend for the current attempt.
     * @param reason Human-readable reason for attempting the fallback.
     * @returns `true` if a fallback was initiated, `false` otherwise.
     */
    bool tryFallbackToExternalBackend(const QString &reason);
    /**
     * Update internal mappings from Jellyfin stream indices to mpv track identifiers.
     * @param audioTrackMap Mapping list for audio tracks (Jellyfin -> mpv).
     * @param subtitleTrackMap Mapping list for subtitle tracks (Jellyfin -> mpv).
     */
    void updateTrackMappings(const QVariantList &audioTrackMap, const QVariantList &subtitleTrackMap);
    /**
     * Map a Jellyfin audio stream index to the corresponding mpv audio track number.
     * @param jellyfinStreamIndex Jellyfin audio stream index.
     * @returns 1-based mpv audio track number, or -1 when auto/none is intended.
     */
    int mpvAudioTrackForJellyfinIndex(int jellyfinStreamIndex) const;
    /**
     * Map a Jellyfin subtitle stream index to the corresponding mpv subtitle track number.
     * @param jellyfinStreamIndex Jellyfin subtitle stream index.
     * @returns 1-based mpv subtitle track number, or -1 when subtitles are disabled.
     */
    int mpvSubtitleTrackForJellyfinIndex(int jellyfinStreamIndex) const;
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

    IPlayerBackend *m_playerBackend;
    std::unique_ptr<IPlayerBackend> m_ownedBackend;
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
    static constexpr int kBufferingTimeoutMs = 60000; // 60 seconds
    
    // Progress reporting timer
    QTimer *m_progressReportTimer;
    QTimer *m_volumePersistTimer;
    static constexpr int kProgressReportIntervalMs = 10000; // 10 seconds
    static constexpr int kVolumePersistDebounceMs = 250;
    
    // State
    PlaybackState m_playbackState = Idle;
    QString m_errorMessage;
    int m_bufferingProgress = 0;
    
    // Playback Tracking
    QString m_currentItemId;
    QString m_currentSeriesId;  // For autoplay next episode
    QString m_currentSeasonId;  // For per-season track preferences
    QString m_currentLibraryId; // For profile resolution
    QString m_pendingUrl;
    double m_currentPosition = 0;
    double m_duration = 0;
    bool m_hasReportedStart = false;
    double m_seekTargetWhileBuffering = -1;
    qint64 m_startPositionTicks = 0;  // Resume position in Jellyfin ticks
    bool m_shouldAutoplay = false;  // Flag to trigger autoplay on next episode loaded
    bool m_awaitingNextEpisodeResolution = false;
    bool m_waitingForNextEpisodeAtPlaybackEnd = false;
    bool m_nextEpisodePrefetchRequestedForAttempt = false;
    bool m_nextEpisodePrefetchReady = false;
    QJsonObject m_prefetchedNextEpisodeData;
    QString m_prefetchedNextEpisodeSeriesId;
    QString m_prefetchedForItemId;

    // Persisted autoplay context across state teardown/idle transition
    QString m_pendingAutoplayItemId;
    QString m_pendingAutoplaySeriesId;
    QString m_pendingAutoplaySeasonId;
    QString m_pendingAutoplayLibraryId;
    int m_pendingAutoplayAudioTrack = -1;
    int m_pendingAutoplaySubtitleTrack = -1;
    double m_pendingAutoplayFramerate = 0.0;
    bool m_pendingAutoplayIsHDR = false;
    
    // Track selection state
    int m_selectedAudioTrack = -1;      // Jellyfin audio stream index (for API reporting)
    int m_selectedSubtitleTrack = -1;   // Jellyfin subtitle stream index (-1 = none)
    int m_mpvAudioTrack = -1;           // mpv audio track number (1-based, -1 = auto)
    int m_mpvSubtitleTrack = -1;        // mpv subtitle track number (1-based, -1 = disabled)
    QHash<int, int> m_audioTrackMap;    // Jellyfin stream index -> mpv aid track ID (1-based)
    QHash<int, int> m_subtitleTrackMap; // Jellyfin stream index -> mpv sid track ID (1-based)
    QString m_mediaSourceId;            // Current media source ID
    QString m_playSessionId;            // Playback session ID for reporting
    QVariantList m_availableAudioTracks;
    QVariantList m_availableSubtitleTracks;
    
    // Track preference persistence for season continuity
    // Maps season ID -> (audio track index, subtitle track index)
    // We use language code matching for best results
    QHash<QString, QPair<int, int>> m_seasonTrackPreferences;
    
    // Flag to prevent saving track preferences during initial track application
    // When true, ignore track changes from mpv (they're from auto-selection, not user choice)
    bool m_applyingInitialTracks = false;
    
    // Content framerate for display refresh rate matching
    double m_contentFramerate = 0.0;
    
    // Content HDR flag for display HDR switching
    bool m_contentIsHDR = false;
    quint64 m_playbackAttemptId = 0;
    bool m_hasReportedStopForAttempt = false;
    bool m_hasEvaluatedCompletionForAttempt = false;
    static constexpr double kNextEpisodePrefetchTriggerPercent = 70.0;
    
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
    bool m_attemptedLinuxEmbeddedFallback = false;
    QString m_overlayTitle;
    QString m_overlaySubtitle;
    QString m_overlayBackdropUrl;
    
    // OSC and trickplay data
    QList<MediaSegmentInfo> m_currentSegments;
    bool m_isInIntroSegment = false;
    bool m_isInOutroSegment = false;
    bool m_hasAutoSkippedIntroForCurrentItem = false;
    bool m_hasAutoSkippedOutroForCurrentItem = false;
    TrickplayTileInfo m_currentTrickplayInfo;
    bool m_hasTrickplayInfo = false;
    QString m_trickplayBinaryPath;
    int m_currentTrickplayFrameIndex = -1;
    QString m_trickplayPreviewUrl;
    bool m_hasTrickplayPreviewPositionOverride = false;
    double m_trickplayPreviewPositionOverrideSeconds = 0.0;
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
