#include "PlayerController.h"
#include "TrickplayProcessor.h"
#include "../network/PlaybackService.h"
#include "../network/LibraryService.h"
#include "../network/AuthenticationService.h"
#include "../network/Types.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QCoreApplication>
#include <QDir>
#include <QDebug>
#include <QThread>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcPlayback, "bloom.playback")

PlayerController::PlayerController(PlayerProcessManager *processManager, ConfigManager *config, TrackPreferencesManager *trackPrefs, DisplayManager *displayManager, PlaybackService *playbackService, LibraryService *libraryService, AuthenticationService *authService, QObject *parent)
    : QObject(parent)
    , m_processManager(processManager)
    , m_config(config)
    , m_trackPrefs(trackPrefs)
    , m_displayManager(displayManager)
    , m_playbackService(playbackService)
    , m_libraryService(libraryService)
    , m_authService(authService)
    , m_trickplayProcessor(new TrickplayProcessor(authService, playbackService, this))
    , m_loadingTimeoutTimer(new QTimer(this))
    , m_bufferingTimeoutTimer(new QTimer(this))
    , m_progressReportTimer(new QTimer(this))
{
    // Setup state machine transitions
    setupStateMachine();
    
    // Connect to PlayerProcessManager signals
    connect(m_processManager, &PlayerProcessManager::stateChanged,
            this, &PlayerController::onProcessStateChanged);
    connect(m_processManager, &PlayerProcessManager::errorOccurred,
            this, &PlayerController::onProcessError);
    connect(m_processManager, &PlayerProcessManager::positionChanged,
            this, &PlayerController::onPositionChanged);
    connect(m_processManager, &PlayerProcessManager::durationChanged,
            this, &PlayerController::onDurationChanged);
    connect(m_processManager, &PlayerProcessManager::pauseChanged,
            this, &PlayerController::onPauseChanged);
    connect(m_processManager, &PlayerProcessManager::playbackEnded,
            this, &PlayerController::onPlaybackEnded);
    connect(m_processManager, &PlayerProcessManager::pausedForCacheChanged,
            this, &PlayerController::onPausedForCacheChanged);
    
    // Connect track change signals from mpv
    // NOTE: We ignore these signals during initial track application (m_applyingInitialTracks)
    // because mpv auto-selects tracks before our set_property commands arrive, and the
    // mpv track indices don't map directly to Jellyfin stream indices anyway.
    // User track selections are preserved from playUrlWithTracks() and only updated
    // when the user explicitly changes tracks via the UI during playback.
    connect(m_processManager, &PlayerProcessManager::audioTrackChanged,
            this, [this](int index) {
                Q_UNUSED(index);
                // Don't update m_selectedAudioTrack from mpv signals - it stores Jellyfin
                // stream indices which don't correspond to mpv's per-type track numbers.
                // Track changes during playback should go through setSelectedAudioTrack()
                // which properly handles the index mapping.
            });
    connect(m_processManager, &PlayerProcessManager::subtitleTrackChanged,
            this, [this](int index) {
                Q_UNUSED(index);
                // Don't update m_selectedSubtitleTrack from mpv signals - it stores Jellyfin
                // stream indices which don't correspond to mpv's per-type track numbers.
                // Track changes during playback should go through setSelectedSubtitleTrack()
                // which properly handles the index mapping.
            });
    
    // Connect script message handler for bidirectional IPC with mpv scripts
    connect(m_processManager, &PlayerProcessManager::scriptMessage,
            this, &PlayerController::onScriptMessage);
    
    // Connect to LibraryService for autoplay next episode
    connect(m_libraryService, &LibraryService::nextUnplayedEpisodeLoaded,
            this, &PlayerController::onNextEpisodeLoaded);
    
    // Connect to PlaybackService for media segments and trickplay info signals
    connect(m_playbackService, &PlaybackService::mediaSegmentsLoaded,
            this, &PlayerController::onMediaSegmentsLoaded);
    connect(m_playbackService, &PlaybackService::trickplayInfoLoaded,
            this, &PlayerController::onTrickplayInfoLoaded);
    connect(m_playbackService, &PlaybackService::itemMarkedPlayed,
            this, &PlayerController::onItemMarkedPlayed);
    
    // Connect TrickplayProcessor signals
    connect(m_trickplayProcessor, &TrickplayProcessor::processingComplete,
            this, &PlayerController::onTrickplayProcessingComplete);
    connect(m_trickplayProcessor, &TrickplayProcessor::processingFailed,
            this, &PlayerController::onTrickplayProcessingFailed);
    
    // Setup timeout timers
    m_loadingTimeoutTimer->setSingleShot(true);
    m_bufferingTimeoutTimer->setSingleShot(true);
    connect(m_loadingTimeoutTimer, &QTimer::timeout,
            this, &PlayerController::onLoadingTimeout);
    connect(m_bufferingTimeoutTimer, &QTimer::timeout,
            this, &PlayerController::onBufferingTimeout);
    
    // Setup progress report timer
    m_progressReportTimer->setInterval(kProgressReportIntervalMs);
    connect(m_progressReportTimer, &QTimer::timeout,
            this, &PlayerController::reportPlaybackProgress);
            
    // Connect to ConfigManager audio delay signal
    connect(m_config, &ConfigManager::audioDelayChanged,
            this, [this]() {
                emit audioDelayChanged();
                // If playing, apply immediately
                if (m_processManager->isRunning()) {
                    double delaySeconds = static_cast<double>(m_config->getAudioDelay()) / 1000.0;
                    m_processManager->sendVariantCommand({"set_property", "audio-delay", delaySeconds});
                }
            });
    
    loadConfig();
}

PlayerController::~PlayerController()
{
    // Cleanup timers
    m_loadingTimeoutTimer->stop();
    m_bufferingTimeoutTimer->stop();
    m_progressReportTimer->stop();
}

