#include "PlayerController.h"
#include "TrickplayProcessor.h"
#if !defined(BLOOM_TESTING)
#include "backend/ExternalMpvBackend.h"
#endif
#include "../network/PlaybackService.h"
#include "../network/LibraryService.h"
#include "../network/AuthenticationService.h"
#include "../network/Types.h"
#include <QFile>
#include <QBuffer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QImage>
#include <QCoreApplication>
#include <QDir>
#include <QDebug>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QRectF>
#include <QtGlobal>
#include <QUrl>
#include <QUrlQuery>
#include <QSet>
#include <QThread>
#include <atomic>

Q_LOGGING_CATEGORY(lcPlayback, "bloom.playback")
Q_LOGGING_CATEGORY(lcPlaybackTrace, "bloom.playback.trace")

namespace {
std::atomic<quint64> gPlaybackAttemptCounter{0};

bool isLinuxEmbeddedLibmpvBackend(const IPlayerBackend *backend)
{
    return backend && backend->backendName() == QStringLiteral("linux-libmpv-opengl");
}

bool mpvStatsHotkeysAllowed(const IPlayerBackend *backend)
{
    if (!isLinuxEmbeddedLibmpvBackend(backend)) {
        return true;
    }
    return qEnvironmentVariableIntValue("BLOOM_LINUX_LIBMPV_ENABLE_STATS_HOTKEYS") == 1;
}

bool embeddedLinuxTrickplayAllowed(const IPlayerBackend *backend)
{
    if (!isLinuxEmbeddedLibmpvBackend(backend)) {
        return true;
    }
    return qEnvironmentVariableIntValue("BLOOM_LINUX_LIBMPV_ENABLE_TRICKPLAY") == 1;
}

class NullPlayerBackend final : public IPlayerBackend
{
public:
    explicit NullPlayerBackend(QObject *parent)
        : IPlayerBackend(parent)
    {
    }

    QString backendName() const override { return QStringLiteral("null-backend"); }
    void startMpv(const QString &, const QStringList &, const QString &) override {}
    void stopMpv() override {}
    bool isRunning() const override { return false; }
    void sendCommand(const QStringList &) override {}
    void sendVariantCommand(const QVariantList &) override {}
    bool supportsEmbeddedVideo() const override { return false; }
    bool attachVideoTarget(QObject *) override { return false; }
    void detachVideoTarget(QObject *) override {}
    void setVideoViewport(const QRectF &) override {}
};
}

PlayerController::PlayerController(IPlayerBackend *playerBackend, ConfigManager *config, TrackPreferencesManager *trackPrefs, DisplayManager *displayManager, PlaybackService *playbackService, LibraryService *libraryService, AuthenticationService *authService, QObject *parent)
    : QObject(parent)
    , m_playerBackend(playerBackend)
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
    , m_volumePersistTimer(new QTimer(this))
    , m_startDelayTimer(new QTimer(this))
{
    if (!m_playerBackend) {
        qCWarning(lcPlayback) << "PlayerController initialized without backend; falling back to null backend";
        m_playerBackend = new NullPlayerBackend(this);
    }

    // Setup state machine transitions
    setupStateMachine();

    // Connect to player backend signals
    connectBackendSignals(m_playerBackend);
    
    // Connect to LibraryService for autoplay next episode
    connect(m_libraryService, &LibraryService::nextUnplayedEpisodeLoaded,
            this, &PlayerController::onNextEpisodeLoaded);
    
    // Connect to PlaybackService for media segments and trickplay info signals
    connect(m_playbackService, &PlaybackService::mediaSegmentsLoaded,
            this, &PlayerController::onMediaSegmentsLoaded);
    connect(m_playbackService, &PlaybackService::trickplayInfoLoaded,
            this, &PlayerController::onTrickplayInfoLoaded);
    
    // Connect TrickplayProcessor signals
    connect(m_trickplayProcessor, &TrickplayProcessor::processingComplete,
            this, &PlayerController::onTrickplayProcessingComplete);
    connect(m_trickplayProcessor, &TrickplayProcessor::processingFailed,
            this, &PlayerController::onTrickplayProcessingFailed);
    
    // Setup timeout timers
    m_loadingTimeoutTimer->setSingleShot(true);
    m_bufferingTimeoutTimer->setSingleShot(true);
    m_startDelayTimer->setSingleShot(true);
    connect(m_loadingTimeoutTimer, &QTimer::timeout,
            this, &PlayerController::onLoadingTimeout);
    connect(m_bufferingTimeoutTimer, &QTimer::timeout,
            this, &PlayerController::onBufferingTimeout);
    connect(m_startDelayTimer, &QTimer::timeout,
            this, &PlayerController::initiateMpvStart);
    
    // Setup progress report timer
    m_progressReportTimer->setInterval(kProgressReportIntervalMs);
    connect(m_progressReportTimer, &QTimer::timeout,
            this, &PlayerController::reportPlaybackProgress);

    m_volumePersistTimer->setSingleShot(true);
    m_volumePersistTimer->setInterval(kVolumePersistDebounceMs);
    connect(m_volumePersistTimer, &QTimer::timeout,
            this, &PlayerController::persistPlaybackVolumeState);
            
    // Connect to ConfigManager audio delay signal
    connect(m_config, &ConfigManager::audioDelayChanged,
            this, [this]() {
                emit audioDelayChanged();
                // If playing, apply immediately
                if (m_playerBackend->isRunning()) {
                    double delaySeconds = static_cast<double>(m_config->getAudioDelay()) / 1000.0;
                    m_playerBackend->sendVariantCommand({"set_property", "audio-delay", delaySeconds});
                }
            });
    
    loadConfig();

    if (m_config) {
        m_volume = m_config->getPlaybackVolume();
        m_muted = m_config->getPlaybackMuted();
    }
}

PlayerController::~PlayerController()
{
    // Cleanup timers
    m_loadingTimeoutTimer->stop();
    m_bufferingTimeoutTimer->stop();
    m_progressReportTimer->stop();
    m_volumePersistTimer->stop();
    m_startDelayTimer->stop();
}

void PlayerController::connectBackendSignals(IPlayerBackend *backend)
{
    if (!backend) {
        return;
    }

    connect(backend, &IPlayerBackend::stateChanged,
            this, &PlayerController::onProcessStateChanged);
    connect(backend, &IPlayerBackend::errorOccurred,
            this, &PlayerController::onProcessError);
    connect(backend, &IPlayerBackend::positionChanged,
            this, &PlayerController::onPositionChanged);
    connect(backend, &IPlayerBackend::durationChanged,
            this, &PlayerController::onDurationChanged);
    connect(backend, &IPlayerBackend::pauseChanged,
            this, &PlayerController::onPauseChanged);
    connect(backend, &IPlayerBackend::playbackEnded,
            this, &PlayerController::onPlaybackEnded);
    connect(backend, &IPlayerBackend::pausedForCacheChanged,
            this, &PlayerController::onPausedForCacheChanged);
    connect(backend, &IPlayerBackend::volumeChanged,
            this, [this](int volume) {
                if (m_playbackState == Loading || m_playbackState == Buffering) {
                    return;
                }
                const int clamped = qBound(0, volume, 200);
                if (m_volume == clamped) {
                    return;
                }
                m_volume = clamped;
                emit volumeChanged();
                schedulePersistPlaybackVolumeState();
            });
    connect(backend, &IPlayerBackend::muteChanged,
            this, [this](bool muted) {
                if (m_playbackState == Loading || m_playbackState == Buffering) {
                    return;
                }
                if (m_muted == muted) {
                    return;
                }
                m_muted = muted;
                emit mutedChanged();
                schedulePersistPlaybackVolumeState();
            });

    // NOTE: We intentionally ignore mpv auto-selected track signals during startup.
    connect(backend, &IPlayerBackend::audioTrackChanged,
            this, [](int index) { Q_UNUSED(index); });
    connect(backend, &IPlayerBackend::subtitleTrackChanged,
            this, [](int index) { Q_UNUSED(index); });

    connect(backend, &IPlayerBackend::scriptMessage,
            this, &PlayerController::onScriptMessage);
}

