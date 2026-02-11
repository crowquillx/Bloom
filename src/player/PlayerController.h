#pragma once

#include <QObject>
#include <QTimer>
#include <QElapsedTimer>
#include <QHash>
#include <QMap>
#include "PlayerProcessManager.h"
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
    Q_PROPERTY(QString stateName READ stateName NOTIFY stateChanged)
    Q_PROPERTY(bool isBuffering READ isBuffering NOTIFY isBufferingChanged)
    Q_PROPERTY(bool isLoading READ isLoading NOTIFY isLoadingChanged)
    Q_PROPERTY(bool hasError READ hasError NOTIFY hasErrorChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
    Q_PROPERTY(int bufferingProgress READ bufferingProgress NOTIFY bufferingProgressChanged)
    Q_PROPERTY(int audioDelay READ audioDelay WRITE setAudioDelay NOTIFY audioDelayChanged)
    
    // Track selection properties
    Q_PROPERTY(int selectedAudioTrack READ selectedAudioTrack WRITE setSelectedAudioTrack NOTIFY selectedAudioTrackChanged)
    Q_PROPERTY(int selectedSubtitleTrack READ selectedSubtitleTrack WRITE setSelectedSubtitleTrack NOTIFY selectedSubtitleTrackChanged)
    Q_PROPERTY(QString mediaSourceId READ mediaSourceId NOTIFY mediaSourceIdChanged)
    Q_PROPERTY(QString playSessionId READ playSessionId NOTIFY playSessionIdChanged)

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

    explicit PlayerController(PlayerProcessManager *processManager, ConfigManager *config, TrackPreferencesManager *trackPrefs, DisplayManager *displayManager, PlaybackService *playbackService, LibraryService *libraryService, AuthenticationService *authService, QObject *parent = nullptr);
    ~PlayerController();

    PlaybackState playbackState() const;
    bool isPlaybackActive() const;
    QString stateName() const;
    bool isBuffering() const;
    bool isLoading() const;
    bool hasError() const;
    QString errorMessage() const;
    int bufferingProgress() const;
    
    // Track selection getters
    int selectedAudioTrack() const { return m_selectedAudioTrack; }
    int selectedSubtitleTrack() const { return m_selectedSubtitleTrack; }
    QString mediaSourceId() const { return m_mediaSourceId; }
    QString playSessionId() const { return m_playSessionId; }
    
    // Audio Delay (ms)
    int audioDelay() const { return m_config->getAudioDelay(); }
    Q_INVOKABLE void setAudioDelay(int ms);

    Q_INVOKABLE void playTestVideo();
    Q_INVOKABLE void playUrl(const QString &url, const QString &itemId = "", qint64 startPositionTicks = 0, const QString &seriesId = "", const QString &seasonId = "", const QString &libraryId = "", double framerate = 0.0, bool isHDR = false);
    
    // Extended playUrl with track selection
    // audioStreamIndex/subtitleStreamIndex: Jellyfin unified stream indices (for API reporting)
    // mpvAudioTrack/mpvSubtitleTrack: mpv 1-based per-type track numbers (for mpv commands)
    Q_INVOKABLE void playUrlWithTracks(const QString &url, const QString &itemId, qint64 startPositionTicks,
                                       const QString &seriesId, const QString &seasonId, const QString &libraryId,
                                       const QString &mediaSourceId, const QString &playSessionId,
                                       int audioStreamIndex, int subtitleStreamIndex,
                                       int mpvAudioTrack, int mpvSubtitleTrack,
                                       double framerate = 0.0, bool isHDR = false);
    
    Q_INVOKABLE void stop();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void resume();
    Q_INVOKABLE void togglePause();
    Q_INVOKABLE void seek(double seconds);
    Q_INVOKABLE void seekRelative(double seconds);
    Q_INVOKABLE void retry();
    Q_INVOKABLE void clearError();
    
    // Track selection methods - change tracks during playback via mpv
    Q_INVOKABLE void setSelectedAudioTrack(int index);
    Q_INVOKABLE void setSelectedSubtitleTrack(int index);
    Q_INVOKABLE void cycleAudioTrack();
    Q_INVOKABLE void cycleSubtitleTrack();
    
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
    
    // Track selection signals
    void selectedAudioTrackChanged();
    void selectedSubtitleTrackChanged();
    void mediaSourceIdChanged();
    void playSessionIdChanged();
    
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
                                int lastAudioIndex, int lastSubtitleIndex);

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
    void loadConfig();
    void startPlayback(const QString &url);
    void initiateMpvStart();
    static QString stateToString(PlaybackState state);
    static QString eventToString(Event event);

    PlayerProcessManager *m_processManager;
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
    static constexpr int kProgressReportIntervalMs = 10000; // 10 seconds
    
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
    
    // Track selection state
    int m_selectedAudioTrack = -1;      // Jellyfin audio stream index (for API reporting)
    int m_selectedSubtitleTrack = -1;   // Jellyfin subtitle stream index (-1 = none)
    int m_mpvAudioTrack = -1;           // mpv audio track number (1-based, -1 = auto)
    int m_mpvSubtitleTrack = -1;        // mpv subtitle track number (1-based, -1 = disabled)
    QString m_mediaSourceId;            // Current media source ID
    QString m_playSessionId;            // Playback session ID for reporting
    
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
    
    // Buffering detection
    QElapsedTimer m_lastPositionUpdateTime;
    double m_lastPosition = 0;
    bool m_isWaitingForPosition = false;
    
    // Config cache
    QString m_mpvBin;
    QString m_testVideoUrl;
    
    // OSC and trickplay data
    QList<MediaSegmentInfo> m_currentSegments;
    TrickplayTileInfo m_currentTrickplayInfo;
    bool m_hasTrickplayInfo = false;
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