void PlayerController::setupStateMachine()
{
    // Define state transitions: (CurrentState, Event) → NextState
    
    // From Idle
    m_transitions[{Idle, Event::Play}] = Loading;
    
    // From Loading
    m_transitions[{Loading, Event::LoadComplete}] = Buffering;
    m_transitions[{Loading, Event::ErrorOccurred}] = Error;
    m_transitions[{Loading, Event::Stop}] = Idle;
    
    // From Buffering
    m_transitions[{Buffering, Event::BufferComplete}] = Playing;
    m_transitions[{Buffering, Event::ErrorOccurred}] = Error;
    m_transitions[{Buffering, Event::Stop}] = Idle;
    m_transitions[{Buffering, Event::Pause}] = Paused;
    
    // From Playing
    m_transitions[{Playing, Event::Pause}] = Paused;
    m_transitions[{Playing, Event::BufferStart}] = Buffering;
    m_transitions[{Playing, Event::ErrorOccurred}] = Error;
    m_transitions[{Playing, Event::Stop}] = Idle;
    m_transitions[{Playing, Event::PlaybackEnd}] = Idle;
    
    // From Paused
    m_transitions[{Paused, Event::Resume}] = Playing;
    m_transitions[{Paused, Event::Play}] = Loading;  // New content
    m_transitions[{Paused, Event::ErrorOccurred}] = Error;
    m_transitions[{Paused, Event::Stop}] = Idle;
    
    // From Error
    m_transitions[{Error, Event::Recover}] = Idle;
    m_transitions[{Error, Event::Play}] = Loading;  // Retry
    m_transitions[{Error, Event::Stop}] = Idle;
    
    qDebug() << "PlayerController: State machine initialized with" << m_transitions.size() << "transitions";
}

bool PlayerController::processEvent(Event event)
{
    StateTransition transition{m_playbackState, event};
    
    if (!m_transitions.contains(transition)) {
        qWarning() << "PlayerController: Invalid transition from" 
                   << stateToString(m_playbackState) << "on event" << eventToString(event);
        return false;
    }
    
    PlaybackState newState = m_transitions[transition];
    qDebug() << "PlayerController: Transition" 
             << stateToString(m_playbackState) << "->" << stateToString(newState)
             << "on event" << eventToString(event);
    
    // Exit current state
    exitState(m_playbackState);
    
    // Enter new state
    enterState(newState);
    
    return true;
}

void PlayerController::exitState(PlaybackState oldState)
{
    switch (oldState) {
    case Idle:
        onExitIdleState();
        break;
    case Loading:
        onExitLoadingState();
        break;
    case Buffering:
        onExitBufferingState();
        break;
    case Playing:
        onExitPlayingState();
        break;
    case Paused:
        onExitPausedState();
        break;
    case Error:
        onExitErrorState();
        break;
    }
}

void PlayerController::enterState(PlaybackState newState)
{
    setPlaybackState(newState);
    
    switch (newState) {
    case Idle:
        onEnterIdleState();
        break;
    case Loading:
        onEnterLoadingState();
        break;
    case Buffering:
        onEnterBufferingState();
        break;
    case Playing:
        onEnterPlayingState();
        break;
    case Paused:
        onEnterPausedState();
        break;
    case Error:
        onEnterErrorState();
        break;
    }
}

// === STATE EXIT HANDLERS ===

void PlayerController::onExitIdleState()
{
    // Nothing to cleanup when leaving Idle
}

void PlayerController::onExitLoadingState()
{
    m_loadingTimeoutTimer->stop();
}

void PlayerController::onExitBufferingState()
{
    m_bufferingTimeoutTimer->stop();
}

void PlayerController::onExitPlayingState()
{
    m_progressReportTimer->stop();
}

void PlayerController::onExitPausedState()
{
    // Nothing to cleanup when leaving Paused
}

void PlayerController::onExitErrorState()
{
    setErrorMessage(QString());
}

// === STATE ENTRY HANDLERS ===

void PlayerController::onEnterIdleState()
{
    qCInfo(lcPlayback) << "Entering Idle state (playback ended)";
    
    // Stop all timers
    m_loadingTimeoutTimer->stop();
    m_bufferingTimeoutTimer->stop();
    m_progressReportTimer->stop();
    
    // Emit playbackStopped so UI can refresh watch progress, next up, etc.
    emit playbackStopped();
    
    // Restore display settings
    if (m_displayManager) {
        m_displayManager->restoreRefreshRate();
        // If we enabled HDR for this content, disable it now
        if (m_config->getEnableHDR() && m_contentIsHDR) {
            qDebug() << "PlayerController: Restoring HDR to off after HDR content playback";
            m_displayManager->setHDR(false);
        }
    }
    
    // Clear playback state
    m_currentItemId.clear();
    m_currentSeriesId.clear();
    m_currentSeasonId.clear();
    m_currentLibraryId.clear();
    m_pendingUrl.clear();
    m_currentPosition = 0;
    m_duration = 0;
    m_hasReportedStart = false;
    m_seekTargetWhileBuffering = -1;
    m_startPositionTicks = 0;
    m_contentFramerate = 0.0;
    m_contentIsHDR = false;
    setBufferingProgress(0);
    
    // Clear track selection state (but keep m_seriesTrackPreferences)
    m_selectedAudioTrack = -1;
    m_selectedSubtitleTrack = -1;
    m_mpvAudioTrack = -1;
    m_mpvSubtitleTrack = -1;
    m_mediaSourceId.clear();
    m_playSessionId.clear();
    m_applyingInitialTracks = false;
    emit selectedAudioTrackChanged();
    emit selectedSubtitleTrackChanged();
    emit mediaSourceIdChanged();
    emit playSessionIdChanged();
    
    // Clear OSC/trickplay state
    m_currentSegments.clear();
    m_hasTrickplayInfo = false;
    
    // Clear trickplay processor data and notify Lua script
    m_trickplayProcessor->clear();
    if (m_processManager->isRunning()) {
        m_processManager->sendCommand({"script-message-to", "thumbfast", "shim-trickplay-clear"});
    }
}

void PlayerController::onEnterLoadingState()
{
    qDebug() << "PlayerController: Entering Loading state";
    
    // Start loading timeout
    m_loadingTimeoutTimer->start(kLoadingTimeoutMs);
    
    // Reset tracking
    m_hasReportedStart = false;
    m_isWaitingForPosition = true;
    setBufferingProgress(0);
    
    // Set flag to prevent mpv's auto-selected tracks from overwriting preferences
    // Will be cleared when we enter Playing state and tracks are settled
    m_applyingInitialTracks = true;
    
    // Start mpv with pending URL
    if (!m_pendingUrl.isEmpty()) {
        startPlayback(m_pendingUrl);
    }
}