bool PlayerController::tryFallbackToExternalBackend(const QString &reason)
{
    if (!m_playerBackend
        || m_playerBackend->backendName() != QStringLiteral("linux-libmpv-opengl")
        || m_attemptedLinuxEmbeddedFallback) {
        return false;
    }

    m_attemptedLinuxEmbeddedFallback = true;
    qCWarning(lcPlayback) << "Embedded Linux backend failed; switching to external-mpv-ipc. Reason:" << reason;

    QObject::disconnect(m_playerBackend, nullptr, this, nullptr);
    if (m_playerBackend->isRunning()) {
        m_playerBackend->stopMpv();
    }

    #if defined(BLOOM_TESTING)
    m_ownedBackend = std::make_unique<NullPlayerBackend>(this);
    #else
    m_ownedBackend = std::make_unique<ExternalMpvBackend>(this);
    #endif

    m_playerBackend = m_ownedBackend.get();
    connectBackendSignals(m_playerBackend);
    emit supportsEmbeddedVideoChanged();

    if (m_playbackState == Loading && !m_pendingUrl.isEmpty()) {
        qCInfo(lcPlayback) << "Retrying current media with external-mpv-ipc fallback backend";
        initiateMpvStart();
    }

    return true;
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
    qCInfo(lcPlaybackTrace) << "[attempt" << m_playbackAttemptId << "] event"
                            << eventToString(event)
                            << "state=" << stateToString(m_playbackState);
    
    if (!m_transitions.contains(transition)) {
        qWarning() << "PlayerController: Invalid transition from" 
                   << stateToString(m_playbackState) << "on event" << eventToString(event);
        qCWarning(lcPlaybackTrace) << "[attempt" << m_playbackAttemptId
                                   << "] invalid-transition"
                                   << "state=" << stateToString(m_playbackState)
                                   << "event=" << eventToString(event);
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
    qCInfo(lcPlaybackTrace) << "[attempt" << m_playbackAttemptId
                            << "] enter-idle"
                            << "itemId=" << m_currentItemId
                            << "contentIsHDR=" << m_contentIsHDR
                            << "contentFramerate=" << m_contentFramerate;
    
    // Stop all timers
    m_loadingTimeoutTimer->stop();
    m_bufferingTimeoutTimer->stop();
    m_progressReportTimer->stop();
    m_startDelayTimer->stop();
    
    // Emit playbackStopped so UI can refresh watch progress, next up, etc.
    emit playbackStopped();
    
    // Restore display settings
    if (m_displayManager) {
        // If we enabled HDR for this content, disable it first.
        // Some setups cannot apply higher refresh rates while HDR is active.
        bool hdrDisabledForRestore = false;
        if (m_config->getEnableHDR() && m_contentIsHDR) {
            qDebug() << "PlayerController: Restoring HDR to off after HDR content playback";
            qCInfo(lcPlaybackTrace) << "[attempt" << m_playbackAttemptId
                                    << "] restore-display: setHDR(false) begin";
            hdrDisabledForRestore = m_displayManager->setHDR(false);
            qCInfo(lcPlaybackTrace) << "[attempt" << m_playbackAttemptId
                                    << "] restore-display: setHDR(false) result=" << hdrDisabledForRestore;
        }

        if (hdrDisabledForRestore) {
            static constexpr unsigned long kHdrOffSettleDelayMs = 300;
            qDebug() << "PlayerController: Waiting" << kHdrOffSettleDelayMs
                     << "ms after HDR-off before refresh restore";
            QThread::msleep(kHdrOffSettleDelayMs);
        }
        qCInfo(lcPlaybackTrace) << "[attempt" << m_playbackAttemptId
                                << "] restore-display: restoreRefreshRate begin";
        m_displayManager->restoreRefreshRate();
        qCInfo(lcPlaybackTrace) << "[attempt" << m_playbackAttemptId
                                << "] restore-display: restoreRefreshRate done";
    }
    
    // Clear playback state
    if (!m_currentItemId.isEmpty()) {
        m_currentItemId.clear();
        emit currentItemIdChanged();
    }
    m_currentSeriesId.clear();
    m_currentSeasonId.clear();
    m_currentLibraryId.clear();
    m_pendingUrl.clear();
    m_currentPosition = 0;
    m_duration = 0;
    m_hasReportedStart = false;
    m_seekTargetWhileBuffering = -1;
    m_reportProgressOnNextPositionUpdate = false;
    m_startPositionTicks = 0;
    m_contentFramerate = 0.0;
    m_contentIsHDR = false;
    m_playMethod = QStringLiteral("DirectPlay");
    clearOverlayMetadata();
    setBufferingProgress(0);
    
    // Clear track selection state (but keep m_seriesTrackPreferences)
    m_selectedAudioTrack = -1;
    m_selectedSubtitleTrack = -1;
    m_mpvAudioTrack = -1;
    m_mpvSubtitleTrack = -1;
    m_audioTrackMap.clear();
    m_subtitleTrackMap.clear();
    m_mediaSourceId.clear();
    m_playSessionId.clear();
    m_availableAudioTracks.clear();
    m_availableSubtitleTracks.clear();
    m_applyingInitialTracks = false;
    emit selectedAudioTrackChanged();
    emit selectedSubtitleTrackChanged();
    emit mediaSourceIdChanged();
    emit playSessionIdChanged();
    emit availableTracksChanged();
    
    // Clear OSC/trickplay state
    m_currentSegments.clear();
    m_isInIntroSegment = false;
    m_isInOutroSegment = false;
    m_hasAutoSkippedIntroForCurrentItem = false;
    m_hasAutoSkippedOutroForCurrentItem = false;
    m_hasTrickplayInfo = false;
    m_currentTrickplayInfo = TrickplayTileInfo{};
    m_trickplayBinaryPath.clear();
    m_currentTrickplayFrameIndex = -1;
    m_hasTrickplayPreviewPositionOverride = false;
    m_trickplayPreviewPositionOverrideSeconds = 0.0;
    clearTrickplayPreview();
    emit timelineChanged();
    emit skipSegmentsChanged();
    emit trickplayStateChanged();
    
    // Clear trickplay processor data
    m_trickplayProcessor->clear();
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
    QUrl pendingPlaybackUrl(m_pendingUrl);
    QUrlQuery pendingPlaybackQuery(pendingPlaybackUrl);
    const bool urlPinsAudioStream = pendingPlaybackQuery.hasQueryItem(QStringLiteral("AudioStreamIndex"));
    const bool urlPinsSubtitleStream = pendingPlaybackQuery.hasQueryItem(QStringLiteral("SubtitleStreamIndex"));
    const int pinnedAudioStreamIndex = pendingPlaybackQuery.queryItemValue(QStringLiteral("AudioStreamIndex")).toInt();
    const int pinnedSubtitleStreamIndex = pendingPlaybackQuery.queryItemValue(QStringLiteral("SubtitleStreamIndex")).toInt();

    const int desiredMpvAudioTrack = mpvAudioTrackForJellyfinIndex(m_selectedAudioTrack);
    const int desiredMpvSubtitleTrack = mpvSubtitleTrackForJellyfinIndex(m_selectedSubtitleTrack);
    const bool shouldOverridePinnedAudio = urlPinsAudioStream
        && m_selectedAudioTrack >= 0
        && m_selectedAudioTrack != pinnedAudioStreamIndex;
    const bool shouldOverridePinnedSubtitle = urlPinsSubtitleStream
        && ((m_selectedSubtitleTrack == -1)
            || (m_selectedSubtitleTrack >= 0 && m_selectedSubtitleTrack != pinnedSubtitleStreamIndex));

    qCDebug(lcPlayback) << "Track startup selection:"
                        << "selectedAudio=" << m_selectedAudioTrack
                        << "selectedSubtitle=" << m_selectedSubtitleTrack
                        << "desiredMpvAudio=" << desiredMpvAudioTrack
                        << "desiredMpvSubtitle=" << desiredMpvSubtitleTrack
                        << "urlPinsAudio=" << urlPinsAudioStream
                        << "urlPinsSubtitle=" << urlPinsSubtitleStream
                        << "overridePinnedAudio=" << shouldOverridePinnedAudio
                        << "overridePinnedSubtitle=" << shouldOverridePinnedSubtitle;

    // Always apply resolved startup track selection if we have one. URL pins are treated as
    // transport/request hints and fallback, but explicit startup selection must win deterministically.
    if (desiredMpvAudioTrack > 0) {
        qCDebug(lcPlayback) << "Applying startup audio track selection via aid:" << desiredMpvAudioTrack;
        m_playerBackend->sendVariantCommand({"set_property", "aid", desiredMpvAudioTrack});
    } else if (urlPinsAudioStream && !shouldOverridePinnedAudio) {
        qCDebug(lcPlayback) << "Keeping URL-pinned audio stream index:" << pinnedAudioStreamIndex;
    }

    if (m_selectedSubtitleTrack == -1) {
        qCDebug(lcPlayback) << "Applying startup subtitle selection: none";
        m_playerBackend->sendVariantCommand({"set_property", "sid", "no"});
    } else if (desiredMpvSubtitleTrack > 0) {
        qCDebug(lcPlayback) << "Applying startup subtitle track selection via sid:" << desiredMpvSubtitleTrack;
        m_playerBackend->sendVariantCommand({"set_property", "sid", desiredMpvSubtitleTrack});
    } else if (urlPinsSubtitleStream && !shouldOverridePinnedSubtitle) {
        qCDebug(lcPlayback) << "Keeping URL-pinned subtitle stream index:" << pinnedSubtitleStreamIndex;
    }
    
    // If there was a pending seek for resume playback, execute it directly
    // We send directly to mpv here instead of calling seek() because seek()
    // would re-queue the seek since we're in Buffering state
    if (m_seekTargetWhileBuffering >= 0) {
        double target = m_seekTargetWhileBuffering;
        m_seekTargetWhileBuffering = -1;
        qDebug() << "PlayerController: Executing queued seek to" << target << "seconds";
        m_playerBackend->sendVariantCommand({"seek", target, "absolute"});
    }
    
    // Apply audio delay
    double delaySeconds = static_cast<double>(m_config->getAudioDelay()) / 1000.0;
    if (delaySeconds != 0.0) {
        qDebug() << "PlayerController: Applying audio delay:" << delaySeconds << "s";
        m_playerBackend->sendVariantCommand({"set_property", "audio-delay", delaySeconds});
    }

    m_playerBackend->sendVariantCommand({"set_property", "volume", m_volume});
    m_playerBackend->sendVariantCommand({"set_property", "mute", m_muted});
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
                                                m_playSessionId, m_duration > 0.0, m_muted,
                                                m_playMethod);
    }
}

/**
 * @brief Handles entry into the Error playback state.
 *
 * Logs the current error message, stops all playback-related timers, stops the backend MPV process if it is running, and clears any prefetched next-episode state.
 */
void PlayerController::onEnterErrorState()
{
    qDebug() << "PlayerController: Entering Error state -" << m_errorMessage;
    
    // Stop all timers
    m_loadingTimeoutTimer->stop();
    m_bufferingTimeoutTimer->stop();
    m_progressReportTimer->stop();
    
    // Stop mpv if running
    if (m_playerBackend->isRunning()) {
        m_playerBackend->stopMpv();
    }
    clearPendingAutoplayContext();
    clearNextEpisodePrefetchState();
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

/**
 * @brief Handle changes to the backend process running state.
 *
 * When the backend reports it is no longer running and the controller's
 * playback state is neither Idle nor Error, this method treats the event
 * as an unexpected stop and triggers the playback stop / autoplay handling.
 *
 * @param running True if the backend process is running, false otherwise.
 */

void PlayerController::onProcessStateChanged(bool running)
{
    qDebug() << "PlayerController: Process state changed, running:" << running;
    qCInfo(lcPlaybackTrace) << "[attempt" << m_playbackAttemptId
                            << "] process-state"
                            << "running=" << running
                            << "state=" << stateToString(m_playbackState);
    
    if (!running && m_playbackState != Idle && m_playbackState != Error) {
        // Process stopped unexpectedly (e.g., mpv quit via 'q' or crash)
        // Treat this like an explicit stop so we report progress and consider autoplay.
        handlePlaybackStopAndAutoplay(Event::Stop);
    }
}

void PlayerController::onProcessError(const QString &error)
{
    qDebug() << "PlayerController: Process error:" << error;
    qCWarning(lcPlaybackTrace) << "[attempt" << m_playbackAttemptId
                               << "] process-error"
                               << error;

    if (error.startsWith(QStringLiteral("linux-libmpv-render-unavailable"))
        && tryFallbackToExternalBackend(error)) {
        return;
    }

    setErrorMessage(error);
    processEvent(Event::ErrorOccurred);
}

/**
 * @brief Handle updated playback position and advance related playback state.
 *
 * Updates internal position tracking, notifies observers, refreshes trickplay preview,
 * resets buffering timeouts and progress while buffering, triggers state transitions
 * from Loading->Buffering and Buffering->Playing when appropriate, and may request
 * a next-episode prefetch.
 *
 * @param seconds Current playback position in seconds.
 */
void PlayerController::onPositionChanged(double seconds)
{
    double previousPosition = m_currentPosition;
    m_currentPosition = seconds;
    if (m_reportProgressOnNextPositionUpdate
        && (m_playbackState == Playing || m_playbackState == Paused)) {
        reportPlaybackProgressNow();
        m_reportProgressOnNextPositionUpdate = false;
    }
    updateSkipSegmentState();
    if (!qFuzzyCompare(previousPosition + 1.0, seconds + 1.0)) {
        emit timelineChanged();
    }

    if (m_hasTrickplayInfo && !m_hasTrickplayPreviewPositionOverride) {
        updateTrickplayPreviewForPosition(seconds);
    }
    
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

    maybeTriggerNextEpisodePrefetch();
    
    m_lastPosition = seconds;
    m_lastPositionUpdateTime.restart();
}

void PlayerController::onDurationChanged(double seconds)
{
    if (!qFuzzyCompare(m_duration + 1.0, seconds + 1.0)) {
        m_duration = seconds;
        emit timelineChanged();
    }
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
                                                 m_playSessionId, m_duration > 0.0, m_muted,
                                                 m_playMethod);
        processEvent(Event::Resume);
    }
}

/**
 * @brief Handle end of the current playback session.
 *
 * Processes end-of-playback state, performs stop-related reporting, and initiates any configured autoplay or next-episode navigation logic.
 */