void PlayerController::onEnterBufferingState()
{
    qDebug() << "PlayerController: Entering Buffering state";
    
    // Start buffering timeout
    m_bufferingTimeoutTimer->start(kBufferingTimeoutMs);
    
    // Initialize buffering detection
    m_lastPositionUpdateTime.start();
    m_lastPosition = m_currentPosition;
    
    // Apply pending track selections now that the file is loaded
    // Use mpv track numbers (1-based, per-type) for mpv commands
    qDebug() << "PlayerController: onEnterBufferingState - m_mpvSubtitleTrack:" << m_mpvSubtitleTrack;
    if (m_mpvAudioTrack > 0) {
        qDebug() << "PlayerController: Applying pending audio track selection, mpv aid:" << m_mpvAudioTrack;
        m_processManager->sendVariantCommand({"set_property", "aid", m_mpvAudioTrack});
    }
    if (m_mpvSubtitleTrack > 0) {
        qDebug() << "PlayerController: Applying pending subtitle track selection, mpv sid:" << m_mpvSubtitleTrack;
        m_processManager->sendVariantCommand({"set_property", "sid", m_mpvSubtitleTrack});
    } else if (m_mpvSubtitleTrack == -1) {
        // Explicitly disable subtitles if user selected "None"
        qDebug() << "PlayerController: Disabling subtitles (user selection), m_mpvSubtitleTrack:" << m_mpvSubtitleTrack;
        m_processManager->sendVariantCommand({"set_property", "sid", "no"});
    } else {
        qDebug() << "PlayerController: No subtitle action taken, m_mpvSubtitleTrack:" << m_mpvSubtitleTrack;
    }
    
    // If there was a pending seek for resume playback, execute it directly
    // We send directly to mpv here instead of calling seek() because seek()
    // would re-queue the seek since we're in Buffering state
    if (m_seekTargetWhileBuffering >= 0) {
        double target = m_seekTargetWhileBuffering;
        m_seekTargetWhileBuffering = -1;
        qDebug() << "PlayerController: Executing queued seek to" << target << "seconds";
        m_processManager->sendVariantCommand({"seek", target, "absolute"});
    }
    
    // Apply audio delay
    double delaySeconds = static_cast<double>(m_config->getAudioDelay()) / 1000.0;
    if (delaySeconds != 0.0) {
        qDebug() << "PlayerController: Applying audio delay:" << delaySeconds << "s";
        m_processManager->sendVariantCommand({"set_property", "audio-delay", delaySeconds});
    }
}

void PlayerController::onEnterPlayingState()
{
    qCInfo(lcPlayback) << "Entering Playing state for item:" << m_currentItemId;
    
    // Clear the initial tracks flag - from now on, track changes are user-initiated
    // and should be saved to preferences
    m_applyingInitialTracks = false;
    
    // Report playback start if not already done
    if (!m_hasReportedStart && !m_currentItemId.isEmpty()) {
        reportPlaybackStart();
        m_hasReportedStart = true;
    }
    
    // Start progress reporting
    m_progressReportTimer->start();
    
    setBufferingProgress(100);
}

void PlayerController::onEnterPausedState()
{
    qCInfo(lcPlayback) << "Entering Paused state, position:" << m_currentPosition << "s";
    
    // Report pause to server
    if (!m_currentItemId.isEmpty()) {
        m_playbackService->reportPlaybackPaused(m_currentItemId, static_cast<qint64>(m_currentPosition * 10000000),
                                        m_mediaSourceId, m_selectedAudioTrack, m_selectedSubtitleTrack,
                                        m_playSessionId);
    }
}

void PlayerController::onEnterErrorState()
{
    qDebug() << "PlayerController: Entering Error state -" << m_errorMessage;
    
    // Stop all timers
    m_loadingTimeoutTimer->stop();
    m_bufferingTimeoutTimer->stop();
    m_progressReportTimer->stop();
    
    // Stop mpv if running
    if (m_processManager->isRunning()) {
        m_processManager->stopMpv();
    }
}

// === TIMEOUT HANDLERS ===

void PlayerController::onLoadingTimeout()
{
    qDebug() << "PlayerController: Loading timeout";
    setErrorMessage(tr("Loading timed out. Please check your connection and try again."));
    processEvent(Event::ErrorOccurred);
}

void PlayerController::onBufferingTimeout()
{
    qDebug() << "PlayerController: Buffering timeout";
    setErrorMessage(tr("Buffering timed out. Network may be too slow."));
    processEvent(Event::ErrorOccurred);
}

// === PROCESS MANAGER SIGNAL HANDLERS ===

void PlayerController::onProcessStateChanged(bool running)
{
    qDebug() << "PlayerController: Process state changed, running:" << running;
    
    if (!running && m_playbackState != Idle && m_playbackState != Error) {
        // Process stopped unexpectedly (e.g., mpv quit via 'q' or crash)
        // Treat this like an explicit stop so we report progress and consider autoplay.
        reportPlaybackStop();
        bool thresholdMet = checkCompletionThresholdAndAutoplay();

        // If threshold is met for an episode, set flag to request next episode after marking as played
        if (thresholdMet && !m_currentSeriesId.isEmpty()) {
            m_shouldAutoplay = true;
            qDebug() << "PlayerController: Process stopped, threshold met, will request next episode after marking current as played";
        }

        processEvent(Event::Stop);
    }
}

void PlayerController::onProcessError(const QString &error)
{
    qDebug() << "PlayerController: Process error:" << error;
    setErrorMessage(error);
    processEvent(Event::ErrorOccurred);
}

void PlayerController::onPositionChanged(double seconds)
{
    double previousPosition = m_currentPosition;
    m_currentPosition = seconds;
    
    // First position update - transition from Loading to Buffering
    if (m_isWaitingForPosition && m_playbackState == Loading) {
        m_isWaitingForPosition = false;
        processEvent(Event::LoadComplete);
        return;
    }
    
    // Reset buffering timeout when we receive position updates - mpv is still responsive
    // This prevents false timeouts during legitimate buffering (e.g., seeking, initial buffer)
    if (m_playbackState == Buffering && m_bufferingTimeoutTimer->isActive()) {
        m_bufferingTimeoutTimer->start(kBufferingTimeoutMs);
    }
    
    // Update buffering progress during Buffering state
    if (m_playbackState == Buffering) {
        // If position is advancing significantly, buffering is complete
        if (seconds > previousPosition + 0.5) {
            processEvent(Event::BufferComplete);
        } else {
            // Update progress based on time waiting (crude estimate)
            int elapsed = m_bufferingTimeoutTimer->interval() - m_bufferingTimeoutTimer->remainingTime();
            int progress = qMin(99, elapsed / 100);
            setBufferingProgress(progress);
        }
    }
    
    m_lastPosition = seconds;
    m_lastPositionUpdateTime.restart();
}

void PlayerController::onDurationChanged(double seconds)
{
    m_duration = seconds;
}

void PlayerController::onPausedForCacheChanged(bool pausedForCache)
{
    qDebug() << "PlayerController: Paused for cache:" << pausedForCache;
    
    if (pausedForCache && m_playbackState == Playing) {
        // mpv reports actual buffering - transition to Buffering state
        qDebug() << "PlayerController: mpv started buffering";
        processEvent(Event::BufferStart);
    } else if (!pausedForCache && m_playbackState == Buffering) {
        // mpv finished buffering - transition back to Playing
        qDebug() << "PlayerController: mpv finished buffering";
        processEvent(Event::BufferComplete);
    }
}

void PlayerController::onPauseChanged(bool paused)
{
    qDebug() << "PlayerController: Pause changed:" << paused;
    
    if (m_currentItemId.isEmpty()) return;
    
    if (paused && m_playbackState == Playing) {
        processEvent(Event::Pause);
    } else if (!paused && m_playbackState == Paused) {
        // Report resume to server
        m_playbackService->reportPlaybackResumed(m_currentItemId, static_cast<qint64>(m_currentPosition * 10000000),
                                         m_mediaSourceId, m_selectedAudioTrack, m_selectedSubtitleTrack,
                                         m_playSessionId);
        processEvent(Event::Resume);
    }
}

void PlayerController::onPlaybackEnded()
{
    qDebug() << "PlayerController: Playback ended";
    
    reportPlaybackStop();
    
    // Check if we should navigate to next episode (or autoplay)
    bool thresholdMet = checkCompletionThresholdAndAutoplay();
    
    // If threshold met for an episode, set flag to request next episode after marking as played
    if (thresholdMet && !m_currentSeriesId.isEmpty()) {
        m_shouldAutoplay = true;  // Flag to handle the response after item is marked as played
        qDebug() << "PlayerController: Threshold met, will request next episode after marking current as played";
    }
    
    processEvent(Event::PlaybackEnd);
}

void PlayerController::onNextEpisodeLoaded(const QString &seriesId, const QJsonObject &episodeData)
{
    // Only handle this if we're expecting an autoplay/navigation
    if (!m_shouldAutoplay) {
        return;
    }
    
    m_shouldAutoplay = false;
    
    if (episodeData.isEmpty()) {
        qDebug() << "PlayerController: No next episode available";
        return;
    }
    
    // Extract episode info
    QString episodeId = episodeData["Id"].toString();
    QString episodeName = episodeData["Name"].toString();
    QString seriesName = episodeData["SeriesName"].toString();
    int seasonNumber = episodeData["ParentIndexNumber"].toInt();
    int episodeNumber = episodeData["IndexNumber"].toInt();
    
    qDebug() << "PlayerController: Next episode found:" << seriesName 
             << "S" << seasonNumber << "E" << episodeNumber << "-" << episodeName;
    
    // Check if autoplay is enabled
    if (m_config->getAutoplayNextEpisode()) {
        // Get resume position if any
        qint64 startPositionTicks = 0;
        if (episodeData.contains("UserData") && episodeData["UserData"].isObject()) {
            QJsonObject userData = episodeData["UserData"].toObject();
            startPositionTicks = static_cast<qint64>(userData["PlaybackPositionTicks"].toDouble());
        }
        
        qDebug() << "PlayerController: Autoplaying next episode";
        
        // Emit signal for UI notification
        emit autoplayingNextEpisode(episodeName, seriesName);
        
        // Build stream URL and start playback
        // Note: For autoplay, we reuse the existing display settings (framerate/HDR)
        // since episodes in the same series typically have the same framerate
        // We also carry forward the current season ID for track preferences
        QString streamUrl = m_libraryService->getStreamUrl(episodeId);
        playUrl(streamUrl, episodeId, startPositionTicks, seriesId, m_currentSeasonId, m_currentLibraryId, m_contentFramerate);
    } else {
        // Autoplay disabled - emit navigation signal to show episode details
        // Include the track preferences from the just-completed episode
        int lastAudioIndex = m_selectedAudioTrack;
        int lastSubtitleIndex = m_selectedSubtitleTrack;
        
        qDebug() << "PlayerController: Emitting navigateToNextEpisode signal with audio:" 
                 << lastAudioIndex << "subtitle:" << lastSubtitleIndex;
        
        emit navigateToNextEpisode(episodeData, seriesId, lastAudioIndex, lastSubtitleIndex);
    }
}

// Handle item marked as played - request next episode if autoplay was requested
void PlayerController::onItemMarkedPlayed(const QString &itemId)
{
    // Only proceed if this is the item we just finished playing and we want autoplay/navigation
    if (itemId != m_currentItemId || !m_shouldAutoplay || m_currentSeriesId.isEmpty()) {
        return;
    }
    
    m_shouldAutoplay = false;  // Clear the flag
    
    qDebug() << "PlayerController: Item marked as played, requesting next episode for seriesId:" << m_currentSeriesId;
    m_libraryService->getNextUnplayedEpisode(m_currentSeriesId);
}

// === PUBLIC API ===

PlayerController::PlaybackState PlayerController::playbackState() const
{
    return m_playbackState;
}

bool PlayerController::isPlaybackActive() const
{
    switch (m_playbackState) {
    case Loading:
    case Buffering:
    case Playing:
    case Paused:
        return true;
    case Idle:
    case Error:
    default:
        return false;
    }
}

QString PlayerController::stateName() const
{
    return stateToString(m_playbackState);
}

bool PlayerController::isBuffering() const
{
    return m_playbackState == Buffering;
}

bool PlayerController::isLoading() const
{
    return m_playbackState == Loading;
}

bool PlayerController::hasError() const
{
    return m_playbackState == Error;
}

QString PlayerController::errorMessage() const
{
    return m_errorMessage;
}

int PlayerController::bufferingProgress() const
{
    return m_bufferingProgress;
}

void PlayerController::playTestVideo()
{
    m_currentItemId.clear();
    m_pendingUrl = m_testVideoUrl;
    
    if (m_processManager->isRunning()) {
        reportPlaybackStop();
        m_processManager->stopMpv();
    }
    
    processEvent(Event::Play);
}