void PlayerController::onPlaybackEnded()
{
    qDebug() << "PlayerController: Playback ended";
    qCInfo(lcPlaybackTrace) << "[attempt" << m_playbackAttemptId
                            << "] playback-ended"
                            << "position=" << m_currentPosition
                            << "duration=" << m_duration;
    
    handlePlaybackStopAndAutoplay(Event::PlaybackEnd);
}

/**
 * @brief Handle end-of-playback duties and trigger autoplay or prefetched navigation when appropriate.
 *
 * Checks completion progress and, if the configured completion threshold is met for the current series,
 * marks the session for autoplay, stashes the pending autoplay context, and awaits next-episode resolution.
 * Always reports playback stop and processes the provided stop event through the state machine.
 * If a usable prefetched next episode is available, consumes that prefetched data and navigates to it.
 *
 * @param stopEvent Event value representing how playback ended (e.g., Stop, PlaybackEnd) to be processed.
 */
void PlayerController::handlePlaybackStopAndAutoplay(Event stopEvent)
{
    reportPlaybackStop();

    bool thresholdMet = checkCompletionThresholdAndAutoplay();
    bool prefetchedReady = false;

    // If threshold met for an episode, request next episode directly.
    if (thresholdMet && !m_currentSeriesId.isEmpty()) {
        m_shouldAutoplay = true;
        m_waitingForNextEpisodeAtPlaybackEnd = true;
        stashPendingAutoplayContext();
        prefetchedReady = hasUsablePrefetchedNextEpisode();
        if (!prefetchedReady) {
            m_libraryService->getNextUnplayedEpisode(m_pendingAutoplaySeriesId, m_pendingAutoplayItemId);
        }
        qDebug() << "PlayerController: Threshold met, requesting next episode for autoplay";
    }

    processEvent(stopEvent);

    if (prefetchedReady) {
        consumePrefetchedNextEpisodeAndNavigate();
    }
}

/**
 * @brief Handle a loaded "next episode" payload and either cache it for later or trigger navigation/autoplay.
 *
 * If the controller is not currently awaiting a next-episode resolution, caches the provided episode payload
 * and related series/item identifiers for potential later consumption. If the controller is awaiting a next-episode
 * resolution and autoplay is required, validates the series, clears awaiting state, and emits navigateToNextEpisode
 * with the episode payload, series id, selected audio/subtitle indices, and the configured autoplay flag. If no
 * episode data is available, clears the pending autoplay context instead of emitting navigation.
 *
 * @param seriesId Series identifier associated with the loaded next-episode payload.
 * @param episodeData JSON object containing the next-episode metadata (expected keys include "Id", "Name",
 *                    "SeriesName", "ParentIndexNumber", "IndexNumber", and user data used when starting playback).
 */
void PlayerController::onNextEpisodeLoaded(const QString &seriesId, const QJsonObject &episodeData)
{
    if (!m_waitingForNextEpisodeAtPlaybackEnd) {
        if (seriesId != m_currentSeriesId || m_currentSeriesId.isEmpty()) {
            return;
        }

        const QString prefetchedEpisodeId = episodeData.value(QStringLiteral("Id")).toString();
        const bool pointsToCurrentEpisode = !prefetchedEpisodeId.isEmpty() && prefetchedEpisodeId == m_currentItemId;
        m_prefetchedNextEpisodeData = episodeData;
        m_prefetchedNextEpisodeSeriesId = seriesId;
        m_prefetchedForItemId = m_currentItemId;
        m_nextEpisodePrefetchReady = !episodeData.isEmpty()
                                  && !prefetchedEpisodeId.isEmpty()
                                  && !pointsToCurrentEpisode;
        qCDebug(lcPlayback) << "Next-episode prefetch result cached"
                            << "itemId=" << m_prefetchedForItemId
                            << "seriesId=" << m_prefetchedNextEpisodeSeriesId
                            << "episodeId=" << prefetchedEpisodeId
                            << "pointsToCurrentEpisode=" << pointsToCurrentEpisode
                            << "ready=" << m_nextEpisodePrefetchReady;
        return;
    }

    // Only handle this if we're expecting an autoplay/navigation
    if (!m_shouldAutoplay) {
        return;
    }

    if (!m_pendingAutoplaySeriesId.isEmpty() && seriesId != m_pendingAutoplaySeriesId) {
        qDebug() << "PlayerController: Ignoring next episode for unexpected series:" << seriesId;
        return;
    }
    
    m_shouldAutoplay = false;
    m_waitingForNextEpisodeAtPlaybackEnd = false;
    
    if (episodeData.isEmpty()) {
        qDebug() << "PlayerController: No next episode available";
        clearPendingAutoplayContext();
        clearNextEpisodePrefetchState();
        return;
    }

    const QString episodeId = episodeData.value(QStringLiteral("Id")).toString();
    if (!episodeId.isEmpty() && episodeId == m_pendingAutoplayItemId) {
        qCWarning(lcPlayback) << "Ignoring next-episode response that points to the current item"
                              << "itemId=" << episodeId;
        clearPendingAutoplayContext();
        clearNextEpisodePrefetchState();
        return;
    }

    // Extract episode info
    QString episodeName = episodeData["Name"].toString();
    QString seriesName = episodeData["SeriesName"].toString();
    int seasonNumber = episodeData["ParentIndexNumber"].toInt();
    int episodeNumber = episodeData["IndexNumber"].toInt();
    
    qDebug() << "PlayerController: Next episode found:" << seriesName 
             << "S" << seasonNumber << "E" << episodeNumber << "-" << episodeName;
    
    // Always emit navigateToNextEpisode to show the Up Next screen
    // The QML screen will handle autoplay countdown vs manual play
    bool autoplay = m_config->getAutoplayNextEpisode();
    int lastAudioIndex = m_pendingAutoplayAudioTrack;
    int lastSubtitleIndex = m_pendingAutoplaySubtitleTrack;
    
    qDebug() << "PlayerController: Emitting navigateToNextEpisode signal with autoplay:" 
             << autoplay << "audio:" << lastAudioIndex << "subtitle:" << lastSubtitleIndex;

    setAwaitingNextEpisodeResolution(false);
    emitNavigateToNextEpisodeQueued(episodeData, seriesId, lastAudioIndex, lastSubtitleIndex, autoplay);
    
    // Note: Don't clear pending autoplay context here - playNextEpisode() needs it
}

/**
 * @brief Starts playback of the provided next-episode item and applies Up Next autoplay context.
 *
 * Extracts episode metadata (ID, title, season/episode numbers) and an optional resume
 * position from the episode JSON, updates the on-screen overlay, emits the autoplayingNextEpisode
 * signal, resolves a stream URL from the LibraryService, and begins playback using the
 * currently stashed autoplay parameters. Clears the pending autoplay context on success
 * or if the episode data is invalid.
 *
 * @param episodeData JSON object describing the episode. If present, the function reads
 *                    "Id", "Name", "SeriesName", "ParentIndexNumber" (season), "IndexNumber"
 *                    (episode), and "UserData.PlaybackPositionTicks" (resume position).
 * @param seriesId    Series identifier associated with the episode (used for playback metadata).
 */