void PlayerController::playUrl(const QString &url, const QString &itemId, qint64 startPositionTicks, const QString &seriesId, const QString &seasonId, const QString &libraryId, double framerate, bool isHDR)
{
    qDebug() << "PlayerController: playUrl called with itemId:" << itemId 
             << "startPositionTicks:" << startPositionTicks
             << "seriesId:" << seriesId
             << "seasonId:" << seasonId
             << "libraryId:" << libraryId
             << "framerate:" << framerate
             << "isHDR:" << isHDR;
    
    // If already playing, stop first
    if (m_processManager->isRunning()) {
        reportPlaybackStop();
        // Don't check completion threshold here - we're starting new content intentionally
        m_processManager->stopMpv();
    }
    
    // Store pending playback info before transition
    m_currentItemId = itemId;
    m_currentSeriesId = seriesId;
    m_currentSeasonId = seasonId;
    m_currentLibraryId = libraryId;
    m_pendingUrl = url;
    m_currentPosition = 0;
    m_duration = 0;
    m_hasReportedStart = false;
    m_startPositionTicks = startPositionTicks;
    m_shouldAutoplay = false;
    m_contentFramerate = framerate;
    m_contentIsHDR = isHDR;
    
    // Clear previous OSC/trickplay state and request new data
    m_currentSegments.clear();
    m_hasTrickplayInfo = false;
    if (!itemId.isEmpty()) {
        m_playbackService->getMediaSegments(itemId);
        m_playbackService->getTrickplayInfo(itemId);
    }
    
    // If we have a start position, queue it as a seek target
    // Jellyfin ticks are 100ns units, so divide by 10,000,000 to get seconds
    if (startPositionTicks > 0) {
        m_seekTargetWhileBuffering = static_cast<double>(startPositionTicks) / 10000000.0;
        qDebug() << "PlayerController: Will seek to" << m_seekTargetWhileBuffering << "seconds after buffering";
    } else {
        m_seekTargetWhileBuffering = -1;
    }
    
    processEvent(Event::Play);
}

void PlayerController::stop()
{
    qDebug() << "PlayerController: stop requested";
    
    reportPlaybackStop();
    checkCompletionThreshold();
    
    m_processManager->stopMpv();
    processEvent(Event::Stop);
}

void PlayerController::pause()
{
    if (m_playbackState == Playing || m_playbackState == Buffering) {
        m_processManager->sendCommand({"set", "pause", "yes"});
    }
}

void PlayerController::resume()
{
    if (m_playbackState == Paused) {
        m_processManager->sendCommand({"set", "pause", "no"});
    }
}

void PlayerController::togglePause()
{
    m_processManager->sendCommand({"cycle", "pause"});
}

void PlayerController::seek(double seconds)
{
    qDebug() << "PlayerController: seek to" << seconds;
    
    // If buffering, queue the seek for when buffering completes
    if (m_playbackState == Buffering) {
        m_seekTargetWhileBuffering = seconds;
        qDebug() << "PlayerController: Queued seek for after buffering";
        return;
    }
    
    if (m_playbackState == Playing || m_playbackState == Paused) {
        m_processManager->sendVariantCommand({"seek", seconds, "absolute"});
    }
}

void PlayerController::seekRelative(double seconds)
{
    qDebug() << "PlayerController: seekRelative" << seconds;
    
    // During buffering, convert relative to absolute and queue
    if (m_playbackState == Buffering) {
        m_seekTargetWhileBuffering = m_currentPosition + seconds;
        qDebug() << "PlayerController: Queued relative seek for after buffering";
        return;
    }
    
    if (m_playbackState == Playing || m_playbackState == Paused) {
        m_processManager->sendVariantCommand({"seek", seconds, "relative"});
    }
}

void PlayerController::retry()
{
    qDebug() << "PlayerController: retry requested";
    
    if (m_playbackState == Error && !m_pendingUrl.isEmpty()) {
        processEvent(Event::Play);
    }
}

void PlayerController::clearError()
{
    qDebug() << "PlayerController: clearError requested";
    
    if (m_playbackState == Error) {
        processEvent(Event::Recover);
    }
}

// === TRACK SELECTION ===

void PlayerController::setSelectedAudioTrack(int index)
{
    if (m_selectedAudioTrack != index) {
        m_selectedAudioTrack = index;
        qDebug() << "PlayerController: Setting audio track to" << index;
        
        // Send command to mpv to switch audio track
        // mpv uses 1-based indexing for aid, but -1 means auto
        if (m_playbackState == Playing || m_playbackState == Paused) {
            if (index >= 0) {
                // Convert from Jellyfin stream index to mpv track number
                // mpv's audio tracks are 1-indexed starting from first audio stream
                m_processManager->sendVariantCommand({"set_property", "aid", index + 1});
            } else {
                m_processManager->sendVariantCommand({"set_property", "aid", "auto"});
            }
        }
        
        // Save preference for season continuity (both in-memory and persistent)
        if (!m_currentSeasonId.isEmpty()) {
            m_seasonTrackPreferences[m_currentSeasonId].first = index;
            m_trackPrefs->setAudioTrack(m_currentSeasonId, index);
        }
        
        emit selectedAudioTrackChanged();
    }
}

void PlayerController::setAudioDelay(int ms)
{
    m_config->setAudioDelay(ms);
}

void PlayerController::setSelectedSubtitleTrack(int index)
{
    if (m_selectedSubtitleTrack != index) {
        m_selectedSubtitleTrack = index;
        qDebug() << "PlayerController: Setting subtitle track to" << index;
        
        // Send command to mpv to switch subtitle track
        // mpv uses "no" or false to disable subtitles
        if (m_playbackState == Playing || m_playbackState == Paused) {
            if (index >= 0) {
                m_processManager->sendVariantCommand({"set_property", "sid", index + 1});
            } else {
                m_processManager->sendVariantCommand({"set_property", "sid", "no"});
            }
        }
        
        // Save preference for season continuity (both in-memory and persistent)
        if (!m_currentSeasonId.isEmpty()) {
            m_seasonTrackPreferences[m_currentSeasonId].second = index;
            m_trackPrefs->setSubtitleTrack(m_currentSeasonId, index);
        }
        
        emit selectedSubtitleTrackChanged();
    }
}

void PlayerController::cycleAudioTrack()
{
    qDebug() << "PlayerController: Cycling audio track";
    if (m_playbackState == Playing || m_playbackState == Paused) {
        m_processManager->sendCommand({"cycle", "audio"});
    }
}

void PlayerController::cycleSubtitleTrack()
{
    qDebug() << "PlayerController: Cycling subtitle track";
    if (m_playbackState == Playing || m_playbackState == Paused) {
        m_processManager->sendCommand({"cycle", "sub"});
    }
}

int PlayerController::getLastAudioTrackForSeason(const QString &seasonId) const
{
    // First check in-memory cache (for current session continuity)
    if (m_seasonTrackPreferences.contains(seasonId)) {
        return m_seasonTrackPreferences[seasonId].first;
    }
    // Fall back to persistent storage
    return m_trackPrefs->getAudioTrack(seasonId);
}

int PlayerController::getLastSubtitleTrackForSeason(const QString &seasonId) const
{
    // First check in-memory cache (for current session continuity)
    if (m_seasonTrackPreferences.contains(seasonId)) {
        return m_seasonTrackPreferences[seasonId].second;
    }
    // Fall back to persistent storage
    return m_trackPrefs->getSubtitleTrack(seasonId);
}