void PlayerController::playNextEpisode(const QJsonObject &episodeData, const QString &seriesId)
{
    QString episodeId = episodeData["Id"].toString();
    QString episodeName = episodeData["Name"].toString();
    QString seriesName = episodeData["SeriesName"].toString();
    int seasonNumber = episodeData["ParentIndexNumber"].toInt();
    int episodeNumber = episodeData["IndexNumber"].toInt();
    
    if (episodeId.isEmpty()) {
        qWarning() << "PlayerController::playNextEpisode: Empty episode ID";
        clearPendingAutoplayContext();
        return;
    }
    
    // Get resume position if any
    qint64 startPositionTicks = 0;
    if (episodeData.contains("UserData") && episodeData["UserData"].isObject()) {
        QJsonObject userData = episodeData["UserData"].toObject();
        startPositionTicks = static_cast<qint64>(userData["PlaybackPositionTicks"].toDouble());
    }
    
    qDebug() << "PlayerController: Playing next episode from Up Next screen:" << seriesName
             << "S" << seasonNumber << "E" << episodeNumber << "-" << episodeName;
    
    QString subtitle = QStringLiteral("S%1 E%2").arg(seasonNumber).arg(episodeNumber);
    if (!episodeName.isEmpty()) {
        subtitle += QStringLiteral(" - ") + episodeName;
    }
    setOverlayMetadata(seriesName.isEmpty() ? QStringLiteral("Now Playing") : seriesName, subtitle);
    
    emit autoplayingNextEpisode(episodeName, seriesName);

    // Preserve stashed Jellyfin track indices across playUrl() clearing autoplay context.
    const int stashedAudioTrack = m_pendingAutoplayAudioTrack;
    const int stashedSubtitleTrack = m_pendingAutoplaySubtitleTrack;
    const bool audioTrackChanged = m_selectedAudioTrack != stashedAudioTrack;
    const bool subtitleTrackChanged = m_selectedSubtitleTrack != stashedSubtitleTrack;
    m_selectedAudioTrack = stashedAudioTrack;
    m_selectedSubtitleTrack = stashedSubtitleTrack;
    if (audioTrackChanged) {
        emit selectedAudioTrackChanged();
    }
    if (subtitleTrackChanged) {
        emit selectedSubtitleTrackChanged();
    }

    qCDebug(lcPlayback) << "playNextEpisode startup track selections"
                        << "audio=" << m_selectedAudioTrack
                        << "subtitle=" << m_selectedSubtitleTrack;

    // Build stream URL and start playback using stashed autoplay context
    QString targetSeasonId = episodeData.value(QStringLiteral("SeasonId")).toString();
    if (targetSeasonId.isEmpty()) {
        targetSeasonId = episodeData.value(QStringLiteral("ParentId")).toString();
    }
    if (targetSeasonId.isEmpty()) {
        targetSeasonId = m_pendingAutoplaySeasonId;
    }
    QString streamUrl = m_libraryService->getStreamUrl(episodeId);
    playUrl(streamUrl, episodeId, startPositionTicks, seriesId,
        targetSeasonId, m_pendingAutoplayLibraryId,
        m_pendingAutoplayFramerate, m_pendingAutoplayIsHDR);
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

bool PlayerController::supportsEmbeddedVideo() const
{
    return m_playerBackend && m_playerBackend->supportsEmbeddedVideo();
}

bool PlayerController::attachEmbeddedVideoTarget(QObject *target)
{
    if (!m_playerBackend) {
        return false;
    }

    return m_playerBackend->attachVideoTarget(target);
}

void PlayerController::detachEmbeddedVideoTarget(QObject *target)
{
    if (!m_playerBackend) {
        return;
    }

    m_playerBackend->detachVideoTarget(target);
}

void PlayerController::setEmbeddedVideoViewport(qreal x, qreal y, qreal width, qreal height)
{
    if (!m_playerBackend) {
        return;
    }

    const QRectF viewport(x, y, width, height);
    m_playerBackend->setVideoViewport(viewport);
}

void PlayerController::setEmbeddedVideoShrinkEnabled(bool enabled)
{
    if (m_embeddedVideoShrinkEnabled == enabled) {
        return;
    }

    m_embeddedVideoShrinkEnabled = enabled;
    emit embeddedVideoShrinkEnabledChanged();
}

/**
 * @brief Start playback of the configured test video.
 *
 * Clears any pending autoplay context and next-episode prefetch state, disables autoplay,
 * clears the current item identifier, sets the pending URL to the configured test video,
 * stops any currently running backend playback, and triggers the player state machine to
 * begin loading and playing the test video.
 */
void PlayerController::playTestVideo()
{
    clearPendingAutoplayContext();
    clearNextEpisodePrefetchState();
    m_shouldAutoplay = false;

    if (!m_currentItemId.isEmpty()) {
        m_currentItemId.clear();
        emit currentItemIdChanged();
    }
    m_pendingUrl = m_testVideoUrl;
    
    if (m_playerBackend->isRunning()) {
        reportPlaybackStop();
        m_playerBackend->stopMpv();
    }
    
    processEvent(Event::Play);
}

/**
 * @brief Begin playback of the given media URL and prepare the controller state for a new item.
 *
 * Updates internal playback context (current item/series/season/library IDs, playback attempt),
 * stops any currently running playback, clears autoplay and next-episode prefetch state, requests
 * media segments and trickplay info for the item, queues an initial seek if a start position is
 * provided, and transitions the internal state machine to start loading the URL.
 *
 * @param url Playback URL to open.
 * @param itemId Optional content item identifier (empty if unknown).
 * @param startPositionTicks Resume/start position in 100-nanosecond ticks (Jellyfin units); if
 *        greater than zero the controller will seek to the corresponding position after buffering.
 * @param seriesId Optional series identifier for episodic content.
 * @param seasonId Optional season identifier for episodic content.
 * @param libraryId Optional library identifier where the item resides.
 * @param framerate Content framerate in frames per second, used for framerate-matching decisions.
 * @param isHDR True if the content is HDR, used for HDR-related display handling.
 */
void PlayerController::playUrl(const QString &url, const QString &itemId, qint64 startPositionTicks, const QString &seriesId, const QString &seasonId, const QString &libraryId, double framerate, bool isHDR)
{
    m_playbackAttemptId = ++gPlaybackAttemptCounter;
    m_reportProgressOnNextPositionUpdate = false;
    qDebug() << "PlayerController: playUrl called with itemId:" << itemId 
             << "startPositionTicks:" << startPositionTicks
             << "seriesId:" << seriesId
             << "seasonId:" << seasonId
             << "libraryId:" << libraryId
             << "framerate:" << framerate
             << "isHDR:" << isHDR;
    qCInfo(lcPlaybackTrace) << "[attempt" << m_playbackAttemptId
                            << "] play-url"
                            << "itemId=" << itemId
                            << "startTicks=" << startPositionTicks
                            << "framerate=" << framerate
                            << "isHDR=" << isHDR
                            << "enableHDRSetting=" << m_config->getEnableHDR()
                            << "enableFramerateMatchSetting=" << m_config->getEnableFramerateMatching();
    
    // If already playing, stop first
    if (m_playerBackend->isRunning()) {
        reportPlaybackStop();
        // Don't check completion threshold here - we're starting new content intentionally
        m_playerBackend->stopMpv();
    }

    clearPendingAutoplayContext();
    clearNextEpisodePrefetchState();
    
    // Store pending playback info before transition
    if (m_currentItemId != itemId) {
        m_currentItemId = itemId;
        emit currentItemIdChanged();
    }
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
    m_playMethod = inferPlayMethod(url);
    m_hasReportedStopForAttempt = false;
    m_hasEvaluatedCompletionForAttempt = false;
    
    // Clear previous OSC/trickplay state and request new data
    m_currentSegments.clear();
    m_isInIntroSegment = false;
    m_isInOutroSegment = false;
    m_hasAutoSkippedIntroForCurrentItem = false;
    m_hasAutoSkippedOutroForCurrentItem = false;
    m_hasTrickplayInfo = false;
    m_currentTrickplayInfo = TrickplayTileInfo{};
    m_trickplayBinaryPath.clear();
    m_currentTrickplayFrameIndex = -1;
    m_hasTrickplayPreviewPositionOverride = false;
    m_trickplayPreviewPositionOverrideSeconds = 0.0;
    clearTrickplayPreview();
    emit timelineChanged();
    emit skipSegmentsChanged();
    emit trickplayStateChanged();
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

/**
 * @brief Stops current playback and clears autoplay/prefetch state.
 *
 * Clears any pending autoplay context and next-episode prefetch data, disables
 * automatic autoplay for the current session, reports playback stop and checks
 * the completion threshold for the current item, and instructs the player
 * backend to stop playback. If the controller is not already in Idle or Error
 * state, processes a Stop event to transition the playback state machine.
 *
 * Note: some backends may emit a synchronous state change when stopped; that
 * may already transition the controller to Idle via onProcessStateChanged.
 */
void PlayerController::stop()
{
    qDebug() << "PlayerController: stop requested";

    clearPendingAutoplayContext();
    clearNextEpisodePrefetchState();
    m_shouldAutoplay = false;
    
    reportPlaybackStop();
    checkCompletionThreshold();
    
    m_playerBackend->stopMpv();
    // Some backends emit stateChanged(false) synchronously from stopMpv(), which
    // can already transition us to Idle via onProcessStateChanged().
    if (m_playbackState != Idle && m_playbackState != Error) {
        processEvent(Event::Stop);
    }
}

void PlayerController::pause()
{
    if (m_playbackState == Playing || m_playbackState == Buffering) {
        m_playerBackend->sendCommand({"set", "pause", "yes"});
    }
}

void PlayerController::resume()
{
    if (m_playbackState == Paused) {
        m_playerBackend->sendCommand({"set", "pause", "no"});
    }
}

void PlayerController::togglePause()
{
    m_playerBackend->sendCommand({"cycle", "pause"});
}

void PlayerController::seek(double seconds)
{
    qDebug() << "PlayerController: seek to" << seconds;
    
    // If loading/buffering, queue the seek for when buffering setup completes.
    // This is required for early intro auto-skip where segments can arrive before
    // we transition out of Loading.
    if (m_playbackState == Loading || m_playbackState == Buffering) {
        m_seekTargetWhileBuffering = seconds;
        qDebug() << "PlayerController: Queued seek for after loading/buffering";
        return;
    }
    
    if (m_playbackState == Playing || m_playbackState == Paused) {
        m_playerBackend->sendVariantCommand({"seek", seconds, "absolute"});
        m_reportProgressOnNextPositionUpdate = true;
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
        m_playerBackend->sendVariantCommand({"seek", seconds, "relative"});
        m_reportProgressOnNextPositionUpdate = true;
    }
}

void PlayerController::skipIntro()
{
    seekToSegmentEnd(MediaSegmentType::Intro);
}

void PlayerController::skipOutro()
{
    seekToSegmentEnd(MediaSegmentType::Outro);
}

void PlayerController::skipActiveSegment()
{
    if (m_isInIntroSegment) {
        skipIntro();
    } else if (m_isInOutroSegment) {
        skipOutro();
    }
}

void PlayerController::retry()
{
    qDebug() << "PlayerController: retry requested";
    
    if (m_playbackState == Error && !m_pendingUrl.isEmpty()) {
        m_reportProgressOnNextPositionUpdate = false;
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
        const int previousAudioTrack = m_selectedAudioTrack;
        m_selectedAudioTrack = index;
        qCDebug(lcPlayback) << "User audio track selection:"
                            << "jellyfinIndex=" << index
                            << "previousJellyfinIndex=" << previousAudioTrack;
        
        if (m_playbackState == Playing || m_playbackState == Paused) {
            if (index >= 0) {
                const int mpvTrackId = mpvAudioTrackForJellyfinIndex(index);
                if (mpvTrackId > 0) {
                    qCDebug(lcPlayback) << "Applying audio track switch via aid:" << mpvTrackId;
                    m_playerBackend->sendVariantCommand({"set_property", "aid", mpvTrackId});
                } else {
                    qCWarning(lcPlayback) << "No mapped mpv audio track for jellyfin index" << index
                                          << "- skipping runtime aid command";
                }
            } else {
                m_playerBackend->sendVariantCommand({"set_property", "aid", "auto"});
            }
        }
        
        // Save preference for season continuity (both in-memory and persistent)
        if (!m_currentSeasonId.isEmpty()) {
            m_seasonTrackPreferences[m_currentSeasonId].first = index;
            m_trackPrefs->setAudioTrack(m_currentSeasonId, index);
        } else if (m_currentSeriesId.isEmpty() && !m_currentItemId.isEmpty()) {
            m_trackPrefs->setMovieAudioTrack(m_currentItemId, index);
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
        const int previousSubtitleTrack = m_selectedSubtitleTrack;
        m_selectedSubtitleTrack = index;
        qCDebug(lcPlayback) << "User subtitle track selection:"
                            << "jellyfinIndex=" << index
                            << "previousJellyfinIndex=" << previousSubtitleTrack;
        
        if (m_playbackState == Playing || m_playbackState == Paused) {
            if (index >= 0) {
                const int mpvTrackId = mpvSubtitleTrackForJellyfinIndex(index);
                if (mpvTrackId > 0) {
                    qCDebug(lcPlayback) << "Applying subtitle track switch via sid:" << mpvTrackId;
                    m_playerBackend->sendVariantCommand({"set_property", "sid", mpvTrackId});
                } else {
                    qCWarning(lcPlayback) << "No mapped mpv subtitle track for jellyfin index" << index
                                          << "- skipping runtime sid command";
                }
            } else {
                m_playerBackend->sendVariantCommand({"set_property", "sid", "no"});
            }
        }
        
        // Save preference for season continuity (both in-memory and persistent)
        if (!m_currentSeasonId.isEmpty()) {
            m_seasonTrackPreferences[m_currentSeasonId].second = index;
            m_trackPrefs->setSubtitleTrack(m_currentSeasonId, index);
        } else if (m_currentSeriesId.isEmpty() && !m_currentItemId.isEmpty()) {
            m_trackPrefs->setMovieSubtitleTrack(m_currentItemId, index);
        }
        
        emit selectedSubtitleTrackChanged();
    }
}

void PlayerController::cycleAudioTrack()
{
    qDebug() << "PlayerController: Cycling audio track";
    if (m_playbackState == Playing || m_playbackState == Paused) {
        m_playerBackend->sendCommand({"cycle", "audio"});
    }
}

void PlayerController::cycleSubtitleTrack()
{
    qDebug() << "PlayerController: Cycling subtitle track";
    if (m_playbackState == Playing || m_playbackState == Paused) {
        m_playerBackend->sendCommand({"cycle", "sub"});
    }
}

void PlayerController::previousChapter()
{
    if (m_playbackState == Playing || m_playbackState == Paused) {
        m_playerBackend->sendCommand({"add", "chapter", "-1"});
    }
}

void PlayerController::nextChapter()
{
    if (m_playbackState == Playing || m_playbackState == Paused) {
        m_playerBackend->sendCommand({"add", "chapter", "1"});
    }
}

void PlayerController::toggleMute()
{
    setMuted(!m_muted);
}

void PlayerController::setMuted(bool muted)
{
    if (m_muted == muted) {
        if (m_playbackState == Loading || m_playbackState == Buffering
            || m_playbackState == Playing || m_playbackState == Paused) {
            m_playerBackend->sendVariantCommand({"set_property", "mute", muted});
        }
        return;
    }

    m_muted = muted;
    emit mutedChanged();

    if (m_playbackState == Loading || m_playbackState == Buffering
        || m_playbackState == Playing || m_playbackState == Paused) {
        m_playerBackend->sendVariantCommand({"set_property", "mute", muted});
    }

    schedulePersistPlaybackVolumeState();
}

void PlayerController::setVolume(int volume)
{
    const int clamped = qBound(0, volume, 200);
    if (m_volume != clamped) {
        m_volume = clamped;
        emit volumeChanged();
    }

    if (clamped > 0 && m_muted) {
        m_muted = false;
        emit mutedChanged();
    }

    if (m_playbackState == Loading || m_playbackState == Buffering
        || m_playbackState == Playing || m_playbackState == Paused) {
        m_playerBackend->sendVariantCommand({"set_property", "volume", clamped});
        if (clamped > 0 && m_muted == false) {
            m_playerBackend->sendVariantCommand({"set_property", "mute", false});
        }
    }

    schedulePersistPlaybackVolumeState();
}

void PlayerController::adjustVolume(int delta)
{
    setVolume(m_volume + delta);
}

void PlayerController::schedulePersistPlaybackVolumeState()
{
    if (!m_config || !m_volumePersistTimer) {
        return;
    }
    m_volumePersistTimer->start();
}

void PlayerController::persistPlaybackVolumeState()
{
    if (!m_config) {
        return;
    }
    m_config->setPlaybackVolume(m_volume);
    m_config->setPlaybackMuted(m_muted);
}

void PlayerController::showMpvStatsOnce()
{
    if (m_playbackState == Loading || m_playbackState == Buffering
        || m_playbackState == Playing || m_playbackState == Paused) {
        if (!mpvStatsHotkeysAllowed(m_playerBackend)) {
            qCWarning(lcPlayback)
                << "Ignoring mpv stats hotkey on embedded linux libmpv backend"
                << "(set BLOOM_LINUX_LIBMPV_ENABLE_STATS_HOTKEYS=1 to override)";
            return;
        }
        m_playerBackend->sendCommand({"script-binding", "stats/display-stats"});
    }
}

void PlayerController::toggleMpvStats()
{
    if (m_playbackState == Loading || m_playbackState == Buffering
        || m_playbackState == Playing || m_playbackState == Paused) {
        if (!mpvStatsHotkeysAllowed(m_playerBackend)) {
            qCWarning(lcPlayback)
                << "Ignoring mpv stats hotkey on embedded linux libmpv backend"
                << "(set BLOOM_LINUX_LIBMPV_ENABLE_STATS_HOTKEYS=1 to override)";
            return;
        }
        m_playerBackend->sendCommand({"script-binding", "stats/display-stats-toggle"});
    }
}

void PlayerController::showMpvStatsPage(int page)
{
    if (page < 0 || page > 9) {
        return;
    }

    if (m_playbackState == Loading || m_playbackState == Buffering
        || m_playbackState == Playing || m_playbackState == Paused) {
        if (!mpvStatsHotkeysAllowed(m_playerBackend)) {
            qCWarning(lcPlayback)
                << "Ignoring mpv stats page hotkey on embedded linux libmpv backend"
                << "(set BLOOM_LINUX_LIBMPV_ENABLE_STATS_HOTKEYS=1 to override)";
            return;
        }
        const int mappedPage = (page == 0) ? 10 : page;
        const QString binding = QStringLiteral("stats/display-page-%1").arg(mappedPage);
        m_playerBackend->sendCommand({"script-binding", binding});
    }
}

void PlayerController::sendMpvKeypress(const QString &key)
{
    if (key.isEmpty()) {
        return;
    }

    if (m_playbackState == Loading || m_playbackState == Buffering
        || m_playbackState == Playing || m_playbackState == Paused) {
        qCDebug(lcPlayback) << "Forwarding keypress to mpv:" << key;
        m_playerBackend->sendCommand({"keypress", key});
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
                                          const QVariantList &audioTrackMap,
                                          const QVariantList &subtitleTrackMap,
                                          const QVariantList &availableAudioTracks,
                                          const QVariantList &availableSubtitleTracks,
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
    m_availableAudioTracks = availableAudioTracks;
    m_availableSubtitleTracks = availableSubtitleTracks;
    updateTrackMappings(audioTrackMap, subtitleTrackMap);
    
    qCDebug(lcPlayback) << "Track mapping contract initialized:"
                        << "audioMapEntries=" << m_audioTrackMap.size()
                        << "subtitleMapEntries=" << m_subtitleTrackMap.size()
                        << "selectedAudio=" << m_selectedAudioTrack
                        << "selectedSubtitle=" << m_selectedSubtitleTrack
                        << "selectedMpvAudio=" << mpvAudioTrackForJellyfinIndex(m_selectedAudioTrack)
                        << "selectedMpvSubtitle=" << mpvSubtitleTrackForJellyfinIndex(m_selectedSubtitleTrack);
    
    emit mediaSourceIdChanged();
    emit playSessionIdChanged();
    emit selectedAudioTrackChanged();
    emit selectedSubtitleTrackChanged();
    emit availableTracksChanged();
    
    // Call base playUrl which handles the rest
    playUrl(url, itemId, startPositionTicks, seriesId, seasonId, libraryId, framerate, isHDR);
}

void PlayerController::updateTrackMappings(const QVariantList &audioTrackMap, const QVariantList &subtitleTrackMap)
{
    auto parseMap = [](const QVariantList &input, QHash<int, int> &output, const char *typeName) {
        output.clear();
        QSet<int> seenMpvTrackIds;

        for (const QVariant &entryVariant : input) {
            const QVariantMap entry = entryVariant.toMap();
            const int jellyfinIndex = entry.value(QStringLiteral("jellyfinIndex"), -1).toInt();
            const int mpvTrackId = entry.value(QStringLiteral("mpvTrackId"), -1).toInt();

            if (jellyfinIndex < 0 || mpvTrackId <= 0) {
                continue;
            }
            if (seenMpvTrackIds.contains(mpvTrackId)) {
                qCWarning(lcPlayback) << "Duplicate mpv track id in" << typeName
                                      << "mapping ignored:" << mpvTrackId;
                continue;
            }

            seenMpvTrackIds.insert(mpvTrackId);
            output.insert(jellyfinIndex, mpvTrackId);
        }
    };

    parseMap(audioTrackMap, m_audioTrackMap, "audio");
    parseMap(subtitleTrackMap, m_subtitleTrackMap, "subtitle");
}

int PlayerController::mpvAudioTrackForJellyfinIndex(int jellyfinStreamIndex) const
{
    if (jellyfinStreamIndex < 0) {
        return -1;
    }
    if (m_audioTrackMap.contains(jellyfinStreamIndex)) {
        return m_audioTrackMap.value(jellyfinStreamIndex);
    }

    // Compatibility fallback only for the startup-selected track carried by playUrlWithTracks.
    if (m_selectedAudioTrack == jellyfinStreamIndex && m_mpvAudioTrack > 0) {
        return m_mpvAudioTrack;
    }
    return -1;
}

int PlayerController::mpvSubtitleTrackForJellyfinIndex(int jellyfinStreamIndex) const
{
    if (jellyfinStreamIndex < 0) {
        return -1;
    }
    if (m_subtitleTrackMap.contains(jellyfinStreamIndex)) {
        return m_subtitleTrackMap.value(jellyfinStreamIndex);
    }

    // Compatibility fallback only for the startup-selected track carried by playUrlWithTracks.
    if (m_selectedSubtitleTrack == jellyfinStreamIndex && m_mpvSubtitleTrack > 0) {
        return m_mpvSubtitleTrack;
    }
    return -1;
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
                                               m_playSessionId, m_duration > 0.0, false, m_muted,
                                               m_playMethod);
    }
}

void PlayerController::reportPlaybackProgress()
{
    if (!m_currentItemId.isEmpty() && m_playbackService && m_playbackState == Playing) {
        qint64 ticks = static_cast<qint64>(m_currentPosition * 10000000); // 100ns ticks
        m_playbackService->reportPlaybackProgress(m_currentItemId, ticks, m_mediaSourceId,
                                                  m_selectedAudioTrack, m_selectedSubtitleTrack,
                                                  m_playSessionId, m_duration > 0.0, false, m_muted,
                                                  m_playMethod);
    }
}

void PlayerController::reportPlaybackProgressNow()
{
    if (!m_currentItemId.isEmpty() && m_playbackService
        && (m_playbackState == Playing || m_playbackState == Paused)) {
        qint64 ticks = static_cast<qint64>(m_currentPosition * 10000000); // 100ns ticks
        m_playbackService->reportPlaybackProgress(m_currentItemId, ticks, m_mediaSourceId,
                                                  m_selectedAudioTrack, m_selectedSubtitleTrack,
                                                  m_playSessionId, m_duration > 0.0,
                                                  m_playbackState == Paused, m_muted,
                                                  m_playMethod);
    }
}

void PlayerController::reportPlaybackStop()
{
    if (m_hasReportedStopForAttempt) {
        qCDebug(lcPlayback) << "Skipping duplicate playback stop report for attempt" << m_playbackAttemptId;
        return;
    }

    if (!m_currentItemId.isEmpty() && m_playbackService) {
        double percentage = m_duration > 0 ? (m_currentPosition / m_duration) * 100.0 : 0;
        qCInfo(lcPlayback) << "Playback stopped: itemId=" << m_currentItemId 
                           << "position=" << m_currentPosition << "s /" << m_duration << "s"
                           << "(" << percentage << "%)";
        qint64 ticks = static_cast<qint64>(m_currentPosition * 10000000);
        reportPlaybackProgressNow();
        m_playbackService->reportPlaybackStopped(m_currentItemId, ticks, m_mediaSourceId,
                                                 m_selectedAudioTrack, m_selectedSubtitleTrack,
                                                 m_playSessionId, m_duration > 0.0,
                                                 m_playbackState == Paused, m_muted,
                                                 m_playMethod);
        m_hasReportedStopForAttempt = true;
    }
}

void PlayerController::checkCompletionThreshold()
{
    checkCompletionThresholdAndAutoplay();
}

/**
 * @brief Evaluate whether the current playback has met the configured completion threshold.
 *
 * If the configured completion percentage is reached, the function reports that the threshold was met.
 * The check is performed at most once per playback attempt; subsequent calls for the same attempt are no-ops.
 * The function does nothing and returns false if there is no current item or the duration is not positive.
 *
 * @return true if the completion threshold was met; false otherwise.
 */
bool PlayerController::checkCompletionThresholdAndAutoplay()
{
    if (m_hasEvaluatedCompletionForAttempt) {
        qCDebug(lcPlayback) << "Skipping duplicate completion-threshold evaluation for attempt" << m_playbackAttemptId;
        return false;
    }
    if (m_currentItemId.isEmpty() || m_duration <= 0) return false;
    m_hasEvaluatedCompletionForAttempt = true;
    
    double percentage = (m_currentPosition / m_duration) * 100.0;
    int threshold = m_config->getPlaybackCompletionThreshold();
    
    if (percentage >= threshold) {
        qDebug() << "PlayerController: Completion threshold met for item" << m_currentItemId
                 << "(" << percentage << "% >= " << threshold << "% threshold)";
        return true;  // Threshold met - eligible for autoplay
    }
    return false;  // Threshold not met
}

/**
 * @brief Attempts to prefetch the next episode when playback nears completion.
 *
 * If playback is in Playing or Paused state, a current series and item are present,
 * duration is positive, and a prefetch has not already been requested for this
 * playback attempt, this will set the internal prefetch-requested flag and ask
 * the LibraryService for the next unplayed episode for the current series/item.
 *
 * Does nothing when any prerequisite is missing (no series/item, non-positive
 * duration, playback not active, or a prefetch already requested).
 */
void PlayerController::maybeTriggerNextEpisodePrefetch()
{
    if (m_nextEpisodePrefetchRequestedForAttempt
        || m_currentSeriesId.isEmpty()
        || m_currentItemId.isEmpty()
        || m_duration <= 0.0
        || (m_playbackState != Playing && m_playbackState != Paused)) {
        return;
    }

    const double progressPercent = (m_currentPosition / m_duration) * 100.0;
    if (progressPercent < kNextEpisodePrefetchTriggerPercent) {
        return;
    }

    m_nextEpisodePrefetchRequestedForAttempt = true;
    qCDebug(lcPlayback) << "Triggering next-episode prefetch"
                        << "itemId=" << m_currentItemId
                        << "seriesId=" << m_currentSeriesId
                        << "progressPercent=" << progressPercent;
    m_libraryService->getNextUnplayedEpisode(m_currentSeriesId, m_currentItemId);
}

/**
 * Determines whether a prefetched next-episode payload is valid and applicable for autoplay.
 *
 * Checks that a prefetched payload exists and is marked ready, that it contains a valid episode id,
 * and that its series and item ids match the current pending autoplay context. Also ensures the
 * prefetched episode is not the same as the item that just finished playing.
 *
 * @return `true` if the prefetched next-episode can be consumed for autoplay, `false` otherwise.
 */
bool PlayerController::hasUsablePrefetchedNextEpisode() const
{
    const QString prefetchedEpisodeId = m_prefetchedNextEpisodeData.value(QStringLiteral("Id")).toString();
    if (!m_nextEpisodePrefetchReady
        || m_prefetchedNextEpisodeData.isEmpty()
        || prefetchedEpisodeId.isEmpty()) {
        return false;
    }
    if (m_prefetchedNextEpisodeSeriesId.isEmpty()
        || m_prefetchedNextEpisodeSeriesId != m_pendingAutoplaySeriesId) {
        return false;
    }
    if (m_prefetchedForItemId.isEmpty()
        || m_prefetchedForItemId != m_pendingAutoplayItemId) {
        return false;
    }
    // Jellyfin may still return the currently playing episode until mark-played settles.
    // Never consume a prefetched candidate that points to the just-finished item.
    if (prefetchedEpisodeId == m_pendingAutoplayItemId) {
        return false;
    }
    return true;
}

/**
 * @brief Consume a prefetched "next episode" payload and trigger navigation to it.
 *
 * If a usable prefetched next-episode is available, emits navigateToNextEpisode with
 * the prefetched episode data, series id, requested audio/subtitle indices, and the
 * current autoplay setting. Clears the prefetch state and related awaiting/autoplay
 * flags after emitting. If no usable prefetched episode exists, this is a no-op.
 */
void PlayerController::consumePrefetchedNextEpisodeAndNavigate()
{
    if (!hasUsablePrefetchedNextEpisode()) {
        return;
    }

    const bool autoplay = m_config->getAutoplayNextEpisode();
    const int lastAudioIndex = m_pendingAutoplayAudioTrack;
    const int lastSubtitleIndex = m_pendingAutoplaySubtitleTrack;
    const QString prefetchedSeriesId = m_prefetchedNextEpisodeSeriesId;

    m_shouldAutoplay = false;
    m_waitingForNextEpisodeAtPlaybackEnd = false;
    setAwaitingNextEpisodeResolution(false);

    qCDebug(lcPlayback) << "Using prefetched next episode for Up Next"
                        << "itemId=" << m_pendingAutoplayItemId
                        << "seriesId=" << prefetchedSeriesId;

    emitNavigateToNextEpisodeQueued(m_prefetchedNextEpisodeData,
                                    prefetchedSeriesId,
                                    lastAudioIndex,
                                    lastSubtitleIndex,
                                    autoplay);
    clearNextEpisodePrefetchState();
}

void PlayerController::emitNavigateToNextEpisodeQueued(const QJsonObject &episodeData,
                                                       const QString &seriesId,
                                                       int lastAudioIndex,
                                                       int lastSubtitleIndex,
                                                       bool autoplay)
{
    QMetaObject::invokeMethod(this,
                              [this, episodeData, seriesId, lastAudioIndex, lastSubtitleIndex, autoplay]() {
                                  emit navigateToNextEpisode(episodeData,
                                                             seriesId,
                                                             lastAudioIndex,
                                                             lastSubtitleIndex,
                                                             autoplay);
                              },
                              Qt::QueuedConnection);
}

/**
 * @brief Clears any staged next-episode prefetch state and cached prefetched data.
 *
 * Resets flags that track awaiting resolution, request attempts, and readiness,
 * and clears the stored prefetched episode JSON payload, its series identifier,
 * and the associated item id.
 */
void PlayerController::clearNextEpisodePrefetchState()
{
    m_waitingForNextEpisodeAtPlaybackEnd = false;
    m_nextEpisodePrefetchRequestedForAttempt = false;
    m_nextEpisodePrefetchReady = false;
    m_prefetchedNextEpisodeData = QJsonObject();
    m_prefetchedNextEpisodeSeriesId.clear();
    m_prefetchedForItemId.clear();
}

/**
 * @brief Save the current playback context for a pending autoplay (next-episode) action.
 *
 * Copies the current item, series, season, library, selected audio/subtitle tracks,
 * framerate, and HDR flag into the controller's pending-autoplay fields and marks
 * the controller as awaiting next-episode resolution.
 */
void PlayerController::stashPendingAutoplayContext()
{
    m_pendingAutoplayItemId = m_currentItemId;
    m_pendingAutoplaySeriesId = m_currentSeriesId;
    m_pendingAutoplaySeasonId = m_currentSeasonId;
    m_pendingAutoplayLibraryId = m_currentLibraryId;
    m_pendingAutoplayAudioTrack = m_selectedAudioTrack;
    m_pendingAutoplaySubtitleTrack = m_selectedSubtitleTrack;
    m_pendingAutoplayFramerate = m_contentFramerate;
    m_pendingAutoplayIsHDR = m_contentIsHDR;
    setAwaitingNextEpisodeResolution(true);
}

/**
 * @brief Clears any stored context for a pending autoplay (Up Next) action.
 *
 * Resets identifiers, track/framerate/HDR hints, and autoplay-related flags so no pending
 * autoplay will be consumed or awaited.
 */
void PlayerController::clearPendingAutoplayContext()
{
    m_pendingAutoplayItemId.clear();
    m_pendingAutoplaySeriesId.clear();
    m_pendingAutoplaySeasonId.clear();
    m_pendingAutoplayLibraryId.clear();
    m_pendingAutoplayAudioTrack = -1;
    m_pendingAutoplaySubtitleTrack = -1;
    m_pendingAutoplayFramerate = 0.0;
    m_pendingAutoplayIsHDR = false;
    setAwaitingNextEpisodeResolution(false);
}

/**
 * @brief Set whether the controller is waiting for the next-episode resolution.
 *
 * Updates the awaiting-next-episode flag and, when the flag actually changes,
 * emits awaitingNextEpisodeResolutionChanged().
 *
 * @param awaiting True to mark that the controller is awaiting a next-episode resolution, false otherwise.
 */
void PlayerController::setAwaitingNextEpisodeResolution(bool awaiting)
{
    if (m_awaitingNextEpisodeResolution == awaiting) {
        return;
    }
    m_awaitingNextEpisodeResolution = awaiting;
    emit awaitingNextEpisodeResolutionChanged();
}

/**
 * @brief Begin playback of the specified media URL.
 *
 * Starts playback for the provided URL, applies display settings required for the content
 * (enables HDR when allowed and content is HDR, capturing the original refresh rate so
 * it can be restored later), and then initiates framerate matching and the backend start sequence.
 *
 * @param url The media resource URL to play.
 */
void PlayerController::startPlayback(const QString &url)
{
    qDebug() << "PlayerController: Starting playback of" << url;
    qCInfo(lcPlaybackTrace) << "[attempt" << m_playbackAttemptId
                            << "] start-playback"
                            << "contentIsHDR=" << m_contentIsHDR
                            << "contentFramerate=" << m_contentFramerate
                            << "url=" << url;
    
    // Cancel any pending deferred mpv start from previous playback
    m_startDelayTimer->stop();
    
    // Handle Display Settings - HDR FIRST (must be done before refresh rate change)
    // Toggling HDR can reset the display mode, so we set HDR first, then refresh rate
    const bool shouldAttemptHdrToggle = m_config->getEnableHDR() && m_contentIsHDR;
    bool hdrEnabled = false;
    if (shouldAttemptHdrToggle) {
        // Snapshot refresh before HDR toggle. Some setups force 60Hz in HDR,
        // and we want restore to return to the pre-HDR rate.
        m_displayManager->captureOriginalRefreshRate();
        qDebug() << "PlayerController: Enabling HDR for HDR content";
        qCInfo(lcPlaybackTrace) << "[attempt" << m_playbackAttemptId
                                << "] setHDR(true) begin";
        hdrEnabled = m_displayManager->setHDR(true);
        qCInfo(lcPlaybackTrace) << "[attempt" << m_playbackAttemptId
                                << "] setHDR(true) result=" << hdrEnabled;
    } else if (m_config->getEnableHDR() && !m_contentIsHDR) {
        qDebug() << "PlayerController: HDR toggle enabled but content is SDR, not switching display HDR";
    }

    applyFramerateMatchingAndStart();
}

void PlayerController::applyFramerateMatchingAndStart()
{
    // Defensive checks: the deferred HDR settle callback may fire after state changes.
    if (m_playbackState != Loading || m_pendingUrl.isEmpty()) {
        qCWarning(lcPlayback) << "PlayerController: applyFramerateMatchingAndStart called in invalid state, ignoring";
        qCWarning(lcPlaybackTrace) << "[attempt" << m_playbackAttemptId
                                   << "] apply-framerate-and-start skipped"
                                   << "state=" << stateToString(m_playbackState)
                                   << "pendingUrlEmpty=" << m_pendingUrl.isEmpty();
        return;
    }

    qCInfo(lcPlaybackTrace) << "[attempt" << m_playbackAttemptId
                            << "] apply-framerate-and-start"
                            << "enableFramerateMatchSetting=" << m_config->getEnableFramerateMatching()
                            << "contentFramerate=" << m_contentFramerate
                            << "enableHDRSetting=" << m_config->getEnableHDR()
                            << "contentIsHDR=" << m_contentIsHDR;

    // Handle Display Settings - Framerate Matching
    if (m_config->getEnableFramerateMatching() && m_contentFramerate > 0) {
        // Pass the exact framerate to DisplayManager for precise matching
        // TVs like LG can match exact 23.976Hz, while others will use closest available (24Hz)
        qDebug() << "PlayerController: Content framerate:" << m_contentFramerate
                 << "-> attempting exact refresh rate match";
        
        if (m_displayManager->setRefreshRate(m_contentFramerate)) {
            qDebug() << "PlayerController: Successfully set display refresh rate for framerate" << m_contentFramerate;
            qCInfo(lcPlaybackTrace) << "[attempt" << m_playbackAttemptId
                                    << "] refresh-rate switch success";

            if (m_displayManager->hasActiveRefreshRateOverride()) {
                // Wait for display to stabilize after an actual refresh rate change.
                int delaySeconds = m_config->getFramerateMatchDelay();
                if (delaySeconds > 0) {
                    qDebug() << "PlayerController: Scheduling mpv start in" << delaySeconds << "seconds for display to stabilize";
                    m_startDelayTimer->start(delaySeconds * 1000);
                } else {
                    initiateMpvStart();
                }
            } else {
                // No mode switch happened (already compatible), so start immediately.
                initiateMpvStart();
            }
            return;  // Important: return early to avoid duplicate startMpv calls
        } else {
            qWarning() << "PlayerController: Failed to set display refresh rate for framerate" << m_contentFramerate;
            qCWarning(lcPlaybackTrace) << "[attempt" << m_playbackAttemptId
                                       << "] refresh-rate switch failed";
        }
    } else if (m_config->getEnableFramerateMatching()) {
        qDebug() << "PlayerController: Framerate matching enabled but no framerate info available (framerate:" << m_contentFramerate << ")";
        qCInfo(lcPlaybackTrace) << "[attempt" << m_playbackAttemptId
                                << "] framerate-matching enabled but no content framerate";
    }
    
    // No framerate matching or delay needed - start immediately
    initiateMpvStart();
}

void PlayerController::initiateMpvStart()
{
    // Defensive checks: ensure state is valid for starting mpv
    // This prevents race conditions where deferred timer fires after state changes
    if (m_playbackState != Loading) {
        qCWarning(lcPlayback) << "PlayerController: initiateMpvStart called but not in Loading state (state="
                              << stateToString(m_playbackState) << "), ignoring";
        qCWarning(lcPlaybackTrace) << "[attempt" << m_playbackAttemptId
                                   << "] initiate-mpv skipped: invalid state"
                                   << stateToString(m_playbackState);
        return;
    }
    
    if (m_pendingUrl.isEmpty()) {
        qCWarning(lcPlayback) << "PlayerController: initiateMpvStart called but no pending URL, ignoring";
        qCWarning(lcPlaybackTrace) << "[attempt" << m_playbackAttemptId
                                   << "] initiate-mpv skipped: pending URL empty";
        return;
    }
    
    if (m_playerBackend->isRunning()) {
        qCWarning(lcPlayback) << "PlayerController: initiateMpvStart called but mpv already running, ignoring";
        qCWarning(lcPlaybackTrace) << "[attempt" << m_playbackAttemptId
                                   << "] initiate-mpv skipped: backend already running";
        return;
    }
    
    // Resolve the MPV profile for this item
    QString profileName = m_config->resolveProfileForItem(m_currentLibraryId, m_currentSeriesId);
    qDebug() << "PlayerController: Using MPV profile:" << profileName
             << "for library:" << m_currentLibraryId
             << "series:" << m_currentSeriesId;
    qCInfo(lcPlaybackTrace) << "[attempt" << m_playbackAttemptId
                            << "] initiate-mpv"
                            << "profile=" << profileName
                            << "backend=" << m_playerBackend->backendName();
    
    // Get the args from the profile (includes HDR overrides if enabled)
    QStringList profileArgs = m_config->getMpvArgsForProfile(profileName, m_contentIsHDR);
    
    // Build final args: Bloom config args + profile args
    QStringList finalArgs;
    finalArgs << ConfigManager::getMpvConfigArgs();  // mpv.conf, input.conf, scripts
    finalArgs << profileArgs;                        // Profile-specific args

#if defined(Q_OS_LINUX)
    if (m_playerBackend->backendName() == QStringLiteral("linux-libmpv-opengl")) {
        // Embedded libmpv render path should avoid external-process mpv config/scripts
        // and profile switches that can override render-critical options.
        QStringList filteredArgs;
        filteredArgs.reserve(finalArgs.size());

        auto optionNameForArg = [](const QString &arg) -> QString {
            if (!arg.startsWith("--")) return QString();
            const QString option = arg.mid(2);
            const int equalsIndex = option.indexOf('=');
            return equalsIndex >= 0 ? option.left(equalsIndex) : option;
        };

        auto shouldSkipEmbeddedArg = [](const QString &name) -> bool {
            if (name == QStringLiteral("config-dir")
                || name == QStringLiteral("config")
                || name == QStringLiteral("input-conf")
                || name == QStringLiteral("include")
                || name == QStringLiteral("script")
                || name == QStringLiteral("script-opts")
                || name == QStringLiteral("scripts")
                || name == QStringLiteral("osc")
                || name == QStringLiteral("no-osc")
                || name == QStringLiteral("profile")
                || name == QStringLiteral("fullscreen")
                || name == QStringLiteral("wid")
                || name == QStringLiteral("input-ipc-server")
                || name == QStringLiteral("idle")
                || name == QStringLiteral("vo")
                || name == QStringLiteral("hwdec")
                || name == QStringLiteral("gpu-context")
                || name == QStringLiteral("gpu-api")) {
                return true;
            }

            return name.startsWith(QStringLiteral("vulkan-"))
                || name.startsWith(QStringLiteral("opengl-"))
                || name.startsWith(QStringLiteral("wayland-"))
                || name.startsWith(QStringLiteral("x11-"));
        };

        for (const QString &arg : std::as_const(finalArgs)) {
            const QString name = optionNameForArg(arg);
            if (!name.isEmpty() && shouldSkipEmbeddedArg(name)) {
                continue;
            }
            filteredArgs << arg;
        }

        qDebug() << "PlayerController: Embedded linux backend filtered mpv args:"
                 << "before=" << finalArgs.size()
                 << "after=" << filteredArgs.size();
        finalArgs = filteredArgs;
    }
#endif

    qDebug() << "PlayerController: Final mpv args:" << finalArgs;
    
    m_playerBackend->startMpv(m_mpvBin, finalArgs, m_pendingUrl);
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

QString PlayerController::inferPlayMethod(const QString &url)
{
    const QUrl parsedUrl(url);
    const QString path = parsedUrl.path(QUrl::FullyDecoded).toLower();
    const QStringList pathSegments = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);

    if (pathSegments.contains(QStringLiteral("transcode"))
        || pathSegments.contains(QStringLiteral("hls"))
        || path.endsWith(QStringLiteral("master.m3u8"))) {
        return QStringLiteral("Transcode");
    }
    if (pathSegments.contains(QStringLiteral("stream"))) {
        return QStringLiteral("DirectStream");
    }

    const QUrlQuery query(parsedUrl);
    const auto queryItems = query.queryItems(QUrl::FullyDecoded);
    for (const auto &item : queryItems) {
        if (item.first.compare(QStringLiteral("static"), Qt::CaseInsensitive) == 0
            && (item.second.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0
                || item.second == QStringLiteral("1"))) {
            return QStringLiteral("DirectPlay");
        }
    }

    return QStringLiteral("DirectPlay");
}

void PlayerController::updateSkipSegmentState()
{
    const bool wasInIntro = m_isInIntroSegment;
    const bool wasInOutro = m_isInOutroSegment;
    bool inIntro = false;
    bool inOutro = false;

    for (const auto &segment : std::as_const(m_currentSegments)) {
        if (segment.startTicks < 0 || segment.endTicks <= segment.startTicks) {
            continue;
        }

        const double startSeconds = segment.startSeconds();
        const double endSeconds = segment.endSeconds();
        const bool containsPosition = m_currentPosition >= startSeconds && m_currentPosition < endSeconds;
        if (!containsPosition) {
            continue;
        }

        if (segment.type == MediaSegmentType::Intro) {
            inIntro = true;
        } else if (segment.type == MediaSegmentType::Outro) {
            inOutro = true;
        }
    }

    if (m_isInIntroSegment == inIntro && m_isInOutroSegment == inOutro) {
        return;
    }

    m_isInIntroSegment = inIntro;
    m_isInOutroSegment = inOutro;

    const bool autoSkipAllowedNow = m_playbackState != Paused
                                    && m_playbackState != Idle
                                    && m_playbackState != Error;

    // Auto-skip only on first entry into intro/outro segment for this playback item.
    if (autoSkipAllowedNow
        && !wasInIntro
        && inIntro
        && !m_hasAutoSkippedIntroForCurrentItem
        && m_config->getAutoSkipIntro()) {
        m_hasAutoSkippedIntroForCurrentItem = true;
        skipIntro();
    } else if (autoSkipAllowedNow
               && !wasInOutro
               && inOutro
               && !m_hasAutoSkippedOutroForCurrentItem
               && m_config->getAutoSkipOutro()) {
        m_hasAutoSkippedOutroForCurrentItem = true;
        skipOutro();
    }

    emit skipSegmentsChanged();
}

bool PlayerController::seekToSegmentEnd(MediaSegmentType segmentType)
{
    for (const auto &segment : std::as_const(m_currentSegments)) {
        if (segment.type != segmentType || segment.endTicks <= 0) {
            continue;
        }

        const double endSeconds = segment.endSeconds();
        qDebug() << "PlayerController: Skipping segment type" << static_cast<int>(segmentType)
                 << "seeking to" << endSeconds;
        seek(endSeconds);
        return true;
    }

    return false;
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
        qDebug() << "PlayerController: Detected user mpv scripts in:" << ConfigManager::getMpvScriptsDir();
    }
}

// === OSC AND TRICKPLAY HANDLERS ===

void PlayerController::onScriptMessage(const QString &messageName, const QStringList &args)
{
    qDebug() << "PlayerController: Received script message:" << messageName << "args:" << args;
    
    if (messageName == "bloom-skip-intro") {
        if (!seekToSegmentEnd(MediaSegmentType::Intro)) {
            qDebug() << "PlayerController: No intro segment found to skip";
        }
        
    } else if (messageName == "bloom-skip-outro") {
        if (!seekToSegmentEnd(MediaSegmentType::Outro)) {
            qDebug() << "PlayerController: No outro segment found to skip";
        }
    }
    // Script-driven trickplay handlers were retired with the native overlay migration.
}

void PlayerController::setOverlayMetadata(const QString &title, const QString &subtitle, const QString &backdropUrl)
{
    const QString normalizedTitle = title.trimmed();
    const QString normalizedSubtitle = subtitle.trimmed();
    const QString normalizedBackdropUrl = backdropUrl.trimmed();
    if (m_overlayTitle == normalizedTitle
        && m_overlaySubtitle == normalizedSubtitle
        && m_overlayBackdropUrl == normalizedBackdropUrl) {
        return;
    }

    m_overlayTitle = normalizedTitle;
    m_overlaySubtitle = normalizedSubtitle;
    m_overlayBackdropUrl = normalizedBackdropUrl;
    emit overlayMetadataChanged();
}

void PlayerController::clearOverlayMetadata()
{
    if (m_overlayTitle.isEmpty() && m_overlaySubtitle.isEmpty() && m_overlayBackdropUrl.isEmpty()) {
        return;
    }

    m_overlayTitle.clear();
    m_overlaySubtitle.clear();
    m_overlayBackdropUrl.clear();
    emit overlayMetadataChanged();
}

void PlayerController::onMediaSegmentsLoaded(const QString &itemId, const QList<MediaSegmentInfo> &segments)
{
    if (itemId != m_currentItemId) {
        qDebug() << "PlayerController: Ignoring segments for different item:" << itemId;
        return;
    }
    
    qDebug() << "PlayerController: Received" << segments.size() << "segments for item:" << itemId;
    m_currentSegments = segments;
    updateSkipSegmentState();

    // Handle early intro/outro segments that can be loaded slightly after playback starts.
    // This keeps auto-skip reliable for intros that begin at/near 0s.
    if (m_playbackState != Paused && m_playbackState != Idle && m_playbackState != Error) {
        constexpr double kEarlySegmentGraceSeconds = 2.0;
        for (const auto &segment : segments) {
            const double startSeconds = segment.startSeconds();
            const double endSeconds = segment.endSeconds();
            if (endSeconds <= startSeconds) {
                continue;
            }

            if (segment.type == MediaSegmentType::Intro
                && !m_hasAutoSkippedIntroForCurrentItem
                && m_config->getAutoSkipIntro()
                && startSeconds <= kEarlySegmentGraceSeconds
                && m_currentPosition < endSeconds) {
                m_hasAutoSkippedIntroForCurrentItem = true;
                skipIntro();
                break;
            }

            if (segment.type == MediaSegmentType::Outro
                && !m_hasAutoSkippedOutroForCurrentItem
                && m_config->getAutoSkipOutro()
                && startSeconds <= kEarlySegmentGraceSeconds
                && m_currentPosition < endSeconds) {
                m_hasAutoSkippedOutroForCurrentItem = true;
                skipOutro();
                break;
            }
        }
    }
    
    // Segment metadata is kept in controller state for native overlay handling.
    for (const auto &segment : segments) {
        double startSeconds = static_cast<double>(segment.startTicks) / 10000000.0;
        double endSeconds = static_cast<double>(segment.endTicks) / 10000000.0;

        if (segment.type == MediaSegmentType::Intro) {
            qDebug() << "PlayerController: Intro segment:" << startSeconds << "->" << endSeconds;
        } else if (segment.type == MediaSegmentType::Outro) {
            qDebug() << "PlayerController: Outro segment:" << startSeconds << "->" << endSeconds;
        }
    }
}

void PlayerController::onTrickplayInfoLoaded(const QString &itemId, const QMap<int, TrickplayTileInfo> &trickplayInfo)
{
    if (itemId != m_currentItemId) {
        qDebug() << "PlayerController: Ignoring trickplay info for different item:" << itemId;
        return;
    }

    const bool wasTrickplayReady = m_hasTrickplayInfo;
    
    if (trickplayInfo.isEmpty()) {
        qDebug() << "PlayerController: No trickplay info available for item:" << itemId;
        m_hasTrickplayInfo = false;
        m_currentTrickplayInfo = TrickplayTileInfo{};
        m_trickplayBinaryPath.clear();
        m_currentTrickplayFrameIndex = -1;
        m_hasTrickplayPreviewPositionOverride = false;
        m_trickplayPreviewPositionOverrideSeconds = 0.0;
        clearTrickplayPreview();
        emit trickplayStateChanged();
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

    if (info.interval <= 0 || info.thumbnailCount <= 0 || info.width <= 0 || info.height <= 0) {
        qWarning() << "PlayerController: Ignoring invalid trickplay info for item:" << itemId
                   << "interval:" << info.interval
                   << "count:" << info.thumbnailCount
                   << "size:" << info.width << "x" << info.height;
        m_hasTrickplayInfo = false;
        m_currentTrickplayInfo = TrickplayTileInfo{};
        m_trickplayBinaryPath.clear();
        m_currentTrickplayFrameIndex = -1;
        m_hasTrickplayPreviewPositionOverride = false;
        m_trickplayPreviewPositionOverrideSeconds = 0.0;
        clearTrickplayPreview();
        emit trickplayStateChanged();
        return;
    }
    
    qDebug() << "PlayerController: Received trickplay info for item:" << itemId
             << "selected width:" << selectedWidth
             << "height:" << info.height
             << "interval:" << info.interval << "ms"
             << "tiles:" << info.tileWidth << "x" << info.tileHeight
             << "count:" << info.thumbnailCount;
    
    m_currentTrickplayInfo = info;
    m_hasTrickplayInfo = false;
    m_trickplayBinaryPath.clear();
    m_currentTrickplayFrameIndex = -1;
    m_hasTrickplayPreviewPositionOverride = false;
    m_trickplayPreviewPositionOverrideSeconds = 0.0;
    clearTrickplayPreview();
    if (wasTrickplayReady) {
        emit trickplayStateChanged();
    }

    if (!embeddedLinuxTrickplayAllowed(m_playerBackend)) {
        qCInfo(lcPlayback)
            << "Skipping trickplay processing for embedded linux libmpv backend"
            << "(set BLOOM_LINUX_LIBMPV_ENABLE_TRICKPLAY=1 to override)";
        return;
    }

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

    if (count <= 0 || intervalMs <= 0 || width <= 0 || height <= 0 || filePath.isEmpty() || !QFile::exists(filePath)) {
        qWarning() << "PlayerController: Trickplay processing result is invalid, disabling trickplay for item:" << itemId;
        m_hasTrickplayInfo = false;
        m_trickplayBinaryPath.clear();
        m_currentTrickplayFrameIndex = -1;
        m_hasTrickplayPreviewPositionOverride = false;
        m_trickplayPreviewPositionOverrideSeconds = 0.0;
        clearTrickplayPreview();
        emit trickplayStateChanged();
        return;
    }

    m_currentTrickplayInfo.thumbnailCount = count;
    m_currentTrickplayInfo.interval = intervalMs;
    m_currentTrickplayInfo.width = width;
    m_currentTrickplayInfo.height = height;
    m_trickplayBinaryPath = filePath;
    m_hasTrickplayInfo = true;
    m_currentTrickplayFrameIndex = -1;
    updateTrickplayPreviewForPosition(
        m_hasTrickplayPreviewPositionOverride ? m_trickplayPreviewPositionOverrideSeconds : m_currentPosition
    );
    emit trickplayStateChanged();
}

void PlayerController::onTrickplayProcessingFailed(const QString &itemId, const QString &error)
{
    if (itemId != m_currentItemId) {
        return;
    }
    
    qWarning() << "PlayerController: Trickplay processing failed for item:" << itemId << "error:" << error;
    // Trickplay thumbnails won't be available, but playback continues normally
    m_hasTrickplayInfo = false;
    m_trickplayBinaryPath.clear();
    m_currentTrickplayFrameIndex = -1;
    m_hasTrickplayPreviewPositionOverride = false;
    m_trickplayPreviewPositionOverrideSeconds = 0.0;
    clearTrickplayPreview();
    emit trickplayStateChanged();
}

void PlayerController::setTrickplayPreviewPositionSeconds(double seconds)
{
    m_hasTrickplayPreviewPositionOverride = true;
    m_trickplayPreviewPositionOverrideSeconds = qMax(0.0, seconds);
    if (m_hasTrickplayInfo) {
        updateTrickplayPreviewForPosition(m_trickplayPreviewPositionOverrideSeconds);
    }
}

void PlayerController::clearTrickplayPreviewPositionOverride()
{
    if (!m_hasTrickplayPreviewPositionOverride) {
        return;
    }

    m_hasTrickplayPreviewPositionOverride = false;
    m_trickplayPreviewPositionOverrideSeconds = 0.0;
    if (m_hasTrickplayInfo) {
        updateTrickplayPreviewForPosition(m_currentPosition);
    }
}

void PlayerController::updateTrickplayPreviewForPosition(double seconds)
{
    if (!m_hasTrickplayInfo
        || m_trickplayBinaryPath.isEmpty()
        || m_currentTrickplayInfo.interval <= 0
        || m_currentTrickplayInfo.thumbnailCount <= 0
        || m_currentTrickplayInfo.width <= 0
        || m_currentTrickplayInfo.height <= 0) {
        clearTrickplayPreview();
        return;
    }

    if (!QFile::exists(m_trickplayBinaryPath)) {
        m_hasTrickplayInfo = false;
        m_trickplayBinaryPath.clear();
        m_currentTrickplayFrameIndex = -1;
        m_hasTrickplayPreviewPositionOverride = false;
        m_trickplayPreviewPositionOverrideSeconds = 0.0;
        clearTrickplayPreview();
        emit trickplayStateChanged();
        return;
    }

    const int requestedMs = qMax(0, static_cast<int>(seconds * 1000.0));
    int frameIndex = requestedMs / m_currentTrickplayInfo.interval;
    frameIndex = qBound(0, frameIndex, m_currentTrickplayInfo.thumbnailCount - 1);
    if (frameIndex == m_currentTrickplayFrameIndex && !m_trickplayPreviewUrl.isEmpty()) {
        return;
    }

    const QString previewUrl = buildTrickplayPreviewDataUrl(
        m_trickplayBinaryPath,
        frameIndex,
        m_currentTrickplayInfo.width,
        m_currentTrickplayInfo.height
    );

    if (previewUrl.isEmpty()) {
        qWarning() << "PlayerController: Failed to load trickplay frame" << frameIndex << "for item" << m_currentItemId;
        m_hasTrickplayInfo = false;
        m_trickplayBinaryPath.clear();
        m_currentTrickplayFrameIndex = -1;
        m_hasTrickplayPreviewPositionOverride = false;
        m_trickplayPreviewPositionOverrideSeconds = 0.0;
        clearTrickplayPreview();
        emit trickplayStateChanged();
        return;
    }

    m_currentTrickplayFrameIndex = frameIndex;
    if (m_trickplayPreviewUrl != previewUrl) {
        m_trickplayPreviewUrl = previewUrl;
        emit trickplayPreviewChanged();
    }
}

void PlayerController::clearTrickplayPreview()
{
    if (m_trickplayPreviewUrl.isEmpty()) {
        return;
    }
    m_trickplayPreviewUrl.clear();
    emit trickplayPreviewChanged();
}

QString PlayerController::buildTrickplayPreviewDataUrl(const QString &binaryPath, int frameIndex, int width, int height)
{
    if (binaryPath.isEmpty() || frameIndex < 0 || width <= 0 || height <= 0) {
        return QString();
    }

    QFile file(binaryPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return QString();
    }

    const qint64 frameSize = static_cast<qint64>(width) * height * 4;
    if (frameSize <= 0) {
        return QString();
    }

    const qint64 offset = frameSize * frameIndex;
    if (!file.seek(offset)) {
        return QString();
    }

    const QByteArray frame = file.read(frameSize);
    if (frame.size() != frameSize) {
        return QString();
    }

    // TrickplayProcessor stores frame data in BGRA byte order.
    QImage image(reinterpret_cast<const uchar *>(frame.constData()), width, height, width * 4, QImage::Format_ARGB32);
    QImage detached = image.copy();

    QByteArray pngBytes;
    QBuffer pngBuffer(&pngBytes);
    if (!pngBuffer.open(QIODevice::WriteOnly) || !detached.save(&pngBuffer, "PNG")) {
        return QString();
    }

    return QStringLiteral("data:image/png;base64,%1").arg(QString::fromLatin1(pngBytes.toBase64()));
}