void PlayerController::saveAudioTrackPreference(const QString &seasonId, int index)
{
    if (seasonId.isEmpty()) return;
    // Update in-memory cache
    m_seasonTrackPreferences[seasonId].first = index;
    // Persist to disk
    m_trackPrefs->setAudioTrack(seasonId, index);
    qDebug() << "PlayerController: Saved audio track preference for season" << seasonId << ":" << index;
}

void PlayerController::saveSubtitleTrackPreference(const QString &seasonId, int index)
{
    if (seasonId.isEmpty()) return;
    // Update in-memory cache
    m_seasonTrackPreferences[seasonId].second = index;
    // Persist to disk
    m_trackPrefs->setSubtitleTrack(seasonId, index);
    qDebug() << "PlayerController: Saved subtitle track preference for season" << seasonId << ":" << index;
}

// ---- Movie track preferences ----

int PlayerController::getLastAudioTrackForMovie(const QString &movieId) const
{
    return m_trackPrefs->getMovieAudioTrack(movieId);
}

int PlayerController::getLastSubtitleTrackForMovie(const QString &movieId) const
{
    return m_trackPrefs->getMovieSubtitleTrack(movieId);
}

void PlayerController::saveMovieAudioTrackPreference(const QString &movieId, int index)
{
    if (movieId.isEmpty()) return;
    m_trackPrefs->setMovieAudioTrack(movieId, index);
    qDebug() << "PlayerController: Saved audio track preference for movie" << movieId << ":" << index;
}

void PlayerController::saveMovieSubtitleTrackPreference(const QString &movieId, int index)
{
    if (movieId.isEmpty()) return;
    m_trackPrefs->setMovieSubtitleTrack(movieId, index);
    qDebug() << "PlayerController: Saved subtitle track preference for movie" << movieId << ":" << index;
}

void PlayerController::playUrlWithTracks(const QString &url, const QString &itemId, qint64 startPositionTicks,
                                          const QString &seriesId, const QString &seasonId, const QString &libraryId,
                                          const QString &mediaSourceId, const QString &playSessionId,
                                          int audioStreamIndex, int subtitleStreamIndex,
                                          int mpvAudioTrack, int mpvSubtitleTrack,
                                          double framerate, bool isHDR)
{
    qDebug() << "PlayerController: playUrlWithTracks called with itemId:" << itemId 
             << "audioIndex:" << audioStreamIndex << "subtitleIndex:" << subtitleStreamIndex
             << "mpvAudio:" << mpvAudioTrack << "mpvSub:" << mpvSubtitleTrack
             << "framerate:" << framerate
             << "isHDR:" << isHDR;
    
    // Store track selection before calling playUrl
    // Jellyfin indices for API reporting
    m_mediaSourceId = mediaSourceId;
    m_playSessionId = playSessionId;
    m_selectedAudioTrack = audioStreamIndex;
    m_selectedSubtitleTrack = subtitleStreamIndex;
    
    // mpv track numbers for mpv commands
    m_mpvAudioTrack = mpvAudioTrack;
    m_mpvSubtitleTrack = mpvSubtitleTrack;
    
    qDebug() << "PlayerController: Stored m_mpvSubtitleTrack:" << m_mpvSubtitleTrack;
    
    emit mediaSourceIdChanged();
    emit playSessionIdChanged();
    emit selectedAudioTrackChanged();
    emit selectedSubtitleTrackChanged();
    
    // Call base playUrl which handles the rest
    playUrl(url, itemId, startPositionTicks, seriesId, seasonId, libraryId, framerate, isHDR);
}

// === PRIVATE HELPERS ===

void PlayerController::setPlaybackState(PlaybackState state)
{
    if (m_playbackState != state) {
        const bool wasActive = isPlaybackActive();
        m_playbackState = state;
        emit playbackStateChanged();
        emit stateChanged(stateName());
        emit isBufferingChanged();
        emit isLoadingChanged();
        emit hasErrorChanged();
        if (wasActive != isPlaybackActive()) {
            emit isPlaybackActiveChanged();
        }
    }
}

void PlayerController::setErrorMessage(const QString &message)
{
    if (m_errorMessage != message) {
        m_errorMessage = message;
        if (!message.isEmpty()) {
            qCWarning(lcPlayback) << "Playback error:" << message 
                                  << "(itemId=" << (m_currentItemId.isEmpty() ? "none" : m_currentItemId) << ")";
        }
        emit errorMessageChanged();
    }
}

void PlayerController::setBufferingProgress(int progress)
{
    progress = qBound(0, progress, 100);
    if (m_bufferingProgress != progress) {
        m_bufferingProgress = progress;
        emit bufferingProgressChanged();
    }
}

void PlayerController::reportPlaybackStart()
{
    if (!m_currentItemId.isEmpty() && m_playbackService) {
        qCInfo(lcPlayback) << "Playback started: itemId=" << m_currentItemId 
                           << "duration=" << m_duration << "s"
                           << "audio=" << m_selectedAudioTrack 
                           << "subtitle=" << m_selectedSubtitleTrack;
        m_playbackService->reportPlaybackStart(m_currentItemId, m_mediaSourceId,
                                       m_selectedAudioTrack, m_selectedSubtitleTrack,
                                       m_playSessionId);
    }
}

void PlayerController::reportPlaybackProgress()
{
    if (!m_currentItemId.isEmpty() && m_playbackService && m_playbackState == Playing) {
        qint64 ticks = static_cast<qint64>(m_currentPosition * 10000000); // 100ns ticks
        m_playbackService->reportPlaybackProgress(m_currentItemId, ticks, m_mediaSourceId,
                                          m_selectedAudioTrack, m_selectedSubtitleTrack,
                                          m_playSessionId);
    }
}

void PlayerController::reportPlaybackStop()
{
    if (!m_currentItemId.isEmpty() && m_playbackService) {
        double percentage = m_duration > 0 ? (m_currentPosition / m_duration) * 100.0 : 0;
        qCInfo(lcPlayback) << "Playback stopped: itemId=" << m_currentItemId 
                           << "position=" << m_currentPosition << "s /" << m_duration << "s"
                           << "(" << percentage << "%)";
        qint64 ticks = static_cast<qint64>(m_currentPosition * 10000000);
        m_playbackService->reportPlaybackStopped(m_currentItemId, ticks, m_mediaSourceId,
                                         m_selectedAudioTrack, m_selectedSubtitleTrack,
                                         m_playSessionId);
    }
}

void PlayerController::checkCompletionThreshold()
{
    checkCompletionThresholdAndAutoplay();
}

bool PlayerController::checkCompletionThresholdAndAutoplay()
{
    if (m_currentItemId.isEmpty() || m_duration <= 0) return false;
    
    double percentage = (m_currentPosition / m_duration) * 100.0;
    int threshold = m_config->getPlaybackCompletionThreshold();
    
    if (percentage >= threshold) {
        qDebug() << "PlayerController: Marking item" << m_currentItemId 
                 << "as played (" << percentage << "% >= " << threshold << "% threshold)";
        m_playbackService->markItemPlayed(m_currentItemId);
        return true;  // Threshold met - eligible for autoplay
    }
    return false;  // Threshold not met
}

void PlayerController::startPlayback(const QString &url)
{
    qDebug() << "PlayerController: Starting playback of" << url;
    
    // Handle Display Settings - HDR FIRST (must be done before refresh rate change)
    // Toggling HDR can reset the display mode, so we set HDR first, then refresh rate
    bool hdrEnabled = false;
    if (m_config->getEnableHDR() && m_contentIsHDR) {
        qDebug() << "PlayerController: Enabling HDR for HDR content";
        hdrEnabled = m_displayManager->setHDR(true);
        if (hdrEnabled) {
            // Small delay to allow display to stabilize after HDR mode change
            // This is especially important on Windows where HDR toggle can reset refresh rate
            QThread::msleep(100);
        }
    } else if (m_config->getEnableHDR() && !m_contentIsHDR) {
        qDebug() << "PlayerController: HDR toggle enabled but content is SDR, not switching display HDR";
    }
    
    // Handle Display Settings - Framerate Matching (AFTER HDR is set)
    if (m_config->getEnableFramerateMatching() && m_contentFramerate > 0) {
        // Pass the exact framerate to DisplayManager for precise matching
        // TVs like LG can match exact 23.976Hz, while others will use closest available (24Hz)
        qDebug() << "PlayerController: Content framerate:" << m_contentFramerate 
                 << "-> attempting exact refresh rate match";
        
        if (m_displayManager->setRefreshRate(m_contentFramerate)) {
            qDebug() << "PlayerController: Successfully set display refresh rate for framerate" << m_contentFramerate;
        } else {
            qWarning() << "PlayerController: Failed to set display refresh rate for framerate" << m_contentFramerate;
        }
    } else if (m_config->getEnableFramerateMatching()) {
        qDebug() << "PlayerController: Framerate matching enabled but no framerate info available (framerate:" << m_contentFramerate << ")";
    }
    
    // Resolve the MPV profile for this item
    QString profileName = m_config->resolveProfileForItem(m_currentLibraryId, m_currentSeriesId);
    qDebug() << "PlayerController: Using MPV profile:" << profileName 
             << "for library:" << m_currentLibraryId 
             << "series:" << m_currentSeriesId;
    
    // Get the args from the profile (includes HDR overrides if enabled)
    QStringList profileArgs = m_config->getMpvArgsForProfile(profileName);
    
    // Build final args: Bloom config args + profile args
    QStringList finalArgs;
    finalArgs << ConfigManager::getMpvConfigArgs();  // mpv.conf, input.conf, scripts
    finalArgs << profileArgs;                        // Profile-specific args
    
    qDebug() << "PlayerController: Final mpv args:" << finalArgs;
    
    m_processManager->startMpv(m_mpvBin, finalArgs, url);
}

QString PlayerController::stateToString(PlaybackState state)
{
    switch (state) {
    case Idle: return QStringLiteral("idle");
    case Loading: return QStringLiteral("loading");
    case Buffering: return QStringLiteral("buffering");
    case Playing: return QStringLiteral("playing");
    case Paused: return QStringLiteral("paused");
    case Error: return QStringLiteral("error");
    }
    return QStringLiteral("unknown");
}

QString PlayerController::eventToString(Event event)
{
    switch (event) {
    case Event::Play: return QStringLiteral("Play");
    case Event::LoadComplete: return QStringLiteral("LoadComplete");
    case Event::BufferComplete: return QStringLiteral("BufferComplete");
    case Event::BufferStart: return QStringLiteral("BufferStart");
    case Event::Pause: return QStringLiteral("Pause");
    case Event::Resume: return QStringLiteral("Resume");
    case Event::Stop: return QStringLiteral("Stop");
    case Event::PlaybackEnd: return QStringLiteral("PlaybackEnd");
    case Event::ErrorOccurred: return QStringLiteral("ErrorOccurred");
    case Event::Recover: return QStringLiteral("Recover");
    }
    return QStringLiteral("Unknown");
}

void PlayerController::loadConfig()
{
    // Look for config in the standard config directory
    QString configPath = ConfigManager::getConfigPath();
    
    // Fallback: Check app directory or current directory for development
    if (!QFile::exists(configPath)) {
        configPath = QCoreApplication::applicationDirPath() + "/config/app.json";
        if (!QFile::exists(configPath)) {
            configPath = "config/app.json"; // Fallback to relative
        }
    }
    
    // Fallback to example config if main config doesn't exist
    if (!QFile::exists(configPath)) {
        configPath = QCoreApplication::applicationDirPath() + "/config/app.example.json";
        if (!QFile::exists(configPath)) {
            configPath = "config/app.example.json";
        }
    }
    
    QFile file(configPath);
    if (file.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        QJsonObject obj = doc.object();
        
        // Check for new versioned config structure
        if (obj.contains("settings") && obj["settings"].isObject()) {
            QJsonObject settings = obj["settings"].toObject();
            
            // Parse mpv settings from new structure - only need path and test video
            // Args now come from profiles via ConfigManager
            if (settings.contains("mpv") && settings["mpv"].isObject()) {
                QJsonObject mpvConfig = settings["mpv"].toObject();
                m_mpvBin = mpvConfig["path"].toString("mpv");
                m_testVideoUrl = mpvConfig["test_video_url"].toString();
            } else {
                // Settings exist but no mpv section - use defaults
                m_mpvBin = "mpv";
            }
        } else {
            // Legacy config format (pre-versioning)
            m_mpvBin = obj["mpv_binary"].toString("mpv");
            m_testVideoUrl = obj["test_video_url"].toString();
        }
        
        qDebug() << "PlayerController: Loaded config from" << configPath;
        qDebug() << "PlayerController: mpv binary:" << m_mpvBin;
    } else {
        qWarning() << "PlayerController: Could not load config from" << configPath;
        // Defaults
        m_mpvBin = "mpv";
        m_testVideoUrl = "http://commondatastorage.googleapis.com/gtv-videos-bucket/sample/BigBuckBunny.mp4";
    }
    
    // Log mpv config directory being used
    QString mpvConfigDir = ConfigManager::getMpvConfigDir();
    qDebug() << "PlayerController: Bloom mpv config directory:" << mpvConfigDir;
    if (!ConfigManager::getMpvConfPath().isEmpty()) {
        qDebug() << "PlayerController: Using mpv.conf from:" << ConfigManager::getMpvConfPath();
    }
    if (!ConfigManager::getMpvInputConfPath().isEmpty()) {
        qDebug() << "PlayerController: Using input.conf from:" << ConfigManager::getMpvInputConfPath();
    }
    if (!ConfigManager::getMpvScriptsDir().isEmpty()) {
        qDebug() << "PlayerController: Using scripts from:" << ConfigManager::getMpvScriptsDir();
    }
}

// === OSC AND TRICKPLAY HANDLERS ===

void PlayerController::onScriptMessage(const QString &messageName, const QStringList &args)
{
    qDebug() << "PlayerController: Received script message:" << messageName << "args:" << args;
    
    if (messageName == "bloom-skip-intro") {
        // Find intro segment and seek to its end
        for (const auto &segment : m_currentSegments) {
            if (segment.type == MediaSegmentType::Intro && segment.endTicks > 0) {
                double endSeconds = static_cast<double>(segment.endTicks) / 10000000.0;
                qDebug() << "PlayerController: Skipping intro, seeking to" << endSeconds;
                seek(endSeconds);
                return;
            }
        }
        qDebug() << "PlayerController: No intro segment found to skip";
        
    } else if (messageName == "bloom-skip-outro") {
        // Find outro segment and seek to its end (or next episode)
        for (const auto &segment : m_currentSegments) {
            if (segment.type == MediaSegmentType::Outro && segment.endTicks > 0) {
                double endSeconds = static_cast<double>(segment.endTicks) / 10000000.0;
                qDebug() << "PlayerController: Skipping outro, seeking to" << endSeconds;
                seek(endSeconds);
                return;
            }
        }
        qDebug() << "PlayerController: No outro segment found to skip";
    }
    // Note: Trickplay thumbnail requests are now handled entirely by thumbfast.lua
    // using the pre-processed binary file from TrickplayProcessor
}

void PlayerController::onMediaSegmentsLoaded(const QString &itemId, const QList<MediaSegmentInfo> &segments)
{
    if (itemId != m_currentItemId) {
        qDebug() << "PlayerController: Ignoring segments for different item:" << itemId;
        return;
    }
    
    qDebug() << "PlayerController: Received" << segments.size() << "segments for item:" << itemId;
    m_currentSegments = segments;
    
    // Send segment info to mpv ModernX OSC script
    for (const auto &segment : segments) {
        double startSeconds = static_cast<double>(segment.startTicks) / 10000000.0;
        double endSeconds = static_cast<double>(segment.endTicks) / 10000000.0;
        
        if (segment.type == MediaSegmentType::Intro) {
            m_processManager->sendCommand({"script-message-to", "modernx", 
                "bloom-segment-intro", 
                QString::number(startSeconds, 'f', 3), 
                QString::number(endSeconds, 'f', 3)});
            qDebug() << "PlayerController: Sent intro segment to OSC:" << startSeconds << "->" << endSeconds;
        } else if (segment.type == MediaSegmentType::Outro) {
            m_processManager->sendCommand({"script-message-to", "modernx", 
                "bloom-segment-outro", 
                QString::number(startSeconds, 'f', 3), 
                QString::number(endSeconds, 'f', 3)});
            qDebug() << "PlayerController: Sent outro segment to OSC:" << startSeconds << "->" << endSeconds;
        }
    }
}

void PlayerController::onTrickplayInfoLoaded(const QString &itemId, const QMap<int, TrickplayTileInfo> &trickplayInfo)
{
    if (itemId != m_currentItemId) {
        qDebug() << "PlayerController: Ignoring trickplay info for different item:" << itemId;
        return;
    }
    
    if (trickplayInfo.isEmpty()) {
        qDebug() << "PlayerController: No trickplay info available for item:" << itemId;
        return;
    }
    
    // Select the best resolution - prefer 320px width for good balance of quality and size
    // Keys are widths: typically 320, 480, etc.
    int preferredWidth = 320;
    int selectedWidth = trickplayInfo.firstKey();  // Default to first available
    
    for (auto it = trickplayInfo.begin(); it != trickplayInfo.end(); ++it) {
        if (it.key() <= preferredWidth && it.key() > selectedWidth) {
            selectedWidth = it.key();
        } else if (selectedWidth > preferredWidth && it.key() < selectedWidth) {
            selectedWidth = it.key();
        }
    }
    
    // Use the preferred width if available, otherwise closest to it
    if (trickplayInfo.contains(preferredWidth)) {
        selectedWidth = preferredWidth;
    }
    
    const TrickplayTileInfo &info = trickplayInfo[selectedWidth];
    
    qDebug() << "PlayerController: Received trickplay info for item:" << itemId
             << "selected width:" << selectedWidth
             << "height:" << info.height
             << "interval:" << info.interval << "ms"
             << "tiles:" << info.tileWidth << "x" << info.tileHeight
             << "count:" << info.thumbnailCount;
    
    m_currentTrickplayInfo = info;
    m_hasTrickplayInfo = true;
    
    // Start trickplay processing - download tiles and create binary file
    // This uses the jellyfin-mpv-shim approach for proper mpv overlay support
    m_trickplayProcessor->startProcessing(itemId, info);
}

void PlayerController::onTrickplayProcessingComplete(const QString &itemId, int count, int intervalMs,
                                                      int width, int height, const QString &filePath)
{
    if (itemId != m_currentItemId) {
        qDebug() << "PlayerController: Ignoring trickplay processing result for different item:" << itemId;
        return;
    }
    
    qDebug() << "PlayerController: Trickplay processing complete for item:" << itemId
             << "count:" << count
             << "interval:" << intervalMs << "ms"
             << "size:" << width << "x" << height
             << "file:" << filePath;
    
    // Send trickplay BIF config to mpv thumbfast script using jellyfin-mpv-shim format
    // Format: shim-trickplay-bif <count> <interval_ms> <width> <height> <file_path>
    m_processManager->sendCommand({"script-message-to", "thumbfast", 
        "shim-trickplay-bif",
        QString::number(count),
        QString::number(intervalMs),
        QString::number(width),
        QString::number(height),
        filePath});
}

void PlayerController::onTrickplayProcessingFailed(const QString &itemId, const QString &error)
{
    if (itemId != m_currentItemId) {
        return;
    }
    
    qWarning() << "PlayerController: Trickplay processing failed for item:" << itemId << "error:" << error;
    // Trickplay thumbnails won't be available, but playback continues normally
    m_hasTrickplayInfo = false;
}
