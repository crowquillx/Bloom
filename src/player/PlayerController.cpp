#include "PlayerController.h"
#include "PlaybackPolicy.h"
#include "AudioPolicy.h"
#include "audio/AudioOutputService.h"
#include <QDateTime>
#include "TrickplayProcessor.h"
#if !defined(BLOOM_TESTING)
#include "backend/ExternalMpvBackend.h"
#endif
#include "../network/PlaybackService.h"
#include "../network/LibraryService.h"
#include "../network/AuthenticationService.h"
#include "../network/Types.h"
#include <QFile>
#include <QFileInfo>
#include <QBuffer>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QImage>
#include <QCoreApplication>
#include <QDir>
#include <QDebug>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QMouseEvent>
#include <QEnterEvent>
#include <QLineF>
#include <QEvent>
#include <QRectF>
#include <QUuid>
#include <QtGlobal>
#include <QUrl>
#include <QSet>
#include <QStringList>
#include <utility>
#include <QThread>
#include <atomic>
#include <algorithm>
#include "../utils/BloomLogging.h"
#include "../utils/MpvArgFilter.h"

namespace {
namespace PlayerPolicy = Bloom::PlaybackPolicy;

QString episodeSeasonId(const QVariantMap &episode)
{
    const QString seasonId = episode.value(QStringLiteral("seasonId")).toString();
    return seasonId.isEmpty() ? episode.value(QStringLiteral("parentId")).toString() : seasonId;
}

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

static QString formatMpvDisplayFps(double fps)
{
    QString value = QString::number(fps, 'f', 6);
    while (value.contains(QLatin1Char('.'))
           && (value.endsWith(QLatin1Char('0')) || value.endsWith(QLatin1Char('.')))) {
        value.chop(1);
    }
    return value;
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
    void appendUrlsToPlaylist(const QStringList &) override {}
    void stopMpv() override {}
    bool isRunning() const override { return false; }
    void sendCommand(const QStringList &) override {}
    void sendVariantCommand(const QVariantList &) override {}
    bool supportsEmbeddedVideo() const override { return false; }
    bool attachVideoTarget(QObject *) override { return false; }
    void detachVideoTarget(QObject *) override {}
    void setVideoViewport(const QRectF &) override {}
};

QVariantList mediaStreams(const QVariantMap &mediaSource)
{
    return mediaSource.value(QStringLiteral("mediaStreams")).toList();
}

int parsePinnedTrackId(const QString &rawTrackId)
{
    bool ok = false;
    const int trackId = rawTrackId.trimmed().toInt(&ok);
    return ok ? trackId : -1;
}

QVariantList mediaStreamsForType(const QVariantMap &mediaSource, const QString &streamType)
{
    QVariantList result;
    const QVariantList streams = mediaStreams(mediaSource);
    for (const QVariant &streamVariant : streams) {
        const QVariantMap stream = streamVariant.toMap();
        if (stream.value(QStringLiteral("type")).toString() == streamType) {
            result.append(stream);
        }
    }
    return result;
}

QString trackTitleForStream(const QVariantMap &stream)
{
    const QString displayTitle = stream.value(QStringLiteral("displayTitle")).toString().trimmed();
    if (!displayTitle.isEmpty()) {
        return displayTitle;
    }

    const QString title = stream.value(QStringLiteral("title")).toString().trimmed();
    if (!title.isEmpty()) {
        return title;
    }

    QStringList parts;
    const QString language = stream.value(QStringLiteral("language")).toString().trimmed();
    if (!language.isEmpty()) {
        parts.append(language);
    }

    const QString codec = stream.value(QStringLiteral("codec")).toString().trimmed();
    if (!codec.isEmpty()) {
        parts.append(codec.toUpper());
    }

    const int channels = stream.value(QStringLiteral("channels")).toInt(0);
    if (channels > 0) {
        parts.append(QString::number(channels) + QStringLiteral("ch"));
    }

    if (parts.isEmpty()) {
        return QStringLiteral("Track %1").arg(stream.value(QStringLiteral("index"), -1).toInt());
    }
    return parts.join(QStringLiteral(" • "));
}

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
    , m_autoplayPlaybackInfoTimeoutTimer(new QTimer(this))
    , m_audioDeviceReloadTimer(new QTimer(this))
    , m_recoveryTimer(new QTimer(this))
{
    if (!m_playerBackend) {
        qCWarning(lcPlayback) << "PlayerController initialized without backend; falling back to null backend";
        m_playerBackend = new NullPlayerBackend(this);
    }

    // Setup state machine transitions
    setupStateMachine();

    // Connect to player backend signals
    connectBackendSignals(m_playerBackend);

    m_sleepTimerTickTimer = new QTimer(this);
    m_sleepTimerTickTimer->setInterval(1000);
    connect(m_sleepTimerTickTimer, &QTimer::timeout, this, &PlayerController::onSleepTimerTick);

    m_afterMovieRestoreTimer = new QTimer(this);
    m_afterMovieRestoreTimer->setSingleShot(true);
    m_afterMovieRestoreTimer->setInterval(kAfterMovieRestoreTimeoutMs);
    connect(m_afterMovieRestoreTimer, &QTimer::timeout, this, [this]() {
        qCInfo(lcPlaybackTrace) << "[attempt" << m_playbackAttemptId
                                << "] after-movie display restore time limit reached";
        // Only running while the restore is parked for an after-movie screen.
        maybeStartDeferredPostPlaybackDisplayRestore();
    });
    
    // Connect to LibraryService for autoplay next episode
    connect(m_libraryService, &LibraryService::canonicalNextUnplayedEpisodeLoaded,
            this, &PlayerController::onNextEpisodeLoaded);
    connect(m_libraryService, &LibraryService::nextUnplayedEpisodeFailed,
            this, &PlayerController::onNextEpisodeFailed);
    connect(m_libraryService, &LibraryService::itemLibraryResolved,
            this, &PlayerController::onItemLibraryResolved);
    connect(m_libraryService, &LibraryService::itemLibraryResolutionFailed,
            this, &PlayerController::onItemLibraryResolutionFailed);
    connect(m_libraryService, &LibraryService::canonicalChaptersLoaded,
            this, &PlayerController::onChaptersLoaded);
    connect(m_libraryService, &LibraryService::chaptersFailed,
            this, &PlayerController::onChaptersFailed);
    connect(m_libraryService, &LibraryService::canonicalItemsLoadedForConnection,
            this, &PlayerController::onAdjacentEpisodeItemsLoaded);
    // Version and episode navigation state is scoped to the item it was built for.
    connect(this, &PlayerController::currentItemIdChanged,
            this, &PlayerController::playbackVersionsChanged);
    connect(this, &PlayerController::currentItemIdChanged,
            this, &PlayerController::adjacentEpisodesChanged);

    // Connect to PlaybackService for metadata and asynchronous provider plans.
    connect(m_playbackService, &PlaybackService::playbackInfoLoaded,
            this, &PlayerController::onPlaybackInfoLoaded);
    connect(m_playbackService, &PlaybackService::playbackInfoLoadedForRequest,
            this, &PlayerController::onPlaybackInfoLoadedForRequest);
    connect(m_playbackService, &PlaybackService::playbackInfoFailedForRequest,
            this, &PlayerController::onPlaybackInfoFailedForRequest);
    connect(m_playbackService, &PlaybackService::playbackDescriptorLoadedForRequest,
            this, &PlayerController::onPlaybackDescriptorLoadedForRequest);
    connect(m_playbackService, &PlaybackService::playbackDescriptorFailedForRequest,
            this, &PlayerController::onPlaybackDescriptorFailedForRequest);
    connect(m_playbackService, &PlaybackService::playbackReplannedForRequest,
            this, &PlayerController::onPlaybackReplannedForRequest);
    connect(m_playbackService, &PlaybackService::playbackReplanFailedForRequest,
            this, &PlayerController::onPlaybackReplanFailedForRequest);
    connect(m_playbackService, &PlaybackService::additionalPartsLoaded,
            this, &PlayerController::onAdditionalPartsLoaded);
    connect(m_playbackService, &PlaybackService::additionalPartsLoadedForRequest,
            this, &PlayerController::onAdditionalPartsLoadedForRequest);
    connect(m_playbackService, &PlaybackService::additionalPartsFailedForRequest,
            this, &PlayerController::onAdditionalPartsFailedForRequest);
    connect(m_playbackService, &PlaybackService::errorOccurred,
            this, &PlayerController::onPlaybackServiceErrorOccurred);
    connect(m_playbackService, &PlaybackService::networkError,
            this, &PlayerController::onPlaybackServiceNetworkError);
    connect(m_playbackService, &PlaybackService::playbackSessionLost,
            this, &PlayerController::onPlaybackSessionLost);
    connect(m_playbackService, &PlaybackService::mediaSegmentsLoaded,
            this, &PlayerController::onMediaSegmentsLoaded);
    connect(m_playbackService, &PlaybackService::trickplayInfoLoaded,
            this, &PlayerController::onTrickplayInfoLoaded);
    
    // Connect TrickplayProcessor signals
    connect(m_trickplayProcessor, &TrickplayProcessor::processingComplete,
            this, &PlayerController::onTrickplayProcessingComplete);
    connect(m_trickplayProcessor, &TrickplayProcessor::processingFailed,
            this, &PlayerController::onTrickplayProcessingFailed);
    m_localSeekPreview = new LocalSeekPreview(this);
    connect(m_localSeekPreview, &LocalSeekPreview::framesChanged, this, [this] {
        ++m_trickplayFrameRevision;
        emit trickplayFramesChanged();
        if (m_useLocalTrickplay) {
            updateTrickplayPreviewForPosition(m_hasTrickplayPreviewPositionOverride
                ? m_trickplayPreviewPositionOverrideSeconds : m_currentPosition);
        }
    });
    connect(m_localSeekPreview, &LocalSeekPreview::unavailable, this, [this] {
        m_useLocalTrickplay = false;
        clearTrickplayPreview();
        emit trickplayStateChanged();
    });
    m_localSeekPreview->setSuspended(true);
    m_localSeekPreviewResumeTimer = new QTimer(this);
    m_localSeekPreviewResumeTimer->setSingleShot(true);
    m_localSeekPreviewResumeTimer->setInterval(kLocalSeekPreviewSettleMs);
    connect(m_localSeekPreviewResumeTimer, &QTimer::timeout, this, [this] {
        if (m_playbackState == Playing || m_playbackState == Paused) {
            m_localSeekPreview->setSuspended(false);
        }
    });
    connect(this, &PlayerController::playbackStateChanged,
            this, &PlayerController::updateLocalSeekPreviewSuspension);
    
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

    // Debounced re-application of the audio output device after a hotplug event.
    m_audioDeviceReloadTimer->setSingleShot(true);
    m_audioDeviceReloadTimer->setInterval(kAudioDeviceReloadDebounceMs);
    connect(m_audioDeviceReloadTimer, &QTimer::timeout,
            this, &PlayerController::onAudioReloadTimeout);

    // Idle enumeration keeps the output selector current without playback.
    m_audioDeviceRefreshTimer = new QTimer(this);
    m_audioDeviceRefreshTimer->setSingleShot(true);
    m_audioDeviceRefreshTimer->setInterval(kAudioDeviceReloadDebounceMs);
    connect(m_audioDeviceRefreshTimer, &QTimer::timeout, this, [this] {
        if (m_playerBackend && !m_playerBackend->isRunning()) m_playerBackend->refreshAudioDevices();
    });

    m_autoplayPlaybackInfoTimeoutTimer->setSingleShot(true);
    m_autoplayPlaybackInfoTimeoutTimer->setInterval(kAutoplayPlaybackInfoTimeoutMs);
    connect(m_autoplayPlaybackInfoTimeoutTimer, &QTimer::timeout,
            this, &PlayerController::onAutoplayPlaybackInfoTimeout);

    m_trackPreferenceWriteTimer = new QTimer(this);
    m_trackPreferenceWriteTimer->setSingleShot(true);
    m_trackPreferenceWriteTimer->setInterval(kTrackPreferenceWriteDelayMs);
    connect(m_trackPreferenceWriteTimer, &QTimer::timeout,
            this, &PlayerController::flushTrackPreferenceWrites);
    if (m_config) {
        connect(m_config, &ConfigManager::useServerTrackPreferencesChanged,
                this, &PlayerController::dropStaleTrackPreferenceWrites);
    }
    if (m_playbackService) {
        connect(m_playbackService, &PlaybackService::trackPreferenceIdentityChanged,
                this, &PlayerController::dropStaleTrackPreferenceWrites);
    }

    // Recovery timer for transient server loss
    m_recoveryTimer->setSingleShot(true);
    m_recoveryTimer->setInterval(kRecoveryPingIntervalMs);
    connect(m_recoveryTimer, &QTimer::timeout,
            this, &PlayerController::onRecoveryTick);

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

    connect(m_playbackService->audioOutput(), &Bloom::AudioOutputService::capabilitiesChanged,
            this, [this] {
                m_forcePcmAudio = false;
                // Reopens only if the plan or endpoint changed (onAudioReloadTimeout).
                if (m_playerBackend->isRunning()) m_audioDeviceReloadTimer->start();
                updateAudioOutputStatus();
            });
    connect(m_playbackService->audioOutput(), &Bloom::AudioOutputService::devicesChanged,
            this, &PlayerController::updateAudioOutputStatus);
    connect(m_playbackService->audioOutput(), &Bloom::AudioOutputService::systemOutputsChanged,
            this, [this] {
                if (!m_playerBackend->isRunning()) m_audioDeviceRefreshTimer->start();
            });
    connect(m_config, &ConfigManager::audioExclusiveModeChanged, this, [this] {
        m_exclusiveFallback = false;
        updateAudioOutputStatus();
        if (m_playerBackend && m_playerBackend->isRunning()) {
            m_audioDeviceReloadTimer->stop();
            applyAudioOutputDevice();
        }
    });
    auto *audioVerificationTimer = new QTimer(this);
    audioVerificationTimer->setInterval(3000);
    connect(audioVerificationTimer, &QTimer::timeout, this, [this] {
        if (m_playerBackend->isRunning())
            m_playerBackend->requestAudioDiagnostics(QStringLiteral("audio|%1").arg(m_audioPlanGeneration));
    });
    audioVerificationTimer->start();

    // Re-apply the chosen audio output device immediately when the user changes it.
    connect(m_config, &ConfigManager::audioOutputDeviceChanged,
            this, [this]() {
                m_exclusiveFallback = false;
                m_reportedMissingAudioDevice.clear();
                updateAudioOutputStatus();
                if (m_playerBackend && m_playerBackend->isRunning()) {
                    m_audioDeviceReloadTimer->stop();
                    applyAudioOutputDevice();
                }
            });
    
    loadConfig();
    // Non-blocking: external mpv is probed asynchronously, embedded libmpv
    // reports its linked runtime.
    m_playerBackend->prepareRuntime(m_mpvBin);
    // Resolve the saved output against a real device list before first playback.
    QTimer::singleShot(0, this, [this] {
        if (!m_playerBackend->isRunning()) m_playerBackend->refreshAudioDevices();
    });
    updateAudioOutputStatus();

    if (m_config) {
        m_volume = m_config->getPlaybackVolume();
        m_muted = m_config->getPlaybackMuted();
    }
}

int PlayerController::subtitleDelayStepMs() const
{
    return 1;
}

PlayerController::~PlayerController()
{
    // Cleanup timers
    m_loadingTimeoutTimer->stop();
    m_bufferingTimeoutTimer->stop();
    m_progressReportTimer->stop();
    m_volumePersistTimer->stop();
    m_startDelayTimer->stop();
    m_autoplayPlaybackInfoTimeoutTimer->stop();
    m_trackPreferenceWriteTimer->stop();
    // Shutting down: a write still waiting could not finish, so it is
    // dropped. The pick itself is saved locally already.
    if (!m_pendingTrackPreferenceWrites.isEmpty()) {
        qCInfo(lcPlayback) << "Dropping" << m_pendingTrackPreferenceWrites.size()
                           << "unsent track preference write(s) at shutdown";
        m_pendingTrackPreferenceWrites.clear();
    }
    m_isRecovering = false;
    m_recoveryTimer->stop();
    if (m_recoveryPing) {
        m_recoveryPing->cancel();
        m_recoveryPing.clear();
    }
    cancelPendingDisplayRestore();
    resetTerminalTransitionState(true);
}

void PlayerController::connectBackendSignals(IPlayerBackend *backend)
{
    if (!backend) {
        return;
    }

    connect(backend, &IPlayerBackend::stateChanged,
            this, &PlayerController::onProcessStateChanged);
    connect(backend, &IPlayerBackend::runtimeInfoChanged,
            this, &PlayerController::onBackendRuntimeInfoChanged);
    connect(backend, &IPlayerBackend::playbackDiagnosticsAvailable,
            this, &PlayerController::onPlaybackDiagnosticsAvailable);
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
    connect(backend, &IPlayerBackend::fileLoaded,
            this, &PlayerController::onBackendFileLoaded);
    connect(backend, &IPlayerBackend::playlistPositionChanged,
            this, &PlayerController::onPlaylistPositionChanged);
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

    connect(backend, &IPlayerBackend::audioTrackChanged,
            this, &PlayerController::syncBackendAudioTrack);
    connect(backend, &IPlayerBackend::subtitleTrackChanged,
            this, &PlayerController::syncBackendSubtitleTrack);
    connect(backend, &IPlayerBackend::audioDeviceListChanged,
            this, &PlayerController::onAudioDeviceListChanged);

    connect(backend, &IPlayerBackend::scriptMessage,
            this, &PlayerController::onScriptMessage);
    connect(backend, &IPlayerBackend::cacheEndChanged,
            this, &PlayerController::onCacheEndChanged);
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

    m_backendFallbackSource = m_playerBackend;
    if (m_playerBackend->isRunning()) {
        m_playerBackend->stopMpv();
    }
    if (!m_backendFallbackSource || !m_backendFallbackSource->isRunning()) {
        queueBackendFallbackFinalization();
    }

    return true;
}

void PlayerController::queueBackendFallbackFinalization()
{
    if (!m_backendFallbackSource || m_backendFallbackFinalizationQueued) {
        return;
    }
    m_backendFallbackFinalizationQueued = true;
    const QPointer<IPlayerBackend> source = m_backendFallbackSource;
    QMetaObject::invokeMethod(this, [this, source]() {
        m_backendFallbackFinalizationQueued = false;
        if (!source || source != m_backendFallbackSource
            || source != m_playerBackend || source->isRunning()) {
            return;
        }
        finalizeBackendFallback();
    }, Qt::QueuedConnection);
}

void PlayerController::finalizeBackendFallback()
{
    IPlayerBackend *source = m_backendFallbackSource;
    if (!source || source != m_playerBackend || source->isRunning()) {
        return;
    }
    QObject::disconnect(source, nullptr, this, nullptr);
    m_backendFallbackSource.clear();

#if defined(BLOOM_TESTING)
    m_ownedBackend = std::make_unique<NullPlayerBackend>(this);
#else
    m_ownedBackend = std::make_unique<ExternalMpvBackend>(this);
#endif

    m_playerBackend = m_ownedBackend.get();
    connectBackendSignals(m_playerBackend);
    m_playerBackend->prepareRuntime(m_mpvBin);
    emit supportsEmbeddedVideoChanged();

    if (m_playbackState == Loading && !m_pendingUrl.isEmpty()) {
        qCInfo(lcPlayback) << "Retrying current media with external-mpv-ipc fallback backend";
        initiateMpvStart();
    }
}

void PlayerController::setupStateMachine()
{
    // Define state transitions: (CurrentState, Event) → NextState
    
    // From Idle
    m_transitions[{Idle, Event::Play}] = Loading;
    m_transitions[{Idle, Event::ErrorOccurred}] = Error;
    
    // From Loading
    m_transitions[{Loading, Event::LoadComplete}] = Buffering;
    m_transitions[{Loading, Event::ErrorOccurred}] = Error;
    m_transitions[{Loading, Event::Stop}] = Idle;
    m_transitions[{Loading, Event::PlaybackEnd}] = Idle;
    
    // From Buffering
    m_transitions[{Buffering, Event::BufferComplete}] = Playing;
    m_transitions[{Buffering, Event::ErrorOccurred}] = Error;
    m_transitions[{Buffering, Event::Stop}] = Idle;
    m_transitions[{Buffering, Event::Pause}] = Paused;
    m_transitions[{Buffering, Event::PlaybackEnd}] = Idle;
    
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
    m_transitions[{Paused, Event::PlaybackEnd}] = Idle;
    
    // From Error
    m_transitions[{Error, Event::Recover}] = Idle;
    m_transitions[{Error, Event::Play}] = Loading;  // Retry
    m_transitions[{Error, Event::Stop}] = Idle;
    
    qCDebug(lcPlayback) << "PlayerController: State machine initialized with" << m_transitions.size() << "transitions";
}

bool PlayerController::processEvent(Event event)
{
    StateTransition transition{m_playbackState, event};
    qCInfo(lcPlaybackTrace) << "[attempt" << m_playbackAttemptId << "] event"
                            << eventToString(event)
                            << "state=" << stateToString(m_playbackState);
    
    if (!m_transitions.contains(transition)) {
        qCWarning(lcPlayback) << "PlayerController: Invalid transition from" 
                   << stateToString(m_playbackState) << "on event" << eventToString(event);
        qCWarning(lcPlaybackTrace) << "[attempt" << m_playbackAttemptId
                                   << "] invalid-transition"
                                   << "state=" << stateToString(m_playbackState)
                                   << "event=" << eventToString(event);
        return false;
    }
    
    PlaybackState newState = m_transitions[transition];
    qCDebug(lcPlayback) << "PlayerController: Transition" 
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
    m_progressReportTimer->stop();
}

void PlayerController::onExitErrorState()
{
    setErrorMessage(QString());
    m_lastErrorWasNetworkRecoverable = false;
    cancelRecovery();
}

// === STATE ENTRY HANDLERS ===

void PlayerController::onEnterIdleState()
{
    const bool needsHdrRestore = m_displayManager != nullptr && m_displayManager->needsHdrRestore();
    const bool needsRefreshRestore = m_displayManager != nullptr && m_displayManager->needsRefreshRestore();

    qCInfo(lcPlayback) << "Entering Idle state (playback ended)";
    qCInfo(lcPlaybackTrace) << "[attempt" << m_playbackAttemptId
                            << "] enter-idle"
                            << "itemId=" << m_currentItemId
                            << "contentIsHDR=" << m_contentIsHDR
                            << "contentFramerate=" << m_contentFramerate;

    enterIdleStateImmediate();
    // Up Next and the after-movie screen keep the display as playback left
    // it until the user leaves them or starts the next title.
    if (m_awaitingNextEpisodeResolution || !m_pendingAfterMovie.isEmpty()) {
        cancelPendingDisplayRestore(false);
        m_deferredPostPlaybackDisplayRestorePending = needsHdrRestore || needsRefreshRestore;
        m_deferredPostPlaybackNeedsHdrRestore = needsHdrRestore;
        m_deferredPostPlaybackNeedsRefreshRestore = needsRefreshRestore;
        releaseAfterMovieRestoreParking();
        if (!m_pendingAfterMovie.isEmpty()) {
            m_afterMovieRestoreAttemptId = m_playbackAttemptId;
            m_afterMovieRestoreTimer->start();
        }
        qCInfo(lcPlaybackTrace) << "[attempt" << m_playbackAttemptId
                                << "] deferred-display-restore parked for post-playback screen"
                                << "needsHdrRestore=" << needsHdrRestore
                                << "needsRefreshRestore=" << needsRefreshRestore;
        return;
    }

    startDeferredDisplayRestore(needsHdrRestore, needsRefreshRestore);
}

void PlayerController::enterIdleStateImmediate()
{
    m_loadingTimeoutTimer->stop();
    m_bufferingTimeoutTimer->stop();
    m_progressReportTimer->stop();
    m_startDelayTimer->stop();

    emit playbackStopped();

    if (!m_currentItemId.isEmpty()) {
        m_currentItemId.clear();
        emit currentItemIdChanged();
    }
    m_currentSeriesId.clear();
    m_currentIsMovie = false;
    m_currentSeasonId.clear();
    m_currentLibraryId.clear();
    m_pendingUrl.clear();
    m_recoveryContext = RecoveryContext{};
    m_lastErrorWasNetworkRecoverable = false;
    setWaitingForRemoteMountInitialCache(false);
    m_currentPosition = 0;
    m_duration = 0;
    m_cacheEndSeconds = 0;
    m_subtitleDelayMs = 0;
    m_hasReportedStart = false;
    m_seekTargetWhileBuffering = -1;
    m_reportProgressOnNextPositionUpdate = false;
    m_startPositionMs = 0;
    m_contentFramerate = 0.0;
    m_contentIsHDR = false;
    m_contentShouldToneMapToSdr = false;
    m_playMethod = QStringLiteral("directPlay");
    m_nextPlaybackMethod.clear();
    m_streamPinsAudioTrack = false;
    m_streamPinsSubtitleTrack = false;
    m_nextStreamPinsAudioTrack = false;
    m_nextStreamPinsSubtitleTrack = false;
    m_pinnedAudioTrack = -1;
    m_pinnedSubtitleTrack = -1;
    m_nextPinnedAudioTrack = -1;
    m_nextPinnedSubtitleTrack = -1;
    m_nextCarryTrackPreferencesFromSeasonId.clear();
    m_carryTrackPreferencesFromSeasonId.clear();
    m_pendingAudioSwitchContext.clear();
    m_pendingAudioSwitchPreviousTrack = -2;
    m_pendingSubtitleReplanPreviousTrack = -2;
    m_replanQueued = false;
    // A pick whose stream change never answered is kept locally but not
    // written back: the server never confirmed it could play it.
    dropHeldTrackPreferenceWrites();
    m_awaitingInPlaceReload = false;
    m_ignoreReplacedFileEnd = false;
    loadActiveStreamLayout({});
    clearOverlayMetadata();
    clearPlaybackChapters();
    setBufferingProgress(0);
    
    // Clear track selection state (but keep m_seriesTrackPreferences)
    m_reopeningAudio = false;
    ++m_audioPlanGeneration;
    m_audioDiagnostics.clear();
    m_preparedAudioPlan.clear();
    m_playbackService->audioOutput()->setEndpointHeld(false);
    emit audioDiagnosticsChanged();
    updateAudioOutputStatus();
    m_selectedAudioTrack = -1;
    m_selectedSubtitleTrack = -1;
    m_mpvAudioTrack = -1;
    m_mpvSubtitleTrack = -1;
    m_audioTrackMap.clear();
    m_subtitleTrackMap.clear();
    m_externalSubtitleTrackMap.clear();
    m_audioTrackReverseMap.clear();
    m_subtitleTrackReverseMap.clear();
    m_mediaSourceId.clear();
    m_playSessionId.clear();
    m_availableAudioTracks.clear();
    m_availableSubtitleTracks.clear();
    m_pendingExternalSubtitleTracks.clear();
    m_activeMediaSource.clear();
    m_applyingInitialTracks = false;
    m_pendingAudioTrackPersistenceFromBackend = false;
    m_pendingSubtitleTrackPersistenceFromBackend = false;
    m_pendingExternalSubtitleIndex = -1;
    stopAutoplayPlaybackInfoWait();
    m_pendingAutoplayEpisodeData.clear();
    clearPlaybackSegments();
    emit selectedAudioTrackChanged();
    emit selectedSubtitleTrackChanged();
    emit subtitleDelayChanged();
    emit mediaSourceIdChanged();
    emit playSessionIdChanged();
    emit availableTracksChanged();
    
    // Clear OSC/trickplay state
    m_currentSegments.clear();
    m_isInIntroSegment = false;
    m_isInOutroSegment = false;
    m_autoSkippedSegments.clear();
    m_activeIntroSkipKey.clear();
    m_activeOutroSkipKey.clear();
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

    m_trickplayProcessor->clear();
}

void PlayerController::onEnterLoadingState()
{
    qCDebug(lcPlayback) << "PlayerController: Entering Loading state";
    
    // Start loading timeout
    const bool remoteMountStartupBuffering = m_config
        && m_config->resolveStartupBufferingModeForItem(m_currentLibraryId) == QStringLiteral("remote-mount");
    m_loadingTimeoutTimer->start(remoteMountStartupBuffering ? kRemoteMountLoadingTimeoutMs : kLoadingTimeoutMs);
    
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
    qCDebug(lcPlayback) << "PlayerController: Entering Buffering state";
    
    // Start buffering timeout
    m_bufferingTimeoutTimer->start(m_waitingForRemoteMountInitialCache
                                   ? kRemoteMountLoadingTimeoutMs
                                   : kBufferingTimeoutMs);
    
    // Initialize buffering detection
    m_lastPositionUpdateTime.start();
    m_lastPosition = m_currentPosition;
    
    // Apply pending track selections now that the file is loaded
    // Use mpv track numbers (1-based, per-type) for mpv commands
    const int desiredMpvAudioTrack = mpvAudioTrackForSourceIndex(m_selectedAudioTrack);
    const int desiredMpvSubtitleTrack = mpvSubtitleTrackForSourceIndex(m_selectedSubtitleTrack);
    const bool shouldOverridePinnedAudio = m_streamPinsAudioTrack
        && m_selectedAudioTrack >= 0
        && m_selectedAudioTrack != m_pinnedAudioTrack;
    const bool shouldOverridePinnedSubtitle = m_streamPinsSubtitleTrack
        && ((m_selectedSubtitleTrack == -1)
            || (m_selectedSubtitleTrack >= 0
                && m_selectedSubtitleTrack != m_pinnedSubtitleTrack));

    qCDebug(lcPlayback) << "Track startup selection:"
                        << "selectedAudio=" << m_selectedAudioTrack
                        << "selectedSubtitle=" << m_selectedSubtitleTrack
                        << "desiredMpvAudio=" << desiredMpvAudioTrack
                        << "desiredMpvSubtitle=" << desiredMpvSubtitleTrack
                        << "streamPinsAudio=" << m_streamPinsAudioTrack
                        << "streamPinsSubtitle=" << m_streamPinsSubtitleTrack
                        << "overridePinnedAudio=" << shouldOverridePinnedAudio
                        << "overridePinnedSubtitle=" << shouldOverridePinnedSubtitle;

    // Always apply resolved startup track selection if we have one. URL pins are treated as
    // transport/request hints and fallback, but explicit startup selection must win deterministically.
    if (desiredMpvAudioTrack > 0) {
        qCDebug(lcPlayback) << "Applying startup audio track selection via aid:" << desiredMpvAudioTrack;
        m_playerBackend->sendVariantCommand({"set_property", "aid", desiredMpvAudioTrack});
    } else if (m_streamPinsAudioTrack && !shouldOverridePinnedAudio) {
        qCDebug(lcPlayback) << "Keeping provider-pinned audio track:" << m_pinnedAudioTrack;
    }

    if (m_selectedSubtitleTrack == -1) {
        qCDebug(lcPlayback) << "Applying startup subtitle selection: none";
        m_playerBackend->sendVariantCommand({"set_property", "sid", "no"});
    } else if (desiredMpvSubtitleTrack > 0) {
        qCDebug(lcPlayback) << "Applying startup subtitle track selection via sid:" << desiredMpvSubtitleTrack;
        m_playerBackend->sendVariantCommand({"set_property", "sid", desiredMpvSubtitleTrack});
    } else if (m_streamPinsSubtitleTrack && !shouldOverridePinnedSubtitle) {
        qCDebug(lcPlayback) << "Keeping provider-pinned subtitle track:" << m_pinnedSubtitleTrack;
    }
    
    // If there was a pending seek for resume playback, execute it directly
    // We send directly to mpv here instead of calling seek() because seek()
    // would re-queue the seek since we're in Buffering state
    if (m_seekTargetWhileBuffering >= 0) {
        double target = m_seekTargetWhileBuffering;
        m_seekTargetWhileBuffering = -1;
        qCDebug(lcPlayback) << "PlayerController: Executing queued seek to" << target << "seconds";
        seekToSourcePosition(target);
    }
    
    // Apply audio delay
    double delaySeconds = static_cast<double>(m_config->getAudioDelay()) / 1000.0;
    if (delaySeconds != 0.0) {
        qCDebug(lcPlayback) << "PlayerController: Applying audio delay:" << delaySeconds << "s";
        m_playerBackend->sendVariantCommand({"set_property", "audio-delay", delaySeconds});
    }

    applySubtitleDelay();

    m_playerBackend->sendVariantCommand({"set_property", "volume", m_volume});
    m_playerBackend->sendVariantCommand({"set_property", "mute", m_muted});
}

void PlayerController::applyPendingExternalSubtitleTracks()
{
    if (!(m_playbackState == Playing || m_playbackState == Paused)) {
        return;
    }

    // Only the selected sidecar is loaded now. The others are registered so the
    // menu can offer them, and are loaded with select semantics when chosen:
    // mpv reports a sid only for the track it selects, so a sidecar added with
    // `auto` would stay unmapped and be fetched again on selection. The pending
    // list is also used when the segment is activated before playback reaches
    // Playing.
    const QVariantList pendingTracks = m_pendingExternalSubtitleTracks;
    for (const QVariant &pendingVariant : pendingTracks) {
        const QVariantMap pending = pendingVariant.toMap();
        if (pending.value(QStringLiteral("select"), false).toBool()) {
            continue;
        }
        addExternalSubtitleTrackInternal(
            pending.value(QStringLiteral("externalUrl")).toString(),
            pending.value(QStringLiteral("displayTitle")).toString(),
            pending.value(QStringLiteral("language")).toString(),
            pending.value(QStringLiteral("index"), -1).toInt(),
            false,
            false);
    }
    for (const QVariant &pendingVariant : pendingTracks) {
        const QVariantMap pending = pendingVariant.toMap();
        if (!pending.value(QStringLiteral("select"), false).toBool()) {
            continue;
        }
        addExternalSubtitleTrackInternal(
            pending.value(QStringLiteral("externalUrl")).toString(),
            pending.value(QStringLiteral("displayTitle")).toString(),
            pending.value(QStringLiteral("language")).toString(),
            pending.value(QStringLiteral("index"), -1).toInt(),
            true);
    }
    m_pendingExternalSubtitleTracks.clear();
}

void PlayerController::queueActiveSegmentExternalSubtitleTracks()
{
    m_pendingExternalSubtitleTracks.clear();
    const QVariantMap segment = activePlaybackSegment();
    if (segment.isEmpty()) {
        return;
    }

    const int selectedSubtitleIndex =
        segment.value(QStringLiteral("subtitleIndex"), -1).toInt();
    for (const QVariant &trackVariant :
         segment.value(QStringLiteral("availableSubtitleTracks")).toList()) {
        const QVariantMap track = trackVariant.toMap();
        const QString externalUrl = track.value(QStringLiteral("externalUrl")).toString().trimmed();
        if (!track.value(QStringLiteral("isExternal")).toBool() || externalUrl.isEmpty()) {
            continue;
        }

        QVariantMap pending = track;
        pending[QStringLiteral("externalUrl")] = externalUrl;
        pending[QStringLiteral("select")] =
            track.value(QStringLiteral("index"), -1).toInt() == selectedSubtitleIndex;
        m_pendingExternalSubtitleTracks.append(pending);
    }

    if (m_playbackState == Playing || m_playbackState == Paused) {
        applyPendingExternalSubtitleTracks();
    }
}

void PlayerController::onEnterPlayingState()
{
    qCInfo(lcPlayback) << "Entering Playing state for item:" << m_currentItemId;

    // Clear the initial tracks flag - from now on, track changes are user-initiated
    // and should be saved to preferences
    m_applyingInitialTracks = false;

    applyPendingExternalSubtitleTracks();
    requestDeferredSegmentDescriptor();
    saveCarriedTrackPreferences();


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

    // Paused heartbeats ride the progress timer (see reportPlaybackProgress).
    m_pausedProgressTicks = 0;
    m_progressReportTimer->start();
    
    // Report pause to server
    const QVariantMap segment = activePlaybackSegment();
    const QString reportItemId = segment.isEmpty()
        ? m_currentItemId
        : segment.value(QStringLiteral("itemId")).toString();
    if (!reportItemId.isEmpty()) {
        m_playbackService->reportPlaybackPaused(reportItemId,
                                                currentReportingPositionMs(),
                                                segment.isEmpty() ? m_mediaSourceId : segment.value(QStringLiteral("mediaSourceId")).toString(),
                                                segment.isEmpty() ? m_selectedAudioTrack : segment.value(QStringLiteral("audioIndex")).toInt(),
                                                segment.isEmpty() ? m_selectedSubtitleTrack : segment.value(QStringLiteral("subtitleIndex")).toInt(),
                                                segment.isEmpty() ? m_playSessionId : segment.value(QStringLiteral("playSessionId")).toString(),
                                                m_duration > 0.0,
                                                m_muted,
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
    qCDebug(lcPlayback) << "PlayerController: Entering Error state -" << m_errorMessage;
    
    // Stop all timers
    m_loadingTimeoutTimer->stop();
    m_bufferingTimeoutTimer->stop();
    m_progressReportTimer->stop();

    clearPendingAutoplayContext();
    clearNextEpisodePrefetchState();
    m_pendingExternalSubtitleTracks.clear();

    if (m_recoveryContext.valid) {
        beginRecovery();
    }
}

// === TIMEOUT HANDLERS ===

void PlayerController::onLoadingTimeout()
{
    qCDebug(lcPlayback) << "PlayerController: Loading timeout";
    m_lastErrorWasNetworkRecoverable = true;
    setErrorMessage(tr("Loading timed out. Please check your connection and try again."));
    requestTerminalTransition(TerminalReason::Error);
}

void PlayerController::onBufferingTimeout()
{
    qCDebug(lcPlayback) << "PlayerController: Buffering timeout";
    if (m_waitingForRemoteMountInitialCache) {
        completeRemoteMountStartupBuffering(m_cacheEndSeconds, "timeout");
        return;
    }
    m_lastErrorWasNetworkRecoverable = true;
    setErrorMessage(tr("Buffering timed out. Network may be too slow."));
    requestTerminalTransition(TerminalReason::Error);
}

/**
 * @brief Handle changes to the backend process running state.
 *
 * When the backend reports it is no longer running and the controller's
 * playback state is neither Idle nor Error, this method treats the event
 * as an unexpected stop and requests a backend-first terminal transition.
 *
 * @param running True if the backend process is running, false otherwise.
 */

void PlayerController::onProcessStateChanged(bool running)
{
    qCDebug(lcPlayback) << "PlayerController: Process state changed, running:" << running;
    qCInfo(lcPlaybackTrace) << "[attempt" << m_playbackAttemptId
                            << "] process-state"
                            << "running=" << running
                            << "state=" << stateToString(m_playbackState);
    
    if (running && !m_pendingPlaylistAppendUrls.isEmpty()) {
        m_playerBackend->appendUrlsToPlaylist(m_pendingPlaylistAppendUrls);
        m_pendingPlaylistAppendUrls.clear();
    }
    updateAudioOutputStatus();

    if (!running) {
        qCInfo(lcPlaybackTrace) << "[attempt" << m_playbackAttemptId
                                << "] backend-stopped-confirmed"
                                << "terminalActive=" << m_terminalTransitionActive
                                << "backendStopRequested=" << m_backendStopRequested;

        if (m_backendFallbackSource == m_playerBackend) {
            qCInfo(lcPlaybackTrace) << "[attempt" << m_playbackAttemptId
                                    << "] embedded-backend stop confirmed for fallback";
            queueBackendFallbackFinalization();
            return;
        }

        if (m_suppressBackendStopHandling) {
            qCInfo(lcPlaybackTrace) << "[attempt" << m_playbackAttemptId
                                    << "] backend-stop ignored during replacement playback";
            finalizeReplacementPlaybackStop();
            return;
        }

        if (m_terminalTransitionActive) {
            m_backendStopRequested = true;
            queueTerminalFinalization();
            return;
        }

        if (m_playbackState != Idle && m_playbackState != Error) {
            requestTerminalTransition(TerminalReason::Stop);
        }
    }
}

void PlayerController::onProcessError(const QString &error)
{
    qCDebug(lcPlayback) << "PlayerController: Process error:" << error;
    qCWarning(lcPlaybackTrace) << "[attempt" << m_playbackAttemptId
                               << "] process-error"
                               << error;

    if (error.startsWith(QStringLiteral("linux-libmpv-render-unavailable"))) {
        if (m_backendFallbackSource == m_playerBackend) {
            qCInfo(lcPlayback) << "Ignoring repeated embedded-render failure while fallback shutdown completes";
            return;
        }
        if (tryFallbackToExternalBackend(error)) {
            return;
        }
    }

    setWaitingForRemoteMountInitialCache(false);
    m_lastErrorWasNetworkRecoverable = isRecoverableBackendPlaybackError(error);
    setErrorMessage(m_lastErrorWasNetworkRecoverable
                    ? tr("Playback stream interrupted. Attempting to recover...")
                    : error);
    requestTerminalTransition(TerminalReason::Error);
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
    if (m_awaitingInPlaceReload) {
        return; // the replaced file's clock, or the new one before it loaded
    }
    double previousPosition = m_currentPosition;
    // Positions are source positions; a server stream may start its clock later.
    m_segmentRelativePosition = seconds + m_streamTimelineOffsetSeconds;
    seconds = m_segmentRelativePosition;
    requestDeferredSegmentDescriptor();
    const double offsetSeconds = static_cast<double>(m_activePlaybackSegmentOffsetMs) / 1000.0;
    const double aggregatePosition = offsetSeconds + seconds;
    m_currentPosition = aggregatePosition;
    if (m_reportProgressOnNextPositionUpdate
        && (m_playbackState == Playing || m_playbackState == Paused)) {
        reportPlaybackProgressNow();
        m_reportProgressOnNextPositionUpdate = false;
    }
    updateSkipSegmentState();
    updateCurrentPlaybackChapterIndex();
    if (!qFuzzyCompare(previousPosition + 1.0, aggregatePosition + 1.0)) {
        emit timelineChanged();
    }

    if (hasTrickplay() && !m_hasTrickplayPreviewPositionOverride) {
        updateTrickplayPreviewForPosition(aggregatePosition);
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
        m_bufferingTimeoutTimer->start(m_waitingForRemoteMountInitialCache
                                       ? kRemoteMountLoadingTimeoutMs
                                       : kBufferingTimeoutMs);
    }
    
    // Update buffering progress during Buffering state
    if (m_playbackState == Buffering) {
        // If position is advancing significantly, buffering is complete
        if (aggregatePosition > previousPosition + 0.5) {
            processEvent(Event::BufferComplete);
        } else {
            // Update progress based on time waiting (crude estimate)
            int elapsed = m_bufferingTimeoutTimer->interval() - m_bufferingTimeoutTimer->remainingTime();
            int progress = qMin(99, elapsed / 100);
            setBufferingProgress(progress);
        }
    }

    maybeTriggerNextEpisodePrefetch();
    
    m_lastPosition = aggregatePosition;
    m_lastPositionUpdateTime.restart();
}

void PlayerController::onDurationChanged(double seconds)
{
    if (!m_streamCarriesSourceTracks && m_duration > 0.0) {
        // A server stream reports what it has produced so far (or the length
        // from its own start); the plan's runtime is the timeline.
        return;
    }
    const double effectiveDuration = (m_playbackSegments.size() > 1 && m_aggregatePlaybackDuration > 0.0)
        ? m_aggregatePlaybackDuration
        : seconds;
    if (!qFuzzyCompare(m_duration + 1.0, effectiveDuration + 1.0)) {
        m_duration = effectiveDuration;
        emit timelineChanged();
    }
}

void PlayerController::onPausedForCacheChanged(bool pausedForCache)
{
    qCDebug(lcPlayback) << "PlayerController: Paused for cache:" << pausedForCache;

    if (!pausedForCache && m_waitingForRemoteMountInitialCache) {
        completeRemoteMountStartupBuffering(m_cacheEndSeconds, "paused-for-cache-false");
        return;
    }
    
    if (pausedForCache && m_playbackState == Playing) {
        // mpv reports actual buffering - transition to Buffering state
        qCDebug(lcPlayback) << "PlayerController: mpv started buffering";
        processEvent(Event::BufferStart);
    } else if (!pausedForCache && m_playbackState == Buffering) {
        // mpv finished buffering - transition back to Playing
        qCDebug(lcPlayback) << "PlayerController: mpv finished buffering";
        processEvent(Event::BufferComplete);
    }
}

void PlayerController::onCacheEndChanged(double seconds)
{
    if (seconds != m_cacheEndSeconds) {
        m_cacheEndSeconds = seconds;
        emit cacheEndChanged();
    }

    if (m_waitingForRemoteMountInitialCache
        && (m_playbackState == Loading || m_playbackState == Buffering)
        && seconds >= kRemoteMountInitialCacheSeconds) {
        completeRemoteMountStartupBuffering(seconds, "cache-ready");
    }
}

void PlayerController::onPauseChanged(bool paused)
{
    qCDebug(lcPlayback) << "PlayerController: Pause changed:" << paused;
    
    const QVariantMap segment = activePlaybackSegment();
    const QString reportItemId = segment.isEmpty()
        ? m_currentItemId
        : segment.value(QStringLiteral("itemId")).toString();
    if (reportItemId.isEmpty()) return;
    
    // Bloom never pauses or resumes on its own while playing, except for the
    // sleep timer, so these transitions are user input. They are the only
    // input Bloom can observe from the external mpv window.
    if (paused && m_playbackState == Playing) {
        if (std::exchange(m_sleepTimerPauseRequested, false)) {
            qCDebug(lcPlayback) << "Sleep timer pause applied";
        } else {
            notePlaybackUserInput();
        }
    } else if (!paused && m_playbackState == Paused) {
        notePlaybackUserInput();
    }

    if (paused && m_playbackState == Playing) {
        processEvent(Event::Pause);
    } else if (!paused && m_playbackState == Paused) {
        // Report resume to server
        m_playbackService->reportPlaybackResumed(reportItemId, currentReportingPositionMs(),
                                                 segment.isEmpty() ? m_mediaSourceId : segment.value(QStringLiteral("mediaSourceId")).toString(),
                                                 segment.isEmpty() ? m_selectedAudioTrack : segment.value(QStringLiteral("audioIndex")).toInt(),
                                                 segment.isEmpty() ? m_selectedSubtitleTrack : segment.value(QStringLiteral("subtitleIndex")).toInt(),
                                                 segment.isEmpty() ? m_playSessionId : segment.value(QStringLiteral("playSessionId")).toString(),
                                                 m_duration > 0.0, m_muted,
                                                 m_playMethod);
        processEvent(Event::Resume);
    }
}

/**
 * @brief Handle end of the current playback session.
 *
 * Processes end-of-playback state, performs stop-related reporting, and initiates any configured autoplay or next-episode navigation logic.
 */
void PlayerController::onPlaybackEnded(Bloom::PlaybackPolicy::EndFileReason reason,
                                       const QString &detail)
{
    // A file mpv could not open or read ends with reason=error. It must not
    // count as a natural end (watched state, Up Next) or wait for the
    // loading timeout. Check it before the guards below: loadfile replace
    // ends the previous file with reason=stop, never error, and backends
    // already hold back ends that mpv follows with another playlist entry,
    // so an error here is a failed replacement or a failed last entry (for
    // example a reloaded non-final segment, whose playlist has one entry).
    if (PlayerPolicy::classifyEndFile(reason, false) == PlayerPolicy::EndFileOutcome::Failed) {
        m_ignoreReplacedFileEnd = false;
        qCInfo(lcPlaybackTrace) << "[attempt" << m_playbackAttemptId
                                << "] playback-ended"
                                << "reason=" << PlayerPolicy::endFileReasonName(reason)
                                << "segmentIndex=" << m_activePlaybackSegmentIndex;
        onProcessError(detail.isEmpty()
                           ? tr("Playback failed.")
                           : tr("Playback failed: %1").arg(redactNetworkLogText(detail)));
        return;
    }

    if (m_ignoreReplacedFileEnd) {
        // loadfile replace ends the previous file first. A new file that fails
        // to load ends again after this flag clears.
        m_ignoreReplacedFileEnd = false;
        qCDebug(lcPlayback) << "Ignoring the end of a stream replaced in place";
        return;
    }
    if (m_activePlaybackSegmentIndex >= 0
        && m_activePlaybackSegmentIndex < (m_playbackSegments.size() - 1)) {
        qCDebug(lcPlayback) << "Ignoring intermediate playlist segment end"
                            << "segmentIndex=" << m_activePlaybackSegmentIndex
                            << "segmentCount=" << m_playbackSegments.size();
        return;
    }

    qCDebug(lcPlayback) << "PlayerController: Playback ended";
    qCInfo(lcPlaybackTrace) << "[attempt" << m_playbackAttemptId
                            << "] playback-ended"
                            << "reason=" << PlayerPolicy::endFileReasonName(reason)
                            << "position=" << m_currentPosition
                            << "duration=" << m_duration;

    requestTerminalTransition(TerminalReason::PlaybackEnd);
}

void PlayerController::onPlaylistPositionChanged(int playlistIndex)
{
    if (m_awaitingInPlaceReload) {
        return; // the replacement playlist, not a segment change
    }
    // After an in-place reload mpv's playlist starts at the reloaded segment.
    const int index = playlistIndex + m_playlistSegmentOffset;
    if (index < 0 || index >= m_playbackSegments.size() || index == m_activePlaybackSegmentIndex) {
        return;
    }

    const QVariantMap previousSegment = activePlaybackSegment();
    if (!previousSegment.isEmpty()) {
        qint64 stopMs = previousSegment.value(QStringLiteral("durationMs")).toLongLong();
        if (stopMs <= 0) {
            stopMs = currentReportingPositionMs();
        }
        reportPlaybackStopForSegment(previousSegment, stopMs);
    }

    applyPlaybackSegment(index, m_playbackState == Playing || m_playbackState == Paused);
    emit timelineChanged();
    requestDeferredSegmentDescriptor();
}

void PlayerController::requestDeferredSegmentDescriptor()
{
    const int next = m_activePlaybackSegmentIndex + 1;
    const QString context =
        QStringLiteral("segment|%1|%2").arg(m_playbackAttemptId).arg(next);
    if (context == m_deferredSegmentContext || !m_playbackService || next <= 0
        || next >= m_playbackSegments.size()
        || !m_playbackSegments.at(next).value(QStringLiteral("deferredDescriptor")).toBool()) {
        return;
    }
    // Silo expires a session that is not read for about 45 s, and mpv opens
    // the next playlist entry only at the handoff, so start it shortly before.
    const qint64 activeDurationMs = m_playbackSegments.at(next - 1)
                                        .value(QStringLiteral("durationMs")).toLongLong();
    if (activeDurationMs > 0
        && activeDurationMs / 1000.0 - m_segmentRelativePosition > kDeferredSegmentLeadSeconds) {
        return;
    }
    const QVariantMap segment = m_playbackSegments.at(next);
    m_deferredSegmentContext = context;
    m_playbackService->requestPlaybackDescriptor(
        segment.value(QStringLiteral("itemId")).toString(),
        segment.value(QStringLiteral("mediaSource")).toMap(),
        segment.value(QStringLiteral("audioIndex"), -1).toInt(),
        segment.value(QStringLiteral("subtitleIndex"), -1).toInt(),
        0,
        QString(),
        m_deferredSegmentContext);
}

/**
 * @brief Prepare a terminal playback transition for a specific termination reason.
 *
 * Starts terminal-transition bookkeeping unless one is already active, reports playback stop once,
 * evaluates completion/autoplay state for non-error transitions, and kicks off next-episode resolution
 * when needed. The matching state-machine event is finalized separately after backend stop is confirmed.
 *
 * @param reason Terminal reason that should eventually drive the state-machine transition.
 */
void PlayerController::prepareTerminalTransition(TerminalReason reason)
{
    if (m_terminalTransitionActive) {
        updatePendingTerminalReason(reason);
        return;
    }

    m_terminalTransitionActive = true;
    m_backendStopRequested = false;
    m_terminalFinalizationQueued = false;
    m_terminalPrefetchedReady = false;
    ++m_terminalTransitionGeneration;
    m_pendingTerminalReason = reason;

    m_useLocalTrickplay = false;
    m_localSeekPreview->clear();
    m_trickplayProcessor->clear();
    emit trickplayStateChanged();

    reportPlaybackStop();

    const bool sleepTimerEndedPlayback = applySleepTimerAtTerminal(reason);

    m_pendingAfterMovie.clear();
    PlayerPolicy::PostPlaybackInput postPlayback;
    postPlayback.contentKind = m_currentContentKind;
    postPlayback.endedWithError = reason == TerminalReason::Error;
    postPlayback.endedBySleepTimer = sleepTimerEndedPlayback;
    postPlayback.alreadyEvaluated = m_hasEvaluatedCompletionForAttempt;
    postPlayback.itemId = m_currentItemId;
    postPlayback.isMovie = m_currentIsMovie;
    postPlayback.seriesId = m_currentSeriesId;
    postPlayback.positionSeconds = m_currentPosition;
    postPlayback.durationSeconds = m_duration;
    postPlayback.thresholdPercent = m_config->getPlaybackCompletionThreshold();
    const PlayerPolicy::PostPlaybackScreen screen = PlayerPolicy::postPlaybackScreen(postPlayback);
    if (reason != TerminalReason::Error) {
        const bool thresholdMet = checkCompletionThresholdAndAutoplay();
        if (screen == PlayerPolicy::PostPlaybackScreen::AfterMovie) {
            m_pendingAfterMovie = {
                {QStringLiteral("itemId"), m_currentItemId},
                {QStringLiteral("connectionId"),
                 m_libraryService ? m_libraryService->getActiveConnectionId() : QString()},
                {QStringLiteral("name"), m_overlayTitle},
                {QStringLiteral("backdropUrl"), m_overlayBackdropUrl},
                {QStringLiteral("logoUrl"), m_overlayLogoUrl},
                {QStringLiteral("attemptId"), QVariant::fromValue<qulonglong>(m_playbackAttemptId)}
            };
            qCDebug(lcPlayback) << "PlayerController: Movie completed, after-movie screen follows";
        }
        if (thresholdMet && screen == PlayerPolicy::PostPlaybackScreen::UpNext) {
            m_shouldAutoplay = true;
            m_waitingForNextEpisodeAtPlaybackEnd = true;
            stashPendingAutoplayContext();
            m_terminalPrefetchedReady = hasUsablePrefetchedNextEpisode();
            qCDebug(lcPlayback) << "PlayerController: Threshold met, next episode will resolve after terminal finalization";
        } else {
            clearPendingAutoplayContext();
            clearNextEpisodePrefetchState();
            m_shouldAutoplay = false;
            // A duration only carries over into the next episode through Up
            // Next; an end that opens nothing (a movie, an early quit in the
            // mpv window) ends the session it belonged to.
            clearSleepTimer();
        }
    } else {
        clearPendingAutoplayContext();
        clearNextEpisodePrefetchState();
        m_shouldAutoplay = false;
        stashRecoveryContextForCurrentPlayback();
    }

    qCInfo(lcPlaybackTrace) << "[attempt" << m_playbackAttemptId
                            << "] terminal-transition prepared"
                            << "reason=" << static_cast<int>(m_pendingTerminalReason)
                            << "prefetchedReady=" << m_terminalPrefetchedReady
                            << "backendRunning=" << (m_playerBackend && m_playerBackend->isRunning());
}

void PlayerController::requestTerminalTransition(TerminalReason reason)
{
    if (reason == TerminalReason::Error && m_playbackState == Error && !m_terminalTransitionActive) {
        return;
    }

    prepareTerminalTransition(reason);

    if (m_backendStopRequested) {
        if (!m_playerBackend || !m_playerBackend->isRunning()) {
            queueTerminalFinalization();
        }
        return;
    }

    m_backendStopRequested = true;
    qCInfo(lcPlaybackTrace) << "[attempt" << m_playbackAttemptId
                            << "] backend-stop requested"
                            << "reason=" << static_cast<int>(m_pendingTerminalReason);

    if (m_playerBackend && m_playerBackend->isRunning()) {
        m_playerBackend->stopMpv();
        if (!m_playerBackend->isRunning()) {
            queueTerminalFinalization();
        }
        return;
    }

    queueTerminalFinalization();
}

void PlayerController::queueTerminalFinalization()
{
    if (!m_terminalTransitionActive || m_terminalFinalizationQueued) {
        return;
    }

    m_terminalFinalizationQueued = true;
    const quint64 generation = m_terminalTransitionGeneration;
    QMetaObject::invokeMethod(this,
                              [this, generation]() {
                                  if (!m_terminalTransitionActive
                                      || generation != m_terminalTransitionGeneration) {
                                      return;
                                  }
                                  finalizeTerminalTransition();
                              },
                              Qt::QueuedConnection);
}

void PlayerController::finalizeTerminalTransition()
{
    if (!m_terminalTransitionActive) {
        return;
    }

    const TerminalReason reason = m_pendingTerminalReason;
    const bool prefetchedReady = m_terminalPrefetchedReady;

    qCInfo(lcPlaybackTrace) << "[attempt" << m_playbackAttemptId
                            << "] terminal-transition finalized"
                            << "reason=" << static_cast<int>(reason)
                            << "prefetchedReady=" << prefetchedReady
                            << "state=" << stateToString(m_playbackState);

    resetTerminalTransitionState();
    if (!processEvent(eventForTerminalReason(reason))
        && m_playbackState != Idle && m_playbackState != Error) {
        // The backend has already stopped; never leave the controller in an
        // active state (and its timeouts running) behind a stopped backend.
        const PlaybackState fallbackState = reason == TerminalReason::Error ? Error : Idle;
        qCWarning(lcPlaybackTrace) << "[attempt" << m_playbackAttemptId
                                   << "] terminal-transition forced"
                                   << "from=" << stateToString(m_playbackState)
                                   << "to=" << stateToString(fallbackState);
        exitState(m_playbackState);
        enterState(fallbackState);
    }

    // Entering Idle above has parked the display restore for it.
    const QVariantMap afterMovie = std::exchange(m_pendingAfterMovie, {});

    if (runPendingReplacementPlaybackAction()) {
        return;
    }

    if (!afterMovie.isEmpty() && reason != TerminalReason::Error) {
        emit navigateToAfterMovie(afterMovie);
        return;
    }

    if (prefetchedReady && reason != TerminalReason::Error) {
        consumePrefetchedNextEpisodeAndNavigate();
    } else if (reason != TerminalReason::Error
               && m_shouldAutoplay
               && m_waitingForNextEpisodeAtPlaybackEnd
               && !m_pendingAutoplaySeriesId.isEmpty()
               && !m_pendingAutoplayItemId.isEmpty()) {
        qCInfo(lcPlaybackTrace) << "[attempt" << m_playbackAttemptId
                                << "] resolving-next-episode after terminal finalization"
                                << "seriesId=" << m_pendingAutoplaySeriesId
                                << "itemId=" << m_pendingAutoplayItemId;
        m_libraryService->getNextUnplayedEpisode(m_pendingAutoplaySeriesId,
                                                 m_pendingAutoplayItemId,
                                                 expectedAutoplayResolutionRequestContext());
    }
}

void PlayerController::resetTerminalTransitionState(bool invalidateQueuedWork)
{
    if (invalidateQueuedWork) {
        ++m_terminalTransitionGeneration;
        m_pendingAfterMovie.clear();
    }
    m_terminalTransitionActive = false;
    m_backendStopRequested = false;
    m_terminalFinalizationQueued = false;
    m_terminalPrefetchedReady = false;
}

void PlayerController::updatePendingTerminalReason(TerminalReason reason)
{
    if (!m_terminalTransitionActive
        || terminalReasonPriority(reason) <= terminalReasonPriority(m_pendingTerminalReason)) {
        return;
    }

    qCInfo(lcPlaybackTrace) << "[attempt" << m_playbackAttemptId
                            << "] terminal-transition reason upgraded"
                            << "from=" << static_cast<int>(m_pendingTerminalReason)
                            << "to=" << static_cast<int>(reason);
    const TerminalReason previousReason = m_pendingTerminalReason;
    m_pendingTerminalReason = reason;

    if (reason == TerminalReason::Error && previousReason != TerminalReason::Error) {
        m_pendingAfterMovie.clear();
        clearPendingAutoplayContext();
        clearNextEpisodePrefetchState();
        m_shouldAutoplay = false;
        m_terminalPrefetchedReady = false;
        const bool stashed = stashRecoveryContextForCurrentPlayback();
        qCInfo(lcPlaybackTrace) << "[attempt" << m_playbackAttemptId
                                << "] terminal-transition upgraded to recoverable error"
                                << "recoveryContextStashed=" << stashed;
    }
}

bool PlayerController::stashRecoveryContextForCurrentPlayback()
{
    const bool wasActive = isPlaybackActive();

    auto clearContext = [this, wasActive](const char *reason) {
        m_recoveryContext = RecoveryContext{};
        qCInfo(lcPlaybackTrace) << "[attempt" << m_playbackAttemptId
                                << "] recovery-context skipped"
                                << "reason=" << reason
                                << "autoRecover=" << (m_config && m_config->getAutoRecoverPlayback())
                                << "recoverable=" << m_lastErrorWasNetworkRecoverable
                                << "itemIdEmpty=" << m_currentItemId.isEmpty()
                                << "urlEmpty=" << m_pendingUrl.isEmpty();
        if (wasActive != isPlaybackActive()) {
            emit isPlaybackActiveChanged();
        }
        return false;
    };

    if (!m_config || !m_config->getAutoRecoverPlayback()) {
        return clearContext("auto-recover-disabled");
    }
    if (!m_lastErrorWasNetworkRecoverable) {
        return clearContext("error-not-recoverable");
    }
    if (m_currentItemId.isEmpty()) {
        return clearContext("missing-item-id");
    }
    if (m_pendingUrl.isEmpty()) {
        return clearContext("missing-url");
    }

    QString segmentUrl = m_pendingUrl;
    if (m_activePlaybackSegmentIndex >= 0
        && m_activePlaybackSegmentIndex < m_playbackSegments.size()) {
        const QString segUrl = m_playbackSegments[m_activePlaybackSegmentIndex]
                                   .value(QStringLiteral("url")).toString();
        if (!segUrl.isEmpty()) {
            segmentUrl = segUrl;
        }
    }

    m_recoveryContext.url = segmentUrl;
    m_recoveryContext.itemId = m_currentItemId;
    m_recoveryContext.startPositionMs = qRound64(m_segmentRelativePosition * 1000.0);
    m_recoveryContext.seriesId = m_currentSeriesId;
    m_recoveryContext.seasonId = m_currentSeasonId;
    m_recoveryContext.libraryId = m_currentLibraryId;
    m_recoveryContext.contentKind = m_currentContentKind;
    m_recoveryContext.isMovie = m_currentIsMovie;
    m_recoveryContext.mediaSourceId = m_mediaSourceId;
    m_recoveryContext.playSessionId = QString(); // avoid stale session
    m_recoveryContext.mediaSource = m_activeMediaSource;
    m_recoveryContext.audioStreamIndex = m_selectedAudioTrack;
    m_recoveryContext.subtitleStreamIndex = m_selectedSubtitleTrack;
    m_recoveryContext.availableAudioTracks = m_availableAudioTracks;
    m_recoveryContext.availableSubtitleTracks = m_availableSubtitleTracks;
    m_recoveryContext.framerate = m_contentFramerate;
    m_recoveryContext.isHDR = m_contentIsHDR;
    m_recoveryContext.playbackSegments = m_playbackSegments;
    m_recoveryContext.activePlaybackSegmentIndex = m_activePlaybackSegmentIndex;
    m_recoveryContext.activePlaybackSegmentOffsetMs = m_activePlaybackSegmentOffsetMs;
    m_recoveryContext.segmentRelativePosition = m_segmentRelativePosition;
    m_recoveryContext.valid = true;

    qCInfo(lcPlaybackTrace) << "[attempt" << m_playbackAttemptId
                            << "] recovery-context stashed"
                            << "itemId=" << m_recoveryContext.itemId
                            << "mediaSourceId=" << m_recoveryContext.mediaSourceId
                            << "positionMs=" << m_recoveryContext.startPositionMs;

    if (wasActive != isPlaybackActive()) {
        emit isPlaybackActiveChanged();
    }
    return true;
}

PlayerController::Event PlayerController::eventForTerminalReason(TerminalReason reason) const
{
    switch (reason) {
    case TerminalReason::PlaybackEnd:
        return Event::PlaybackEnd;
    case TerminalReason::Error:
        return Event::ErrorOccurred;
    case TerminalReason::Stop:
    default:
        return Event::Stop;
    }
}

int PlayerController::terminalReasonPriority(TerminalReason reason)
{
    switch (reason) {
    case TerminalReason::Error:
        return 3;
    case TerminalReason::PlaybackEnd:
        return 2;
    case TerminalReason::Stop:
    default:
        return 1;
    }
}

void PlayerController::startDeferredDisplayRestore(bool needsHdrRestore, bool needsRefreshRestore)
{
    cancelPendingDisplayRestore();

    if (!needsHdrRestore && !needsRefreshRestore) {
        return;
    }

    const quint64 generation = m_displayRestoreGeneration;
    qCInfo(lcPlaybackTrace) << "[attempt" << m_playbackAttemptId
                            << "] deferred-display-restore queued"
                            << "generation=" << generation
                            << "needsHdrRestore=" << needsHdrRestore
                            << "needsRefreshRestore=" << needsRefreshRestore;

    QMetaObject::invokeMethod(this,
                              [this, generation, needsHdrRestore, needsRefreshRestore]() {
                                  if (generation != m_displayRestoreGeneration) {
                                      qCInfo(lcPlaybackTrace) << "[attempt" << m_playbackAttemptId
                                                              << "] deferred-display-restore canceled before start"
                                                              << "generation=" << generation;
                                      return;
                                  }

                                  qCInfo(lcPlaybackTrace) << "[attempt" << m_playbackAttemptId
                                                          << "] deferred-display-restore begin"
                                                          << "generation=" << generation
                                                          << "needsHdrRestore=" << needsHdrRestore
                                                          << "needsRefreshRestore=" << needsRefreshRestore;

                                  if (needsHdrRestore && m_displayManager != nullptr) {
                                      m_hdrRestoreFinishedConnection = connect(m_displayManager,
                                                                               &DisplayManager::hdrChangeFinished,
                                                                               this,
                                                                               [this, generation, needsRefreshRestore](bool enabled, bool success) {
                                                                                   if (enabled || generation != m_displayRestoreGeneration) {
                                                                                       if (m_hdrRestoreFinishedConnection) {
                                                                                           disconnect(m_hdrRestoreFinishedConnection);
                                                                                           m_hdrRestoreFinishedConnection = QMetaObject::Connection();
                                                                                       }
                                                                                       return;
                                                                                   }
                                                                                   if (m_hdrRestoreFinishedConnection) {
                                                                                       disconnect(m_hdrRestoreFinishedConnection);
                                                                                       m_hdrRestoreFinishedConnection = QMetaObject::Connection();
                                                                                   }

                                                                                   qCInfo(lcPlaybackTrace) << "[attempt" << m_playbackAttemptId
                                                                                                           << "] deferred-display-restore hdr-finished"
                                                                                                           << "generation=" << generation
                                                                                                           << "success=" << success;

                                                                                   if (needsRefreshRestore) {
                                                                                       scheduleDeferredRefreshRestore(generation, success ? 300 : 0);
                                                                                   }
                                                                               });
                                      m_displayManager->setHDRAsync(false);
                                      return;
                                  }

                                  if (needsRefreshRestore) {
                                      scheduleDeferredRefreshRestore(generation, 0);
                                  }
                              },
                              Qt::QueuedConnection);
}

void PlayerController::releaseAfterMovieRestoreParking()
{
    m_afterMovieRestoreAttemptId = 0;
    if (m_afterMovieRestoreTimer) {
        m_afterMovieRestoreTimer->stop();
    }
}

void PlayerController::maybeStartDeferredPostPlaybackDisplayRestore()
{
    releaseAfterMovieRestoreParking();
    if (!m_deferredPostPlaybackDisplayRestorePending) {
        return;
    }

    const bool needsHdrRestore = m_deferredPostPlaybackNeedsHdrRestore;
    const bool needsRefreshRestore = m_deferredPostPlaybackNeedsRefreshRestore;
    m_deferredPostPlaybackDisplayRestorePending = false;
    m_deferredPostPlaybackNeedsHdrRestore = false;
    m_deferredPostPlaybackNeedsRefreshRestore = false;
    startDeferredDisplayRestore(needsHdrRestore, needsRefreshRestore);
}

void PlayerController::scheduleDeferredRefreshRestore(quint64 generation, int delayMs)
{
    if (m_displayManager == nullptr) {
        return;
    }

    QTimer::singleShot(delayMs,
                       this,
                       [this, generation]() {
                           if (generation != m_displayRestoreGeneration || m_displayManager == nullptr) {
                               qCInfo(lcPlaybackTrace) << "[attempt" << m_playbackAttemptId
                                                       << "] deferred-display-restore refresh canceled"
                                                       << "generation=" << generation;
                               return;
                           }

                           qCInfo(lcPlaybackTrace) << "[attempt" << m_playbackAttemptId
                                                   << "] deferred-display-restore refresh begin"
                                                   << "generation=" << generation;
                           if (m_refreshRestoreFinishedConnection) {
                               disconnect(m_refreshRestoreFinishedConnection);
                           }
                           m_refreshRestoreFinishedConnection = connect(
                               m_displayManager,
                               &DisplayManager::refreshRateRestoreFinished,
                               this,
                               [this, generation](bool success) {
                                   if (generation != m_displayRestoreGeneration) {
                                       return;
                                   }
                                   if (m_refreshRestoreFinishedConnection) {
                                       disconnect(m_refreshRestoreFinishedConnection);
                                       m_refreshRestoreFinishedConnection = QMetaObject::Connection();
                                   }
                                   qCInfo(lcPlaybackTrace)
                                       << "[attempt" << m_playbackAttemptId
                                       << "] deferred-display-restore refresh done"
                                       << "generation=" << generation
                                       << "success=" << success;
                               });
                           m_displayManager->restoreRefreshRateAsync();
                       });
}

void PlayerController::cancelPendingDisplayRestore(
    bool clearParkedPostPlaybackRestore)
{
    qCInfo(lcPlaybackTrace) << "[attempt" << m_playbackAttemptId
                            << "] deferred-display-restore cancel"
                            << "generation=" << (m_displayRestoreGeneration + 1);
    ++m_displayRestoreGeneration;
    if (m_hdrRestoreFinishedConnection) {
        disconnect(m_hdrRestoreFinishedConnection);
        m_hdrRestoreFinishedConnection = QMetaObject::Connection();
    }
    if (m_refreshRestoreFinishedConnection) {
        disconnect(m_refreshRestoreFinishedConnection);
        m_refreshRestoreFinishedConnection = QMetaObject::Connection();
    }
    if (m_displayPreparationConnection) {
        disconnect(m_displayPreparationConnection);
        m_displayPreparationConnection = QMetaObject::Connection();
    }
    if (clearParkedPostPlaybackRestore) {
        m_deferredPostPlaybackDisplayRestorePending = false;
        m_deferredPostPlaybackNeedsHdrRestore = false;
        m_deferredPostPlaybackNeedsRefreshRestore = false;
        releaseAfterMovieRestoreParking();
    }
    if (m_displayManager != nullptr) {
        m_displayManager->cancelPendingOperations();
    }
}

void PlayerController::cancelPendingTerminalTransition()
{
    if (!m_terminalTransitionActive && !m_terminalFinalizationQueued) {
        return;
    }

    qCInfo(lcPlaybackTrace) << "[attempt" << m_playbackAttemptId
                            << "] terminal-transition canceled";
    resetTerminalTransitionState(true);
}

void PlayerController::scheduleReplacementPlayback(const std::function<void()> &action)
{
    m_pendingReplacementPlaybackAction = action;

    if (m_terminalTransitionActive || m_terminalFinalizationQueued) {
        qCInfo(lcPlaybackTrace) << "[attempt" << m_playbackAttemptId
                                << "] replacement playback deferred until terminal transition finalizes"
                                << "terminalActive=" << m_terminalTransitionActive
                                << "terminalFinalizationQueued=" << m_terminalFinalizationQueued;
        return;
    }

    if (m_playerBackend && m_playerBackend->isRunning()) {
        reportPlaybackStop();
        m_suppressBackendStopHandling = true;
        m_playerBackend->stopMpv();
        if (m_playerBackend->isRunning()) {
            return;
        }
    }

    finalizeReplacementPlaybackStop();
}

void PlayerController::finalizeReplacementPlaybackStop()
{
    m_suppressBackendStopHandling = false;

    if (m_playbackState != Idle) {
        const PlaybackState oldState = m_playbackState;
        qCInfo(lcPlaybackTrace) << "[attempt" << m_playbackAttemptId
                                << "] replacement-stop finalized"
                                << "state=" << stateToString(oldState);
        exitState(oldState);
        m_loadingTimeoutTimer->stop();
        m_bufferingTimeoutTimer->stop();
        m_progressReportTimer->stop();
        m_startDelayTimer->stop();
        m_externalSubtitleTrackMap.clear();
        m_pendingExternalSubtitleIndex = -1;
        setPlaybackState(Idle);
        setBufferingProgress(0);
        emit playbackStopped();
    }

    if (!m_pendingReplacementPlaybackAction) {
        return;
    }

    runPendingReplacementPlaybackAction();
}

bool PlayerController::runPendingReplacementPlaybackAction()
{
    if (!m_pendingReplacementPlaybackAction) {
        return false;
    }

    const std::function<void()> action = m_pendingReplacementPlaybackAction;
    m_pendingReplacementPlaybackAction = std::function<void()>();
    action();
    return true;
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
 * @param connectionId Connection that owns the canonical episode payload.
 * @param seriesId Series identifier associated with the loaded next-episode payload.
 * @param episodeData Canonical episode metadata with camelCase fields and millisecond timing.
 */
void PlayerController::onNextEpisodeLoaded(const QString &connectionId,
                                           const QString &seriesId,
                                           const QVariantMap &episodeData,
                                           const QString &requestContext)
{
    if (m_libraryService && !connectionId.isEmpty()
        && connectionId != m_libraryService->getActiveConnectionId()) {
        return;
    }
    if (!m_waitingForNextEpisodeAtPlaybackEnd) {
        if (seriesId != m_currentSeriesId || m_currentSeriesId.isEmpty()) {
            return;
        }
        if (!matchesPlaybackRequestContext(requestContext, false)) {
            return;
        }

        const QString prefetchedEpisodeId = episodeData.value(QStringLiteral("itemId")).toString();
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
                            << "requestContext=" << requestContext
                            << "episodeId=" << prefetchedEpisodeId
                            << "pointsToCurrentEpisode=" << pointsToCurrentEpisode
                            << "ready=" << m_nextEpisodePrefetchReady;
        return;
    }

    // Only handle this if we're expecting an autoplay/navigation
    if (!m_shouldAutoplay) {
        return;
    }
    if (!matchesPlaybackRequestContext(requestContext, true)) {
        return;
    }

    if (!m_pendingAutoplaySeriesId.isEmpty() && seriesId != m_pendingAutoplaySeriesId) {
        qCDebug(lcPlayback) << "PlayerController: Ignoring next episode for unexpected series:" << seriesId;
        return;
    }
    
    m_shouldAutoplay = false;
    m_waitingForNextEpisodeAtPlaybackEnd = false;

    const QString carrySeasonId = pendingAutoplayCarrySeasonId(episodeSeasonId(episodeData));
    
    if (episodeData.isEmpty()) {
        qCDebug(lcPlayback) << "PlayerController: No next episode available";
        QVariantMap noNextEpisodeData;
        noNextEpisodeData.insert(QStringLiteral("seriesId"), seriesId);
        noNextEpisodeData.insert(QStringLiteral("seasonId"), m_pendingAutoplaySeasonId);
        noNextEpisodeData.insert(QStringLiteral("noNextEpisode"), true);
        emitNavigateToNextEpisodeQueued(noNextEpisodeData, seriesId, QString(), false);
        clearNextEpisodePrefetchState();
        return;
    }

    const QString episodeId = episodeData.value(QStringLiteral("itemId")).toString();
    if (!episodeId.isEmpty() && episodeId == m_pendingAutoplayItemId) {
        qCWarning(lcPlayback) << "Ignoring next-episode response that points to the current item"
                              << "itemId=" << episodeId;
        clearPendingAutoplayContext();
        clearNextEpisodePrefetchState();
        return;
    }

    // Extract episode info
    const QString episodeName = episodeData.value(QStringLiteral("name")).toString();
    const QString seriesName = episodeData.value(QStringLiteral("seriesName")).toString();
    const int seasonNumber = episodeData.value(QStringLiteral("parentIndexNumber")).toInt();
    const int episodeNumber = episodeData.value(QStringLiteral("indexNumber")).toInt();
    
    qCDebug(lcPlayback) << "PlayerController: Next episode found:" << seriesName 
             << "S" << seasonNumber << "E" << episodeNumber << "-" << episodeName;
    
    // Always emit navigateToNextEpisode to show the Up Next screen
    // The QML screen will handle autoplay countdown vs manual play
    const bool configuredAutoplay = m_config->getAutoplayNextEpisode();
    const bool askStillWatching = configuredAutoplay && shouldAskStillWatchingForUpNext();
    const bool autoplay = configuredAutoplay && !askStillWatching;
    
    qCDebug(lcPlayback) << "PlayerController: Emitting navigateToNextEpisode signal with autoplay:" 
             << autoplay << "stillWatching:" << askStillWatching
             << "carrying track picks from season:" << carrySeasonId;

    emitNavigateToNextEpisodeQueued(episodeData, seriesId, carrySeasonId, autoplay, askStillWatching);
    
    // Note: Don't clear pending autoplay context here - playNextEpisode() needs it
}

/**
 * @brief Handle a failed next-episode lookup.
 *
 * A failed prefetch is ignored; playback end resolves the next episode again.
 * When the end-of-playback lookup fails, Up Next cannot open, so the pending
 * autoplay context is dropped (which ends the Up Next handoff surface) and
 * the display restore parked for Up Next starts.
 */
void PlayerController::onNextEpisodeFailed(const QString &seriesId,
                                           const QString &error,
                                           const QString &requestContext)
{
    if (!m_waitingForNextEpisodeAtPlaybackEnd || !m_shouldAutoplay) {
        return;
    }
    if (!matchesPlaybackRequestContext(requestContext, true)) {
        return;
    }
    if (!m_pendingAutoplaySeriesId.isEmpty() && seriesId != m_pendingAutoplaySeriesId) {
        return;
    }

    qCWarning(lcPlayback) << "Next-episode lookup failed; skipping Up Next"
                          << "seriesId=" << seriesId
                          << "error=" << redactNetworkLogText(error);
    m_shouldAutoplay = false;
    clearNextEpisodePrefetchState();
    clearPendingAutoplayContext();
    if (!isPlaybackActive()) {
        clearSleepTimer();
    }
    maybeStartDeferredPostPlaybackDisplayRestore();
}

void PlayerController::onPlaybackInfoLoaded(const QString &itemId, const PlaybackInfoResponse &playbackInfo)
{
    if (!m_waitingForAutoplayPlaybackInfo) {
        return;
    }

    const QString pendingEpisodeId = m_pendingAutoplayEpisodeData.value(QStringLiteral("itemId")).toString();
    if (itemId != pendingEpisodeId) {
        qCDebug(lcPlayback) << "Ignoring stale autoplay PlaybackInfo response"
                            << "itemId=" << itemId
                            << "pendingEpisodeId=" << pendingEpisodeId;
        return;
    }

    // A matching response can be delivered more than once. Once a descriptor
    // request is in flight, do not replace its context or issue a duplicate.
    if (!m_pendingAutoplayDescriptorContext.isEmpty()) {
        return;
    }
    stopAutoplayPlaybackInfoWait();

    const qint64 startPositionMs = resumePositionMs(m_pendingAutoplayEpisodeData);

    QString targetSeasonId = m_pendingAutoplayEpisodeData.value(QStringLiteral("seasonId")).toString();
    if (targetSeasonId.isEmpty()) {
        targetSeasonId = m_pendingAutoplayEpisodeData.value(QStringLiteral("parentId")).toString();
    }
    if (targetSeasonId.isEmpty()) {
        targetSeasonId = m_pendingAutoplaySeasonId;
    }

    if (playbackInfo.mediaSources.isEmpty()) {
        fallbackToPendingAutoplayPlayback();
        return;
    }

    const QVariantList mediaSources = playbackInfo.getMediaSourcesVariant();
    const QVariantMap mediaSource = mediaSources.isEmpty() ? QVariantMap{} : mediaSources.first().toMap();
    // The finished episode's picks reach a next season that has none of its
    // own by what the tracks are, never by their index in another file.
    const QString carrySeasonId = pendingAutoplayCarrySeasonId(targetSeasonId);
    const ScopedTrackPreferences preferences =
        seasonTrackPreferencesWithCarry(targetSeasonId, carrySeasonId);
    const ResolvedTrackSelection resolved = resolveTrackSelection(mediaSource, preferences);

    m_pendingAutoplayDescriptorItemId = itemId;
    m_pendingAutoplayDescriptorSource = mediaSource;
    m_pendingAutoplayDescriptorAudioIndex = resolved.audioIndex;
    m_pendingAutoplayDescriptorSubtitleIndex = resolved.subtitleIndex;
    m_pendingAutoplayDescriptorStartPositionMs = startPositionMs;
    m_pendingAutoplayDescriptorSessionId = playbackInfo.playSessionId;
    m_pendingAutoplayDescriptorAudioTracks =
        buildAvailableTrackOptions(mediaSource, QStringLiteral("Audio"));
    m_pendingAutoplayDescriptorSubtitleTracks =
        buildAvailableTrackOptions(mediaSource, QStringLiteral("Subtitle"));
    m_pendingAutoplayDescriptorContext =
        QStringLiteral("autoplay|descriptor|") + itemId + QStringLiteral("|")
        + QString::number(m_playbackAttemptId);
    armAutoplayPlaybackInfoWait();
    m_playbackService->requestPlaybackDescriptor(
        itemId,
        mediaSource,
        resolved.audioIndex,
        resolved.subtitleIndex,
        startPositionMs,
        playbackInfo.playSessionId,
        m_pendingAutoplayDescriptorContext);
}

void PlayerController::onPlaybackInfoLoadedForRequest(const QString &itemId,
                                                      const PlaybackInfoResponse &playbackInfo,
                                                      const QString &requestContext)
{
    auto it = m_pendingPlaybackRequests.find(requestContext);
    if (it == m_pendingPlaybackRequests.end() || !it->awaitedPlaybackInfoIds.contains(itemId)) {
        return;
    }

    if (itemId == it->itemId && playbackInfo.mediaSources.isEmpty()) {
        failPendingPlaybackRequest(requestContext, tr("No playable media sources found."));
        return;
    }

    it->playbackInfos.insert(itemId, playbackInfo);
    it->awaitedPlaybackInfoIds.remove(itemId);
    maybeFinalizePendingPlaybackRequest(requestContext);
}

void PlayerController::onPlaybackInfoFailedForRequest(const QString &itemId,
                                                      const QString &error,
                                                      const QString &requestContext)
{
    auto it = m_pendingPlaybackRequests.find(requestContext);
    if (it == m_pendingPlaybackRequests.end() || !it->awaitedPlaybackInfoIds.contains(itemId)) {
        return;
    }

    it->failedPlaybackInfoIds.insert(itemId);
    it->awaitedPlaybackInfoIds.remove(itemId);
    if (itemId == it->itemId) {
        const QString message = error.isEmpty()
            ? tr("Unable to load playback information.")
            : error;
        failPendingPlaybackRequest(requestContext, message);
        return;
    }

    maybeFinalizePendingPlaybackRequest(requestContext);
}

void PlayerController::onPlaybackReplannedForRequest(const QString &sessionId,
                                                     const Bloom::PlaybackDescriptor &descriptor,
                                                     const QString &requestContext)
{
    if (requestContext.isEmpty() || requestContext != m_pendingAudioSwitchContext) {
        return;
    }
    const bool seek = m_pendingReplanIsSeek;
    m_pendingAudioSwitchContext.clear();
    m_pendingAudioSwitchPreviousTrack = -2;
    m_pendingSubtitleReplanPreviousTrack = -2;
    if (sessionId != m_playSessionId || !descriptor.stream.isValid()
        || !(m_playbackState == Playing || m_playbackState == Paused
             || m_playbackState == Buffering)
        || m_activePlaybackSegmentIndex < 0
        || m_activePlaybackSegmentIndex >= m_playbackSegments.size()) {
        m_replanQueued = false;
        m_queuedAudioTrack = kNoQueuedTrack;
        m_queuedSubtitleTrack = kNoQueuedTrack;
        dropHeldTrackPreferenceWrites();
        return;
    }

    QVariantMap &segment = m_playbackSegments[m_activePlaybackSegmentIndex];
    // Rebuild the subtitle menu from the source: the previous stream may have
    // turned entries into sidecars or dropped ones it could not deliver.
    segment[QStringLiteral("availableSubtitleTracks")] =
        buildAvailableTrackOptions(m_activeMediaSource, QStringLiteral("Subtitle"));
    // A track change delivers what the server granted; a reanchor keeps the
    // viewer's current subtitle, which may be a sidecar added locally.
    const int keptSubtitle = m_selectedSubtitleTrack;
    segment[QStringLiteral("subtitleIndex")] = seek ? keptSubtitle : -1;
    applyPlaybackDescriptorToSegment(segment, descriptor);
    if (seek) {
        segment[QStringLiteral("subtitleIndex")] = keptSubtitle;
    }
    if (!descriptor.selectedAudioTrackId.isEmpty()) {
        segment[QStringLiteral("audioIndex")] = descriptor.selectedAudioTrackId.toInt();
    }

    m_pendingUrl = segment.value(QStringLiteral("url")).toString();
    m_playMethod = segment.value(QStringLiteral("playMethod")).toString();
    m_streamPinsAudioTrack = segment.value(QStringLiteral("pinsAudioTrack")).toBool();
    m_streamPinsSubtitleTrack = segment.value(QStringLiteral("pinsSubtitleTrack")).toBool();
    m_pinnedAudioTrack = segment.value(QStringLiteral("pinnedAudioTrack"), -1).toInt();
    m_pinnedSubtitleTrack = segment.value(QStringLiteral("pinnedSubtitleTrack"), -1).toInt();
    m_availableSubtitleTracks = segment.value(QStringLiteral("availableSubtitleTracks")).toList();
    const int previousAudio = m_selectedAudioTrack;
    const int previousSubtitle = m_selectedSubtitleTrack;
    m_selectedAudioTrack = segment.value(QStringLiteral("audioIndex"), -1).toInt();
    m_selectedSubtitleTrack = segment.value(QStringLiteral("subtitleIndex"), -1).toInt();
    loadActiveStreamLayout(segment);
    updateTrackMappings(m_activeMediaSource);
    m_mpvAudioTrack = mpvAudioTrackForSourceIndex(m_selectedAudioTrack);
    m_mpvSubtitleTrack = mpvSubtitleTrackForSourceIndex(m_selectedSubtitleTrack);
    // loadfile drops every sidecar mpv added; they are added again once the
    // new stream has loaded.
    m_externalSubtitleTrackMap.clear();
    m_pendingExternalSubtitleIndex = -1;
    m_pendingExternalSubtitleTracks.clear();
    if (m_useLocalTrickplay) {
        m_localSeekPreview->updateStream(descriptor.stream);
        m_useLocalTrickplay = m_localSeekPreview->isAvailable();
        if (!m_useLocalTrickplay) clearTrickplayPreview();
        emit trickplayStateChanged();
    }
    m_forcePcmAudio = false;
    updateAudioPlan();
    applyAudioPlanOptions();
    // Credentials follow the stream: a later stream on another origin must
    // not inherit the previous one's headers.
    // Embedded backends set list-valued properties as strings, so replace the
    // list entry by entry instead of with set_property.
    m_playerBackend->sendVariantCommand({"change-list", "http-header-fields", "clr", QString()});
    for (auto it = descriptor.stream.headers.cbegin(); it != descriptor.stream.headers.cend(); ++it) {
        m_playerBackend->sendVariantCommand(
            {"change-list", "http-header-fields", "append",
             it.key() + QStringLiteral(": ") + it.value().toString()});
    }
    if (m_streamSeekRequiresReanchor) {
        m_playerBackend->sendVariantCommand(
            {"change-list", "demuxer-lavf-o", "append", "live_start_index=0"});
    }
    const double playerStart = qMax(0.0, m_streamPlayerStartSeconds);
    qCInfo(lcPlayback) << "Replacing the server stream"
                       << "seek=" << seek
                       << "method=" << m_playMethod
                       << "playerStart=" << playerStart
                       << "timelineOffset=" << m_streamTimelineOffsetSeconds;
    m_seekTargetWhileBuffering = -1;
    m_awaitingInPlaceReload = true;
    m_ignoreReplacedFileEnd = true;
    // loadfile replace leaves mpv a one-entry playlist holding the active
    // segment; later segments are appended again once it has loaded.
    m_playlistSegmentOffset = m_activePlaybackSegmentIndex;
    m_playerBackend->sendVariantCommand({"loadfile", m_pendingUrl, "replace", -1,
                                         QStringLiteral("start=%1").arg(playerStart, 0, 'f', 3)});
    if (previousAudio != m_selectedAudioTrack) {
        emit selectedAudioTrackChanged();
    }
    if (previousSubtitle != m_selectedSubtitleTrack) {
        emit selectedSubtitleTrackChanged();
    }
    emit availableTracksChanged();
    emit timelineChanged();

    rollBackUngrantedTrackPicks(m_inFlightAudioTrack, m_inFlightSubtitleTrack);
    if (m_replanQueued) {
        // The new stream has not reported a position yet; it starts here.
        dispatchQueuedReplan(static_cast<double>(descriptor.startPositionMs) / 1000.0);
    }
    releaseHeldTrackPreferenceWrites();
}

void PlayerController::dispatchQueuedReplan(double fallbackSourceSeconds)
{
    m_replanQueued = false;
    const double sourceSeconds = m_queuedReplanSourceSeconds >= 0.0
        ? m_queuedReplanSourceSeconds
        : fallbackSourceSeconds;
    const int queuedAudio = std::exchange(m_queuedAudioTrack, kNoQueuedTrack);
    const int queuedSubtitle = std::exchange(m_queuedSubtitleTrack, kNoQueuedTrack);
    if (queuedAudio == kNoQueuedTrack && queuedSubtitle == kNoQueuedTrack) {
        requestStreamReplan(m_queuedReplanIsSeek, sourceSeconds);
        return;
    }
    // A refusal of the queued request restores what the stream now carries.
    const int grantedAudio = m_selectedAudioTrack;
    const int grantedSubtitle = m_selectedSubtitleTrack;
    m_pendingAudioSwitchPreviousTrack = grantedAudio;
    m_pendingSubtitleReplanPreviousTrack = grantedSubtitle;
    if (queuedAudio != kNoQueuedTrack) {
        m_selectedAudioTrack = queuedAudio;
    }
    if (queuedSubtitle != kNoQueuedTrack) {
        m_selectedSubtitleTrack = queuedSubtitle;
    }
    if (!requestStreamReplan(m_queuedReplanIsSeek, sourceSeconds)) {
        m_selectedAudioTrack = grantedAudio;
        m_selectedSubtitleTrack = grantedSubtitle;
        m_pendingAudioSwitchPreviousTrack = -2;
        m_pendingSubtitleReplanPreviousTrack = -2;
        // The queued picks are undone: the season or movie keeps what plays,
        // and their waiting writes are dropped.
        if (queuedAudio != kNoQueuedTrack) {
            persistAudioPreferenceForCurrentScope(grantedAudio, TrackPick::Restore);
        }
        if (queuedSubtitle != kNoQueuedTrack) {
            persistSubtitlePreferenceForCurrentScope(grantedSubtitle, TrackPick::Restore);
        }
        return;
    }
    // A failed change before this one may have restored the local choice;
    // the queued picks are the user's again, written once they play.
    if (queuedAudio != kNoQueuedTrack) {
        persistAudioPreferenceForCurrentScope(m_selectedAudioTrack, TrackPick::UserAwaitingReplan,
                                              grantedAudio);
    }
    if (queuedSubtitle != kNoQueuedTrack && m_selectedSubtitleTrack >= -1) {
        persistSubtitlePreferenceForCurrentScope(m_selectedSubtitleTrack,
                                                 TrackPick::UserAwaitingReplan, grantedSubtitle);
    }
    if (m_activePlaybackSegmentIndex >= 0
        && m_activePlaybackSegmentIndex < m_playbackSegments.size()) {
        QVariantMap &segment = m_playbackSegments[m_activePlaybackSegmentIndex];
        segment[QStringLiteral("audioIndex")] = m_selectedAudioTrack;
        segment[QStringLiteral("subtitleIndex")] = m_selectedSubtitleTrack;
    }
    if (grantedAudio != m_selectedAudioTrack) {
        emit selectedAudioTrackChanged();
    }
    if (grantedSubtitle != m_selectedSubtitleTrack) {
        emit selectedSubtitleTrackChanged();
    }
}

void PlayerController::onPlaybackReplanFailedForRequest(const QString &sessionId,
                                                        const QString &error,
                                                        const QString &requestContext)
{
    if (requestContext.isEmpty() || requestContext != m_pendingAudioSwitchContext) {
        return;
    }
    const bool seek = m_pendingReplanIsSeek;
    m_pendingAudioSwitchContext.clear();
    const int previousAudioTrack = m_pendingAudioSwitchPreviousTrack;
    const int previousSubtitleTrack = m_pendingSubtitleReplanPreviousTrack;
    m_pendingAudioSwitchPreviousTrack = -2;
    m_pendingSubtitleReplanPreviousTrack = -2;
    if (sessionId != m_playSessionId) {
        m_replanQueued = false;
        m_queuedAudioTrack = kNoQueuedTrack;
        m_queuedSubtitleTrack = kNoQueuedTrack;
        dropHeldTrackPreferenceWrites();
        return;
    }
    if (previousAudioTrack != -2 && previousAudioTrack != m_selectedAudioTrack) {
        m_selectedAudioTrack = previousAudioTrack;
        updateAudioPlan();
        if (m_playerBackend->isRunning()) applyAudioPlanOptions();
        persistAudioPreferenceForCurrentScope(previousAudioTrack, TrackPick::Restore);
        if (m_activePlaybackSegmentIndex >= 0
            && m_activePlaybackSegmentIndex < m_playbackSegments.size()) {
            m_playbackSegments[m_activePlaybackSegmentIndex][QStringLiteral("audioIndex")] =
                previousAudioTrack;
        }
        emit selectedAudioTrackChanged();
    }
    if (previousSubtitleTrack != -2 && previousSubtitleTrack != m_selectedSubtitleTrack) {
        m_selectedSubtitleTrack = previousSubtitleTrack;
        persistSubtitlePreferenceForCurrentScope(previousSubtitleTrack, TrackPick::Restore);
        if (m_activePlaybackSegmentIndex >= 0
            && m_activePlaybackSegmentIndex < m_playbackSegments.size()) {
            m_playbackSegments[m_activePlaybackSegmentIndex][QStringLiteral("subtitleIndex")] =
                previousSubtitleTrack;
        }
        emit selectedSubtitleTrackChanged();
    }
    qCWarning(lcPlayback) << (seek ? "Server seek failed:" : "Server track change failed:")
                          << error;
    if (m_replanQueued) {
        dispatchQueuedReplan(currentReportingPositionMs() / 1000.0);
    }
    // Picks this failure did not undo were delivered by earlier changes.
    releaseHeldTrackPreferenceWrites();
}

void PlayerController::onBackendFileLoaded()
{
    if (!m_awaitingInPlaceReload) {
        return;
    }
    m_awaitingInPlaceReload = false;
    m_ignoreReplacedFileEnd = false;
    if (m_mpvAudioTrack > 0) {
        m_playerBackend->sendVariantCommand({"set_property", "aid", m_mpvAudioTrack});
    }
    if (m_selectedSubtitleTrack == -1) {
        m_playerBackend->sendVariantCommand({"set_property", "sid", "no"});
    } else if (m_mpvSubtitleTrack > 0) {
        m_playerBackend->sendVariantCommand({"set_property", "sid", m_mpvSubtitleTrack});
    }
    queueActiveSegmentExternalSubtitleTracks();
    applySubtitleDelay();
    m_reportProgressOnNextPositionUpdate = true;
    QStringList laterSegmentUrls;
    for (int index = m_activePlaybackSegmentIndex + 1; index < m_playbackSegments.size(); ++index) {
        const QVariantMap segment = m_playbackSegments.at(index);
        if (segment.value(QStringLiteral("deferredDescriptor")).toBool()) {
            break; // appended when its session starts
        }
        const QString url = segment.value(QStringLiteral("url")).toString();
        if (!url.isEmpty()) {
            laterSegmentUrls.append(url);
        }
    }
    if (!laterSegmentUrls.isEmpty()) {
        m_playerBackend->appendUrlsToPlaylist(laterSegmentUrls);
    }
}

void PlayerController::loadActiveStreamLayout(const QVariantMap &segment)
{
    const auto intList = [](const QVariant &value) {
        QList<int> result;
        for (const QVariant &entry : value.toList()) {
            result.append(entry.toInt());
        }
        return result;
    };
    m_streamTimelineOffsetSeconds =
        static_cast<double>(segment.value(QStringLiteral("timelineOffsetMs")).toLongLong()) / 1000.0;
    m_streamPlayerStartSeconds =
        static_cast<double>(segment.value(QStringLiteral("streamStartMs")).toLongLong()) / 1000.0
        - m_streamTimelineOffsetSeconds;
    m_streamSeekRequiresReanchor = segment.value(QStringLiteral("seekRequiresReanchor")).toBool();
    m_streamCarriesSourceTracks =
        segment.value(QStringLiteral("carriesSourceTracks"), true).toBool();
    m_streamSubtitleBurnedIn = segment.value(QStringLiteral("subtitleBurnedIn")).toBool();
    m_streamDeliveredAudioTracks = intList(segment.value(QStringLiteral("deliveredAudioTracks")));
    m_streamDeliveredSubtitleTracks =
        intList(segment.value(QStringLiteral("deliveredSubtitleTracks")));
}

bool PlayerController::requestStreamReplan(bool seek, double sourceSeconds)
{
    if (m_playSessionId.isEmpty() || !m_playbackService) {
        return false;
    }
    if (!m_pendingAudioSwitchContext.isEmpty()) {
        // One replan at a time: each names the plan it replaces. The queued
        // one carries the latest selection and target when it runs.
        // A track change also moves the stream to its position, so a seek and
        // a track change merge into a track change at the seek target.
        const bool wasQueued = m_replanQueued;
        m_queuedReplanIsSeek = seek && (!wasQueued || m_queuedReplanIsSeek);
        if (seek) {
            m_queuedReplanSourceSeconds = sourceSeconds;
        } else if (!wasQueued) {
            m_queuedReplanSourceSeconds = -1.0; // where the stream is when it runs
        }
        if (!seek) {
            // The answer to the replan in flight overwrites the selection (or
            // rolls it back), so remember only what the viewer changed since.
            m_queuedAudioTrack = m_selectedAudioTrack != m_inFlightAudioTrack
                ? m_selectedAudioTrack : kNoQueuedTrack;
            m_queuedSubtitleTrack = m_selectedSubtitleTrack != m_inFlightSubtitleTrack
                ? m_selectedSubtitleTrack : kNoQueuedTrack;
        }
        m_replanQueued = true;
        return true;
    }
    PlaybackReplan replan;
    replan.operation = seek ? PlaybackReplan::Operation::SeekReanchor
                            : PlaybackReplan::Operation::TrackChange;
    replan.audioTrackIndex = m_selectedAudioTrack;
    replan.subtitleTrackIndex = m_selectedSubtitleTrack;
    m_inFlightAudioTrack = m_selectedAudioTrack;
    m_inFlightSubtitleTrack = m_selectedSubtitleTrack;
    for (const QVariant &trackVariant : m_availableSubtitleTracks) {
        const QVariantMap option = trackVariant.toMap();
        if (m_selectedSubtitleTrack != -1
            && option.value(QStringLiteral("index"), -1).toInt() == m_selectedSubtitleTrack) {
            replan.subtitleTrackId = option.value(QStringLiteral("trackId")).toString();
            break;
        }
    }
    replan.positionMs = qMax<qint64>(0, qRound64(sourceSeconds * 1000.0));
    static quint64 replanCounter = 0;
    m_pendingAudioSwitchContext = QStringLiteral("replan|%1|%2")
                                      .arg(m_playbackAttemptId)
                                      .arg(++replanCounter);
    m_pendingReplanIsSeek = seek;
    const QString context = m_pendingAudioSwitchContext;
    const bool dispatched = m_playbackService->replanPlayback(m_playSessionId, replan, context);
    if (!dispatched && m_pendingAudioSwitchContext == context) {
        m_pendingAudioSwitchContext.clear();
    }
    return dispatched;
}

bool PlayerController::canSeekLocally(double playerSeconds) const
{
    if (!m_streamSeekRequiresReanchor) {
        return true;
    }
    // A server-anchored stream holds only what the server has produced from
    // its start: forward seeks inside mpv's cache, or to the plan's own start.
    const double current = m_segmentRelativePosition - m_streamTimelineOffsetSeconds;
    if (qAbs(playerSeconds - m_streamPlayerStartSeconds) < 0.5 && current <= playerSeconds + 0.5) {
        return true;
    }
    return playerSeconds >= current - 0.5 && playerSeconds <= m_cacheEndSeconds - 0.5;
}

void PlayerController::seekToSourcePosition(double sourceSeconds)
{
    double playerSeconds = sourceSeconds - m_streamTimelineOffsetSeconds;
    if (m_streamSeekRequiresReanchor
        && (playerSeconds < 0.0 || !canSeekLocally(playerSeconds))) {
        if (requestStreamReplan(true, qMax(0.0, sourceSeconds))) {
            return;
        }
        qCWarning(lcPlayback) << "Cannot move the server stream to" << sourceSeconds;
        playerSeconds = qMax(0.0, playerSeconds);
    }
    m_playerBackend->sendVariantCommand({"seek", playerSeconds, "absolute"});
}

bool PlayerController::subtitleOptionRequiresReplan(int index) const
{
    for (const QVariant &trackVariant : m_availableSubtitleTracks) {
        const QVariantMap option = trackVariant.toMap();
        if (option.value(QStringLiteral("index"), -1).toInt() == index) {
            return option.value(QStringLiteral("requiresReplan")).toBool();
        }
    }
    return false;
}

bool PlayerController::selectedSubtitleIsSidecar() const
{
    if (m_selectedSubtitleTrack == -1) {
        return false;
    }
    if (m_externalSubtitleTrackMap.contains(m_selectedSubtitleTrack)) {
        return true;
    }
    for (const QVariant &trackVariant : m_availableSubtitleTracks) {
        const QVariantMap option = trackVariant.toMap();
        if (option.value(QStringLiteral("index"), -1).toInt() == m_selectedSubtitleTrack) {
            return option.value(QStringLiteral("isExternal")).toBool();
        }
    }
    return false;
}

void PlayerController::onPlaybackServiceErrorOccurred(const QString &endpoint, const QString &error)
{
    const bool isPlaybackInfoEndpoint = endpoint == QStringLiteral("getPlaybackInfo")
        || endpoint.contains(QStringLiteral("/PlaybackInfo"), Qt::CaseInsensitive);
    if (!m_waitingForAutoplayPlaybackInfo) {
        return;
    }

    if (!isPlaybackInfoEndpoint) {
        return;
    }

    qCWarning(lcPlayback) << "Autoplay PlaybackInfo request failed, falling back to basic playback"
                          << "endpoint=" << endpoint
                          << "error=" << error;
    fallbackToPendingAutoplayPlayback();
}

void PlayerController::onPlaybackServiceNetworkError(const NetworkError &error)
{
    onPlaybackServiceErrorOccurred(error.endpoint, error.userMessage);
}

void PlayerController::onPlaybackSessionLost(const QString &playbackSessionId, bool endedByServer)
{
    const QVariantMap segment = activePlaybackSegment();
    const QString activeSessionId = segment.isEmpty()
        ? m_playSessionId
        : segment.value(QStringLiteral("playSessionId")).toString();
    if (playbackSessionId.isEmpty() || playbackSessionId != activeSessionId
        || !(m_playbackState == Playing || m_playbackState == Paused)) {
        return;
    }
    qCWarning(lcPlayback) << "Server playback session lost"
                          << "sessionId=" << playbackSessionId
                          << "endedByServer=" << endedByServer;
    // An expired session recovers like a dropped stream: a new session at the
    // current position. A server-ended session must not be restarted.
    m_lastErrorWasNetworkRecoverable = !endedByServer;
    setErrorMessage(endedByServer
                    ? tr("The server ended this playback.")
                    : tr("The server playback session expired. Reconnecting..."));
    requestTerminalTransition(TerminalReason::Error);
}

void PlayerController::onAutoplayPlaybackInfoTimeout()
{
    if (!m_waitingForAutoplayPlaybackInfo) {
        return;
    }

    qCWarning(lcPlayback) << "Autoplay PlaybackInfo request timed out, falling back to basic playback";
    fallbackToPendingAutoplayPlayback();
}

void PlayerController::requestPlayback(const QVariantMap &request)
{
    const QString itemId = request.value(QStringLiteral("itemId")).toString();
    if (itemId.isEmpty()) {
        qCWarning(lcPlayback) << "requestPlayback called without itemId";
        return;
    }

    // A retry, recovery or version switch continues the same playback; any
    // other request is a new start for the still-watching count and the
    // sleep timer. Up Next and the episode controls ("continuesWatching":
    // "upNext" or "episodeSkip") keep a running timer.
    if (!request.value(QStringLiteral("continuesSession")).toBool()) {
        m_autoplayChainCount = PlayerPolicy::autoplayChainCountAfterStart(
            m_autoplayChainCount, request.value(QStringLiteral("startedByAutoplay")).toBool());
        setStillWatchingPromptActive(false);
        const QString continuesWatching = request.value(QStringLiteral("continuesWatching")).toString();
        const PlayerPolicy::SleepTimerStart start =
            continuesWatching == QStringLiteral("upNext") ? PlayerPolicy::SleepTimerStart::UpNext
            : continuesWatching == QStringLiteral("episodeSkip") ? PlayerPolicy::SleepTimerStart::EpisodeSkip
                                                                 : PlayerPolicy::SleepTimerStart::New;
        const int choice = PlayerPolicy::sleepTimerChoiceForNewPlayback(
            m_sleepTimerChoice, m_config ? m_config->getSleepTimerDefault() : 0, start);
        armSleepTimer(choice, start != PlayerPolicy::SleepTimerStart::New && choice == m_sleepTimerChoice);
        qCDebug(lcPlayback) << "Playback start"
                            << "autoplayChainCount=" << m_autoplayChainCount
                            << "sleepTimerChoice=" << m_sleepTimerChoice
                            << "sleepTimerRemainingSeconds=" << m_sleepTimerRemainingSeconds;
    }

    // Only keep one pending explicit request at a time.
    const QStringList pendingIds = m_pendingPlaybackRequests.keys();
    for (const QString &pendingId : pendingIds) {
        cancelPendingPlaybackRequest(pendingId);
    }

    PendingPlaybackRequest pending;
    pending.requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    pending.itemId = itemId;
    pending.seriesId = request.value(QStringLiteral("seriesId")).toString();
    pending.seasonId = request.value(QStringLiteral("seasonId")).toString();
    pending.libraryId = request.value(QStringLiteral("libraryId")).toString();
    pending.overlayTitle = request.value(QStringLiteral("overlayTitle")).toString();
    pending.overlaySubtitle = request.value(QStringLiteral("overlaySubtitle")).toString();
    pending.overlayBackdropUrl = request.value(QStringLiteral("overlayBackdropUrl")).toString();
    pending.overlayLogoUrl = request.value(QStringLiteral("overlayLogoUrl")).toString();
    pending.startPositionMs = request.value(QStringLiteral("startPositionMs")).toLongLong();
    pending.preferredAudioIndex = request.value(QStringLiteral("preferredAudioIndex"), -2).toInt();
    pending.preferredSubtitleIndex = request.value(QStringLiteral("preferredSubtitleIndex"), -2).toInt();
    pending.carryTrackPreferencesFromSeasonId =
        request.value(QStringLiteral("carryTrackPreferencesFromSeasonId")).toString();
    pending.isMovie = request.value(QStringLiteral("isMovie")).toBool();
    // "contentKind": "extra" plays a trailer or another local extra on its
    // own: no series context, no resume, no Up Next.
    pending.contentKind = PlayerPolicy::playbackContentKindFromName(
        request.value(QStringLiteral("contentKind")).toString());
    const PlayerPolicy::PlaybackContentRules contentRules =
        PlayerPolicy::playbackContentRules(pending.contentKind);
    if (!contentRules.keepsSeriesContext) {
        pending.seriesId.clear();
        pending.seasonId.clear();
    }
    // A retry, recovery or version switch continues the extra where it was.
    if (!contentRules.resumesFromRequestedPosition
        && !request.value(QStringLiteral("continuesSession")).toBool()) {
        pending.startPositionMs = 0;
    }
    pending.allowVersionPrompt = request.value(QStringLiteral("allowVersionPrompt"), true).toBool();
    pending.useAffinityFallback = request.value(QStringLiteral("useAffinityFallback"), false).toBool();
    // A forced version skips the prompt (in-playback quality switch).
    pending.chosenMediaSourceId = request.value(QStringLiteral("mediaSourceId")).toString();
    pending.restoreFocusTarget = qobject_cast<QObject *>(request.value(QStringLiteral("restoreFocusTarget")).value<QObject *>());
    pending.awaitedPlaybackInfoIds.insert(itemId);

    m_pendingPlaybackRequests.insert(pending.requestId, pending);
    maybeResolvePendingRequestLibraryId(m_pendingPlaybackRequests[pending.requestId]);

    m_playbackService->getPlaybackInfo(itemId, pending.requestId);
    m_playbackService->getAdditionalParts(itemId, pending.requestId);
}

void PlayerController::onItemLibraryResolved(const QString &itemId, const QString &libraryId)
{
    if (itemId.isEmpty() || libraryId.isEmpty()) {
        return;
    }

    m_seriesLibraryResolutionInFlight.remove(itemId);
    m_seriesLibraryIdCache.insert(itemId, libraryId);

    QStringList requestIdsToFinalize;
    for (auto it = m_pendingPlaybackRequests.begin(); it != m_pendingPlaybackRequests.end(); ++it) {
        if (it->seriesId != itemId || !it->awaitingLibraryResolution) {
            continue;
        }

        it->libraryId = libraryId;
        it->awaitingLibraryResolution = false;
        requestIdsToFinalize.append(it.key());
    }

    for (const QString &requestId : std::as_const(requestIdsToFinalize)) {
        maybeFinalizePendingPlaybackRequest(requestId);
    }
}

void PlayerController::onItemLibraryResolutionFailed(const QString &itemId, const QString &error)
{
    if (itemId.isEmpty()) {
        return;
    }

    qCWarning(lcPlayback) << "Failed to resolve library for series"
                          << "seriesId=" << itemId
                          << "error=" << error;
    m_seriesLibraryResolutionInFlight.remove(itemId);

    QStringList requestIdsToFinalize;
    for (auto it = m_pendingPlaybackRequests.begin(); it != m_pendingPlaybackRequests.end(); ++it) {
        if (it->seriesId != itemId || !it->awaitingLibraryResolution) {
            continue;
        }
        it->awaitingLibraryResolution = false;
        requestIdsToFinalize.append(it.key());
    }

    for (const QString &requestId : std::as_const(requestIdsToFinalize)) {
        maybeFinalizePendingPlaybackRequest(requestId);
    }
}

void PlayerController::confirmPlaybackVersion(const QString &requestId, const QString &mediaSourceId)
{
    auto it = m_pendingPlaybackRequests.find(requestId);
    if (it == m_pendingPlaybackRequests.end()) {
        return;
    }

    it->chosenMediaSourceId = mediaSourceId;
    it->versionSelectionRequested = false;
    launchResolvedPlaybackRequest(requestId);
}

void PlayerController::cancelPendingPlaybackRequest(const QString &requestId)
{
    if (requestId.isEmpty()) {
        return;
    }
    m_pendingPlaybackRequests.remove(requestId);
}

void PlayerController::onAdditionalPartsLoaded(const QString &itemId, const QVariantList &parts)
{
    Q_UNUSED(itemId);
    Q_UNUSED(parts);
}

void PlayerController::onAdditionalPartsLoadedForRequest(const QString &itemId,
                                                         const QVariantList &parts,
                                                         const QString &requestContext)
{
    auto it = m_pendingPlaybackRequests.find(requestContext);
    if (it == m_pendingPlaybackRequests.end() || it->itemId != itemId) {
        return;
    }

    it->additionalPartsLoaded = true;
    it->additionalParts = parts;

    for (const QVariant &partValue : parts) {
        const QString partId = partValue.toMap().value(QStringLiteral("itemId")).toString();
        if (partId.isEmpty() || partId == itemId || it->awaitedPlaybackInfoIds.contains(partId)) {
            continue;
        }
        it->awaitedPlaybackInfoIds.insert(partId);
        m_playbackService->getPlaybackInfo(partId, requestContext);
    }

    maybeFinalizePendingPlaybackRequest(requestContext);
}

void PlayerController::onAdditionalPartsFailedForRequest(const QString &itemId,
                                                         const QString &error,
                                                         const QString &requestContext)
{
    Q_UNUSED(error);
    auto it = m_pendingPlaybackRequests.find(requestContext);
    if (it == m_pendingPlaybackRequests.end() || it->itemId != itemId) {
        return;
    }

    it->additionalPartsLoaded = true;
    it->additionalParts = {};
    maybeFinalizePendingPlaybackRequest(requestContext);
}

void PlayerController::maybeFinalizePendingPlaybackRequest(const QString &requestId)
{
    auto it = m_pendingPlaybackRequests.find(requestId);
    if (it == m_pendingPlaybackRequests.end()) {
        return;
    }

    if (!it->additionalPartsLoaded
        || !it->awaitedPlaybackInfoIds.isEmpty()) {
        return;
    }

    if (it->awaitingLibraryResolution) {
        return;
    }

    if (it->primaryPlaybackInfoFailed) {
        failPendingPlaybackRequest(requestId, it->failureMessage);
        return;
    }

    if (!it->playbackInfos.contains(it->itemId)) {
        failPendingPlaybackRequest(requestId, tr("Unable to load playback information."));
        return;
    }

    const PlaybackInfoResponse primaryPlaybackInfo = it->playbackInfos.value(it->itemId);
    const QVariantList primarySources =
        PlayerPolicy::primaryPresentationSources(
            primaryPlaybackInfo.getMediaSourcesVariant());
    if (primarySources.size() > 1
        && it->allowVersionPrompt
        && it->chosenMediaSourceId.isEmpty()
        && PlayerPolicy::parseVersionPreference(m_config->getPlaybackVersionPreference())
               == PlayerPolicy::VersionPreference::Ask) {
        if (!it->versionSelectionRequested) {
            it->versionSelectionRequested = true;
            emit playbackVersionSelectionRequested(requestId,
                                                  buildPlaybackVersionDialogModel(requestId),
                                                  it->restoreFocusTarget);
        }
        return;
    }

    launchResolvedPlaybackRequest(requestId);
}

void PlayerController::launchResolvedPlaybackRequest(const QString &requestId)
{
    auto it = m_pendingPlaybackRequests.find(requestId);
    if (it == m_pendingPlaybackRequests.end()) {
        return;
    }

    const PendingPlaybackRequest request = *it;
    const PlaybackInfoResponse primaryPlaybackInfo =
        request.playbackInfos.value(request.itemId);
    const QVariantList primaryMediaSources =
        PlayerPolicy::primaryPresentationSources(
            primaryPlaybackInfo.getMediaSourcesVariant());

    if (primaryMediaSources.isEmpty()) {
        failPendingPlaybackRequest(requestId, tr("No playable media sources found."));
        return;
    }

    const QVariantMap primarySource = selectMediaSourceForRequest(primaryMediaSources,
                                                                  request.chosenMediaSourceId,
                                                                  request.useAffinityFallback);
    updateVersionAffinityFromMediaSource(primarySource);
    const QVariantList segments = buildMultipartSegments(QVariantMap{
                                                            {QStringLiteral("itemId"), request.itemId},
                                                            {QStringLiteral("seasonId"), request.seasonId},
                                                            {QStringLiteral("preferredAudioIndex"), request.preferredAudioIndex},
                                                            {QStringLiteral("preferredSubtitleIndex"), request.preferredSubtitleIndex},
                                                            {QStringLiteral("carryTrackPreferencesFromSeasonId"),
                                                             request.carryTrackPreferencesFromSeasonId},
                                                            {QStringLiteral("startPositionMs"), request.startPositionMs},
                                                            {QStringLiteral("isMovie"), request.isMovie}
                                                        },
                                                        primarySource,
                                                        primaryPlaybackInfo);
    // A version restart always begins at the first part, so multipart items
    // cannot switch versions mid-playback without losing their place.
    setPlaybackVersions(request.itemId,
                        segments.size() > 1
                            ? QVariantList{}
                            : PlayerPolicy::playbackVersionOptions(
                                  primaryMediaSources,
                                  primarySource.value(QStringLiteral("id")).toString()));
    if (segments.isEmpty()) {
        failPendingPlaybackRequest(requestId, tr("No playable media sources found."));
        return;
    }
    setOverlayMetadata(request.overlayTitle,
                       request.overlaySubtitle,
                       request.overlayBackdropUrl,
                       request.overlayLogoUrl);
    it->descriptorSegments = segments;
    it->descriptors.clear();
    it->deferLaterSegments = segments.size() > 1 && m_playbackService
        && m_playbackService->queuedPlaybackSessionsExpire();
    it->pendingDescriptorCount = it->deferLaterSegments ? 1 : segments.size();
    it->descriptorsRequested = true;
    requestPlaybackDescriptors(requestId);
}

void PlayerController::requestPlaybackDescriptors(const QString &requestId)
{
    auto it = m_pendingPlaybackRequests.constFind(requestId);
    if (it == m_pendingPlaybackRequests.constEnd()) {
        return;
    }

    // Provider test doubles may complete descriptor requests synchronously. Do
    // not retain an iterator into the hash across those callbacks: the final
    // callback removes this request from m_pendingPlaybackRequests.
    const QVariantList descriptorSegments = it->descriptorSegments;
    const int requestedSegments = it->deferLaterSegments ? 1 : descriptorSegments.size();
    for (int index = 0;
         index < requestedSegments && m_pendingPlaybackRequests.contains(requestId);
         ++index) {
        const QVariantMap segment = descriptorSegments.at(index).toMap();
        const QString itemId = segment.value(QStringLiteral("itemId")).toString();
        const QVariantMap source = segment.value(QStringLiteral("mediaSource")).toMap();
        if (itemId.isEmpty() || source.isEmpty()) {
            failPendingPlaybackRequest(requestId, tr("Unable to resolve playback stream."));
            return;
        }
        const QString context = requestId + QStringLiteral("|descriptor|") + QString::number(index);
        m_playbackService->requestPlaybackDescriptor(
            itemId,
            source,
            segment.value(QStringLiteral("audioIndex"), -1).toInt(),
            segment.value(QStringLiteral("subtitleIndex"), -1).toInt(),
            segment.value(QStringLiteral("startPositionMs")).toLongLong(),
            segment.value(QStringLiteral("playSessionId")).toString(),
            context);
    }
}

void PlayerController::appendDescriptorExternalSubtitleTracks(
    QVariantList &availableSubtitleTracks,
    const Bloom::PlaybackDescriptor &descriptor,
    int *selectedSubtitleIndex) const
{
    if (selectedSubtitleIndex == nullptr) {
        return;
    }

    QSet<int> usedIndexes;
    for (const QVariant &trackVariant : availableSubtitleTracks) {
        usedIndexes.insert(trackVariant.toMap().value(QStringLiteral("index"), -1).toInt());
    }

    QSet<int> deliverable;
    int nextSyntheticIndex = -1000;
    for (const Bloom::PlaybackTrack &track : descriptor.subtitleTracks) {
        const QVariantMap trackMap = track.toVariantMap();
        QByteArray encodedUrl;
        if (track.isExternal) {
            const QVariant rawUrl = trackMap.value(QStringLiteral("externalUrl"));
            if (rawUrl.canConvert<QUrl>()) {
                encodedUrl = rawUrl.toUrl().toEncoded(QUrl::FullyEncoded);
            } else {
                encodedUrl = rawUrl.toString().toUtf8();
            }
            if (encodedUrl.isEmpty()) {
                continue;
            }
        } else if (track.sourceIndex < 0 && !track.requiresReplan) {
            // Carried in the stream, but the source does not list it.
            continue;
        }

        // A track the source already lists keeps its menu entry and index; only
        // tracks that exist solely in the descriptor get a synthetic index.
        qsizetype existingPosition = -1;
        if (track.sourceIndex >= 0) {
            for (qsizetype position = 0; position < availableSubtitleTracks.size(); ++position) {
                if (availableSubtitleTracks.at(position).toMap()
                        .value(QStringLiteral("index"), -1).toInt() == track.sourceIndex) {
                    existingPosition = position;
                    break;
                }
            }
        }
        int optionIndex = track.sourceIndex;
        if (existingPosition < 0) {
            while (usedIndexes.contains(nextSyntheticIndex)) {
                --nextSyntheticIndex;
            }
            optionIndex = nextSyntheticIndex--;
            usedIndexes.insert(optionIndex);
        }

        QVariantMap option;
        if (track.isExternal || existingPosition < 0) {
            option[QStringLiteral("index")] = optionIndex;
            option[QStringLiteral("displayTitle")] = track.displayTitle.trimmed().isEmpty()
                ? (track.language.trimmed().isEmpty()
                       ? QStringLiteral("External subtitle")
                       : track.language.trimmed())
                : track.displayTitle;
            option[QStringLiteral("language")] = track.language;
            option[QStringLiteral("codec")] = track.codec;
            option[QStringLiteral("channels")] = 0;
            option[QStringLiteral("channelLayout")] = QString();
            option[QStringLiteral("isDefault")] = track.isDefault;
            option[QStringLiteral("isForced")] = track.isForced;
            option[QStringLiteral("isHearingImpaired")] = track.isHearingImpaired;
        } else {
            // In the stream, burned in, or reached by asking for a new stream:
            // the source's own entry describes it.
            option = availableSubtitleTracks.at(existingPosition).toMap();
        }
        option[QStringLiteral("isExternal")] = track.isExternal;
        if (track.isExternal) {
            option[QStringLiteral("externalUrl")] = QString::fromUtf8(encodedUrl);
        } else {
            option.remove(QStringLiteral("externalUrl"));
        }
        option[QStringLiteral("requiresReplan")] = track.requiresReplan;
        option[QStringLiteral("trackId")] = track.trackId;
        if (existingPosition >= 0) {
            availableSubtitleTracks[existingPosition] = option;
        } else {
            availableSubtitleTracks.append(option);
        }
        deliverable.insert(optionIndex);

        if (!descriptor.selectedSubtitleTrackId.isEmpty()
            && track.trackId == descriptor.selectedSubtitleTrackId) {
            *selectedSubtitleIndex = optionIndex;
        }
    }

    if (descriptor.stream.carriesSourceTracks) {
        return;
    }
    // A packaged stream carries none of the source's own subtitle streams:
    // keep only the tracks the descriptor can deliver.
    QVariantList kept;
    for (const QVariant &trackVariant : availableSubtitleTracks) {
        const QVariantMap option = trackVariant.toMap();
        const int index = option.value(QStringLiteral("index"), -1).toInt();
        if (deliverable.contains(index)
            || (option.value(QStringLiteral("isExternal")).toBool()
                && !option.value(QStringLiteral("externalUrl")).toString().isEmpty())) {
            kept.append(option);
        } else if (*selectedSubtitleIndex == index) {
            *selectedSubtitleIndex = -1;
        }
    }
    availableSubtitleTracks = kept;
}

void PlayerController::applyPlaybackDescriptorToSegment(
    QVariantMap &segment, const Bloom::PlaybackDescriptor &descriptor) const
{
    segment[QStringLiteral("url")] =
        QString::fromUtf8(descriptor.stream.url.toEncoded(QUrl::FullyEncoded));
    segment[QStringLiteral("playMethod")] = Bloom::playbackMethodName(descriptor.stream.method);
    segment[QStringLiteral("pinsAudioTrack")] = descriptor.stream.pinsAudioTrack;
    segment[QStringLiteral("pinsSubtitleTrack")] = descriptor.stream.pinsSubtitleTrack;
    segment[QStringLiteral("pinnedAudioTrack")] =
        parsePinnedTrackId(descriptor.stream.pinnedAudioTrackId);
    segment[QStringLiteral("pinnedSubtitleTrack")] =
        parsePinnedTrackId(descriptor.stream.pinnedSubtitleTrackId);
    segment[QStringLiteral("durationMs")] = descriptor.durationMs;
    const QVariantMap stream = descriptor.stream.toVariantMap();
    for (const char *key : {"timelineOffsetMs", "seekRequiresReanchor", "carriesSourceTracks",
                            "deliveredAudioTracks", "deliveredSubtitleTracks",
                            "subtitleBurnedIn"}) {
        segment[QLatin1String(key)] = stream.value(QLatin1String(key));
    }
    segment[QStringLiteral("streamStartMs")] = descriptor.startPositionMs;
    segment[QStringLiteral("streamHeaders")] = descriptor.stream.headers;
    if (!descriptor.stream.carriesSourceTracks && !descriptor.selectedAudioTrackId.isEmpty()) {
        // The stream carries only the audio the server selected.
        segment[QStringLiteral("audioIndex")] = descriptor.selectedAudioTrackId.toInt();
    }
    if (!descriptor.playbackSessionId.isEmpty()) {
        segment[QStringLiteral("playSessionId")] = descriptor.playbackSessionId;
    }
    if (!descriptor.mediaVersionId.isEmpty()) {
        segment[QStringLiteral("mediaSourceId")] = descriptor.mediaVersionId;
    }
    QVariantList subtitleTracks = segment.value(QStringLiteral("availableSubtitleTracks")).toList();
    // A packaged stream delivers exactly the subtitle the server selected.
    int selectedSubtitleIndex = descriptor.stream.carriesSourceTracks
        ? segment.value(QStringLiteral("subtitleIndex"), -1).toInt()
        : -1;
    appendDescriptorExternalSubtitleTracks(subtitleTracks, descriptor, &selectedSubtitleIndex);
    segment[QStringLiteral("availableSubtitleTracks")] = subtitleTracks;
    segment[QStringLiteral("subtitleIndex")] = selectedSubtitleIndex;
    segment[QStringLiteral("descriptor")] = QVariant::fromValue(descriptor);
}

void PlayerController::maybeFinalizePlaybackDescriptors(const QString &requestId)
{
    auto it = m_pendingPlaybackRequests.find(requestId);
    if (it == m_pendingPlaybackRequests.end() || it->pendingDescriptorCount > 0) {
        return;
    }

    QVariantList segments = it->descriptorSegments;
    for (int index = 0; index < segments.size(); ++index) {
        const QString context = requestId + QStringLiteral("|descriptor|") + QString::number(index);
        const auto descriptorIt = it->descriptors.constFind(context);
        QVariantMap segment = segments.at(index).toMap();
        if (descriptorIt == it->descriptors.constEnd()) {
            if (it->deferLaterSegments && index > 0) {
                segment[QStringLiteral("deferredDescriptor")] = true;
                segments[index] = segment;
                continue;
            }
            failPendingPlaybackRequest(requestId, tr("Unable to resolve playback stream."));
            return;
        }
        applyPlaybackDescriptorToSegment(segment, descriptorIt.value());
        segments[index] = segment;
    }

    const PendingPlaybackRequest request = *it;
    const QVariantMap firstSegment = segments.first().toMap();
    m_nextPlaybackMethod = firstSegment.value(QStringLiteral("playMethod")).toString();
    m_nextStreamPinsAudioTrack = firstSegment.value(QStringLiteral("pinsAudioTrack")).toBool();
    m_nextStreamPinsSubtitleTrack = firstSegment.value(QStringLiteral("pinsSubtitleTrack")).toBool();
    m_nextPinnedAudioTrack = firstSegment.value(QStringLiteral("pinnedAudioTrack"), -1).toInt();
    m_nextPinnedSubtitleTrack = firstSegment.value(QStringLiteral("pinnedSubtitleTrack"), -1).toInt();
    m_nextPlaybackSegments = segments;
    m_nextContentKind = request.contentKind;
    m_nextCarryTrackPreferencesFromSeasonId = request.carryTrackPreferencesFromSeasonId;
    m_nextIsMovie = request.isMovie;
    m_nextPlaylistAppendUrls.clear();
    for (int index = 1; index < segments.size(); ++index) {
        const QVariantMap segment = segments.at(index).toMap();
        if (segment.value(QStringLiteral("deferredDescriptor")).toBool()) {
            break; // mpv playlist positions must stay aligned with segment indexes
        }
        const QString url = segment.value(QStringLiteral("url")).toString();
        if (!url.isEmpty()) {
            m_nextPlaylistAppendUrls.append(url);
        }
    }
    playUrlWithTracks(firstSegment.value(QStringLiteral("url")).toString(),
                      request.itemId,
                      request.startPositionMs,
                      request.seriesId,
                      request.seasonId,
                      request.libraryId,
                      firstSegment.value(QStringLiteral("mediaSourceId")).toString(),
                      firstSegment.value(QStringLiteral("playSessionId")).toString(),
                      firstSegment.value(QStringLiteral("mediaSource")).toMap(),
                      firstSegment.value(QStringLiteral("audioIndex"), -1).toInt(),
                      firstSegment.value(QStringLiteral("subtitleIndex"), -1).toInt(),
                      firstSegment.value(QStringLiteral("availableAudioTracks")).toList(),
                      firstSegment.value(QStringLiteral("availableSubtitleTracks")).toList(),
                      firstSegment.value(QStringLiteral("durationMs")).toLongLong(),
                      firstSegment.value(QStringLiteral("framerate")).toDouble(),
                      firstSegment.value(QStringLiteral("isHDR")).toBool(),
                      firstSegment.value(QStringLiteral("toneMapToSdr")).toBool());
    m_pendingPlaybackRequests.remove(requestId);
}

void PlayerController::onPlaybackDescriptorLoadedForRequest(
    const QString &itemId, const Bloom::PlaybackDescriptor &descriptor, const QString &requestContext)
{
    if (requestContext.startsWith(QStringLiteral("autoplay|descriptor|"))) {
        if (requestContext != m_pendingAutoplayDescriptorContext
            || itemId != m_pendingAutoplayDescriptorItemId
            || !descriptor.stream.isValid()) {
            return;
        }
        const QString url =
            QString::fromUtf8(descriptor.stream.url.toEncoded(QUrl::FullyEncoded));
        const QString autoplaySeriesId = m_pendingAutoplaySeriesId;
        const QString autoplaySeasonId = m_pendingAutoplaySeasonId;
        const QString autoplayLibraryId = m_pendingAutoplayLibraryId;
        const QString episodeSeasonId = m_pendingAutoplayEpisodeData.value(
            QStringLiteral("seasonId")).toString();
        const QString targetSeasonId = episodeSeasonId.isEmpty()
            ? m_pendingAutoplayEpisodeData.value(QStringLiteral("parentId")).toString()
            : episodeSeasonId;
        const QString resolvedSeasonId = targetSeasonId.isEmpty()
            ? autoplaySeasonId : targetSeasonId;
        const QString mediaSourceId = descriptor.mediaVersionId.isEmpty()
            ? m_pendingAutoplayDescriptorSource.value(QStringLiteral("id")).toString()
            : descriptor.mediaVersionId;
        const QString sessionId = descriptor.playbackSessionId.isEmpty()
            ? m_pendingAutoplayDescriptorSessionId : descriptor.playbackSessionId;
        QVariantMap autoplaySegment{
            {QStringLiteral("itemId"), itemId},
            {QStringLiteral("mediaSourceId"), mediaSourceId},
            {QStringLiteral("playSessionId"), sessionId},
            {QStringLiteral("mediaSource"), m_pendingAutoplayDescriptorSource},
            {QStringLiteral("audioIndex"), m_pendingAutoplayDescriptorAudioIndex},
            {QStringLiteral("subtitleIndex"), m_pendingAutoplayDescriptorSubtitleIndex},
            {QStringLiteral("availableAudioTracks"), m_pendingAutoplayDescriptorAudioTracks},
            {QStringLiteral("availableSubtitleTracks"), m_pendingAutoplayDescriptorSubtitleTracks}
        };
        applyPlaybackDescriptorToSegment(autoplaySegment, descriptor);
        const QVariantList autoplaySubtitleTracks =
            autoplaySegment.value(QStringLiteral("availableSubtitleTracks")).toList();
        const int autoplaySubtitleIndex = autoplaySegment.value(QStringLiteral("subtitleIndex"), -1).toInt();
        stopAutoplayPlaybackInfoWait();
        m_nextPlaybackMethod = Bloom::playbackMethodName(descriptor.stream.method);
        m_nextStreamPinsAudioTrack = descriptor.stream.pinsAudioTrack;
        m_nextStreamPinsSubtitleTrack = descriptor.stream.pinsSubtitleTrack;
        m_nextPinnedAudioTrack = parsePinnedTrackId(descriptor.stream.pinnedAudioTrackId);
        m_nextPinnedSubtitleTrack = parsePinnedTrackId(descriptor.stream.pinnedSubtitleTrackId);
        m_nextPlaybackSegments = {autoplaySegment};
        m_nextCarryTrackPreferencesFromSeasonId = pendingAutoplayCarrySeasonId(resolvedSeasonId);
        playUrlWithTracks(url,
                          itemId,
                          m_pendingAutoplayDescriptorStartPositionMs,
                          autoplaySeriesId,
                          resolvedSeasonId,
                          autoplayLibraryId,
                          mediaSourceId,
                          sessionId,
                          m_pendingAutoplayDescriptorSource,
                          m_pendingAutoplayDescriptorAudioIndex,
                          autoplaySubtitleIndex,
                          m_pendingAutoplayDescriptorAudioTracks,
                          autoplaySubtitleTracks,
                          descriptor.durationMs,
                          m_pendingAutoplayDescriptorSource.isEmpty()
                              ? m_pendingAutoplayFramerate
                              : videoFramerateForMediaSource(m_pendingAutoplayDescriptorSource),
                          m_pendingAutoplayDescriptorSource.isEmpty()
                              ? m_pendingAutoplayIsHDR
                              : mediaSourceIsHdr(m_pendingAutoplayDescriptorSource),
                          m_pendingAutoplayDescriptorSource.isEmpty()
                              ? m_pendingAutoplayToneMapToSdr
                              : PlayerPolicy::shouldToneMapToSdr(
                                  PlayerPolicy::classifyMediaSourceHdr(
                                      m_pendingAutoplayDescriptorSource),
                                  m_config->getDolbyVisionFallbackMode()));
        m_pendingAutoplayDescriptorContext.clear();
        return;
    }

    if (requestContext.startsWith(QStringLiteral("segment|"))) {
        // segment|<playback attempt>|<segment index>
        const QStringList parts = requestContext.split(QLatin1Char('|'));
        if (requestContext != m_deferredSegmentContext || parts.size() != 3
            || parts.at(1).toULongLong() != m_playbackAttemptId) {
            return;
        }
        m_deferredSegmentContext.clear();
        const int index = parts.at(2).toInt();
        if (!descriptor.stream.isValid() || index <= 0 || index >= m_playbackSegments.size()
            || m_playbackSegments.at(index).value(QStringLiteral("itemId")).toString() != itemId
            || !m_playbackSegments.at(index).value(QStringLiteral("deferredDescriptor")).toBool()) {
            return;
        }
        QVariantMap &segment = m_playbackSegments[index];
        applyPlaybackDescriptorToSegment(segment, descriptor);
        segment.remove(QStringLiteral("deferredDescriptor"));
        qCInfo(lcPlayback) << "Appending deferred multipart segment" << index;
        m_playerBackend->appendUrlsToPlaylist({segment.value(QStringLiteral("url")).toString()});
        return;
    }

    const int marker = requestContext.lastIndexOf(QStringLiteral("|descriptor|"));
    if (marker <= 0) {
        return;
    }
    const QString requestId = requestContext.left(marker);
    auto it = m_pendingPlaybackRequests.find(requestId);
    if (it == m_pendingPlaybackRequests.end()
        || !descriptor.stream.isValid()
        || !it->descriptorsRequested) {
        return;
    }
    const int index = requestContext.mid(marker + QStringLiteral("|descriptor|").size()).toInt();
    if (index < 0 || index >= it->descriptorSegments.size()
        || it->descriptorSegments.at(index).toMap().value(QStringLiteral("itemId")).toString() != itemId) {
        return;
    }
    if (!it->descriptors.contains(requestContext)) {
        it->descriptors.insert(requestContext, descriptor);
        it->pendingDescriptorCount = qMax(0, it->pendingDescriptorCount - 1);
    }
    maybeFinalizePlaybackDescriptors(requestId);
}
void PlayerController::onPlaybackDescriptorFailedForRequest(
    const QString &itemId, const QString &error, const QString &requestContext)
{
    if (requestContext.startsWith(QStringLiteral("segment|"))) {
        if (requestContext == m_deferredSegmentContext) {
            m_deferredSegmentContext.clear();
            qCWarning(lcPlayback) << "Deferred multipart segment failed; playback ends with"
                                  << "the current part" << itemId << error;
        }
        return;
    }
    if (requestContext == m_pendingAutoplayDescriptorContext) {
        qCWarning(lcPlayback) << "Autoplay descriptor failed" << itemId << error;
        stopAutoplayPlaybackInfoWait();
        clearPendingAutoplayContext();
        clearNextEpisodePrefetchState();
        return;
    }
    const int marker = requestContext.lastIndexOf(QStringLiteral("|descriptor|"));
    if (marker <= 0) {
        return;
    }
    const QString requestId = requestContext.left(marker);
    if (m_pendingPlaybackRequests.contains(requestId)) {
        failPendingPlaybackRequest(requestId, error);
    }
}

void PlayerController::failPendingPlaybackRequest(const QString &requestId, const QString &message)
{
    auto it = m_pendingPlaybackRequests.find(requestId);
    if (it == m_pendingPlaybackRequests.end()) {
        return;
    }

    const QString errorMessage = message.isEmpty()
        ? tr("Unable to start playback.")
        : message;
    m_pendingPlaybackRequests.erase(it);
    setErrorMessage(errorMessage);
    if (m_playbackState == Idle) {
        // Nothing replaces the playback that parked a display restore (Up
        // Next's Play, a title started from the after-movie screen's
        // shelf): give the display back.
        maybeStartDeferredPostPlaybackDisplayRestore();
        processEvent(Event::ErrorOccurred);
    }
}

QVariantMap PlayerController::buildPlaybackVersionDialogModel(const QString &requestId) const
{
    const auto it = m_pendingPlaybackRequests.constFind(requestId);
    if (it == m_pendingPlaybackRequests.constEnd()) {
        return QVariantMap{};
    }

    const PlaybackInfoResponse playbackInfo = it->playbackInfos.value(it->itemId);
    const QVariantList options = PlayerPolicy::playbackVersionOptions(
        PlayerPolicy::primaryPresentationSources(playbackInfo.getMediaSourcesVariant()),
        QString());

    return QVariantMap{
        {QStringLiteral("title"), tr("Select Version")},
        {QStringLiteral("subtitle"), it->overlayTitle},
        {QStringLiteral("options"), options}
    };
}

QVariantMap PlayerController::selectMediaSourceForRequest(const QVariantList &mediaSources,
                                                          const QString &forcedMediaSourceId,
                                                          bool useAffinityFallback) const
{
    return PlayerPolicy::selectMediaSource(
        mediaSources,
        forcedMediaSourceId,
        useAffinityFallback,
        PlayerPolicy::VersionAffinity{
            m_lastVersionParentPath,
            m_lastVersionName,
            m_lastVersionSignature
        },
        PlayerPolicy::parseVersionPreference(m_config->getPlaybackVersionPreference()));
}

QVariantMap PlayerController::resolveSegmentPlaybackContext(const ScopedTrackPreferences &trackPreferences,
                                                            const PlaybackInfoResponse &playbackInfo,
                                                            const QVariantList &mediaSources,
                                                            const QVariantMap &preferredMediaSource,
                                                            int preferredAudioIndex,
                                                            int preferredSubtitleIndex,
                                                            bool useAffinityFallback) const
{
    const QString forcedMediaSourceId = preferredMediaSource.value(QStringLiteral("id")).toString();
    const QVariantMap resolvedSource = selectMediaSourceForRequest(mediaSources,
                                                                   forcedMediaSourceId,
                                                                   useAffinityFallback);
    if (resolvedSource.isEmpty()) {
        return QVariantMap{};
    }

    const ResolvedTrackSelection resolvedTracks = resolveTrackSelection(resolvedSource,
                                                                        trackPreferences,
                                                                        preferredAudioIndex,
                                                                        preferredSubtitleIndex);
    const PlayerPolicy::HdrContentKind hdrKind =
        PlayerPolicy::classifyMediaSourceHdr(resolvedSource);
    return QVariantMap{
        {QStringLiteral("mediaSource"), resolvedSource},
        {QStringLiteral("mediaSourceId"), resolvedSource.value(QStringLiteral("id")).toString()},
        {QStringLiteral("playSessionId"), playbackInfo.playSessionId},
        {QStringLiteral("audioIndex"), resolvedTracks.audioIndex},
        {QStringLiteral("subtitleIndex"), resolvedTracks.subtitleIndex},
        {QStringLiteral("availableAudioTracks"), buildAvailableTrackOptions(resolvedSource, QStringLiteral("Audio"))},
        {QStringLiteral("availableSubtitleTracks"), buildAvailableTrackOptions(resolvedSource, QStringLiteral("Subtitle"))},
        {QStringLiteral("framerate"), videoFramerateForMediaSource(resolvedSource)},
        {QStringLiteral("isHDR"), PlayerPolicy::isHdr(hdrKind)},
        {QStringLiteral("hdrKind"), PlayerPolicy::hdrContentKindName(hdrKind)},
        {QStringLiteral("toneMapToSdr"), PlayerPolicy::shouldToneMapToSdr(
             hdrKind, m_config->getDolbyVisionFallbackMode())}
    };
}

QVariantList PlayerController::buildMultipartSegments(const QVariantMap &request,
                                                      const QVariantMap &primarySource,
                                                      const PlaybackInfoResponse &primaryPlaybackInfo)
{
    QVariantList segments;
    const QString itemId = request.value(QStringLiteral("itemId")).toString();
    const QString scopeId = request.value(QStringLiteral("isMovie")).toBool()
        ? itemId
        : request.value(QStringLiteral("seasonId")).toString();
    const int preferredAudioIndex = request.value(QStringLiteral("preferredAudioIndex"), -2).toInt();
    const int preferredSubtitleIndex = request.value(QStringLiteral("preferredSubtitleIndex"), -2).toInt();
    const bool isMovie = request.value(QStringLiteral("isMovie")).toBool();
    const ScopedTrackPreferences trackPreferences = isMovie
        ? m_trackPrefs->getMoviePreferences(scopeId)
        : seasonTrackPreferencesWithCarry(
              scopeId,
              request.value(QStringLiteral("carryTrackPreferencesFromSeasonId")).toString());

    const QVariantMap primaryContext = resolveSegmentPlaybackContext(trackPreferences,
                                                                     primaryPlaybackInfo,
                                                                     primaryPlaybackInfo.getMediaSourcesVariant(),
                                                                     primarySource,
                                                                     preferredAudioIndex,
                                                                     preferredSubtitleIndex,
                                                                     false);
    if (primaryContext.isEmpty()) {
        return segments;
    }

    QVariantMap primarySegment = primaryContext;
    primarySegment[QStringLiteral("itemId")] = itemId;
    primarySegment[QStringLiteral("startPositionMs")] =
        request.value(QStringLiteral("startPositionMs")).toLongLong();
    segments.append(primarySegment);
    const int primaryPartIndex =
        primarySource.value(QStringLiteral("presentationPartIndex")).toInt();
    const int partTotal =
        primarySource.value(QStringLiteral("presentationPartTotal")).toInt();
    if (primaryPartIndex > 0 && partTotal > 1) {
        const QString variantId =
            primarySource.value(QStringLiteral("playbackVariantId")).toString();
        const QVariantList allSources = primaryPlaybackInfo.getMediaSourcesVariant();
        for (int partIndex = primaryPartIndex + 1; partIndex <= partTotal; ++partIndex) {
            QVariantMap partSource;
            for (const QVariant &sourceValue : allSources) {
                const QVariantMap candidate = sourceValue.toMap();
                if (candidate.value(QStringLiteral("presentationPartIndex")).toInt()
                        != partIndex) {
                    continue;
                }
                if (!variantId.isEmpty()
                    && candidate.value(QStringLiteral("playbackVariantId")).toString()
                        != variantId) {
                    continue;
                }
                partSource = candidate;
                break;
            }
            if (partSource.isEmpty()) {
                return {};
            }

            const QVariantMap partContext = resolveSegmentPlaybackContext(
                trackPreferences, primaryPlaybackInfo, allSources, partSource,
                preferredAudioIndex, preferredSubtitleIndex, false);
            if (partContext.isEmpty()) {
                return {};
            }
            QVariantMap segment = partContext;
            segment[QStringLiteral("itemId")] = itemId;
            segment[QStringLiteral("startPositionMs")] = 0;
            segments.append(segment);
        }
        return segments;
    }

    for (const PendingPlaybackRequest &pendingRequest : m_pendingPlaybackRequests) {
        if (pendingRequest.itemId != itemId) {
            continue;
        }

        for (const QVariant &partValue : pendingRequest.additionalParts) {
            const QString partId = partValue.toMap().value(QStringLiteral("itemId")).toString();
            if (partId.isEmpty()) {
                continue;
            }

            const PlaybackInfoResponse partPlaybackInfo = pendingRequest.playbackInfos.value(partId);
            if (partPlaybackInfo.mediaSources.isEmpty()) {
                continue;
            }

            const QVariantMap partContext = resolveSegmentPlaybackContext(trackPreferences,
                                                                          partPlaybackInfo,
                                                                          partPlaybackInfo.getMediaSourcesVariant(),
                                                                          primarySource,
                                                                          preferredAudioIndex,
                                                                          preferredSubtitleIndex,
                                                                          true);
            if (partContext.isEmpty()) {
                continue;
            }

            QVariantMap segment = partContext;
            segment[QStringLiteral("itemId")] = partId;
            segment[QStringLiteral("startPositionMs")] = 0;
            segments.append(segment);
        }
        break;
    }

    return segments;
}

void PlayerController::clearPlaybackSegments()
{
    m_useLocalTrickplay = false;
    m_localSeekPreview->clear();
    m_trickplayProcessor->clear();
    m_playbackSegments.clear();
    m_activePlaybackSegmentIndex = -1;
    m_activePlaybackSegmentOffsetMs = 0;
    m_segmentRelativePosition = 0.0;
    m_aggregatePlaybackDuration = 0.0;
    m_pendingPlaylistAppendUrls.clear();
}

QVariantMap PlayerController::activePlaybackSegment() const
{
    if (m_activePlaybackSegmentIndex < 0 || m_activePlaybackSegmentIndex >= m_playbackSegments.size()) {
        return QVariantMap{};
    }
    return m_playbackSegments.at(m_activePlaybackSegmentIndex);
}

QString PlayerController::activePlaybackSegmentItemId() const
{
    const QVariantMap segment = activePlaybackSegment();
    if (!segment.isEmpty()) {
        return segment.value(QStringLiteral("itemId")).toString();
    }
    return m_currentItemId;
}

void PlayerController::applyPlaybackSegment(int index, bool reportSegmentStart)
{
    if (index < 0 || index >= m_playbackSegments.size()) {
        return;
    }

    m_activePlaybackSegmentIndex = index;
    m_activePlaybackSegmentOffsetMs = 0;
    for (int i = 0; i < index; ++i) {
        m_activePlaybackSegmentOffsetMs += m_playbackSegments.at(i).value(QStringLiteral("durationMs")).toLongLong();
    }
    m_segmentRelativePosition = 0.0;

    const QVariantMap segment = m_playbackSegments.at(index);
    m_useLocalTrickplay = false;
    m_trickplayProcessor->clear();
    m_localSeekPreview->clear();
    m_mediaSourceId = segment.value(QStringLiteral("mediaSourceId")).toString();
    m_playSessionId = segment.value(QStringLiteral("playSessionId")).toString();
    const QString segmentPlayMethod = segment.value(QStringLiteral("playMethod")).toString();
    if (!segmentPlayMethod.isEmpty()) {
        m_playMethod = segmentPlayMethod;
    }
    m_streamPinsAudioTrack = segment.value(QStringLiteral("pinsAudioTrack")).toBool();
    m_streamPinsSubtitleTrack = segment.value(QStringLiteral("pinsSubtitleTrack")).toBool();
    m_pinnedAudioTrack = segment.value(QStringLiteral("pinnedAudioTrack"), -1).toInt();
    m_pinnedSubtitleTrack = segment.value(QStringLiteral("pinnedSubtitleTrack"), -1).toInt();
    m_activeMediaSource = segment.value(QStringLiteral("mediaSource")).toMap();
    m_availableAudioTracks = segment.value(QStringLiteral("availableAudioTracks")).toList();
    m_availableSubtitleTracks = segment.value(QStringLiteral("availableSubtitleTracks")).toList();
    m_selectedAudioTrack = segment.value(QStringLiteral("audioIndex"), -1).toInt();
    m_selectedSubtitleTrack = segment.value(QStringLiteral("subtitleIndex"), -1).toInt();
    // A playlist position switch changes the external subtitle namespace as well as
    // the embedded stream maps. Drop stale mpv ids before queuing this segment's URLs.
    m_externalSubtitleTrackMap.clear();
    m_pendingExternalSubtitleIndex = -1;
    m_mpvSubtitleTrack = -1;
    loadActiveStreamLayout(segment);
    updateTrackMappings(m_activeMediaSource);
    m_mpvAudioTrack = mpvAudioTrackForSourceIndex(m_selectedAudioTrack);
    m_forcePcmAudio = false;
    updateAudioPlan();
    if (m_playerBackend->isRunning()) applyAudioPlanOptions();
    m_mpvSubtitleTrack = m_selectedSubtitleTrack >= 0 ? mpvSubtitleTrackForSourceIndex(m_selectedSubtitleTrack) : -1;
    queueActiveSegmentExternalSubtitleTracks();

    emit mediaSourceIdChanged();
    emit playSessionIdChanged();
    emit selectedAudioTrackChanged();
    emit selectedSubtitleTrackChanged();
    emit availableTracksChanged();

    m_currentSegments.clear();
    m_isInIntroSegment = false;
    m_isInOutroSegment = false;
    m_autoSkippedSegments.clear();
    m_activeIntroSkipKey.clear();
    m_activeOutroSkipKey.clear();
    emit skipSegmentsChanged();

    m_hasTrickplayInfo = false;
    m_currentTrickplayInfo = TrickplayTileInfo{};
    m_trickplayBinaryPath.clear();
    m_currentTrickplayFrameIndex = -1;
    m_hasTrickplayPreviewPositionOverride = false;
    m_trickplayPreviewPositionOverrideSeconds = 0.0;
    clearTrickplayPreview();
    emit trickplayStateChanged();

    const QString activeItemId = segment.value(QStringLiteral("itemId")).toString();
    if (!activeItemId.isEmpty()) {
        m_playbackService->getMediaSegments(activeItemId, m_mediaSourceId);
        requestTrickplayInfo(activeItemId);
    }

    if (reportSegmentStart) {
        reportPlaybackStartForSegment(segment);
    }
}

qint64 PlayerController::currentReportingPositionMs() const
{
    if (m_activePlaybackSegmentIndex >= 0) {
        return qRound64(m_segmentRelativePosition * 1000.0);
    }
    return qRound64(m_currentPosition * 1000.0);
}

void PlayerController::reportPlaybackStartForSegment(const QVariantMap &segment)
{
    if (segment.isEmpty() || !m_playbackService) {
        return;
    }

    const QString itemId = segment.value(QStringLiteral("itemId")).toString();
    if (itemId.isEmpty()) {
        return;
    }

    m_playbackService->reportPlaybackStart(itemId,
                                           segment.value(QStringLiteral("mediaSourceId")).toString(),
                                           segment.value(QStringLiteral("audioIndex"), -1).toInt(),
                                           segment.value(QStringLiteral("subtitleIndex"), -1).toInt(),
                                           segment.value(QStringLiteral("playSessionId")).toString(),
                                           m_duration > 0.0,
                                           false,
                                           m_muted,
                                           m_playMethod);
}

void PlayerController::reportPlaybackStopForSegment(const QVariantMap &segment, qint64 positionMs)
{
    if (segment.isEmpty() || !m_playbackService) {
        return;
    }

    const QString itemId = segment.value(QStringLiteral("itemId")).toString();
    if (itemId.isEmpty()) {
        return;
    }

    m_playbackService->reportPlaybackStopped(itemId,
                                             positionMs,
                                             segment.value(QStringLiteral("mediaSourceId")).toString(),
                                             segment.value(QStringLiteral("audioIndex"), -1).toInt(),
                                             segment.value(QStringLiteral("subtitleIndex"), -1).toInt(),
                                             segment.value(QStringLiteral("playSessionId")).toString(),
                                             true,
                                             m_playbackState == Paused,
                                             m_muted,
                                             m_playMethod);
}

void PlayerController::updateVersionAffinityFromMediaSource(const QVariantMap &mediaSource)
{
    m_lastVersionName = mediaSource.value(QStringLiteral("name")).toString();
    m_lastVersionParentPath = mediaSourceParentPath(mediaSource);
    m_lastVersionSignature = mediaSourceSignature(mediaSource);
}

QString PlayerController::mediaSourceParentPath(const QVariantMap &mediaSource) const
{
    return PlayerPolicy::mediaSourceParentPath(mediaSource);
}

QString PlayerController::mediaSourceSignature(const QVariantMap &mediaSource) const
{
    return PlayerPolicy::mediaSourceSignature(mediaSource);
}

QString PlayerController::buildVersionSubtitle(const QVariantMap &mediaSource) const
{
    return PlayerPolicy::buildVersionSubtitle(mediaSource);
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
 * @param episodeData Canonical episode model with camelCase fields and millisecond timing.
 * @param seriesId Series identifier associated with the episode (used for playback metadata).
 */
void PlayerController::playNextEpisode(const QVariantMap &episodeData, const QString &seriesId,
                                       bool startedByAutoplay)
{
    const QString episodeId = episodeData.value(QStringLiteral("itemId")).toString();
    const QString episodeName = episodeData.value(QStringLiteral("name")).toString();
    const QString seriesName = episodeData.value(QStringLiteral("seriesName")).toString();
    const int seasonNumber = episodeData.value(QStringLiteral("parentIndexNumber")).toInt();
    const int episodeNumber = episodeData.value(QStringLiteral("indexNumber")).toInt();
    
    if (episodeId.isEmpty()) {
        qCWarning(lcPlayback) << "PlayerController::playNextEpisode: Empty episode ID";
        cancelPendingDisplayRestore();
        clearPendingAutoplayContext();
        return;
    }

    cancelPendingDisplayRestore();
    
    qCDebug(lcPlayback) << "PlayerController: Playing next episode from Up Next screen:" << seriesName
             << "S" << seasonNumber << "E" << episodeNumber << "-" << episodeName;
    
    const QString subtitle = episodeOverlaySubtitle(episodeData);
    const QString overlayLogoUrl = (m_libraryService && !seriesId.isEmpty())
        ? m_libraryService->getCachedImageUrlWithWidth(seriesId, QStringLiteral("Logo"), 600)
        : QString();
    setOverlayMetadata(seriesName.isEmpty() ? QStringLiteral("Now Playing") : seriesName,
                       subtitle,
                       QString(),
                       overlayLogoUrl);

    emit autoplayingNextEpisode(episodeName, seriesName);

    QString targetSeasonId = episodeData.value(QStringLiteral("seasonId")).toString();
    if (targetSeasonId.isEmpty()) {
        targetSeasonId = episodeData.value(QStringLiteral("parentId")).toString();
    }
    if (targetSeasonId.isEmpty()) {
        targetSeasonId = m_pendingAutoplaySeasonId;
    }

    const qint64 startPositionMs = resumePositionMs(episodeData);

    QVariantMap request;
    request[QStringLiteral("itemId")] = episodeId;
    request[QStringLiteral("seriesId")] = seriesId;
    request[QStringLiteral("seasonId")] = targetSeasonId;
    request[QStringLiteral("libraryId")] = m_pendingAutoplayLibraryId;
    request[QStringLiteral("startPositionMs")] = startPositionMs;
    request[QStringLiteral("overlayTitle")] = seriesName.isEmpty() ? QStringLiteral("Now Playing") : seriesName;
    request[QStringLiteral("overlaySubtitle")] = subtitle;
    if (!overlayLogoUrl.isEmpty()) {
        request[QStringLiteral("overlayLogoUrl")] = overlayLogoUrl;
    }
    request[QStringLiteral("isMovie")] = false;
    request[QStringLiteral("allowVersionPrompt")] = false;
    request[QStringLiteral("useAffinityFallback")] = true;
    request[QStringLiteral("carryTrackPreferencesFromSeasonId")] =
        pendingAutoplayCarrySeasonId(targetSeasonId);
    request[QStringLiteral("startedByAutoplay")] = startedByAutoplay;
    request[QStringLiteral("continuesWatching")] = QStringLiteral("upNext");
    requestPlayback(request);
}

void PlayerController::maybeResolvePendingRequestLibraryId(PlayerController::PendingPlaybackRequest &pending)
{
    pending.awaitingLibraryResolution = false;

    if (!pending.libraryId.isEmpty() || pending.seriesId.isEmpty()) {
        return;
    }

    if (pending.seriesId == m_currentSeriesId && !m_currentLibraryId.isEmpty()) {
        pending.libraryId = m_currentLibraryId;
        return;
    }

    if (pending.seriesId == m_pendingAutoplaySeriesId && !m_pendingAutoplayLibraryId.isEmpty()) {
        pending.libraryId = m_pendingAutoplayLibraryId;
        return;
    }

    const auto cachedIt = m_seriesLibraryIdCache.constFind(pending.seriesId);
    if (cachedIt != m_seriesLibraryIdCache.constEnd() && !cachedIt.value().isEmpty()) {
        pending.libraryId = cachedIt.value();
        return;
    }

    if (!m_libraryService) {
        return;
    }

    pending.awaitingLibraryResolution = true;
    if (!m_seriesLibraryResolutionInFlight.contains(pending.seriesId)) {
        m_seriesLibraryResolutionInFlight.insert(pending.seriesId);
        m_libraryService->resolveLibraryForItem(pending.seriesId);
    }
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
    case Error:
        return m_recoveryContext.valid || m_isRecovering;
    case Idle:
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

bool PlayerController::isStartupBuffering() const
{
    return m_playbackState == Loading || m_waitingForRemoteMountInitialCache;
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
 * reports/stops any currently running backend playback, replaces the current item context
 * with the configured test video, and triggers the player state machine to begin loading it.
 */
void PlayerController::playTestVideo()
{
    clearPendingAutoplayContext();
    clearNextEpisodePrefetchState();
    m_shouldAutoplay = false;

    scheduleReplacementPlayback([this]() {
        m_currentSeriesId.clear();
        m_currentIsMovie = false;
        m_currentSeasonId.clear();
        m_currentLibraryId.clear();
        m_contentFramerate = 0.0;
        emit subtitleDelayStepChanged();
        m_contentIsHDR = false;
        m_contentShouldToneMapToSdr = false;
        m_currentPosition = 0.0;
        m_duration = 0.0;
        m_hasReportedStart = false;
        m_seekTargetWhileBuffering = -1;
        m_reportProgressOnNextPositionUpdate = false;
        m_startPositionMs = 0;
        m_playMethod = QStringLiteral("directPlay");
        m_hasReportedStopForAttempt = false;
        m_hasEvaluatedCompletionForAttempt = false;
        cancelPendingDisplayRestore();
        clearPlaybackSegments();
        clearPlaybackChapters();
        if (!m_currentItemId.isEmpty()) {
            m_currentItemId.clear();
            emit currentItemIdChanged();
        }
        clearOverlayMetadata();
        m_selectedAudioTrack = -1;
        m_selectedSubtitleTrack = -1;
        m_subtitleDelayMs = 0;
        m_mpvAudioTrack = -1;
        m_mpvSubtitleTrack = -1;
        m_audioTrackMap.clear();
        m_subtitleTrackMap.clear();
        m_externalSubtitleTrackMap.clear();
        m_audioTrackReverseMap.clear();
        m_subtitleTrackReverseMap.clear();
        m_pendingExternalSubtitleIndex = -1;
        m_mediaSourceId.clear();
        m_playSessionId.clear();
        m_availableAudioTracks.clear();
        m_availableSubtitleTracks.clear();
        m_activeMediaSource.clear();
        emit selectedAudioTrackChanged();
        emit selectedSubtitleTrackChanged();
        emit subtitleDelayChanged();
        emit mediaSourceIdChanged();
        emit playSessionIdChanged();
        emit availableTracksChanged();
        m_currentSegments.clear();
        m_isInIntroSegment = false;
        m_isInOutroSegment = false;
        m_autoSkippedSegments.clear();
        m_activeIntroSkipKey.clear();
        m_activeOutroSkipKey.clear();
    m_activeIntroSkipKey.clear();
    m_activeOutroSkipKey.clear();
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
        m_pendingUrl = m_testVideoUrl;

        processEvent(Event::Play);
    });
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
 * @param startPositionMs Resume/start position in milliseconds; if greater than zero the
 *        controller will seek to the corresponding position after buffering.
 * @param seriesId Optional series identifier for episodic content.
 * @param seasonId Optional season identifier for episodic content.
 * @param libraryId Optional library identifier where the item resides.
 * @param framerate Content framerate in frames per second, used for framerate-matching decisions.
 * @param isHDR True if the content is HDR, used for HDR-related display handling.
 */
void PlayerController::playUrl(const QString &url, const QString &itemId, qint64 startPositionMs, const QString &seriesId, const QString &seasonId, const QString &libraryId, double framerate, bool isHDR, bool toneMapToSdr)
{
    m_playbackAttemptId = ++gPlaybackAttemptCounter;
    const quint64 requestedPlaybackAttemptId = m_playbackAttemptId;
    m_reportProgressOnNextPositionUpdate = false;
    qCDebug(lcPlayback) << "PlayerController: playUrl called with itemId:" << itemId 
             << "startPositionMs:" << startPositionMs
             << "seriesId:" << seriesId
             << "seasonId:" << seasonId
             << "libraryId:" << libraryId
             << "framerate:" << framerate
             << "isHDR:" << isHDR
             << "toneMapToSdr:" << toneMapToSdr;
    qCInfo(lcPlaybackTrace) << "[attempt" << m_playbackAttemptId
                            << "] play-url"
                            << "itemId=" << itemId
                            << "startPositionMs=" << startPositionMs
                            << "framerate=" << framerate
                            << "isHDR=" << isHDR
                            << "toneMapToSdr=" << toneMapToSdr
                            << "enableHDRSetting=" << m_config->getEnableHDR()
                            << "hdrOutputMode=" << m_config->getHDROutputMode()
                            << "dolbyVisionFallbackMode=" << m_config->getDolbyVisionFallbackMode()
                            << "enableFramerateMatchSetting=" << m_config->getEnableFramerateMatching();

    clearPendingAutoplayContext();
    clearNextEpisodePrefetchState();
    m_pendingExternalSubtitleTracks.clear();

    const QString requestedPlayMethod = m_nextPlaybackMethod.isEmpty()
        ? QStringLiteral("directPlay")
        : m_nextPlaybackMethod;
    const bool requestedPinsAudioTrack = m_nextStreamPinsAudioTrack;
    const bool requestedPinsSubtitleTrack = m_nextStreamPinsSubtitleTrack;
    const int requestedPinnedAudioTrack = m_nextPinnedAudioTrack;
    const int requestedPinnedSubtitleTrack = m_nextPinnedSubtitleTrack;
    const QVariantList requestedPlaybackSegments = m_nextPlaybackSegments;
    const QStringList requestedPlaylistAppendUrls = m_nextPlaylistAppendUrls;
    const PlayerPolicy::PlaybackContentKind requestedContentKind = m_nextContentKind;
    m_nextContentKind = PlayerPolicy::PlaybackContentKind::Feature;
    const QString requestedCarrySeasonId =
        std::exchange(m_nextCarryTrackPreferencesFromSeasonId, QString());
    const bool requestedIsMovie = std::exchange(m_nextIsMovie, false);
    m_nextPlaybackMethod.clear();
    m_nextStreamPinsAudioTrack = false;
    m_nextStreamPinsSubtitleTrack = false;
    m_nextPinnedAudioTrack = -1;
    m_nextPinnedSubtitleTrack = -1;
    m_nextPlaybackSegments.clear();
    m_nextPlaylistAppendUrls.clear();

    scheduleReplacementPlayback([this, url, itemId, startPositionMs, seriesId, seasonId,
                                 libraryId, framerate, isHDR, toneMapToSdr,
                                 requestedPlayMethod, requestedPinsAudioTrack,
                                 requestedPinsSubtitleTrack, requestedPinnedAudioTrack,
                                 requestedPinnedSubtitleTrack, requestedPlaybackAttemptId,
                                 requestedPlaybackSegments, requestedPlaylistAppendUrls,
                                 requestedContentKind, requestedCarrySeasonId,
                                 requestedIsMovie]() {
        // Store pending playback info after the previous item has fully stopped.
        if (m_currentItemId != itemId) {
            m_currentItemId = itemId;
            emit currentItemIdChanged();
        }
        m_currentSeriesId = seriesId;
        m_currentSeasonId = seasonId;
        m_currentLibraryId = libraryId;
        m_currentContentKind = requestedContentKind;
        m_carryTrackPreferencesFromSeasonId = requestedCarrySeasonId;
        m_currentIsMovie = requestedIsMovie;
        emit sleepTimerChanged(); // sleepTimerEndOfItemLabel
        m_pendingUrl = url;
        refreshAdjacentEpisodes();
        m_currentPosition = 0;
        m_duration = 0;
        m_hasReportedStart = false;
        m_startPositionMs = startPositionMs;
        m_shouldAutoplay = false;
        m_contentFramerate = framerate;
        emit subtitleDelayStepChanged();
        const int scopedSubtitleDelayMs =
            PlayerPolicy::playbackContentRules(requestedContentKind).persistsSubtitleDelay
            ? subtitleDelayForScope(seasonId, itemId, seriesId.isEmpty())
            : 0;
        if (m_subtitleDelayMs != scopedSubtitleDelayMs) {
            m_subtitleDelayMs = scopedSubtitleDelayMs;
            emit subtitleDelayChanged();
        }
        m_mpvDisplayFpsOverride = 0.0;
        m_contentIsHDR = isHDR;
        m_contentShouldToneMapToSdr = toneMapToSdr;
        m_playMethod = requestedPlayMethod;
        m_streamPinsAudioTrack = requestedPinsAudioTrack;
        m_streamPinsSubtitleTrack = requestedPinsSubtitleTrack;
        m_pinnedAudioTrack = requestedPinnedAudioTrack;
        m_pinnedSubtitleTrack = requestedPinnedSubtitleTrack;
        m_hasReportedStopForAttempt = false;
        m_hasEvaluatedCompletionForAttempt = false;
        // A parked post-playback restore stays parked until startPlayback
        // takes the display over.
        cancelPendingDisplayRestore(false);
        clearPlaybackSegments();
        if (!m_mediaSourceId.isEmpty()) {
            m_mediaSourceId.clear();
            emit mediaSourceIdChanged();
        }
        for (const QVariant &segment : requestedPlaybackSegments) {
            QVariantMap segmentMap = segment.toMap();
            segmentMap[QStringLiteral("playbackAttemptId")] =
                QVariant::fromValue(requestedPlaybackAttemptId);
            m_playbackSegments.append(segmentMap);
            m_aggregatePlaybackDuration += static_cast<double>(
                segmentMap.value(QStringLiteral("durationMs")).toLongLong()) / 1000.0;
        }
        m_pendingExternalSubtitleTracks.clear();
        if (!m_playbackSegments.isEmpty()) {
            const QVariantMap firstSegment = m_playbackSegments.first();
            const int selectedSubtitleIndex =
                firstSegment.value(QStringLiteral("subtitleIndex"), -1).toInt();
            for (const QVariant &trackVariant :
                 firstSegment.value(QStringLiteral("availableSubtitleTracks")).toList()) {
                const QVariantMap track = trackVariant.toMap();
                const QString externalUrl = track.value(QStringLiteral("externalUrl")).toString();
                if (!track.value(QStringLiteral("isExternal")).toBool() || externalUrl.isEmpty()) {
                    continue;
                }
                QVariantMap pending = track;
                pending[QStringLiteral("select")] =
                    track.value(QStringLiteral("index"), -1).toInt() == selectedSubtitleIndex;
                m_pendingExternalSubtitleTracks.append(pending);
            }
        }
        if (!m_playbackSegments.isEmpty()) {
            if (m_aggregatePlaybackDuration > 0.0) {
                m_duration = m_aggregatePlaybackDuration;
                emit timelineChanged();
            }
            applyPlaybackSegment(0, false);
            updateVersionAffinityFromMediaSource(m_activeMediaSource);
            qCDebug(lcPlayback) << "Track mapping contract initialized:"
                                << "audioMapEntries=" << m_audioTrackMap.size()
                                << "subtitleMapEntries=" << m_subtitleTrackMap.size()
                                << "selectedAudio=" << m_selectedAudioTrack
                                << "selectedSubtitle=" << m_selectedSubtitleTrack
                                << "selectedMpvAudio=" << m_mpvAudioTrack
                                << "selectedMpvSubtitle=" << m_mpvSubtitleTrack;
        }
        m_pendingPlaylistAppendUrls = requestedPlaylistAppendUrls;
        m_playlistSegmentOffset = 0;

        // Clear previous OSC/trickplay state and request new data
        m_currentSegments.clear();
        m_isInIntroSegment = false;
        m_isInOutroSegment = false;
        m_autoSkippedSegments.clear();
        m_activeIntroSkipKey.clear();
        m_activeOutroSkipKey.clear();
    m_activeIntroSkipKey.clear();
    m_activeOutroSkipKey.clear();
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
            m_libraryService->getChapters(itemId);
            m_playbackService->getMediaSegments(itemId, m_mediaSourceId);
            requestTrickplayInfo(itemId);
        }

        // If we have a start position, queue it as a seek target.
        if (startPositionMs > 0) {
            m_seekTargetWhileBuffering = static_cast<double>(startPositionMs) / 1000.0;
            qCDebug(lcPlayback) << "PlayerController: Will seek to" << m_seekTargetWhileBuffering << "seconds after buffering";
        } else {
            m_seekTargetWhileBuffering = -1;
        }

        processEvent(Event::Play);
        // A start that never reached startPlayback gives the display back.
        maybeStartDeferredPostPlaybackDisplayRestore();
    });
}

/**
 * @brief Stops current playback.
 *
 * Requests a backend-first terminal transition using TerminalReason::Stop.
 * Completion-threshold and autoplay evaluation happen inside prepareTerminalTransition(),
 * and the state-machine transition to Idle is finalized only after backend stop is confirmed.
 * If a terminal transition is already in flight, this method returns without duplicating it.
 */
void PlayerController::stop()
{
    qCDebug(lcPlayback) << "PlayerController: stop requested";
    qCInfo(lcPlaybackTrace) << "[attempt" << m_playbackAttemptId
                            << "] stop requested"
                            << "state=" << stateToString(m_playbackState)
                            << "terminalActive=" << m_terminalTransitionActive;

    if (m_playbackState == Idle || m_playbackState == Error || m_terminalTransitionActive) {
        return;
    }

    requestTerminalTransition(TerminalReason::Stop);
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
    qCDebug(lcPlayback) << "PlayerController: seek to" << seconds;
    
    // If loading/buffering, queue the seek for when buffering setup completes.
    // This is required for early intro auto-skip where segments can arrive before
    // we transition out of Loading.
    if (m_playbackState == Loading || m_playbackState == Buffering) {
        m_seekTargetWhileBuffering = seconds;
        qCDebug(lcPlayback) << "PlayerController: Queued seek for after loading/buffering";
        return;
    }
    
    if (m_playbackState == Playing || m_playbackState == Paused) {
        seekToSourcePosition(seconds);
        m_reportProgressOnNextPositionUpdate = true;
    }
}

void PlayerController::seekRelative(double seconds)
{
    qCDebug(lcPlayback) << "PlayerController: seekRelative" << seconds;
    
    // During buffering, convert relative to absolute and queue
    if (m_playbackState == Buffering) {
        const double physicalPosition = m_activePlaybackSegmentIndex >= 0
            ? m_segmentRelativePosition : m_currentPosition;
        m_seekTargetWhileBuffering = physicalPosition + seconds;
        qCDebug(lcPlayback) << "PlayerController: Queued relative seek for after buffering";
        return;
    }
    
    if (m_playbackState == Playing || m_playbackState == Paused) {
        if (m_streamSeekRequiresReanchor) {
            seekToSourcePosition(m_segmentRelativePosition + seconds);
        } else {
            m_playerBackend->sendVariantCommand({"seek", seconds, "relative"});
        }
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
    qCDebug(lcPlayback) << "PlayerController: retry requested";
    
    if (m_playbackState == Error) {
        m_reportProgressOnNextPositionUpdate = false;

        if (m_recoveryContext.valid) {
            cancelRecovery();
            resumeFromRecoveryContext();
            return;
        }

        if (!m_currentItemId.isEmpty()) {
            const double retryPositionSeconds = m_activePlaybackSegmentIndex >= 0
                ? m_segmentRelativePosition
                : m_currentPosition;
            const qint64 retryPositionMs = qRound64(retryPositionSeconds * 1000.0);
            requestPlayback(QVariantMap{
                {QStringLiteral("itemId"), m_currentItemId},
                {QStringLiteral("startPositionMs"), retryPositionMs},
                {QStringLiteral("seriesId"), m_currentSeriesId},
                {QStringLiteral("seasonId"), m_currentSeasonId},
                {QStringLiteral("libraryId"), m_currentLibraryId},
                {QStringLiteral("overlayTitle"), m_overlayTitle},
                {QStringLiteral("overlaySubtitle"), m_overlaySubtitle},
                {QStringLiteral("overlayBackdropUrl"), m_overlayBackdropUrl},
                {QStringLiteral("overlayLogoUrl"), m_overlayLogoUrl},
                {QStringLiteral("preferredAudioIndex"), m_selectedAudioTrack},
                {QStringLiteral("preferredSubtitleIndex"), m_selectedSubtitleTrack},
                {QStringLiteral("isMovie"), m_currentIsMovie},
                {QStringLiteral("allowVersionPrompt"), false},
                {QStringLiteral("useAffinityFallback"), true},
                {QStringLiteral("contentKind"),
                 PlayerPolicy::playbackContentKindName(m_currentContentKind)},
                {QStringLiteral("continuesSession"), true}
            });
            return;
        }

        if (!m_pendingUrl.isEmpty()) {
            processEvent(Event::Play);
        }
    }
}

void PlayerController::clearError()
{
    qCDebug(lcPlayback) << "PlayerController: clearError requested";
    
    if (m_playbackState == Error) {
        processEvent(Event::Recover);
    }
}

void PlayerController::beginRecovery()
{
    if (m_isRecovering) {
        return;
    }
    const bool wasActive = isPlaybackActive();
    m_isRecovering = true;
    m_recoveryAttemptCount = 0;
    emit isRecoveringChanged();
    emit recoveryAttemptCountChanged();
    if (wasActive != isPlaybackActive()) {
        emit isPlaybackActiveChanged();
    }
    setErrorMessage(tr("Connection lost. Attempting to reconnect..."));
    onRecoveryTick();
}

void PlayerController::cancelRecovery()
{
    if (!m_isRecovering) {
        return;
    }
    const bool wasActive = isPlaybackActive();
    m_recoveryTimer->stop();
    m_isRecovering = false;
    m_recoveryAttemptCount = 0;
    emit isRecoveringChanged();
    emit recoveryAttemptCountChanged();
    if (wasActive != isPlaybackActive()) {
        emit isPlaybackActiveChanged();
    }
    if (m_recoveryPing) {
        m_recoveryPing->cancel();
        m_recoveryPing.clear();
    }
}

void PlayerController::onRecoveryTick()
{
    if (!m_isRecovering) {
        return;
    }
    ++m_recoveryAttemptCount;
    emit recoveryAttemptCountChanged();
    qCDebug(lcPlayback) << "PlayerController: Recovery ping attempt" << m_recoveryAttemptCount;

    const auto answered = std::make_shared<bool>(false);
    HttpRequestHandle *ping = m_libraryService->pingServer(
        this, [this, answered](ServerReachability reachability) {
        *answered = true;
        m_recoveryPing.clear();
        if (reachability == ServerReachability::Reachable) {
            qCDebug(lcPlayback) << "PlayerController: Recovery ping succeeded";
            if (m_isRecovering && m_playbackState == Error && !m_terminalTransitionActive) {
                cancelRecovery();
                resumeFromRecoveryContext();
            }
        } else if (reachability == ServerReachability::SessionExpired) {
            qCDebug(lcPlayback) << "PlayerController: Recovery ping got 401, auth expired";
            if (m_isRecovering) {
                cancelRecovery();
                setErrorMessage(tr("Session expired. Please log in again."));
                const bool wasActive = isPlaybackActive();
                m_recoveryContext = RecoveryContext{};
                if (wasActive != isPlaybackActive()) {
                    emit isPlaybackActiveChanged();
                }
            }
        } else {
            qCDebug(lcPlayback) << "PlayerController: Recovery ping failed";
            if (m_isRecovering) {
                m_recoveryTimer->start(kRecoveryPingIntervalMs);
            }
        }
    });
    if (!ping) {
        m_recoveryTimer->start(kRecoveryPingIntervalMs);
        return;
    }
    // A session change cancels the shared transport without an answer; keep
    // pinging instead of stalling recovery. A newer ping already in flight
    // (recovery restarted before this one was deleted) keeps the chain.
    connect(ping, &QObject::destroyed, this, [this, answered]() {
        if (!*answered && m_isRecovering && !m_recoveryTimer->isActive() && !m_recoveryPing) {
            m_recoveryTimer->start(kRecoveryPingIntervalMs);
        }
    });
    m_recoveryPing = ping;
}

void PlayerController::resumeFromRecoveryContext()
{
    if (!m_recoveryContext.valid) {
        return;
    }

    const RecoveryContext context = m_recoveryContext;
    const bool wasActive = isPlaybackActive();
    m_recoveryContext = RecoveryContext{};
    if (wasActive != isPlaybackActive()) {
        emit isPlaybackActiveChanged();
    }

    requestPlayback(QVariantMap{
        {QStringLiteral("itemId"), context.itemId},
        {QStringLiteral("startPositionMs"), context.startPositionMs},
        {QStringLiteral("seriesId"), context.seriesId},
        {QStringLiteral("seasonId"), context.seasonId},
        {QStringLiteral("libraryId"), context.libraryId},
        {QStringLiteral("overlayTitle"), m_overlayTitle},
        {QStringLiteral("overlaySubtitle"), m_overlaySubtitle},
        {QStringLiteral("overlayBackdropUrl"), m_overlayBackdropUrl},
        {QStringLiteral("overlayLogoUrl"), m_overlayLogoUrl},
        {QStringLiteral("preferredAudioIndex"), context.audioStreamIndex},
        {QStringLiteral("preferredSubtitleIndex"), context.subtitleStreamIndex},
        {QStringLiteral("isMovie"), context.isMovie},
        {QStringLiteral("allowVersionPrompt"), false},
        {QStringLiteral("useAffinityFallback"), true},
        {QStringLiteral("contentKind"), PlayerPolicy::playbackContentKindName(context.contentKind)},
        {QStringLiteral("continuesSession"), true}
    });
}

// === TRACK SELECTION ===

void PlayerController::setSelectedAudioTrack(int index)
{
    if (m_selectedAudioTrack == index) {
        return;
    }

    const int previousAudioTrack = m_selectedAudioTrack;
    m_selectedAudioTrack = index;
    m_forcePcmAudio = false;
    updateAudioPlan();
    qCDebug(lcPlayback) << "User audio track selection:"
                        << "sourceIndex=" << index
                        << "previousSourceIndex=" << previousAudioTrack;

    bool selectionRolledBackSynchronously = false;
    bool awaitingReplan = false;
    if (m_playbackState == Playing || m_playbackState == Paused) {
        const auto applyMpvAudioTrack = [this, index]() {
            applyAudioPlanOptions();
            if (index >= 0) {
                const int mpvTrackId = mpvAudioTrackForSourceIndex(index);
                if (mpvTrackId > 0) {
                    m_mpvAudioTrack = mpvTrackId;
                    qCDebug(lcPlayback) << "Applying audio track switch via aid:" << mpvTrackId;
                    m_playerBackend->sendVariantCommand({"set_property", "aid", mpvTrackId});
                } else {
                    qCWarning(lcPlayback) << "No mapped mpv audio track for provider source index" << index
                                          << "- skipping runtime aid command";
                }
            } else {
                m_mpvAudioTrack = -1;
                m_playerBackend->sendVariantCommand({"set_property", "aid", "auto"});
            }
        };

        bool nativeSwitchDispatched = false;
        // A replan in flight replaces the pinned stream, so even the pinned
        // track needs a new one then.
        const bool replanInFlight = !m_pendingAudioSwitchContext.isEmpty();
        if (m_streamPinsAudioTrack && !m_playSessionId.isEmpty() && index >= 0
            && (index != m_pinnedAudioTrack || replanInFlight)) {
            if (m_pendingAudioSwitchContext.isEmpty()) {
                m_pendingAudioSwitchPreviousTrack = previousAudioTrack;
            }
            nativeSwitchDispatched = requestStreamReplan(false, m_segmentRelativePosition);
            if (!nativeSwitchDispatched) {
                // Providers without a native audio-switch request (notably
                // Jellyfin) retain the existing mpv aid behavior. A provider
                // can emit a synchronous failure before returning false; in
                // that case the rollback callback has already restored the
                // selected track and must not be overwritten by mpv.
                m_pendingAudioSwitchContext.clear();
                m_pendingAudioSwitchPreviousTrack = -2;
                if (m_selectedAudioTrack == index) {
                    applyMpvAudioTrack();
                }
            } else {
                selectionRolledBackSynchronously = m_selectedAudioTrack != index;
                awaitingReplan = !selectionRolledBackSynchronously;
            }
        } else {
            // A local selection supersedes the audio of any in-flight replan:
            // its failure must not roll back this newer selection.
            m_pendingAudioSwitchPreviousTrack = -2;
            applyMpvAudioTrack();
        }
    } else {
        // A selection outside active playback is also a new selection path;
        // never let an old native response roll it back later.
        m_pendingAudioSwitchPreviousTrack = -2;
    }

    const int effectiveAudioTrack = m_selectedAudioTrack;
    persistAudioPreferenceForCurrentScope(
        effectiveAudioTrack,
        selectionRolledBackSynchronously ? TrackPick::Restore
            : awaitingReplan             ? TrackPick::UserAwaitingReplan
                                         : TrackPick::User,
        previousAudioTrack);
    if (m_activePlaybackSegmentIndex >= 0 && m_activePlaybackSegmentIndex < m_playbackSegments.size()) {
        m_playbackSegments[m_activePlaybackSegmentIndex][QStringLiteral("audioIndex")] =
            effectiveAudioTrack;
    }
    if (!selectionRolledBackSynchronously) {
        emit selectedAudioTrackChanged();
    }
}

void PlayerController::setAudioDelay(int ms)
{
    m_config->setAudioDelay(ms);
}

void PlayerController::setSubtitleDelayMs(int delayMs)
{
    const int clamped = qBound(-kMaxSubtitleDelayMs, delayMs, kMaxSubtitleDelayMs);
    if (m_subtitleDelayMs == clamped) {
        if (m_playbackState == Loading || m_playbackState == Buffering
            || m_playbackState == Playing || m_playbackState == Paused) {
            applySubtitleDelay();
        }
        return;
    }

    m_subtitleDelayMs = clamped;
    emit subtitleDelayChanged();

    if (m_playbackState == Loading || m_playbackState == Buffering
        || m_playbackState == Playing || m_playbackState == Paused) {
        applySubtitleDelay();
    }

    persistSubtitleDelayForCurrentScope(m_subtitleDelayMs);
}

void PlayerController::adjustSubtitleDelayMs(int deltaMs)
{
    if (deltaMs == 0) {
        return;
    }
    setSubtitleDelayMs(m_subtitleDelayMs + deltaMs);
}

void PlayerController::resetSubtitleDelay()
{
    setSubtitleDelayMs(0);
}

void PlayerController::setSelectedSubtitleTrack(int index)
{
    if (m_selectedSubtitleTrack == index) {
        return;
    }

    const int previousSubtitleTrack = m_selectedSubtitleTrack;
    m_selectedSubtitleTrack = index;
    qCDebug(lcPlayback) << "User subtitle track selection:"
                        << "sourceIndex=" << index
                        << "previousSourceIndex=" << previousSubtitleTrack;

    // A subtitle the stream cannot show locally (an embedded track of a
    // packaged server stream, or any change away from a burned-in one) needs
    // a new stream; the selection stays unless the server refuses it.
    const bool needsReplan = (m_playbackState == Playing || m_playbackState == Paused)
        && (subtitleOptionRequiresReplan(index)
            || (m_streamSubtitleBurnedIn && index != m_pinnedSubtitleTrack)
            || (!m_pendingAudioSwitchContext.isEmpty() && !m_streamCarriesSourceTracks));
    if (needsReplan) {
        if (m_pendingAudioSwitchContext.isEmpty()) {
            m_pendingSubtitleReplanPreviousTrack = previousSubtitleTrack;
        }
        if (requestStreamReplan(false, m_segmentRelativePosition)) {
            // Written back only once the server delivers the new stream. A
            // refusal answered at once has already restored the previous one.
            if (index >= -1 && m_selectedSubtitleTrack == index) {
                persistSubtitlePreferenceForCurrentScope(index, TrackPick::UserAwaitingReplan,
                                                         previousSubtitleTrack);
            }
            if (m_activePlaybackSegmentIndex >= 0
                && m_activePlaybackSegmentIndex < m_playbackSegments.size() && index >= -1) {
                m_playbackSegments[m_activePlaybackSegmentIndex][QStringLiteral("subtitleIndex")] =
                    index;
            }
            if (m_selectedSubtitleTrack == index) {
                emit selectedSubtitleTrackChanged();
            }
            return;
        }
        m_pendingSubtitleReplanPreviousTrack = -2;
        if (m_selectedSubtitleTrack != index) {
            return; // the provider refused synchronously and rolled back
        }
    }

    if (m_playbackState == Playing || m_playbackState == Paused) {
        if (index >= 0 || m_externalSubtitleTrackMap.contains(index)) {
            const bool isExternal = m_externalSubtitleTrackMap.contains(index);
            const int mpvTrackId =
                m_externalSubtitleTrackMap.value(index, mpvSubtitleTrackForSourceIndex(index));
            if (isExternal && mpvTrackId <= 0) {
                QVariantMap externalTrack;
                for (const QVariant &trackVariant : m_availableSubtitleTracks) {
                    const QVariantMap candidate = trackVariant.toMap();
                    if (candidate.value(QStringLiteral("index"), -1).toInt() == index) {
                        externalTrack = candidate;
                        break;
                    }
                }
                const QString externalUrl =
                    externalTrack.value(QStringLiteral("externalUrl")).toString().trimmed();
                if (!externalUrl.isEmpty()) {
                    // Non-selected multipart subtitles may not receive an mpv sid
                    // until explicitly added with select semantics.
                    m_pendingExternalSubtitleIndex = index;
                    addExternalSubtitleTrackInternal(
                        externalUrl,
                        externalTrack.value(QStringLiteral("displayTitle")).toString(),
                        externalTrack.value(QStringLiteral("language")).toString(),
                        index,
                        true);
                } else {
                    qCWarning(lcPlayback) << "External subtitle has no usable URL:" << index;
                }
            } else if (mpvTrackId > 0) {
                m_mpvSubtitleTrack = mpvTrackId;
                qCDebug(lcPlayback) << "Applying subtitle track switch via sid:" << mpvTrackId;
                m_playerBackend->sendVariantCommand({"set_property", "sid", mpvTrackId});
                m_pendingExternalSubtitleIndex = -1;
            } else {
                qCWarning(lcPlayback) << "No mapped mpv subtitle track for provider source index" << index
                                      << "- skipping runtime sid command";
            }
        } else {
            m_mpvSubtitleTrack = -1;
            m_playerBackend->sendVariantCommand({"set_property", "sid", "no"});
        }
    }

    if (index >= -1) {
        persistSubtitlePreferenceForCurrentScope(index);
    }
    if (m_activePlaybackSegmentIndex >= 0 && m_activePlaybackSegmentIndex < m_playbackSegments.size()) {
        if (index >= -1) {
            m_playbackSegments[m_activePlaybackSegmentIndex][QStringLiteral("subtitleIndex")] = index;
        }
    }
    if (m_streamTimelineOffsetSeconds != 0.0
        && (m_playbackState == Playing || m_playbackState == Paused)) {
        applySubtitleDelay();
    }
    emit selectedSubtitleTrackChanged();
}

void PlayerController::addExternalSubtitleTrack(const QString &subtitleUrl,
                                                const QString &displayTitle,
                                                const QString &language,
                                                int sourceStreamIndexHint)
{
    addExternalSubtitleTrackInternal(subtitleUrl, displayTitle, language, sourceStreamIndexHint, true);
}

void PlayerController::addExternalSubtitleTrackInternal(const QString &subtitleUrl,
                                                        const QString &displayTitle,
                                                        const QString &language,
                                                        int sourceStreamIndexHint,
                                                        bool select,
                                                        bool loadInMpv)
{
    const QString normalizedSubtitleUrl = subtitleUrl.trimmed();
    if (normalizedSubtitleUrl.isEmpty()) {
        qCWarning(lcPlayback) << "Cannot add external subtitle: empty URL/path";
        return;
    }

    if (!(m_playbackState == Playing || m_playbackState == Paused)) {
        qCWarning(lcPlayback) << "Cannot add external subtitle outside active playback state";
        return;
    }

    // mpv sends http-header-fields with every request, so while the stream
    // carries credentials a sidecar from another origin would receive them.
    const QVariantMap activeSegment = activePlaybackSegment();
    if (!activeSegment.value(QStringLiteral("streamHeaders")).toMap().isEmpty()) {
        const QUrl sidecar(normalizedSubtitleUrl);
        const QUrl stream(activeSegment.value(QStringLiteral("url")).toString());
        const auto port = [](const QUrl &url) {
            return url.port(url.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0
                                ? 443 : 80);
        };
        if (sidecar.scheme().compare(stream.scheme(), Qt::CaseInsensitive) != 0
            || sidecar.host().compare(stream.host(), Qt::CaseInsensitive) != 0
            || port(sidecar) != port(stream)) {
            qCWarning(lcPlayback) << "Skipping a cross-origin subtitle while stream headers are active";
            return;
        }
    }

    int syntheticIndex = -1000 - m_externalSubtitleTrackMap.size();
    if (sourceStreamIndexHint >= 0 || sourceStreamIndexHint <= -1000) {
        syntheticIndex = sourceStreamIndexHint;
    }
    const bool trackIndexAlreadyPresent = std::any_of(m_availableSubtitleTracks.cbegin(),
                                                      m_availableSubtitleTracks.cend(),
                                                      [syntheticIndex](const QVariant &trackVariant) {
                                                          return trackVariant.toMap().value(QStringLiteral("index")).toInt() == syntheticIndex;
                                                      });
    if (trackIndexAlreadyPresent
        && m_externalSubtitleTrackMap.contains(syntheticIndex)
        && m_externalSubtitleTrackMap.value(syntheticIndex) != -1) {
        qCDebug(lcPlayback) << "Skipping external subtitle; index already present and resolved:" << syntheticIndex;
        return;
    }

    QVariantMap option;
    if (trackIndexAlreadyPresent) {
        for (const QVariant &trackVariant : m_availableSubtitleTracks) {
            const QVariantMap existing = trackVariant.toMap();
            if (existing.value(QStringLiteral("index")).toInt() == syntheticIndex) {
                option = existing;
                break;
            }
        }
    }
    if (option.isEmpty()) {
        option[QStringLiteral("index")] = syntheticIndex;
        option[QStringLiteral("displayTitle")] = displayTitle.trimmed().isEmpty()
            ? QStringLiteral("External subtitle")
            : displayTitle.trimmed();
        option[QStringLiteral("language")] = language.trimmed();
        option[QStringLiteral("codec")] = QString();
        option[QStringLiteral("channels")] = 0;
        option[QStringLiteral("channelLayout")] = QString();
        option[QStringLiteral("isDefault")] = false;
        option[QStringLiteral("isForced")] = false;
        option[QStringLiteral("isHearingImpaired")] = false;
        option[QStringLiteral("isExternal")] = true;
        option[QStringLiteral("externalUrl")] = normalizedSubtitleUrl;
        m_availableSubtitleTracks.append(option);
        emit availableTracksChanged();
    }
    m_externalSubtitleTrackMap.insert(syntheticIndex, -1);
    if (!loadInMpv) {
        return;
    }
    if (select) {
        m_pendingExternalSubtitleIndex = syntheticIndex;
    }

    m_playerBackend->sendVariantCommand({
        QStringLiteral("sub-add"),
        normalizedSubtitleUrl,
        select ? QStringLiteral("select") : QStringLiteral("auto"),
        option.value(QStringLiteral("displayTitle")).toString(),
        option.value(QStringLiteral("language")).toString()
    });
}

void PlayerController::cycleAudioTrack()
{
    qCDebug(lcPlayback) << "PlayerController: Cycling audio track";
    if (m_playbackState == Playing || m_playbackState == Paused) {
        m_pendingAudioTrackPersistenceFromBackend = true;
        m_playerBackend->sendCommand({"cycle", "audio"});
    }
}

void PlayerController::cycleSubtitleTrack()
{
    qCDebug(lcPlayback) << "PlayerController: Cycling subtitle track";
    if (m_playbackState == Playing || m_playbackState == Paused) {
        m_pendingSubtitleTrackPersistenceFromBackend = true;
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

void PlayerController::seekToPlaybackChapter(int index)
{
    if (index < 0 || index >= m_playbackChapters.size()) {
        return;
    }
    const QVariantMap chapter = m_playbackChapters.at(index).toMap();
    const qint64 startMs = chapter.value(QStringLiteral("startMs")).toLongLong();
    seek(static_cast<double>(startMs) / 1000.0);
}

QString PlayerController::episodeOverlaySubtitle(const QVariantMap &episode)
{
    const int seasonNumber = episode.value(QStringLiteral("parentIndexNumber")).toInt();
    const int episodeNumber = episode.value(QStringLiteral("indexNumber")).toInt();
    const QString episodeName = episode.value(QStringLiteral("name")).toString();
    QString subtitle = QStringLiteral("S%1 E%2").arg(seasonNumber).arg(episodeNumber);
    if (!episodeName.isEmpty()) {
        subtitle += QStringLiteral(" - ") + episodeName;
    }
    return subtitle;
}

QVariantList PlayerController::playbackVersions() const
{
    return m_playbackVersionsItemId == m_currentItemId ? m_playbackVersions : QVariantList{};
}

void PlayerController::setPlaybackVersions(const QString &itemId, const QVariantList &versions)
{
    if (m_playbackVersionsItemId == itemId && m_playbackVersions == versions) {
        return;
    }
    m_playbackVersionsItemId = itemId;
    m_playbackVersions = versions;
    emit playbackVersionsChanged();
}

void PlayerController::switchPlaybackVersion(const QString &mediaSourceId)
{
    if (mediaSourceId.isEmpty() || m_currentItemId.isEmpty() || !isPlaybackActive()) {
        return;
    }
    bool known = false;
    for (const QVariant &value : playbackVersions()) {
        const QVariantMap version = value.toMap();
        if (version.value(QStringLiteral("id")).toString() != mediaSourceId) {
            continue;
        }
        if (version.value(QStringLiteral("selected")).toBool()) {
            return;
        }
        known = true;
        break;
    }
    if (!known) {
        qCWarning(lcPlayback) << "switchPlaybackVersion: unknown version" << mediaSourceId;
        return;
    }

    // Before mpv reports a position, the resume point still lives in the
    // queued start seek.
    double switchPositionSeconds = m_currentPosition;
    if ((m_playbackState == Loading || m_playbackState == Buffering)
        && m_seekTargetWhileBuffering >= 0.0) {
        switchPositionSeconds = m_seekTargetWhileBuffering;
    } else if (m_playbackState == Loading && m_currentPosition <= 0.0) {
        switchPositionSeconds = static_cast<double>(m_startPositionMs) / 1000.0;
    }
    qCInfo(lcPlayback) << "Switching playback version"
                       << "itemId=" << m_currentItemId
                       << "mediaSourceId=" << mediaSourceId
                       << "positionSeconds=" << switchPositionSeconds;

    // Track indices are per version, so the new version resolves tracks from
    // the saved scope preferences instead of carrying raw indices across.
    requestPlayback(QVariantMap{
        {QStringLiteral("itemId"), m_currentItemId},
        {QStringLiteral("seriesId"), m_currentSeriesId},
        {QStringLiteral("seasonId"), m_currentSeasonId},
        {QStringLiteral("libraryId"), m_currentLibraryId},
        {QStringLiteral("startPositionMs"), qMax<qint64>(0, qRound64(switchPositionSeconds * 1000.0))},
        {QStringLiteral("overlayTitle"), m_overlayTitle},
        {QStringLiteral("overlaySubtitle"), m_overlaySubtitle},
        {QStringLiteral("overlayBackdropUrl"), m_overlayBackdropUrl},
        {QStringLiteral("overlayLogoUrl"), m_overlayLogoUrl},
        {QStringLiteral("isMovie"), m_currentIsMovie},
        {QStringLiteral("allowVersionPrompt"), false},
        {QStringLiteral("mediaSourceId"), mediaSourceId},
        {QStringLiteral("contentKind"), PlayerPolicy::playbackContentKindName(m_currentContentKind)},
        {QStringLiteral("continuesSession"), true}
    });
}

bool PlayerController::hasEpisodeNavigation() const
{
    return !m_currentSeriesId.isEmpty() && !m_currentSeasonId.isEmpty()
        && m_adjacentEpisodesItemId == m_currentItemId && !m_currentItemId.isEmpty();
}

bool PlayerController::hasPreviousEpisode() const
{
    return hasEpisodeNavigation() && !m_previousEpisode.isEmpty();
}

bool PlayerController::hasNextEpisode() const
{
    return hasEpisodeNavigation() && !m_nextEpisode.isEmpty();
}

namespace {
const QString kAdjacentEpisodesKeyPrefix = QStringLiteral("player-adjacent-episodes");
const QString kAdjacentCurrentSeason = QStringLiteral("season");
const QString kAdjacentSeries = QStringLiteral("series");
const QString kAdjacentPreviousSeason = QStringLiteral("previous-season");
const QString kAdjacentNextSeason = QStringLiteral("next-season");
}

void PlayerController::refreshAdjacentEpisodes()
{
    const bool hadEpisodes = !m_previousEpisode.isEmpty() || !m_nextEpisode.isEmpty();
    const bool wasAvailable = !m_adjacentEpisodesItemId.isEmpty();
    m_adjacentListings.clear();
    m_adjacentListingsRequested.clear();
    m_previousEpisode.clear();
    m_nextEpisode.clear();
    m_adjacentEpisodesItemId.clear();
    m_adjacentEpisodesConnectionId.clear();

    if (m_libraryService && !m_currentItemId.isEmpty()
        && !m_currentSeriesId.isEmpty() && !m_currentSeasonId.isEmpty()) {
        m_adjacentEpisodesItemId = m_currentItemId;
        m_adjacentEpisodesConnectionId = m_libraryService->getActiveConnectionId();
        requestAdjacentEpisodeListing(m_currentSeasonId, kAdjacentCurrentSeason);
        requestAdjacentEpisodeListing(m_currentSeriesId, kAdjacentSeries);
    }

    if (hadEpisodes || wasAvailable || !m_adjacentEpisodesItemId.isEmpty()) {
        emit adjacentEpisodesChanged();
    }
}

void PlayerController::requestAdjacentEpisodeListing(const QString &parentId, const QString &kind)
{
    const QString requestedKey = kind + QLatin1Char('|') + parentId;
    if (parentId.isEmpty() || m_adjacentListingsRequested.contains(requestedKey)) {
        return;
    }
    m_adjacentListingsRequested.insert(requestedKey);

    // The private request key keeps the response out of view-model request
    // bookkeeping.
    LibraryItemQuery query;
    query.parentId = parentId;
    query.includeHeavyFields = false;
    // The item type selects the hierarchy route on Silo and filters the
    // children on Jellyfin.
    query.includeItemTypes = {kind == kAdjacentSeries ? QStringLiteral("Season")
                                                      : QStringLiteral("Episode")};
    query.requestKey = kAdjacentEpisodesKeyPrefix + QLatin1Char('\n')
        + m_adjacentEpisodesItemId + QLatin1Char('\n') + kind;
    m_libraryService->getItems(query);
}

void PlayerController::onAdjacentEpisodeItemsLoaded(const QString &connectionId,
                                                    const QString &parentId,
                                                    const QString &queryKey,
                                                    const QVariantList &items,
                                                    int totalRecordCount)
{
    Q_UNUSED(parentId);
    Q_UNUSED(totalRecordCount);
    const QStringList parts = queryKey.split(QLatin1Char('\n'));
    if (parts.size() != 3 || parts.at(0) != kAdjacentEpisodesKeyPrefix) {
        return;
    }
    if (parts.at(1) != m_adjacentEpisodesItemId || m_adjacentEpisodesItemId.isEmpty()
        || connectionId != m_adjacentEpisodesConnectionId) {
        return;
    }
    m_adjacentListings.insert(parts.at(2), items);
    resolveAdjacentEpisodes();
}

void PlayerController::resolveAdjacentEpisodes()
{
    if (!m_adjacentListings.contains(kAdjacentCurrentSeason)) {
        return;
    }
    const PlayerPolicy::AdjacentEpisodes adjacent = PlayerPolicy::resolveAdjacentEpisodes(
        m_adjacentListings.value(kAdjacentCurrentSeason), m_adjacentEpisodesItemId);
    QVariantMap previous = adjacent.previous;
    QVariantMap next = adjacent.next;

    if (adjacent.currentFound && m_adjacentListings.contains(kAdjacentSeries)) {
        const QVariantList seriesItems = m_adjacentListings.value(kAdjacentSeries);
        if (adjacent.previousInAdjacentSeason) {
            const QString seasonId =
                PlayerPolicy::adjacentSeasonId(seriesItems, m_currentSeasonId, -1);
            if (m_adjacentListings.contains(kAdjacentPreviousSeason)) {
                previous = PlayerPolicy::boundaryEpisode(
                    m_adjacentListings.value(kAdjacentPreviousSeason), true);
            } else {
                requestAdjacentEpisodeListing(seasonId, kAdjacentPreviousSeason);
            }
        }
        if (adjacent.nextInAdjacentSeason) {
            const QString seasonId =
                PlayerPolicy::adjacentSeasonId(seriesItems, m_currentSeasonId, 1);
            if (m_adjacentListings.contains(kAdjacentNextSeason)) {
                next = PlayerPolicy::boundaryEpisode(
                    m_adjacentListings.value(kAdjacentNextSeason), false);
            } else {
                requestAdjacentEpisodeListing(seasonId, kAdjacentNextSeason);
            }
        }
    }

    if (previous == m_previousEpisode && next == m_nextEpisode) {
        return;
    }
    m_previousEpisode = previous;
    m_nextEpisode = next;
    emit adjacentEpisodesChanged();
}

void PlayerController::skipToPreviousEpisode()
{
    if (hasPreviousEpisode()) {
        requestAdjacentEpisodePlayback(m_previousEpisode);
    }
}

void PlayerController::skipToNextEpisode()
{
    if (hasNextEpisode()) {
        requestAdjacentEpisodePlayback(m_nextEpisode);
    }
}

void PlayerController::requestAdjacentEpisodePlayback(const QVariantMap &episode)
{
    const QString episodeId = episode.value(QStringLiteral("itemId")).toString();
    if (episodeId.isEmpty()) {
        return;
    }
    QString seasonId = episode.value(QStringLiteral("seasonId")).toString();
    if (seasonId.isEmpty()) {
        seasonId = episode.value(QStringLiteral("parentId")).toString();
    }
    if (seasonId.isEmpty()) {
        seasonId = m_currentSeasonId;
    }
    const QString seriesName = episode.value(QStringLiteral("seriesName")).toString();
    const QString overlayTitle = !seriesName.isEmpty() ? seriesName : m_overlayTitle;
    const QString overlayLogoUrl = !m_overlayLogoUrl.isEmpty()
        ? m_overlayLogoUrl
        : m_libraryService->getCachedImageUrlWithWidth(m_currentSeriesId, QStringLiteral("Logo"), 600);

    // Carry the current tracks forward the same way autoplay does: audio by
    // index, subtitles only when the season scope holds an explicit choice.
    int subtitleOverride = -2;
    switch (m_trackPrefs->getSeasonPreferences(m_currentSeasonId).subtitle.mode) {
    case TrackPreferenceMode::Off:
        subtitleOverride = -1;
        break;
    case TrackPreferenceMode::ExplicitStream:
        subtitleOverride = m_selectedSubtitleTrack;
        break;
    default:
        break;
    }

    qCInfo(lcPlayback) << "Skipping to adjacent episode"
                       << "fromItemId=" << m_currentItemId
                       << "toItemId=" << episodeId;
    requestPlayback(QVariantMap{
        {QStringLiteral("itemId"), episodeId},
        {QStringLiteral("seriesId"), m_currentSeriesId},
        {QStringLiteral("seasonId"), seasonId},
        {QStringLiteral("libraryId"), m_currentLibraryId},
        {QStringLiteral("startPositionMs"), resumePositionMs(episode)},
        {QStringLiteral("overlayTitle"), overlayTitle},
        {QStringLiteral("overlaySubtitle"), episodeOverlaySubtitle(episode)},
        {QStringLiteral("overlayBackdropUrl"), m_overlayBackdropUrl},
        {QStringLiteral("overlayLogoUrl"), overlayLogoUrl},
        {QStringLiteral("isMovie"), false},
        {QStringLiteral("allowVersionPrompt"), false},
        {QStringLiteral("useAffinityFallback"), true},
        {QStringLiteral("preferredAudioIndex"), m_selectedAudioTrack},
        {QStringLiteral("preferredSubtitleIndex"), subtitleOverride},
        {QStringLiteral("continuesWatching"), QStringLiteral("episodeSkip")}
    });
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

void PlayerController::toggleSubtitleAssOverride()
{
    if (m_playbackState == Loading || m_playbackState == Buffering
        || m_playbackState == Playing || m_playbackState == Paused) {
        m_playerBackend->sendCommand({"cycle-values", "sub-ass-override", "no", "yes"});
    }
}

void PlayerController::toggleDeband()
{
    if (m_playbackState == Loading || m_playbackState == Buffering
        || m_playbackState == Playing || m_playbackState == Paused) {
        m_playerBackend->sendCommand({"cycle-values", "deband", "no", "yes"});
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
    const ScopedTrackPreferences preferences = m_trackPrefs->getSeasonPreferences(seasonId);
    return preferences.audio.mode == TrackPreferenceMode::ExplicitStream ? preferences.audio.streamIndex : -1;
}

int PlayerController::getLastSubtitleTrackForSeason(const QString &seasonId) const
{
    const ScopedTrackPreferences preferences = m_trackPrefs->getSeasonPreferences(seasonId);
    if (preferences.subtitle.mode == TrackPreferenceMode::ExplicitStream) {
        return preferences.subtitle.streamIndex;
    }
    return -1;
}

void PlayerController::saveAudioTrackPreference(const QString &seasonId, int index)
{
    setExplicitSeasonAudioPreference(seasonId, index);
}

void PlayerController::saveSubtitleTrackPreference(const QString &seasonId, int index)
{
    setExplicitSeasonSubtitlePreference(seasonId, index);
}

// ---- Movie track preferences ----

int PlayerController::getLastAudioTrackForMovie(const QString &movieId) const
{
    const ScopedTrackPreferences preferences = m_trackPrefs->getMoviePreferences(movieId);
    return preferences.audio.mode == TrackPreferenceMode::ExplicitStream ? preferences.audio.streamIndex : -1;
}

int PlayerController::getLastSubtitleTrackForMovie(const QString &movieId) const
{
    const ScopedTrackPreferences preferences = m_trackPrefs->getMoviePreferences(movieId);
    if (preferences.subtitle.mode == TrackPreferenceMode::ExplicitStream) {
        return preferences.subtitle.streamIndex;
    }
    return -1;
}

void PlayerController::saveMovieAudioTrackPreference(const QString &movieId, int index)
{
    setExplicitMovieAudioPreference(movieId, index);
}

void PlayerController::saveMovieSubtitleTrackPreference(const QString &movieId, int index)
{
    setExplicitMovieSubtitlePreference(movieId, index);
}

void PlayerController::setExplicitSeasonAudioPreference(const QString &seasonId, int index)
{
    saveSeasonTrackPreference(seasonId, TrackPreferenceWrite::Kind::Audio,
                              PlayerPolicy::explicitTrackPreference({}, index, false));
}

void PlayerController::setExplicitSeasonSubtitlePreference(const QString &seasonId, int index)
{
    saveSeasonTrackPreference(seasonId, TrackPreferenceWrite::Kind::Subtitle,
                              PlayerPolicy::explicitTrackPreference({}, index, true));
}

void PlayerController::saveSeasonTrackPreference(const QString &seasonId,
                                                 TrackPreferenceWrite::Kind kind,
                                                 const TrackSelectionPreference &preference)
{
    updateSeasonPreference(seasonId, [kind, &preference](ScopedTrackPreferences &preferences) {
        (kind == TrackPreferenceWrite::Kind::Audio ? preferences.audio : preferences.subtitle) =
            preference;
    });
}

void PlayerController::clearSeasonAudioPreference(const QString &seasonId)
{
    updateSeasonPreference(seasonId, [](ScopedTrackPreferences &preferences) {
        preferences.audio = {};
    });
}

void PlayerController::clearSeasonSubtitlePreference(const QString &seasonId)
{
    updateSeasonPreference(seasonId, [](ScopedTrackPreferences &preferences) {
        preferences.subtitle = {};
    });
}

void PlayerController::setExplicitMovieAudioPreference(const QString &movieId, int index)
{
    saveMovieTrackPreference(movieId, TrackPreferenceWrite::Kind::Audio,
                             PlayerPolicy::explicitTrackPreference({}, index, false));
}

void PlayerController::setExplicitMovieSubtitlePreference(const QString &movieId, int index)
{
    saveMovieTrackPreference(movieId, TrackPreferenceWrite::Kind::Subtitle,
                             PlayerPolicy::explicitTrackPreference({}, index, true));
}

void PlayerController::saveMovieTrackPreference(const QString &movieId,
                                                TrackPreferenceWrite::Kind kind,
                                                const TrackSelectionPreference &preference)
{
    updateMoviePreference(movieId, [kind, &preference](ScopedTrackPreferences &preferences) {
        (kind == TrackPreferenceWrite::Kind::Audio ? preferences.audio : preferences.subtitle) =
            preference;
    });
}

void PlayerController::clearMovieAudioPreference(const QString &movieId)
{
    updateMoviePreference(movieId, [](ScopedTrackPreferences &preferences) {
        preferences.audio = {};
    });
}

void PlayerController::clearMovieSubtitlePreference(const QString &movieId)
{
    updateMoviePreference(movieId, [](ScopedTrackPreferences &preferences) {
        preferences.subtitle = {};
    });
}

QVariantMap PlayerController::resolveTrackSelectionForMediaSource(const QVariantMap &mediaSource,
                                                                 const QString &scopeId,
                                                                 bool isMovie,
                                                                 int preferredAudioIndex,
                                                                 int preferredSubtitleIndex,
                                                                 const QString &carryTrackPreferencesFromSeasonId) const
{
    const ScopedTrackPreferences preferences = isMovie
        ? m_trackPrefs->getMoviePreferences(scopeId)
        : seasonTrackPreferencesWithCarry(scopeId, carryTrackPreferencesFromSeasonId);
    const ResolvedTrackSelection resolved = resolveTrackSelection(mediaSource,
                                                                  preferences,
                                                                  preferredAudioIndex,
                                                                  preferredSubtitleIndex);

    QVariantMap result;
    result[QStringLiteral("audioIndex")] = resolved.audioIndex;
    result[QStringLiteral("subtitleIndex")] = resolved.subtitleIndex;
    result[QStringLiteral("audioSource")] = resolved.audioSource;
    result[QStringLiteral("subtitleSource")] = resolved.subtitleSource;
    result[QStringLiteral("hasExplicitAudioPreference")] =
        preferences.audio.mode == TrackPreferenceMode::ExplicitStream;
    result[QStringLiteral("hasExplicitSubtitlePreference")] =
        preferences.subtitle.mode != TrackPreferenceMode::Unset;
    return result;
}

void PlayerController::playUrlWithTracks(const QString &url, const QString &itemId, qint64 startPositionMs,
                                         const QString &seriesId, const QString &seasonId, const QString &libraryId,
                                         const QString &mediaSourceId, const QString &playSessionId,
                                         const QVariantMap &mediaSource,
                                         int audioStreamIndex, int subtitleStreamIndex,
                                         const QVariantList &availableAudioTracks,
                                         const QVariantList &availableSubtitleTracks,
                                         qint64 durationMs, double framerate,
                                         bool isHDR, bool toneMapToSdr)
{
    qCDebug(lcPlayback) << "PlayerController: playUrlWithTracks called with itemId:" << itemId
             << "audioIndex:" << audioStreamIndex
             << "subtitleIndex:" << subtitleStreamIndex
             << "framerate:" << framerate
             << "isHDR:" << isHDR
             << "toneMapToSdr:" << toneMapToSdr;

    const QVariantList resolvedAvailableAudioTracks = availableAudioTracks.isEmpty()
        ? buildAvailableTrackOptions(mediaSource, QStringLiteral("Audio"))
        : availableAudioTracks;
    const QVariantList resolvedAvailableSubtitleTracks = availableSubtitleTracks.isEmpty()
        ? buildAvailableTrackOptions(mediaSource, QStringLiteral("Subtitle"))
        : availableSubtitleTracks;
    const QString requestedPlayMethod = m_nextPlaybackMethod.isEmpty()
        ? QStringLiteral("directPlay")
        : m_nextPlaybackMethod;
    const bool requestedPinsAudioTrack = m_nextStreamPinsAudioTrack;
    const bool requestedPinsSubtitleTrack = m_nextStreamPinsSubtitleTrack;
    const int requestedPinnedAudioTrack = m_nextPinnedAudioTrack;
    const int requestedPinnedSubtitleTrack = m_nextPinnedSubtitleTrack;

    if (m_nextPlaybackSegments.isEmpty()) {
        m_nextPlaybackSegments = {
            QVariantMap{
                {QStringLiteral("itemId"), itemId},
                {QStringLiteral("mediaSourceId"), mediaSourceId},
                {QStringLiteral("playSessionId"), playSessionId},
                {QStringLiteral("mediaSource"), mediaSource},
                {QStringLiteral("audioIndex"), audioStreamIndex},
                {QStringLiteral("subtitleIndex"), subtitleStreamIndex},
                {QStringLiteral("availableAudioTracks"), resolvedAvailableAudioTracks},
                {QStringLiteral("availableSubtitleTracks"), resolvedAvailableSubtitleTracks},
                {QStringLiteral("durationMs"), durationMs},
                {QStringLiteral("playMethod"), requestedPlayMethod},
                {QStringLiteral("pinsAudioTrack"), requestedPinsAudioTrack},
                {QStringLiteral("pinsSubtitleTrack"), requestedPinsSubtitleTrack},
                {QStringLiteral("pinnedAudioTrack"), requestedPinnedAudioTrack},
                {QStringLiteral("pinnedSubtitleTrack"), requestedPinnedSubtitleTrack},
                {QStringLiteral("url"), url}
            }
        };
    }

    playUrl(url, itemId, startPositionMs, seriesId, seasonId, libraryId,
            framerate, isHDR, toneMapToSdr);
}

void PlayerController::updateTrackMappings(const QVariantMap &mediaSource)
{
    m_audioTrackMap.clear();
    m_subtitleTrackMap.clear();
    m_audioTrackReverseMap.clear();
    m_subtitleTrackReverseMap.clear();

    if (!m_streamCarriesSourceTracks) {
        // A packaged server stream numbers only the tracks it carries.
        for (int i = 0; i < m_streamDeliveredAudioTracks.size(); ++i) {
            m_audioTrackMap.insert(m_streamDeliveredAudioTracks.at(i), i + 1);
            m_audioTrackReverseMap.insert(i + 1, m_streamDeliveredAudioTracks.at(i));
        }
        for (int i = 0; i < m_streamDeliveredSubtitleTracks.size(); ++i) {
            m_subtitleTrackMap.insert(m_streamDeliveredSubtitleTracks.at(i), i + 1);
            m_subtitleTrackReverseMap.insert(i + 1, m_streamDeliveredSubtitleTracks.at(i));
        }
        return;
    }

    int audioTrackId = 1;
    int subtitleTrackId = 1;
    for (const QVariant &streamVariant : mediaStreams(mediaSource)) {
        const QVariantMap stream = streamVariant.toMap();
        const QString type = stream.value(QStringLiteral("type")).toString();
        const int sourceIndex = stream.value(QStringLiteral("index"), -1).toInt();
        if (sourceIndex < 0) {
            continue;
        }

        if (type == QStringLiteral("Audio")) {
            m_audioTrackMap.insert(sourceIndex, audioTrackId);
            m_audioTrackReverseMap.insert(audioTrackId, sourceIndex);
            ++audioTrackId;
        } else if (type == QStringLiteral("Subtitle")
                   && !stream.value(QStringLiteral("isExternal")).toBool()) {
            // mpv numbers the file's embedded subtitles first; external files
            // get their sid when they are added and live in the external map.
            m_subtitleTrackMap.insert(sourceIndex, subtitleTrackId);
            m_subtitleTrackReverseMap.insert(subtitleTrackId, sourceIndex);
            ++subtitleTrackId;
        }
    }
}

int PlayerController::mpvAudioTrackForSourceIndex(int sourceStreamIndex) const
{
    return sourceStreamIndex < 0 ? -1 : m_audioTrackMap.value(sourceStreamIndex, -1);
}

int PlayerController::sourceAudioTrackForMpvTrack(int mpvTrackId) const
{
    return mpvTrackId < 0 ? -1 : m_audioTrackReverseMap.value(mpvTrackId, -1);
}

int PlayerController::mpvSubtitleTrackForSourceIndex(int sourceStreamIndex) const
{
    return sourceStreamIndex < 0 ? -1 : m_subtitleTrackMap.value(sourceStreamIndex, -1);
}

int PlayerController::sourceSubtitleTrackForMpvTrack(int mpvTrackId) const
{
    return mpvTrackId < 0 ? -1 : m_subtitleTrackReverseMap.value(mpvTrackId, -1);
}

PlayerController::ResolvedTrackSelection PlayerController::resolveTrackSelection(const QVariantMap &mediaSource,
                                                                                 const ScopedTrackPreferences &preferences,
                                                                                 int preferredAudioIndex,
                                                                                 int preferredSubtitleIndex) const
{
    return PlayerPolicy::resolveTrackSelection(
        mediaSource,
        preferences,
        m_config ? m_config->getDefaultAudioTrackSelection()
                 : QStringLiteral("jellyfin-default"),
        m_config ? m_config->getDefaultSubtitleTrackSelection()
                 : QStringLiteral("jellyfin-default"),
        preferredAudioIndex,
        preferredSubtitleIndex,
        !m_config || m_config->getUseServerTrackPreferences());
}

void PlayerController::syncBackendAudioTrack(int mpvTrackId)
{
    if (m_reopeningAudio) return;
    if (m_applyingInitialTracks) {
        if (mpvTrackId != m_mpvAudioTrack) {
            qCWarning(lcPlayback) << "mpv reported audio track" << mpvTrackId
                                  << "during startup; expected" << m_mpvAudioTrack;
        }
        return;
    }

    m_mpvAudioTrack = mpvTrackId;
    const int sourceIndex = sourceAudioTrackForMpvTrack(mpvTrackId);
    if (sourceIndex < 0) {
        m_pendingAudioTrackPersistenceFromBackend = false;
        return;
    }

    if (m_selectedAudioTrack != sourceIndex) {
        m_selectedAudioTrack = sourceIndex;
        if (m_activePlaybackSegmentIndex >= 0 && m_activePlaybackSegmentIndex < m_playbackSegments.size()) {
            m_playbackSegments[m_activePlaybackSegmentIndex][QStringLiteral("audioIndex")] = sourceIndex;
        }
        emit selectedAudioTrackChanged();
        m_forcePcmAudio = false;
        updateAudioPlan();
        applyAudioPlanOptions();
    }
    if (m_pendingAudioTrackPersistenceFromBackend) {
        persistAudioPreferenceForCurrentScope(sourceIndex);
    }
    m_pendingAudioTrackPersistenceFromBackend = false;
}

void PlayerController::syncBackendSubtitleTrack(int mpvTrackId)
{
    if (m_applyingInitialTracks) {
        if (mpvTrackId != m_mpvSubtitleTrack) {
            qCWarning(lcPlayback) << "mpv reported subtitle track" << mpvTrackId
                                  << "during startup; expected" << m_mpvSubtitleTrack;
        }
        return;
    }

    m_mpvSubtitleTrack = mpvTrackId;
    if (mpvTrackId >= 0) {
        auto externalTrackIt = std::find_if(m_externalSubtitleTrackMap.begin(),
                                            m_externalSubtitleTrackMap.end(),
                                            [mpvTrackId](int mappedTrackId) { return mappedTrackId == mpvTrackId; });
        if (externalTrackIt == m_externalSubtitleTrackMap.end()) {
            auto pendingIt = m_externalSubtitleTrackMap.find(m_pendingExternalSubtitleIndex);
            if (pendingIt != m_externalSubtitleTrackMap.end() && pendingIt.value() < 0) {
                pendingIt.value() = mpvTrackId;
                externalTrackIt = pendingIt;
            }
        }
        if (externalTrackIt != m_externalSubtitleTrackMap.end()) {
            const int externalIndex = externalTrackIt.key();
            if (m_selectedSubtitleTrack != externalIndex) {
                m_selectedSubtitleTrack = externalIndex;
                emit selectedSubtitleTrackChanged();
            }
            if (m_pendingExternalSubtitleIndex == externalIndex) {
                m_playerBackend->sendVariantCommand({"set_property", "sid", mpvTrackId});
                m_pendingExternalSubtitleIndex = -1;
            }
            if (m_activePlaybackSegmentIndex >= 0 && m_activePlaybackSegmentIndex < m_playbackSegments.size()) {
                m_playbackSegments[m_activePlaybackSegmentIndex][QStringLiteral("subtitleIndex")] = externalIndex;
            }
            m_pendingSubtitleTrackPersistenceFromBackend = false;
            if (m_streamTimelineOffsetSeconds != 0.0) {
                applySubtitleDelay();
            }
            return;
        }
    }
    const int sourceIndex = mpvTrackId < 0 ? -1 : sourceSubtitleTrackForMpvTrack(mpvTrackId);
    if (mpvTrackId >= 0 && sourceIndex < 0) {
        m_pendingSubtitleTrackPersistenceFromBackend = false;
        return;
    }

    if (m_selectedSubtitleTrack != sourceIndex) {
        m_selectedSubtitleTrack = sourceIndex;
        if (m_activePlaybackSegmentIndex >= 0 && m_activePlaybackSegmentIndex < m_playbackSegments.size()) {
            m_playbackSegments[m_activePlaybackSegmentIndex][QStringLiteral("subtitleIndex")] = sourceIndex;
        }
        emit selectedSubtitleTrackChanged();
    }
    if (m_pendingSubtitleTrackPersistenceFromBackend) {
        persistSubtitlePreferenceForCurrentScope(sourceIndex);
    }
    m_pendingSubtitleTrackPersistenceFromBackend = false;
    if (m_streamTimelineOffsetSeconds != 0.0) {
        applySubtitleDelay();
    }
}

void PlayerController::onAudioDeviceListChanged(const QVariantList &devices)
{
    if (m_availableAudioDevices == devices && m_audioDeviceListInitialized) return;
    m_availableAudioDevices = devices;
    m_playbackService->audioOutput()->setMpvDevices(devices);
    emit availableAudioDevicesChanged();

    if (!m_audioDeviceListInitialized) {
        // First snapshot reported after mpv starts. The configured device was
        // already applied via startup args, so there is nothing to reload yet.
        m_audioDeviceListInitialized = true;
        qCDebug(lcPlayback) << "PlayerController: initial audio device list ("
                            << devices.size() << "devices )";
        return;
    }

    // Debounce endpoint churn before reopening audio on the existing media session.
    if (m_playerBackend && m_playerBackend->isRunning()) {
        qCInfo(lcPlayback) << "PlayerController: audio device list changed ("
                           << devices.size() << "devices ); scheduling audio output reload";
        // Hotplug is also the retry point for an endpoint that refused exclusive mode.
        m_exclusiveFallback = false;
        m_audioReloadForced = true;
        m_audioDeviceReloadTimer->start();
    }
}

void PlayerController::onAudioReloadTimeout()
{
    if (!m_playerBackend || !m_playerBackend->isRunning()) {
        m_audioReloadForced = false;
        return;
    }
    // Capability refreshes that leave the plan and endpoint unchanged must not
    // interrupt audio; hotplug and explicit requests always reopen.
    if (!std::exchange(m_audioReloadForced, false)) {
        const auto candidate = computeAudioPlan();
        const QString endpoint = m_playbackService->audioOutput()->capabilities().endpointId;
        if (candidate.device == m_audioPlan.device && candidate.path == m_audioPlan.path
            && candidate.passthroughCodec == m_audioPlan.passthroughCodec
            && candidate.mpvChannels == m_audioPlan.mpvChannels
            && candidate.exclusivePcm == m_audioPlan.exclusivePcm
            && endpoint == m_audioPlanEndpointId) {
            return;
        }
    }
    applyAudioOutputDevice();
}

void PlayerController::applyAudioOutputDevice()
{
    if (!m_playerBackend || !m_playerBackend->isRunning()) {
        return;
    }

    // Point mpv at the desired device (a no-op if it already matches), then force
    // the audio output to reinitialize. For "auto" this re-selects the current
    // system default; for an explicit device it (re)opens that endpoint.
    // ao-reload can cause mpv to publish a fresh aid value. Rebuild the
    // canonical maps before that notification arrives so reverse lookup stays
    // aligned with the active source.
    if (!m_activeMediaSource.isEmpty()) {
        updateTrackMappings(m_activeMediaSource);
        m_mpvAudioTrack = mpvAudioTrackForSourceIndex(m_selectedAudioTrack);
    }
    updateAudioPlan();
    qCInfo(lcPlayback) << "PlayerController: reopening audio output on" << m_audioPlan.device
                       << "(preferred" << m_config->getAudioOutputDevice() << ") exclusive"
                       << m_audioPlan.exclusivePcm;
    applyAudioPlanOptions();
    m_reopeningAudio = true;
    const auto generation = m_audioPlanGeneration;
    // Verify the reopened output sooner than the periodic check so a failed
    // exclusive or device open falls back within about a second.
    QTimer::singleShot(1200, this, [this, generation] {
        if (generation == m_audioPlanGeneration && m_playerBackend->isRunning())
            m_playerBackend->requestAudioDiagnostics(QStringLiteral("audio|%1").arg(generation));
    });
    // Reopen the selected decoder without restarting the stream or its provider session.
    if (m_mpvAudioTrack > 0) {
        m_playerBackend->sendVariantCommand({"set_property", "aid", "no"});
        m_playerBackend->sendVariantCommand({"set_property", "aid", m_mpvAudioTrack});
    }
    QTimer::singleShot(500, this, [this, generation] {
        if (generation == m_audioPlanGeneration) m_reopeningAudio = false;
    });
    m_playerBackend->sendVariantCommand({"ao-reload"});
}

Bloom::AudioOutputPlan PlayerController::computeAudioPlan()
{
    auto source = Bloom::AudioPolicy::selectedSource(m_activeMediaSource, m_selectedAudioTrack);
    if (m_activePlaybackSegmentIndex >= 0 && m_activePlaybackSegmentIndex < m_playbackSegments.size()) {
        const auto descriptor = m_playbackSegments.at(m_activePlaybackSegmentIndex)
                                    .value(QStringLiteral("descriptor")).value<Bloom::PlaybackDescriptor>();
        if (descriptor.selectedAudioTrackId == QString::number(m_selectedAudioTrack)
            && !descriptor.audioOutputPlan.source.codec.isEmpty()
            && (m_preparedAudioPlan.isEmpty()
                || m_preparedAudioPlan.value(QStringLiteral("source")).toMap() != descriptor.audioOutputPlan.source.toVariantMap()))
            m_preparedAudioPlan = descriptor.audioOutputPlan.toVariantMap();
        for (const auto &track : descriptor.audioTracks) {
            if (track.trackId == QString::number(m_selectedAudioTrack)) source = track.audio;
        }
    }
    auto plan = m_playbackService->audioOutput()->planForMetadata(source, !m_exclusiveFallback);
    if (m_preparedAudioPlan.isEmpty()
        || m_preparedAudioPlan.value(QStringLiteral("source")).toMap() != source.toVariantMap())
        m_preparedAudioPlan = plan.toVariantMap();
    if (m_forcePcmAudio) {
        // Channel layout and exclusive access are planned independently of the
        // carrier, so only the encoded path is dropped here.
        plan.path = QStringLiteral("pcm");
        plan.passthroughCodec.clear();
        plan.atmosRetained = false;
        plan.dtsXRetained = false;
        plan.reason = QStringLiteral("Runtime rejected planned output; local PCM fallback");
    }
    if (m_exclusiveFallback)
        plan.reason += QStringLiteral("; exclusive open failed, shared mode");
    return plan;
}

void PlayerController::updateAudioPlan()
{
    m_audioPlan = computeAudioPlan();
    m_audioPlanEndpointId = m_playbackService->audioOutput()->capabilities().endpointId;
    m_reopeningAudio = false;
    ++m_audioPlanGeneration;
    m_audioDiagnostics = {{QStringLiteral("plan"), m_audioPlan.toVariantMap()},
                         {QStringLiteral("preparedPlan"), m_preparedAudioPlan},
                         {QStringLiteral("capabilities"), m_playbackService->audioOutput()->capabilities().toVariantMap()}};
    emit audioDiagnosticsChanged();
    qCInfo(lcPlayback) << "Audio output plan" << m_audioDiagnostics;
    updateAudioOutputStatus();
}

void PlayerController::applyAudioPlanOptions()
{
    const auto args = Bloom::AudioPolicy::mpvArguments(m_audioPlan);
    for (const auto &arg : args) {
        const int separator = arg.indexOf(QLatin1Char('='));
        m_playerBackend->sendVariantCommand({"set_property", arg.mid(2, separator - 2), arg.mid(separator + 1)});
    }
}

QString PlayerController::audioDeviceLabel(const QString &device) const
{
    for (const QVariant &value : m_availableAudioDevices) {
        const QVariantMap entry = value.toMap();
        if (entry.value(QStringLiteral("name")).toString() == device) {
            const QString description = entry.value(QStringLiteral("description")).toString();
            return description.isEmpty() ? device : description;
        }
    }
    if (m_config && device == m_config->getAudioOutputDevice()
        && !m_config->getAudioOutputDeviceDescription().isEmpty()) {
        return m_config->getAudioOutputDeviceDescription();
    }
    return device;
}

void PlayerController::updateAudioOutputStatus()
{
    auto *service = m_playbackService->audioOutput();
    const Bloom::AudioDeviceResolution resolution = service->deviceResolution();
    const bool running = m_playerBackend && m_playerBackend->isRunning();
    const QVariantMap runtime = m_audioDiagnostics.value(QStringLiteral("runtime")).toMap();
    const QString ao = runtime.value(QStringLiteral("current-ao")).toString();
    const bool observable = m_audioDiagnostics.value(QStringLiteral("outputObservable")).toBool();
    const QVariantMap status{
        {QStringLiteral("preferredDevice"), resolution.preferred},
        {QStringLiteral("preferredDescription"), resolution.preferred == QStringLiteral("auto")
             ? QString() : audioDeviceLabel(resolution.preferred)},
        {QStringLiteral("preferredAvailable"), resolution.preferredAvailable},
        {QStringLiteral("device"), resolution.device},
        {QStringLiteral("description"), resolution.device == QStringLiteral("auto")
             ? QString() : audioDeviceLabel(resolution.device)},
        {QStringLiteral("rematched"), resolution.rematched},
        {QStringLiteral("usingFallback"), resolution.preferred != resolution.device && !resolution.rematched},
        {QStringLiteral("driver"), service->activeDriver()},
        {QStringLiteral("exclusiveSupported"), service->capabilities().exclusiveModeSupported},
        {QStringLiteral("exclusiveEnabled"), m_config && m_config->getAudioExclusiveMode()},
        {QStringLiteral("exclusiveRequested"), running && m_audioPlan.exclusivePcm},
        {QStringLiteral("exclusiveActive"), running
             && m_audioDiagnostics.value(QStringLiteral("exclusiveActive")).toBool()},
        {QStringLiteral("exclusiveFallback"), running && m_exclusiveFallback},
        {QStringLiteral("outputOpen"), running && observable && ao != QStringLiteral("null")},
        {QStringLiteral("currentAo"), running ? ao : QString()},
    };
    if (resolution.preferredAvailable) {
        m_reportedMissingAudioDevice.clear();
    } else if (running && m_reportedMissingAudioDevice != resolution.preferred) {
        // Once per device per session; the saved preference is left unchanged.
        m_reportedMissingAudioDevice = resolution.preferred;
        reportAudioOutputNotice(tr("%1 is unavailable. Using System Default.")
                                    .arg(audioDeviceLabel(resolution.preferred)));
    }

    const QVariantList options = service->outputOptions();
    if (status == m_audioOutputStatus && options == m_audioOutputOptions) return;
    m_audioOutputStatus = status;
    m_audioOutputOptions = options;
    emit audioOutputStatusChanged();
}

void PlayerController::reportAudioOutputNotice(const QString &message)
{
    qCInfo(lcPlayback) << "Audio output notice:" << message;
    emit audioOutputNotice(message);
    // External mpv covers Bloom's window, so mirror the notice on mpv's OSD.
    if (m_playerBackend && m_playerBackend->isRunning() && !m_playerBackend->supportsEmbeddedVideo())
        m_playerBackend->sendVariantCommand({QStringLiteral("show-text"), message, 4000});
}

void PlayerController::selectAudioOutputDevice(const QString &device)
{
    QString id = device.trimmed();
    if (id.isEmpty()) id = QStringLiteral("auto");
    QString description;
    for (const QVariant &value : m_availableAudioDevices) {
        const QVariantMap entry = value.toMap();
        if (entry.value(QStringLiteral("name")).toString() == id)
            description = entry.value(QStringLiteral("description")).toString();
    }
    if (description.isEmpty() && id == m_config->getAudioOutputDevice())
        description = m_config->getAudioOutputDeviceDescription();
    const bool unchanged = id == m_config->getAudioOutputDevice()
        && description == m_config->getAudioOutputDeviceDescription();
    m_config->setAudioOutputDeviceSelection(id, description);
    if (!unchanged) return; // audioOutputDeviceChanged reopens audio on the new device.

    // Choosing the current output again retries it after a session fallback.
    m_exclusiveFallback = false;
    m_reportedMissingAudioDevice.clear();
    m_playbackService->audioOutput()->clearDeviceFailures();
    if (m_playerBackend->isRunning()) {
        m_audioDeviceReloadTimer->stop();
        m_audioReloadForced = false;
        applyAudioOutputDevice();
    }
}

void PlayerController::refreshAudioOutputDevices()
{
    m_playbackService->audioOutput()->refresh();
    // A running player already observes mpv's live list.
    if (!m_playerBackend->isRunning()) m_playerBackend->refreshAudioDevices();
}

void PlayerController::applySubtitleDelay()
{
    if (!m_playerBackend) {
        return;
    }

    double delaySeconds = static_cast<double>(m_subtitleDelayMs) / 1000.0;
    if (selectedSubtitleIsSidecar()) {
        // Sidecar cues are on the source clock; a server stream that starts
        // its clock later shows them that much earlier.
        delaySeconds -= m_streamTimelineOffsetSeconds;
    }
    qCDebug(lcPlayback) << "PlayerController: Applying subtitle delay:" << delaySeconds << "s";
    m_playerBackend->sendVariantCommand({"set_property", "sub-delay", delaySeconds});
}

void PlayerController::persistSubtitleDelayForCurrentScope(int delayMs)
{
    if (!PlayerPolicy::playbackContentRules(m_currentContentKind).persistsSubtitleDelay) {
        return;
    }
    if (!m_currentSeasonId.isEmpty()) {
        updateSeasonPreference(m_currentSeasonId, [delayMs](ScopedTrackPreferences &preferences) {
            preferences.subtitleDelayMs = delayMs;
        });
    } else if (m_currentSeriesId.isEmpty() && !m_currentItemId.isEmpty()) {
        updateMoviePreference(m_currentItemId, [delayMs](ScopedTrackPreferences &preferences) {
            preferences.subtitleDelayMs = delayMs;
        });
    }
}

int PlayerController::subtitleDelayForScope(const QString &seasonId, const QString &itemId, bool isMovie) const
{
    if (!seasonId.isEmpty()) {
        return m_trackPrefs->getSeasonPreferences(seasonId).subtitleDelayMs;
    }
    if (isMovie && !itemId.isEmpty()) {
        return m_trackPrefs->getMoviePreferences(itemId).subtitleDelayMs;
    }
    return 0;
}

void PlayerController::persistAudioPreferenceForCurrentScope(int index, TrackPick pick,
                                                             int previousIndex)
{
    persistTrackPreferenceForCurrentScope(TrackPreferenceWrite::Kind::Audio, index, pick,
                                          previousIndex);
}

void PlayerController::persistSubtitlePreferenceForCurrentScope(int index, TrackPick pick,
                                                                int previousIndex)
{
    persistTrackPreferenceForCurrentScope(TrackPreferenceWrite::Kind::Subtitle, index, pick,
                                          previousIndex);
}

void PlayerController::persistTrackPreferenceForCurrentScope(TrackPreferenceWrite::Kind kind,
                                                             int index, TrackPick pick,
                                                             int previousIndex)
{
    if (!PlayerPolicy::playbackContentRules(m_currentContentKind).persistsTrackPreferences) {
        return;
    }
    const bool subtitle = kind == TrackPreferenceWrite::Kind::Subtitle;
    // Silo keeps track preferences per series for episodes and per movie.
    QString contentId;
    TrackSelectionPreference saved;
    if (!m_currentSeasonId.isEmpty()) {
        contentId = m_currentSeriesId;
        const ScopedTrackPreferences scope = m_trackPrefs->getSeasonPreferences(m_currentSeasonId);
        saved = subtitle ? scope.subtitle : scope.audio;
    } else if (m_currentSeriesId.isEmpty() && !m_currentItemId.isEmpty()) {
        contentId = m_currentItemId;
        const ScopedTrackPreferences scope = m_trackPrefs->getMoviePreferences(m_currentItemId);
        saved = subtitle ? scope.subtitle : scope.audio;
    } else {
        return;
    }
    std::optional<HeldTrackPick> &held = m_heldTrackPicks[subtitle ? 1 : 0];
    if (pick == TrackPick::Restore) {
        restoreHeldTrackPick(kind, contentId, index);
        return;
    }
    if (pick == TrackPick::User) {
        held.reset(); // this pick plays already and supersedes a held one
    } else if (!held || held->streamIndex != index) {
        // Remember what the pick replaces: the saved choice, and a write of
        // the replaced track still waiting (a held pick's included), so a
        // refusal brings both back.
        HeldTrackPick next;
        next.streamIndex = index;
        ReplacedTrackPick replaced;
        replaced.streamIndex = previousIndex;
        replaced.preference = saved;
        replaced.write = takePendingTrackPreferenceWrite(kind, contentId);
        next.replaced.append(replaced);
        if (held) {
            next.replaced.append(held->replaced);
        }
        held = next;
    }
    saveCurrentScopeTrackPreference(
        kind, PlayerPolicy::explicitTrackPreference(m_activeMediaSource, index, subtitle));
    scheduleTrackPreferenceWrite(kind, contentId, m_activeMediaSource, index,
                                 pick == TrackPick::UserAwaitingReplan);
}

void PlayerController::saveCurrentScopeTrackPreference(TrackPreferenceWrite::Kind kind,
                                                       const TrackSelectionPreference &preference)
{
    if (!m_currentSeasonId.isEmpty()) {
        saveSeasonTrackPreference(m_currentSeasonId, kind, preference);
    } else if (m_currentSeriesId.isEmpty() && !m_currentItemId.isEmpty()) {
        saveMovieTrackPreference(m_currentItemId, kind, preference);
    }
}

void PlayerController::restoreHeldTrackPick(TrackPreferenceWrite::Kind kind,
                                            const QString &contentId, int playingIndex)
{
    const bool subtitle = kind == TrackPreferenceWrite::Kind::Subtitle;
    std::optional<HeldTrackPick> &slot = m_heldTrackPicks[subtitle ? 1 : 0];
    if (!slot) {
        return; // the undone pick was never saved, so nothing changed
    }
    const HeldTrackPick held = *std::exchange(slot, std::nullopt);
    // The undone pick's own write never goes out.
    cancelTrackPreferenceWrite(kind, contentId);
    const auto replaced = std::find_if(
        held.replaced.cbegin(), held.replaced.cend(),
        [playingIndex](const ReplacedTrackPick &entry) {
            return entry.streamIndex == playingIndex;
        });
    TrackSelectionPreference restored;
    if (replaced != held.replaced.cend()) {
        // The track that played before plays again: so does the choice that
        // picked it, whether a pick, the server's or Bloom's defaults.
        restored = replaced->preference;
        if (replaced->write) {
            queueTrackPreferenceWrite(*replaced->write);
            saveCurrentScopeTrackPreference(kind, restored);
            return;
        }
    } else if (playingIndex >= 0 || (subtitle && playingIndex == -1)) {
        // The server delivered another track than asked for: that is what
        // the season or movie keeps.
        restored = PlayerPolicy::explicitTrackPreference(m_activeMediaSource, playingIndex,
                                                         subtitle);
    } else {
        restored = held.replaced.isEmpty() ? TrackSelectionPreference{}
                                           : held.replaced.first().preference;
    }
    saveCurrentScopeTrackPreference(kind, restored);
    if (restored.mode == TrackPreferenceMode::Unset || !m_playbackService) {
        return;
    }
    // A server that already holds an undone pick gets the restored track.
    TrackPreferenceWrite key;
    key.kind = kind;
    key.contentId = contentId;
    key.requestIdentity = m_playbackService->trackPreferenceWriteIdentity();
    const auto sent = m_sentTrackPreferenceIndexes.constFind(trackPreferenceWriteKey(key));
    if (sent != m_sentTrackPreferenceIndexes.cend() && sent.value() != playingIndex) {
        scheduleTrackPreferenceWrite(kind, contentId, m_activeMediaSource, playingIndex);
    }
}

std::optional<TrackPreferenceWrite> PlayerController::takePendingTrackPreferenceWrite(
    TrackPreferenceWrite::Kind kind, const QString &contentId)
{
    for (qsizetype position = 0; position < m_pendingTrackPreferenceWrites.size(); ++position) {
        const TrackPreferenceWrite &write = m_pendingTrackPreferenceWrites.at(position).write;
        if (write.kind == kind && write.contentId == contentId) {
            TrackPreferenceWrite taken = write;
            m_pendingTrackPreferenceWrites.removeAt(position);
            return taken;
        }
    }
    return std::nullopt;
}

void PlayerController::queueTrackPreferenceWrite(const TrackPreferenceWrite &write)
{
    cancelTrackPreferenceWrite(write.kind, write.contentId);
    PendingTrackPreferenceWrite pending;
    pending.write = write;
    m_pendingTrackPreferenceWrites.append(pending);
    m_trackPreferenceWriteTimer->start();
}

void PlayerController::rollBackUngrantedTrackPicks(int inFlightAudio, int inFlightSubtitle)
{
    // A held pick the answered change asked for, but the server delivered
    // another track: the pick is undone like a refusal, and only what plays
    // may reach the server.
    const std::optional<HeldTrackPick> &audio = m_heldTrackPicks[0];
    if (audio && audio->streamIndex == inFlightAudio && m_selectedAudioTrack != inFlightAudio) {
        qCInfo(lcPlayback) << "Server delivered audio" << m_selectedAudioTrack
                           << "instead of the picked" << inFlightAudio;
        persistAudioPreferenceForCurrentScope(m_selectedAudioTrack, TrackPick::Restore);
    }
    const std::optional<HeldTrackPick> &subtitle = m_heldTrackPicks[1];
    if (subtitle && subtitle->streamIndex == inFlightSubtitle
        && m_selectedSubtitleTrack != inFlightSubtitle) {
        qCInfo(lcPlayback) << "Server delivered subtitle" << m_selectedSubtitleTrack
                           << "instead of the picked" << inFlightSubtitle;
        persistSubtitlePreferenceForCurrentScope(m_selectedSubtitleTrack, TrackPick::Restore);
    }
}

void PlayerController::chooseSeasonAudioTrack(const QString &seasonId, const QString &seriesId,
                                              const QVariantMap &mediaSource, int index)
{
    if (seasonId.isEmpty()) {
        return;
    }
    saveSeasonTrackPreference(seasonId, TrackPreferenceWrite::Kind::Audio,
                              PlayerPolicy::explicitTrackPreference(mediaSource, index, false));
    scheduleTrackPreferenceWrite(TrackPreferenceWrite::Kind::Audio, seriesId, mediaSource, index);
}

void PlayerController::chooseSeasonSubtitleTrack(const QString &seasonId, const QString &seriesId,
                                                 const QVariantMap &mediaSource, int index)
{
    if (seasonId.isEmpty()) {
        return;
    }
    saveSeasonTrackPreference(seasonId, TrackPreferenceWrite::Kind::Subtitle,
                              PlayerPolicy::explicitTrackPreference(mediaSource, index, true));
    scheduleTrackPreferenceWrite(TrackPreferenceWrite::Kind::Subtitle, seriesId, mediaSource,
                                 index);
}

void PlayerController::chooseMovieAudioTrack(const QString &movieId,
                                             const QVariantMap &mediaSource, int index)
{
    if (movieId.isEmpty()) {
        return;
    }
    saveMovieTrackPreference(movieId, TrackPreferenceWrite::Kind::Audio,
                             PlayerPolicy::explicitTrackPreference(mediaSource, index, false));
    scheduleTrackPreferenceWrite(TrackPreferenceWrite::Kind::Audio, movieId, mediaSource, index);
}

void PlayerController::chooseMovieSubtitleTrack(const QString &movieId,
                                                const QVariantMap &mediaSource, int index)
{
    if (movieId.isEmpty()) {
        return;
    }
    saveMovieTrackPreference(movieId, TrackPreferenceWrite::Kind::Subtitle,
                             PlayerPolicy::explicitTrackPreference(mediaSource, index, true));
    scheduleTrackPreferenceWrite(TrackPreferenceWrite::Kind::Subtitle, movieId, mediaSource,
                                 index);
}

QString PlayerController::trackPreferenceWriteKey(const TrackPreferenceWrite &write) const
{
    return QStringList{write.requestIdentity,
                       write.kind == TrackPreferenceWrite::Kind::Audio ? QStringLiteral("audio")
                                                                       : QStringLiteral("subtitle"),
                       write.contentId}
        .join(QChar(0x1f));
}

void PlayerController::scheduleTrackPreferenceWrite(TrackPreferenceWrite::Kind kind,
                                                    const QString &contentId,
                                                    const QVariantMap &mediaSource,
                                                    int index,
                                                    bool awaitingReplan)
{
    cancelTrackPreferenceWrite(kind, contentId);
    if (!m_playbackService || contentId.isEmpty() || mediaSource.isEmpty()
        || (kind == TrackPreferenceWrite::Kind::Audio ? index < 0 : index < -1)
        || (m_config && !m_config->getUseServerTrackPreferences())) {
        return;
    }
    PendingTrackPreferenceWrite pending;
    pending.write.kind = kind;
    pending.write.contentId = contentId;
    pending.write.mediaSource = mediaSource;
    pending.write.streamIndex = index;
    pending.write.requestIdentity = m_playbackService->trackPreferenceWriteIdentity();
    pending.awaitingReplan = awaitingReplan;
    if (pending.write.requestIdentity.isEmpty()) {
        return;
    }
    m_pendingTrackPreferenceWrites.append(pending);
    if (!awaitingReplan) {
        m_trackPreferenceWriteTimer->start();
    }
}

void PlayerController::cancelTrackPreferenceWrite(TrackPreferenceWrite::Kind kind,
                                                  const QString &contentId)
{
    m_pendingTrackPreferenceWrites.removeIf(
        [kind, &contentId](const PendingTrackPreferenceWrite &pending) {
            return pending.write.kind == kind && pending.write.contentId == contentId;
        });
    if (std::none_of(m_pendingTrackPreferenceWrites.cbegin(),
                     m_pendingTrackPreferenceWrites.cend(),
                     [](const PendingTrackPreferenceWrite &pending) {
                         return !pending.awaitingReplan;
                     })) {
        m_trackPreferenceWriteTimer->stop();
    }
}

void PlayerController::releaseHeldTrackPreferenceWrites()
{
    if (!m_pendingAudioSwitchContext.isEmpty() || m_replanQueued) {
        return;
    }
    // Every change answered: the picks still held play.
    m_heldTrackPicks = {};
    bool released = false;
    for (PendingTrackPreferenceWrite &pending : m_pendingTrackPreferenceWrites) {
        released = released || pending.awaitingReplan;
        pending.awaitingReplan = false;
    }
    if (released) {
        m_trackPreferenceWriteTimer->start();
    }
}

void PlayerController::dropHeldTrackPreferenceWrites()
{
    m_heldTrackPicks = {};
    m_pendingTrackPreferenceWrites.removeIf([](const PendingTrackPreferenceWrite &pending) {
        return pending.awaitingReplan;
    });
}

void PlayerController::dropStaleTrackPreferenceWrites()
{
    const QString identity =
        m_playbackService ? m_playbackService->trackPreferenceWriteIdentity() : QString();
    const bool enabled = !m_config || m_config->getUseServerTrackPreferences();
    const qsizetype dropped = m_pendingTrackPreferenceWrites.removeIf(
        [&identity, enabled](const PendingTrackPreferenceWrite &pending) {
            return !enabled || identity.isEmpty() || pending.write.requestIdentity != identity;
        });
    if (dropped > 0) {
        qCInfo(lcPlayback) << "Dropped" << dropped
                           << "track preference write(s) for a previous account or profile";
    }
    const QString prefix = identity + QChar(0x1f);
    for (auto it = m_sentTrackPreferenceIndexes.begin();
         it != m_sentTrackPreferenceIndexes.end();) {
        it = identity.isEmpty() || !it.key().startsWith(prefix)
            ? m_sentTrackPreferenceIndexes.erase(it) : std::next(it);
    }
    if (m_pendingTrackPreferenceWrites.isEmpty()) {
        m_trackPreferenceWriteTimer->stop();
    }
}

void PlayerController::flushTrackPreferenceWrites()
{
    m_trackPreferenceWriteTimer->stop();
    // A write picked under another account or profile, or after the user
    // turned server preferences off, is dropped rather than sent.
    dropStaleTrackPreferenceWrites();
    QList<PendingTrackPreferenceWrite> held;
    const QList<PendingTrackPreferenceWrite> writes =
        std::exchange(m_pendingTrackPreferenceWrites, {});
    for (const PendingTrackPreferenceWrite &pending : writes) {
        if (pending.awaitingReplan) {
            held.append(pending);
            continue;
        }
        m_sentTrackPreferenceIndexes.insert(trackPreferenceWriteKey(pending.write),
                                            pending.write.streamIndex);
        m_playbackService->writeTrackPreference(pending.write);
    }
    m_pendingTrackPreferenceWrites = held;
}

void PlayerController::updateSeasonPreference(const QString &seasonId,
                                              std::function<void(ScopedTrackPreferences &preferences)> updater)
{
    if (seasonId.isEmpty()) {
        return;
    }

    ScopedTrackPreferences preferences = m_trackPrefs->getSeasonPreferences(seasonId);
    updater(preferences);
    if (preferences.isEmpty()) {
        m_trackPrefs->clearSeasonPreferences(seasonId);
    } else {
        m_trackPrefs->setSeasonPreferences(seasonId, preferences);
    }
}

void PlayerController::updateMoviePreference(const QString &movieId,
                                             std::function<void(ScopedTrackPreferences &preferences)> updater)
{
    if (movieId.isEmpty()) {
        return;
    }

    ScopedTrackPreferences preferences = m_trackPrefs->getMoviePreferences(movieId);
    updater(preferences);
    if (preferences.isEmpty()) {
        m_trackPrefs->clearMoviePreferences(movieId);
    } else {
        m_trackPrefs->setMoviePreferences(movieId, preferences);
    }
}

QVariantList PlayerController::buildAvailableTrackOptions(const QVariantMap &mediaSource, const QString &streamType) const
{
    QVariantList options;
    for (const QVariant &streamVariant : mediaStreamsForType(mediaSource, streamType)) {
        const QVariantMap stream = streamVariant.toMap();
        QVariantMap option;
        option[QStringLiteral("index")] = stream.value(QStringLiteral("index"), -1).toInt();
        option[QStringLiteral("displayTitle")] = trackTitleForStream(stream);
        option[QStringLiteral("language")] = stream.value(QStringLiteral("language")).toString();
        option[QStringLiteral("codec")] = stream.value(QStringLiteral("codec")).toString();
        option[QStringLiteral("channels")] = stream.value(QStringLiteral("channels")).toInt(0);
        option[QStringLiteral("channelLayout")] = stream.value(QStringLiteral("channelLayout")).toString();
        option[QStringLiteral("isDefault")] = stream.value(QStringLiteral("isDefault"), false).toBool();
        option[QStringLiteral("isForced")] = stream.value(QStringLiteral("isForced"), false).toBool();
        option[QStringLiteral("isHearingImpaired")] =
            stream.value(QStringLiteral("isHearingImpaired"), false).toBool();
        options.append(option);
    }
    return options;
}

double PlayerController::videoFramerateForMediaSource(const QVariantMap &mediaSource) const
{
    for (const QVariant &streamVariant : mediaStreamsForType(mediaSource, QStringLiteral("Video"))) {
        const QVariantMap stream = streamVariant.toMap();
        const double realFrameRate = stream.value(QStringLiteral("realFrameRate")).toDouble();
        if (realFrameRate > 0.0) {
            return realFrameRate;
        }
        const double averageFrameRate = stream.value(QStringLiteral("averageFrameRate")).toDouble();
        if (averageFrameRate > 0.0) {
            return averageFrameRate;
        }
    }
    return 0.0;
}

bool PlayerController::mediaSourceIsHdr(const QVariantMap &mediaSource) const
{
    return PlayerPolicy::isHdr(
        PlayerPolicy::classifyMediaSourceHdr(mediaSource));
}

// === PRIVATE HELPERS ===

void PlayerController::setPlaybackState(PlaybackState state)
{
    if (m_playbackState != state) {
        const bool wasActive = isPlaybackActive();
        const bool wasStartupBuffering = isStartupBuffering();
        m_playbackState = state;
        updateSleepTimerTickTimer();
        emit playbackStateChanged();
        emit stateChanged(stateName());
        emit isBufferingChanged();
        emit isLoadingChanged();
        if (wasStartupBuffering != isStartupBuffering()) {
            emit isStartupBufferingChanged();
        }
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

void PlayerController::setWaitingForRemoteMountInitialCache(bool waiting)
{
    if (m_waitingForRemoteMountInitialCache == waiting) {
        return;
    }

    const bool wasStartupBuffering = isStartupBuffering();
    m_waitingForRemoteMountInitialCache = waiting;
    if (wasStartupBuffering != isStartupBuffering()) {
        emit isStartupBufferingChanged();
    }
}

void PlayerController::completeRemoteMountStartupBuffering(double cacheSeconds, const char *reason)
{
    if (!m_waitingForRemoteMountInitialCache || m_completingRemoteMountStartupBuffering) {
        return;
    }

    m_completingRemoteMountStartupBuffering = true;
    setWaitingForRemoteMountInitialCache(false);
    qCInfo(lcPlaybackTrace) << "[attempt" << m_playbackAttemptId
                            << "] remote-mount initial cache complete"
                            << "reason=" << reason
                            << "cacheSeconds=" << cacheSeconds
                            << "state=" << stateToString(m_playbackState);
    m_playerBackend->sendVariantCommand(QVariantList{
        QStringLiteral("set_property"),
        QStringLiteral("pause"),
        false,
    });

    if (m_playbackState == Buffering) {
        processEvent(Event::BufferComplete);
    }
    m_completingRemoteMountStartupBuffering = false;
}

void PlayerController::reportPlaybackStart()
{
    const QVariantMap segment = activePlaybackSegment();
    const QString reportItemId = segment.isEmpty()
        ? m_currentItemId
        : segment.value(QStringLiteral("itemId")).toString();
    if (!reportItemId.isEmpty() && m_playbackService) {
        qCInfo(lcPlayback) << "Playback started: itemId=" << reportItemId
                           << "duration=" << m_duration << "s"
                           << "audio=" << (segment.isEmpty() ? m_selectedAudioTrack : segment.value(QStringLiteral("audioIndex")).toInt())
                           << "subtitle=" << (segment.isEmpty() ? m_selectedSubtitleTrack : segment.value(QStringLiteral("subtitleIndex")).toInt());
        m_playbackService->reportPlaybackStart(reportItemId,
                                               segment.isEmpty() ? m_mediaSourceId : segment.value(QStringLiteral("mediaSourceId")).toString(),
                                               segment.isEmpty() ? m_selectedAudioTrack : segment.value(QStringLiteral("audioIndex")).toInt(),
                                               segment.isEmpty() ? m_selectedSubtitleTrack : segment.value(QStringLiteral("subtitleIndex")).toInt(),
                                               segment.isEmpty() ? m_playSessionId : segment.value(QStringLiteral("playSessionId")).toString(),
                                               m_duration > 0.0, false, m_muted,
                                               m_playMethod);
    }
}

void PlayerController::reportPlaybackProgress()
{
    // Servers reap sessions that stop reporting (Silo after 30 paused minutes),
    // so a paused player still sends a paused sample once a minute.
    if (m_playbackState == Paused) {
        if (++m_pausedProgressTicks >= kPausedProgressEveryTicks) {
            m_pausedProgressTicks = 0;
            reportPlaybackProgressNow();
        }
        return;
    }
    m_pausedProgressTicks = 0;
    const QVariantMap segment = activePlaybackSegment();
    const QString reportItemId = segment.isEmpty()
        ? m_currentItemId
        : segment.value(QStringLiteral("itemId")).toString();
    if (!reportItemId.isEmpty() && m_playbackService && m_playbackState == Playing) {
        m_playbackService->reportPlaybackProgress(reportItemId,
                                                  currentReportingPositionMs(),
                                                  segment.isEmpty() ? m_mediaSourceId : segment.value(QStringLiteral("mediaSourceId")).toString(),
                                                  segment.isEmpty() ? m_selectedAudioTrack : segment.value(QStringLiteral("audioIndex")).toInt(),
                                                  segment.isEmpty() ? m_selectedSubtitleTrack : segment.value(QStringLiteral("subtitleIndex")).toInt(),
                                                  segment.isEmpty() ? m_playSessionId : segment.value(QStringLiteral("playSessionId")).toString(),
                                                  m_duration > 0.0, false, m_muted,
                                                  m_playMethod);
    }
}

void PlayerController::reportPlaybackProgressNow()
{
    const QVariantMap segment = activePlaybackSegment();
    const QString reportItemId = segment.isEmpty()
        ? m_currentItemId
        : segment.value(QStringLiteral("itemId")).toString();
    if (!reportItemId.isEmpty() && m_playbackService
        && (m_playbackState == Playing || m_playbackState == Paused)) {
        m_playbackService->reportPlaybackProgress(reportItemId,
                                                  currentReportingPositionMs(),
                                                  segment.isEmpty() ? m_mediaSourceId : segment.value(QStringLiteral("mediaSourceId")).toString(),
                                                  segment.isEmpty() ? m_selectedAudioTrack : segment.value(QStringLiteral("audioIndex")).toInt(),
                                                  segment.isEmpty() ? m_selectedSubtitleTrack : segment.value(QStringLiteral("subtitleIndex")).toInt(),
                                                  segment.isEmpty() ? m_playSessionId : segment.value(QStringLiteral("playSessionId")).toString(),
                                                  m_duration > 0.0,
                                                  m_playbackState == Paused, m_muted,
                                                  m_playMethod);
    }
}

PlayerController::PlaybackStopReportSnapshot PlayerController::capturePlaybackStopReportSnapshot() const
{
    PlaybackStopReportSnapshot snapshot;
    const QVariantMap segment = activePlaybackSegment();

    snapshot.itemId = segment.isEmpty()
        ? m_currentItemId
        : segment.value(QStringLiteral("itemId")).toString();
    snapshot.mediaSourceId = segment.isEmpty()
        ? m_mediaSourceId
        : segment.value(QStringLiteral("mediaSourceId")).toString();
    snapshot.audioStreamIndex = segment.isEmpty()
        ? m_selectedAudioTrack
        : segment.value(QStringLiteral("audioIndex"), -1).toInt();
    snapshot.subtitleStreamIndex = segment.isEmpty()
        ? m_selectedSubtitleTrack
        : segment.value(QStringLiteral("subtitleIndex"), -1).toInt();
    snapshot.playSessionId = segment.isEmpty()
        ? m_playSessionId
        : segment.value(QStringLiteral("playSessionId")).toString();
    snapshot.positionMs = currentReportingPositionMs();
    snapshot.canSeek = m_duration > 0.0;
    snapshot.isPaused = m_playbackState == Paused;
    snapshot.isMuted = m_muted;
    snapshot.playMethod = m_playMethod;
    snapshot.aggregatePositionSeconds = m_currentPosition;
    snapshot.durationSeconds = m_duration;
    snapshot.valid = !snapshot.itemId.isEmpty() && m_playbackService;

    return snapshot;
}

void PlayerController::reportPlaybackStop()
{
    if (m_hasReportedStopForAttempt) {
        qCDebug(lcPlayback) << "Skipping duplicate playback stop report for attempt" << m_playbackAttemptId;
        return;
    }

    const PlaybackStopReportSnapshot snapshot = capturePlaybackStopReportSnapshot();
    if (snapshot.valid) {
        double percentage = snapshot.durationSeconds > 0
            ? (snapshot.aggregatePositionSeconds / snapshot.durationSeconds) * 100.0
            : 0;
        qCInfo(lcPlayback) << "Playback stopped: itemId=" << snapshot.itemId
                           << "position=" << snapshot.aggregatePositionSeconds << "s /" << snapshot.durationSeconds << "s"
                           << "(" << percentage << "%)";
        m_playbackService->reportPlaybackProgress(snapshot.itemId,
                                                  snapshot.positionMs,
                                                  snapshot.mediaSourceId,
                                                  snapshot.audioStreamIndex,
                                                  snapshot.subtitleStreamIndex,
                                                  snapshot.playSessionId,
                                                  snapshot.canSeek,
                                                  snapshot.isPaused,
                                                  snapshot.isMuted,
                                                  snapshot.playMethod);
        m_playbackService->reportPlaybackStopped(snapshot.itemId,
                                                 snapshot.positionMs,
                                                 snapshot.mediaSourceId,
                                                 snapshot.audioStreamIndex,
                                                 snapshot.subtitleStreamIndex,
                                                 snapshot.playSessionId,
                                                 snapshot.canSeek,
                                                 snapshot.isPaused,
                                                 snapshot.isMuted,
                                                 snapshot.playMethod);
        m_hasReportedStopForAttempt = true;
    } else {
        qCWarning(lcPlayback) << "Skipping playback stop report due to missing item or playback service context"
                              << "attempt=" << m_playbackAttemptId
                              << "itemId=" << snapshot.itemId
                              << "hasPlaybackService=" << (m_playbackService != nullptr);
    }
}

void PlayerController::checkCompletionThreshold()
{
    checkCompletionThresholdAndAutoplay();
}

bool PlayerController::wouldMeetCompletionThreshold() const
{
    return PlayerPolicy::meetsCompletionThreshold({
        m_hasEvaluatedCompletionForAttempt,
        m_currentItemId,
        m_currentSeriesId,
        m_currentPosition,
        m_duration,
        m_config->getPlaybackCompletionThreshold()
    });
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
    const bool thresholdMet = wouldMeetCompletionThreshold();
    m_hasEvaluatedCompletionForAttempt = true;

    if (thresholdMet) {
        const double percentage = (m_currentPosition / m_duration) * 100.0;
        const int threshold = m_config->getPlaybackCompletionThreshold();
        qCDebug(lcPlayback) << "PlayerController: Completion threshold met for item" << m_currentItemId
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
    if (!PlayerPolicy::playbackContentRules(m_currentContentKind).allowsNextEpisode) {
        return;
    }
    if (!PlayerPolicy::shouldPrefetchNextEpisode({
            m_nextEpisodePrefetchRequestedForAttempt,
            m_playbackState == Playing || m_playbackState == Paused,
            m_currentSeriesId,
            m_currentItemId,
            m_currentPosition,
            m_duration,
            kNextEpisodePrefetchTriggerPercent
        })) {
        return;
    }

    const double progressPercent = (m_currentPosition / m_duration) * 100.0;
    m_nextEpisodePrefetchRequestedForAttempt = true;
    qCDebug(lcPlayback) << "Triggering next-episode prefetch"
                        << "itemId=" << m_currentItemId
                        << "seriesId=" << m_currentSeriesId
                        << "progressPercent=" << progressPercent;
    m_libraryService->getNextUnplayedEpisode(m_currentSeriesId,
                                             m_currentItemId,
                                             expectedPrefetchRequestContext());
}

QString PlayerController::expectedPrefetchRequestContext() const
{
    return PlayerPolicy::nextEpisodeRequestContext(
        QStringLiteral("prefetch"), m_currentSeriesId, m_currentItemId);
}

QString PlayerController::expectedAutoplayResolutionRequestContext() const
{
    return PlayerPolicy::nextEpisodeRequestContext(
        QStringLiteral("resolve"),
        m_pendingAutoplaySeriesId,
        m_pendingAutoplayItemId);
}

bool PlayerController::matchesPlaybackRequestContext(const QString &requestContext,
                                                     bool awaitingPlaybackEnd) const
{
    const QString expectedContext = awaitingPlaybackEnd
        ? expectedAutoplayResolutionRequestContext()
        : expectedPrefetchRequestContext();
    if (expectedContext.isEmpty()) {
        return requestContext.isEmpty();
    }
    if (requestContext != expectedContext) {
        qCDebug(lcPlayback) << "Ignoring next-episode response with unexpected context"
                            << "expected=" << expectedContext
                            << "received=" << requestContext
                            << "awaitingPlaybackEnd=" << awaitingPlaybackEnd;
        return false;
    }
    return true;
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
    return PlayerPolicy::isUsablePrefetchedEpisode({
        m_nextEpisodePrefetchReady,
        m_prefetchedNextEpisodeData,
        m_prefetchedNextEpisodeSeriesId,
        m_prefetchedForItemId,
        m_pendingAutoplaySeriesId,
        m_pendingAutoplayItemId
    });
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

    const bool configuredAutoplay = m_config->getAutoplayNextEpisode();
    const bool askStillWatching = configuredAutoplay && shouldAskStillWatchingForUpNext();
    const bool autoplay = configuredAutoplay && !askStillWatching;
    const QString carrySeasonId =
        pendingAutoplayCarrySeasonId(episodeSeasonId(m_prefetchedNextEpisodeData));
    const QString prefetchedSeriesId = m_prefetchedNextEpisodeSeriesId;

    m_shouldAutoplay = false;
    m_waitingForNextEpisodeAtPlaybackEnd = false;

    qCDebug(lcPlayback) << "Using prefetched next episode for Up Next"
                        << "itemId=" << m_pendingAutoplayItemId
                        << "seriesId=" << prefetchedSeriesId;

    emitNavigateToNextEpisodeQueued(m_prefetchedNextEpisodeData,
                                    prefetchedSeriesId,
                                    carrySeasonId,
                                    autoplay,
                                    askStillWatching);
    clearNextEpisodePrefetchState();
}

void PlayerController::emitNavigateToNextEpisodeQueued(const QVariantMap &episodeData,
                                                       const QString &seriesId,
                                                       const QString &carryTrackPreferencesFromSeasonId,
                                                       bool autoplay,
                                                       bool stillWatchingPrompt)
{
    QMetaObject::invokeMethod(this,
                              [this, episodeData, seriesId, carryTrackPreferencesFromSeasonId,
                               autoplay, stillWatchingPrompt]() {
                                  // Main.qml reads the prompt state when it opens Up Next.
                                  setStillWatchingPromptActive(stillWatchingPrompt);
                                  emit navigateToNextEpisode(episodeData,
                                                             seriesId,
                                                             carryTrackPreferencesFromSeasonId,
                                                             autoplay);
                                  setAwaitingNextEpisodeResolution(false);
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
    m_prefetchedNextEpisodeData.clear();
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
    m_pendingAutoplayToneMapToSdr = m_contentShouldToneMapToSdr;
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
    stopAutoplayPlaybackInfoWait();
    m_pendingAutoplayDescriptorContext.clear();
    m_pendingAutoplayDescriptorItemId.clear();
    m_pendingAutoplayDescriptorSource.clear();
    m_pendingAutoplayDescriptorSessionId.clear();
    m_pendingAutoplayDescriptorAudioTracks.clear();
    m_pendingAutoplayDescriptorSubtitleTracks.clear();
    m_pendingAutoplayItemId.clear();
    m_pendingAutoplaySeriesId.clear();
    m_pendingAutoplaySeasonId.clear();
    m_pendingAutoplayLibraryId.clear();
    m_pendingAutoplayAudioTrack = -1;
    m_pendingAutoplaySubtitleTrack = -1;
    m_pendingAutoplayFramerate = 0.0;
    m_pendingAutoplayIsHDR = false;
    m_pendingAutoplayToneMapToSdr = false;
    m_pendingAutoplayEpisodeData.clear();
    setAwaitingNextEpisodeResolution(false);
    setStillWatchingPromptActive(false);
}

void PlayerController::releaseDeferredPostPlaybackDisplayRestore()
{
    if (m_afterMovieRestoreAttemptId != 0) {
        // Parked for an after-movie screen, which releases it itself.
        return;
    }
    // Leaving Up Next without starting the next episode also ends the
    // session a running sleep timer belonged to.
    if (!isPlaybackActive()) {
        clearSleepTimer();
    }
    maybeStartDeferredPostPlaybackDisplayRestore();
}

void PlayerController::releaseAfterMovieDisplayRestore(qulonglong attemptId)
{
    if (attemptId == 0 || attemptId != m_afterMovieRestoreAttemptId) {
        qCDebug(lcPlaybackTrace) << "[attempt" << m_playbackAttemptId
                                 << "] stale after-movie display release ignored"
                                 << "attemptId=" << attemptId
                                 << "parkedFor=" << m_afterMovieRestoreAttemptId;
        return;
    }
    // The movie's sleep timer already ended with it; leave any other alone.
    maybeStartDeferredPostPlaybackDisplayRestore();
}

QString PlayerController::pendingAutoplayCarrySeasonId(const QString &targetSeasonId) const
{
    const ScopedTrackPreferences carried =
        seasonTrackPreferencesWithCarry(targetSeasonId, m_pendingAutoplaySeasonId);
    return carried == seasonTrackPreferencesWithCarry(targetSeasonId, QString())
        ? QString() : m_pendingAutoplaySeasonId;
}

ScopedTrackPreferences PlayerController::seasonTrackPreferencesWithCarry(
    const QString &seasonId, const QString &carryFromSeasonId) const
{
    ScopedTrackPreferences preferences = m_trackPrefs->getSeasonPreferences(seasonId);
    if (seasonId.isEmpty() || carryFromSeasonId.isEmpty() || carryFromSeasonId == seasonId) {
        return preferences;
    }
    // Only an explicit pick carries; Bloom's own choice saves nothing.
    const ScopedTrackPreferences carried = m_trackPrefs->getSeasonPreferences(carryFromSeasonId);
    if (preferences.audio.mode == TrackPreferenceMode::Unset) {
        preferences.audio = carried.audio;
    }
    if (preferences.subtitle.mode == TrackPreferenceMode::Unset) {
        preferences.subtitle = carried.subtitle;
    }
    return preferences;
}

void PlayerController::saveCarriedTrackPreferences()
{
    const QString carrySeasonId = std::exchange(m_carryTrackPreferencesFromSeasonId, QString());
    if (carrySeasonId.isEmpty() || m_currentSeasonId.isEmpty()
        || !PlayerPolicy::playbackContentRules(m_currentContentKind).persistsTrackPreferences) {
        return;
    }
    const ScopedTrackPreferences carried =
        seasonTrackPreferencesWithCarry(m_currentSeasonId, carrySeasonId);
    const ScopedTrackPreferences own = m_trackPrefs->getSeasonPreferences(m_currentSeasonId);
    if (carried == own) {
        return;
    }
    // Local only: the series' server row already holds the carried pick.
    qCInfo(lcPlayback) << "Keeping the track picks of season" << carrySeasonId
                       << "for season" << m_currentSeasonId;
    updateSeasonPreference(m_currentSeasonId, [&carried](ScopedTrackPreferences &preferences) {
        preferences.audio = carried.audio;
        preferences.subtitle = carried.subtitle;
    });
}

qint64 PlayerController::resumePositionMs(const QVariantMap &item)
{
    return qMax<qint64>(0, item.value(QStringLiteral("positionMs")).toLongLong());
}

void PlayerController::fallbackToPendingAutoplayPlayback()
{
    // A fallback descriptor is itself asynchronous. If it has already timed
    // out, stop here rather than issuing the same request repeatedly.
    if (m_pendingAutoplayDescriptorContext.endsWith(QStringLiteral("|fallback"))) {
        stopAutoplayPlaybackInfoWait();
        clearPendingAutoplayContext();
        clearNextEpisodePrefetchState();
        return;
    }
    stopAutoplayPlaybackInfoWait();

    const QString itemId = m_pendingAutoplayEpisodeData.value(QStringLiteral("itemId")).toString();
    if (itemId.isEmpty()) {
        qCWarning(lcPlayback) << "Cannot fall back to pending autoplay playback without an episode id";
        clearPendingAutoplayContext();
        clearNextEpisodePrefetchState();
        return;
    }

    const qint64 startPositionMs = resumePositionMs(m_pendingAutoplayEpisodeData);

    QString targetSeasonId = m_pendingAutoplayEpisodeData.value(QStringLiteral("seasonId")).toString();
    if (targetSeasonId.isEmpty()) {
        targetSeasonId = m_pendingAutoplayEpisodeData.value(QStringLiteral("parentId")).toString();
    }
    if (targetSeasonId.isEmpty()) {
        targetSeasonId = m_pendingAutoplaySeasonId;
    }

    const QString seriesId = m_pendingAutoplaySeriesId;
    const QString libraryId = m_pendingAutoplayLibraryId;
    const double framerate = m_pendingAutoplayFramerate;
    const bool isHdr = m_pendingAutoplayIsHDR;
    const bool toneMapToSdr = m_pendingAutoplayToneMapToSdr;
    m_pendingAutoplayDescriptorItemId = itemId;
    m_pendingAutoplayDescriptorSource.clear();
    m_pendingAutoplayDescriptorAudioIndex = -1;
    m_pendingAutoplayDescriptorSubtitleIndex = -1;
    m_pendingAutoplayDescriptorStartPositionMs = startPositionMs;
    m_pendingAutoplayDescriptorSessionId.clear();
    m_pendingAutoplayDescriptorAudioTracks.clear();
    m_pendingAutoplayDescriptorSubtitleTracks.clear();
    m_pendingAutoplayDescriptorContext =
        QStringLiteral("autoplay|descriptor|") + itemId + QStringLiteral("|fallback");
    armAutoplayPlaybackInfoWait();
    m_playbackService->requestPlaybackDescriptor(
        itemId,
        {},
        -1,
        -1,
        startPositionMs,
        QString(),
        m_pendingAutoplayDescriptorContext);
}

void PlayerController::stopAutoplayPlaybackInfoWait()
{
    m_waitingForAutoplayPlaybackInfo = false;
    if (m_autoplayPlaybackInfoTimeoutTimer) {
        m_autoplayPlaybackInfoTimeoutTimer->stop();
    }
}
void PlayerController::armAutoplayPlaybackInfoWait()
{
    m_waitingForAutoplayPlaybackInfo = true;
    if (m_autoplayPlaybackInfoTimeoutTimer) {
        m_autoplayPlaybackInfoTimeoutTimer->start(kAutoplayPlaybackInfoTimeoutMs);
    }
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
    const QString redactedUrl = QUrl(url).toString(QUrl::RemoveQuery | QUrl::RemoveUserInfo);
    qCDebug(lcPlayback) << "PlayerController: Starting playback of" << redactedUrl;
    qCInfo(lcPlaybackTrace) << "[attempt" << m_playbackAttemptId
                            << "] start-playback"
                            << "contentIsHDR=" << m_contentIsHDR
                            << "toneMapToSdr=" << m_contentShouldToneMapToSdr
                            << "hdrOutputMode=" << m_config->getHDROutputMode()
                            << "contentFramerate=" << m_contentFramerate
                            << "url=" << redactedUrl;
    
    // Cancel any pending deferred mpv start from previous playback
    m_startDelayTimer->stop();
    cancelPendingDisplayRestore();
    const quint64 displayGeneration = m_displayRestoreGeneration;
    
    // Handle Display Settings - HDR FIRST (must be done before refresh rate change)
    // Toggling HDR can reset the display mode, so we set HDR first, then refresh rate
    const HdrPlaybackPolicy hdrPolicy = computeEffectiveHdrPlaybackPolicy();
    const bool shouldRestoreHdr = !hdrPolicy.shouldToggleDisplayHdr
        && m_displayManager->needsHdrRestore();
    if (hdrPolicy.shouldToggleDisplayHdr || shouldRestoreHdr) {
        // Snapshot refresh before HDR toggle. Some setups force 60Hz in HDR,
        // and we want restore to return to the pre-HDR rate.
        if (hdrPolicy.shouldToggleDisplayHdr) {
            m_displayManager->captureOriginalRefreshRate();
        }
        const bool requestedHdrState = hdrPolicy.shouldToggleDisplayHdr;
        qCDebug(lcPlayback) << "PlayerController: Applying HDR state"
                            << requestedHdrState;
        qCInfo(lcPlaybackTrace) << "[attempt" << m_playbackAttemptId
                                << "] set-hdr begin"
                                << "enabled=" << requestedHdrState
                                << "generation=" << displayGeneration;
        m_displayPreparationConnection = connect(
            m_displayManager,
            &DisplayManager::hdrChangeFinished,
            this,
            [this, displayGeneration, requestedHdrState](bool enabled, bool success) {
                if (displayGeneration != m_displayRestoreGeneration
                    || enabled != requestedHdrState) {
                    return;
                }
                if (m_displayPreparationConnection) {
                    disconnect(m_displayPreparationConnection);
                    m_displayPreparationConnection = QMetaObject::Connection();
                }
                qCInfo(lcPlaybackTrace) << "[attempt" << m_playbackAttemptId
                                        << "] set-hdr finished"
                                        << "enabled=" << enabled
                                        << "success=" << success
                                        << "generation=" << displayGeneration;
                applyFramerateMatchingAndStart();
            });
        m_displayManager->setHDRAsync(requestedHdrState);
        return;
    } else if (m_config->getEnableHDR() && !m_contentIsHDR) {
        qCDebug(lcPlayback) << "PlayerController: HDR toggle enabled but content is SDR, not switching display HDR";
    } else if (hdrPolicy.toneMapToSdr) {
        qCDebug(lcPlayback) << "PlayerController: HDR content will be tone-mapped locally to SDR; not switching display HDR";
    }

    applyFramerateMatchingAndStart();
}

QStringList PlayerController::applyDolbyVisionPlaybackArgs(const QStringList &args)
{
    const PlayerPolicy::HdrContentKind kind = m_activeMediaSource.isEmpty()
        ? (m_contentIsHDR ? PlayerPolicy::HdrContentKind::Hdr : PlayerPolicy::HdrContentKind::Sdr)
        : PlayerPolicy::classifyMediaSourceHdr(m_activeMediaSource);
    m_activeDolbyVisionSource = PlayerPolicy::describeDolbyVisionSource(m_activeMediaSource);
    m_activeRendererKind = m_playerBackend->rendererKind(args);

    const PlayerPolicy::MpvRuntimeInfo runtime = m_playerBackend->runtimeInfo();
    const QStringList result =
        PlayerPolicy::appendDolbyVisionFilterArgs(args, kind, runtime.formatFilter);
    if (!m_activeDolbyVisionSource.isDolbyVision) {
        return result;
    }

    const PlayerPolicy::FelCapability capability =
        PlayerPolicy::evaluateFelCapability(runtime, m_activeRendererKind);
    qCInfo(lcPlayback).noquote()
        << QStringLiteral("Dolby Vision source: %1 hdrKind=%2 backend=%3 formatFilter=%4 dvFilter=%5 %6")
               .arg(PlayerPolicy::dolbyVisionSourceSummary(m_activeDolbyVisionSource),
                    PlayerPolicy::hdrContentKindName(kind),
                    m_playerBackend->backendName(),
                    PlayerPolicy::formatFilterSupportName(runtime.formatFilter),
                    result != args ? result.last() : QStringLiteral("none"),
                    PlayerPolicy::felCapabilitySummary(capability));
    const QStringList stripping = PlayerPolicy::dolbyVisionStrippingFilterArgs(result);
    if (!stripping.isEmpty()) {
        qCWarning(lcPlayback) << "Dolby Vision metadata or enhancement layer disabled by configured filters:"
                              << stripping;
    }
    if (m_activeDolbyVisionSource.dualLayer
        && capability.overall == PlayerPolicy::Capability::Unsupported) {
        qCWarning(lcPlayback).noquote()
            << QStringLiteral("Dolby Vision Profile 7 enhancement-layer (FEL) reconstruction is not available on this runtime; "
                              "continuing with the available base-layer renderer. %1")
                   .arg(capability.reasons.join(QStringLiteral("; ")));
    }
    return result;
}

void PlayerController::onBackendRuntimeInfoChanged()
{
    const PlayerPolicy::MpvRuntimeInfo runtime = m_playerBackend->runtimeInfo();
    const QString identity = QStringList{runtime.backend, runtime.executablePath, runtime.libraryPath,
                                         runtime.mpvVersion,
                                         PlayerPolicy::formatFilterSupportName(runtime.formatFilter)}
                                 .join(QLatin1Char('|'));
    if (identity == m_loggedRuntimeIdentity) {
        return;
    }
    m_loggedRuntimeIdentity = identity;
    const PlayerPolicy::RendererKind renderer = m_playerBackend->rendererKind(
        m_config ? m_config->getMpvArgsForProfile(m_config->getDefaultProfileName(), true) : QStringList{});
    const PlayerPolicy::FelCapability capability = PlayerPolicy::evaluateFelCapability(runtime, renderer);
    qCInfo(lcPlayback).noquote()
        << QStringLiteral("mpv runtime: backend=%1 mpv=%2 executable=%3 libmpv=%4 client-api=%5 ffmpeg=%6 libavcodec=%7 libplacebo=%8 "
                          "verified-pin=%9 format-filter=%10 renderer=%11 %12")
               .arg(runtime.backend,
                    runtime.mpvVersion.isEmpty() ? QStringLiteral("unknown") : runtime.mpvVersion,
                    runtime.executablePath.isEmpty() ? QStringLiteral("n/a") : runtime.executablePath,
                    runtime.libraryPath.isEmpty() ? QStringLiteral("n/a") : runtime.libraryPath,
                    runtime.clientApiVersion.isEmpty() ? QStringLiteral("n/a") : runtime.clientApiVersion,
                    runtime.ffmpegVersion.isEmpty() ? QStringLiteral("unknown") : runtime.ffmpegVersion,
                    runtime.libavcodecVersion.isNull() ? QStringLiteral("unknown")
                                                       : runtime.libavcodecVersion.toString(),
                    runtime.libplaceboVersion.isEmpty() ? QStringLiteral("unknown") : runtime.libplaceboVersion,
                    runtime.matchesVerifiedPackagedStack ? QStringLiteral("yes") : QStringLiteral("no"))
               .arg(PlayerPolicy::formatFilterSupportName(runtime.formatFilter),
                    PlayerPolicy::rendererKindName(renderer),
                    PlayerPolicy::felCapabilitySummary(capability));
    if (runtime.formatFilter != PlayerPolicy::FormatFilterSupport::Unknown
        && capability.overall == PlayerPolicy::Capability::Unsupported) {
        qCWarning(lcPlayback).noquote()
            << QStringLiteral("mpv runtime lacks Dolby Vision Profile 7 FEL support: %1")
                   .arg(capability.reasons.join(QStringLiteral("; ")));
    }
}

void PlayerController::onPlaybackDiagnosticsAvailable(const QString &stage, const QVariantMap &properties)
{
    if (stage.startsWith(QStringLiteral("audio|"))) {
        if (stage != QStringLiteral("audio|%1").arg(m_audioPlanGeneration)) return;
        auto diagnostics = Bloom::AudioPolicy::verify(m_audioPlan, properties);
        const bool endpointAvailable = m_playbackService->audioOutput()->capabilities().endpointAvailable;
        diagnostics.insert(QStringLiteral("endpointAvailable"), endpointAvailable);
        diagnostics.insert(QStringLiteral("effectiveDeviceEvidence"), QStringLiteral("Exact opened device is not exposed by mpv; OS endpoint snapshot only"));
        if (!endpointAvailable) diagnostics.insert(QStringLiteral("differsFromPlan"), true);
        diagnostics.insert(QStringLiteral("preparedPlan"), m_preparedAudioPlan);
        const bool fallbackOccurred = m_forcePcmAudio;
        diagnostics.insert(QStringLiteral("fallbackOccurred"), fallbackOccurred);
        diagnostics.insert(QStringLiteral("fallbackReason"), fallbackOccurred ? m_audioPlan.reason : QString());
        diagnostics.insert(QStringLiteral("differsFromPreparedPlan"), fallbackOccurred
            || diagnostics.value(QStringLiteral("differsFromPlan")).toBool()
            || m_preparedAudioPlan.value(QStringLiteral("path")).toString() != m_audioPlan.path
            || m_preparedAudioPlan.value(QStringLiteral("device")).toString() != m_audioPlan.device);
        diagnostics.insert(QStringLiteral("capabilities"), m_playbackService->audioOutput()->capabilities().toVariantMap());
        if (diagnostics != m_audioDiagnostics) {
            m_audioDiagnostics = diagnostics;
            emit audioDiagnosticsChanged();
            qCInfo(lcPlayback) << "Audio runtime verification" << diagnostics;
        }
        auto *audioOutput = m_playbackService->audioOutput();
        const QString currentAo = properties.value(QStringLiteral("current-ao")).toString();
        audioOutput->setRuntimeDriver(currentAo);
        audioOutput->setEndpointHeld(diagnostics.value(QStringLiteral("exclusiveActive")).toBool());
        updateAudioOutputStatus();
        const bool nullOutput = currentAo == QStringLiteral("null");
        // Fallback ladder, one step per verification: encoded carrier -> PCM,
        // exclusive -> shared, explicit endpoint -> System Default, then
        // rate-limited reopen attempts.
        if (!m_forcePcmAudio && diagnostics.value(QStringLiteral("needsPcmFallback")).toBool()) {
            m_forcePcmAudio = true;
            applyAudioOutputDevice();
        } else if (nullOutput && m_audioPlan.exclusivePcm && !m_exclusiveFallback && endpointAvailable) {
            // Device in use by another exclusive client, or no exclusive format.
            m_exclusiveFallback = true;
            reportAudioOutputNotice(tr("Exclusive mode is unavailable for this output. Using shared mode."));
            applyAudioOutputDevice();
        } else if (nullOutput && m_audioPlan.device != QStringLiteral("auto")) {
            const QString failed = m_audioPlan.device;
            reportAudioOutputNotice(tr("%1 could not be opened. Using System Default.")
                                        .arg(audioDeviceLabel(failed)));
            // Resolution changes to auto; capabilitiesChanged schedules the reopen.
            m_reportedMissingAudioDevice = m_config->getAudioOutputDevice();
            m_audioReloadForced = true;
            audioOutput->markDeviceFailed(failed);
        } else if (nullOutput && QDateTime::currentMSecsSinceEpoch() >= m_nextAudioRetryMs) {
            m_nextAudioRetryMs = QDateTime::currentMSecsSinceEpoch() + 5000;
            m_playbackService->audioOutput()->refresh();
            applyAudioOutputDevice();
        }
        return;
    }
    const PlayerPolicy::PlaybackDiagnostics diagnostics =
        PlayerPolicy::evaluatePlaybackDiagnostics(properties);
    const QString summary = QStringLiteral("Playback diagnostics [%1] backend=%2 mpv=%3 ffmpeg=%4 libplacebo=%5 hdrOutputMode=%6 %7")
                                .arg(stage,
                                     m_playerBackend->backendName(),
                                     properties.value(QStringLiteral("mpv-version")).toString(),
                                     properties.value(QStringLiteral("ffmpeg-version")).toString(),
                                     properties.value(QStringLiteral("libplacebo-version")).toString(),
                                     m_config ? m_config->getHDROutputMode() : QString(),
                                     PlayerPolicy::playbackDiagnosticsSummary(diagnostics,
                                                                             m_activeDolbyVisionSource));
    // HDR/DV playback is where these details matter; keep SDR logs quiet.
    if (m_contentIsHDR || m_activeDolbyVisionSource.isDolbyVision || diagnostics.dolbyVisionProfile > 0) {
        qCInfo(lcPlayback).noquote() << summary;
    } else {
        qCDebug(lcPlayback).noquote() << summary;
    }

    const bool profile7 = diagnostics.dolbyVisionProfile == 7 || m_activeDolbyVisionSource.profile == 7;
    if (stage == QStringLiteral("steady") && profile7 && diagnostics.renderPassesObservable) {
        if (diagnostics.enhancementLayerRendered) {
            qCInfo(lcPlayback) << "Dolby Vision Profile 7 FEL: enhancement layer composed by gpu-next/libplacebo";
        } else {
            // libplacebo only composes the EL when the RPU enables NLQ (FEL);
            // MEL streams legitimately render without it.
            qCInfo(lcPlayback) << "Dolby Vision Profile 7: no enhancement-layer composition observed"
                               << "(MEL stream, EL disabled, or EL not delivered by this runtime)";
        }
    }
}

PlayerController::HdrPlaybackPolicy PlayerController::computeEffectiveHdrPlaybackPolicy() const
{
    return PlayerPolicy::evaluateHdrPlayback(
        m_contentIsHDR,
        m_contentShouldToneMapToSdr,
        m_config->getEnableHDR(),
        m_config->getHDROutputMode());
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
                            << "hdrOutputMode=" << m_config->getHDROutputMode()
                            << "toneMapToSdr=" << m_contentShouldToneMapToSdr
                            << "contentIsHDR=" << m_contentIsHDR;

    m_mpvDisplayFpsOverride = 0.0;
    const quint64 displayGeneration = m_displayRestoreGeneration;

    // Handle Display Settings - Framerate Matching
    if (m_config->getEnableFramerateMatching() && m_contentFramerate > 0) {
        // Pass the exact framerate to DisplayManager for precise matching
        // TVs like LG can match exact 23.976Hz, while others will use closest available (24Hz)
        qCDebug(lcPlayback) << "PlayerController: Content framerate:" << m_contentFramerate
                 << "-> attempting exact refresh rate match";
        
        const double requestedFramerate = m_contentFramerate;
        m_displayPreparationConnection = connect(
            m_displayManager,
            &DisplayManager::refreshRateChangeFinished,
            this,
            [this, displayGeneration, requestedFramerate](double requestedHz,
                                                          bool success) {
                if (displayGeneration != m_displayRestoreGeneration
                    || qAbs(requestedHz - requestedFramerate) >= 0.001) {
                    return;
                }
                if (m_displayPreparationConnection) {
                    disconnect(m_displayPreparationConnection);
                    m_displayPreparationConnection = QMetaObject::Connection();
                }
                if (!success) {
                    qCWarning(lcPlaybackTrace) << "[attempt" << m_playbackAttemptId
                                               << "] refresh-rate switch failed";
                    initiateMpvStart();
                    return;
                }

                const double effectiveRefreshRate =
                    m_displayManager->lastRefreshRateSwitchEffectiveRate();
                if (!m_displayManager->lastRefreshRateSwitchSkippedCompatibleMultiple()
                    && qAbs(effectiveRefreshRate - requestedFramerate) < 0.01) {
                    m_mpvDisplayFpsOverride = requestedFramerate;
                }
                if (m_displayManager->lastRefreshRateSwitchChanged()
                    && m_config->getFramerateMatchDelay() > 0) {
                    m_startDelayTimer->start(
                        m_config->getFramerateMatchDelay() * 1000);
                } else {
                    initiateMpvStart();
                }
            });
        m_displayManager->setRefreshRateAsync(requestedFramerate);
        return;
    } else if (m_config->getEnableFramerateMatching()) {
        qCDebug(lcPlayback) << "PlayerController: Framerate matching enabled but no framerate info available (framerate:" << m_contentFramerate << ")";
        qCInfo(lcPlaybackTrace) << "[attempt" << m_playbackAttemptId
                                << "] framerate-matching enabled but no content framerate";
    }
    
    if (m_displayManager->needsRefreshRestore()) {
        m_displayPreparationConnection = connect(
            m_displayManager,
            &DisplayManager::refreshRateRestoreFinished,
            this,
            [this, displayGeneration](bool success) {
                if (displayGeneration != m_displayRestoreGeneration) {
                    return;
                }
                if (m_displayPreparationConnection) {
                    disconnect(m_displayPreparationConnection);
                    m_displayPreparationConnection = QMetaObject::Connection();
                }
                qCInfo(lcPlaybackTrace) << "[attempt" << m_playbackAttemptId
                                        << "] pre-playback refresh restore finished"
                                        << "success=" << success;
                initiateMpvStart();
            });
        m_displayManager->restoreRefreshRateAsync();
        return;
    }

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
    qCDebug(lcPlayback) << "PlayerController: Using MPV profile:" << profileName
             << "for library:" << m_currentLibraryId
             << "series:" << m_currentSeriesId;
    qCInfo(lcPlaybackTrace) << "[attempt" << m_playbackAttemptId
                            << "] initiate-mpv"
                            << "profile=" << profileName
                            << "backend=" << m_playerBackend->backendName();
    
    // Get the args from the profile (includes HDR overrides if enabled)
    QStringList profileArgs = m_config->getMpvArgsForProfile(profileName, m_contentIsHDR, m_contentShouldToneMapToSdr);
    if (m_playerBackend->backendName() == QStringLiteral("win-libmpv")) {
        const QVariantMap profileData = m_config->getMpvProfile(profileName);
        profileArgs << QStringLiteral("--bloom-profile-name=%1").arg(profileName);
        profileArgs << QStringLiteral("--bloom-windows-render-api=%1")
                           .arg(profileData.value(QStringLiteral("windowsRenderApi"), QStringLiteral("auto")).toString());
        profileArgs << QStringLiteral("--bloom-windows-10bit-output=%1")
                           .arg(profileData.value(QStringLiteral("windows10BitOutput"), false).toBool()
                                    ? QStringLiteral("yes")
                                    : QStringLiteral("no"));
    }
    
    // Build final args: Bloom config args + profile args
    QStringList finalArgs;
    finalArgs << m_config->getMpvConfigArgs();  // mpv.conf, input.conf, scripts
    if (m_playerBackend->backendName() == QStringLiteral("win-libmpv")) {
        // The Windows mpv build streams HTTP through libcurl, which retries a
        // dropped transfer only a few times with no delay and then ends the
        // stream. FFmpeg's http protocol honors the --stream-lavf-o reconnect
        // backoff, so mpv buffers through server hiccups instead of hitting EOF.
        finalArgs << QStringLiteral("--curl-enabled=no");
    }
    finalArgs << profileArgs;                        // Profile-specific args

    const bool remoteMountStartupBuffering =
        m_config->resolveStartupBufferingModeForItem(m_currentLibraryId) == QStringLiteral("remote-mount");
    setWaitingForRemoteMountInitialCache(remoteMountStartupBuffering);
    if (remoteMountStartupBuffering) {
        finalArgs << QStringLiteral("--pause=yes");
        finalArgs << QStringLiteral("--cache=yes");
        finalArgs << QStringLiteral("--cache-pause=yes");
        finalArgs << QStringLiteral("--cache-pause-initial=yes");
        finalArgs << QStringLiteral("--cache-pause-wait=%1").arg(kRemoteMountInitialCacheSeconds, 0, 'f', 0);
        finalArgs << QStringLiteral("--cache-secs=120");
        finalArgs << QStringLiteral("--demuxer-readahead-secs=%1").arg(kRemoteMountInitialCacheSeconds, 0, 'f', 0);
        finalArgs << QStringLiteral("--demuxer-max-bytes=2048M");
        finalArgs << QStringLiteral("--demuxer-max-back-bytes=512M");
        finalArgs << QStringLiteral("--stream-buffer-size=4MiB");
        qCInfo(lcPlaybackTrace) << "[attempt" << m_playbackAttemptId
                                << "] remote-mount startup prebuffer enabled"
                                << "library=" << m_currentLibraryId;
    }

    if (m_mpvDisplayFpsOverride > 0.0) {
        // Windows reports fractional modes such as 23.976/29.97/59.94 as
        // integer 23/29/59 Hz. Give mpv the target clock explicitly so its
        // display-sync path does not pace 23.976 fps content against 23.000 Hz.
        finalArgs << "--video-sync=display-resample";
        finalArgs << "--display-fps=" + formatMpvDisplayFps(m_mpvDisplayFpsOverride);
    }

#if defined(Q_OS_LINUX)
    if (m_playerBackend->backendName() == QStringLiteral("linux-libmpv-opengl")) {
        // Embedded libmpv render path should avoid external-process mpv config/scripts
        // and profile switches that can override render-critical options.
        const QStringList filteredArgs = MpvArgFilter::filterBloomManagedArgs(finalArgs);

        qCDebug(lcPlayback) << "PlayerController: Embedded linux backend filtered mpv args:"
                 << "before=" << finalArgs.size()
                 << "after=" << filteredArgs.size();
        finalArgs = filteredArgs;
    }
#endif

    m_forcePcmAudio = false;
    // Each playback session retries exclusive access and failed endpoints.
    m_exclusiveFallback = false;
    m_reportedMissingAudioDevice.clear();
    m_playbackService->audioOutput()->clearDeviceFailures();
    updateAudioPlan();
    finalArgs << Bloom::AudioPolicy::mpvArguments(m_audioPlan);

    // Must run last: the DV filter is appended through mpv's list semantics so
    // a later `--vf=` (profile deinterlace, user extra args) cannot drop it.
    finalArgs = applyDolbyVisionPlaybackArgs(finalArgs);

    if (!m_streamCarriesSourceTracks) {
        // FFmpeg opens a growing (EVENT) HLS playlist near its live edge;
        // a server stream's timeline starts at its first segment.
        finalArgs << QStringLiteral("--demuxer-lavf-o-append=live_start_index=0");
    }

    qCDebug(lcPlayback) << "PlayerController: Final mpv args:" << finalArgs;

    // Stream credentials are appended after logging.
    const QVariantMap streamHeaders =
        activePlaybackSegment().value(QStringLiteral("streamHeaders")).toMap();
    for (auto it = streamHeaders.cbegin(); it != streamHeaders.cend(); ++it) {
        finalArgs << QStringLiteral("--http-header-fields-append=%1: %2")
                         .arg(it.key(), it.value().toString());
    }

    // The desired audio device is applied via startup args above; the first
    // audio-device-list snapshot mpv reports should therefore not trigger a reload.
    m_audioDeviceListInitialized = false;
    m_audioDeviceReloadTimer->stop();

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

bool PlayerController::isRecoverableBackendPlaybackError(const QString &error) const
{
    const QString normalized = error.toLower();
    return normalized.startsWith(QStringLiteral("recoverable-stream-ended-prematurely"))
        || normalized.contains(QStringLiteral("stream ends prematurely"))
        || normalized.contains(QStringLiteral("http error 5"))
        || normalized.contains(QStringLiteral("i/o error"))
        || normalized.contains(QStringLiteral("io error"));
}

void PlayerController::updateSkipSegmentState()
{
    const QString wasInIntroKey = m_activeIntroSkipKey;
    const QString wasInOutroKey = m_activeOutroSkipKey;
    // A file can carry several ranges of one type; touching or overlapping
    // ones count as one, and the one holding the position decides.
    const qint64 positionMs = qRound64(m_currentPosition * 1000.0);
    const auto activeKey = [this, positionMs](MediaSegmentType type) {
        const QList<PlayerPolicy::SkipRange> ranges = skipRanges(type);
        const int index = PlayerPolicy::skipRangeIndex(ranges, positionMs, 0);
        return index >= 0 && ranges.at(index).startMs <= positionMs
            ? segmentSkipKey(type, ranges.at(index)) : QString();
    };
    const QString introKey = activeKey(MediaSegmentType::Intro);
    const QString outroKey = activeKey(MediaSegmentType::Outro);
    const bool inIntro = !introKey.isEmpty();
    const bool inOutro = !outroKey.isEmpty();

    if (wasInIntroKey == introKey && wasInOutroKey == outroKey) {
        return;
    }

    m_activeIntroSkipKey = introKey;
    m_activeOutroSkipKey = outroKey;
    m_isInIntroSegment = inIntro;
    m_isInOutroSegment = inOutro;

    const bool autoSkipAllowedNow = m_playbackState != Paused
                                    && m_playbackState != Idle
                                    && m_playbackState != Error;

    // Auto-skip only on first entry into each intro/outro range of this
    // playback item, so a seek straight from one intro into another still
    // skips the second.
    if (autoSkipAllowedNow
        && inIntro
        && introKey != wasInIntroKey
        && !m_autoSkippedSegments.contains(introKey)
        && m_config->getAutoSkipIntro()) {
        m_autoSkippedSegments.insert(introKey);
        skipIntro();
    } else if (autoSkipAllowedNow
               && inOutro
               && outroKey != wasInOutroKey
               && !m_autoSkippedSegments.contains(outroKey)
               && m_config->getAutoSkipOutro()) {
        m_autoSkippedSegments.insert(outroKey);
        skipOutro();
    }

    emit skipSegmentsChanged();
}

QList<PlayerPolicy::SkipRange> PlayerController::skipRanges(MediaSegmentType type) const
{
    QList<PlayerPolicy::SkipRange> ranges;
    for (const auto &segment : std::as_const(m_currentSegments)) {
        if (segment.type == type && segment.startMs >= 0) {
            ranges.append({segment.startMs, segment.endMs});
        }
    }
    return PlayerPolicy::mergeSkipRanges(std::move(ranges));
}

QString PlayerController::segmentSkipKey(MediaSegmentType type, const PlayerPolicy::SkipRange &range)
{
    return QStringLiteral("%1:%2").arg(static_cast<int>(type)).arg(range.startMs);
}

bool PlayerController::seekToSegmentEnd(MediaSegmentType segmentType)
{
    // Skip the (merged) range holding the position, else one starting
    // within the early grace; never an earlier range, nor a later one far
    // ahead.
    const QList<PlayerPolicy::SkipRange> ranges = skipRanges(segmentType);
    const int index = PlayerPolicy::skipRangeIndex(
        ranges, qRound64(m_currentPosition * 1000.0), kSkipAheadGraceMs);
    if (index < 0) {
        return false;
    }

    const double endSeconds = static_cast<double>(ranges.at(index).endMs) / 1000.0;
    qCDebug(lcPlayback) << "PlayerController: Skipping segment type" << static_cast<int>(segmentType)
             << "seeking to" << endSeconds;
    seek(endSeconds);
    return true;
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
        
        qCDebug(lcPlayback) << "PlayerController: Loaded config from" << configPath;
        qCDebug(lcPlayback) << "PlayerController: mpv binary:" << m_mpvBin;
    } else {
        qCWarning(lcPlayback) << "PlayerController: Could not load config from" << configPath;
        // Defaults
        m_mpvBin = "mpv";
        m_testVideoUrl = "http://commondatastorage.googleapis.com/gtv-videos-bucket/sample/BigBuckBunny.mp4";
    }
    
    if (m_mpvBin == QStringLiteral("mpv")) {
        const QFileInfo bundledMpv(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("mpv")));
        if (bundledMpv.isFile() && bundledMpv.isExecutable()) {
            m_mpvBin = bundledMpv.absoluteFilePath();
        }
    }

    // Log mpv config directory being used
    QString mpvConfigDir = ConfigManager::getMpvConfigDir();
    qCDebug(lcPlayback) << "PlayerController: Bloom mpv config directory:" << mpvConfigDir;
    if (!ConfigManager::getMpvConfPath().isEmpty()) {
        qCDebug(lcPlayback) << "PlayerController: Using mpv.conf from:" << ConfigManager::getMpvConfPath();
    }
    if (!ConfigManager::getMpvInputConfPath().isEmpty()) {
        qCDebug(lcPlayback) << "PlayerController: Using input.conf from:" << ConfigManager::getMpvInputConfPath();
    }
    if (!ConfigManager::getMpvScriptsDir().isEmpty()) {
        qCDebug(lcPlayback) << "PlayerController: Detected user mpv scripts in:" << ConfigManager::getMpvScriptsDir();
    }
}

// === OSC AND TRICKPLAY HANDLERS ===

void PlayerController::onScriptMessage(const QString &messageName, const QStringList &args)
{
    qCDebug(lcPlayback) << "PlayerController: Received script message:" << messageName << "args:" << args;

    // Bloom's script messages come from bindings in mpv's input.conf, so
    // they are user input made in the external mpv window. Other scripts'
    // messages are not counted: scripts broadcast them on their own.
    if (messageName.startsWith(QStringLiteral("bloom-"))) {
        notePlaybackUserInput();
    }

    if (messageName == "bloom-skip-intro") {
        if (!seekToSegmentEnd(MediaSegmentType::Intro)) {
            qCDebug(lcPlayback) << "PlayerController: No intro segment found to skip";
        }
        
    } else if (messageName == "bloom-skip-outro") {
        if (!seekToSegmentEnd(MediaSegmentType::Outro)) {
            qCDebug(lcPlayback) << "PlayerController: No outro segment found to skip";
        }

    } else if (messageName == "bloom-sleep-timer") {
        // No argument steps through the choices; "off", "end" or a listed
        // number of minutes sets one.
        if (!isPlaybackActive()) {
            return;
        }
        const QString argument = args.isEmpty() ? QString() : args.first().trimmed().toLower();
        if (argument.isEmpty()) {
            cycleSleepTimer();
        } else if (argument == QStringLiteral("off")) {
            setSleepTimer(PlayerPolicy::kSleepTimerOff);
        } else if (argument == QStringLiteral("end")) {
            setSleepTimer(PlayerPolicy::kSleepTimerEndOfItem);
        } else {
            bool ok = false;
            const int minutes = argument.toInt(&ok);
            if (ok && minutes > 0 && PlayerPolicy::sleepTimerChoices().contains(minutes)) {
                setSleepTimer(minutes);
            } else {
                qCWarning(lcPlayback) << "Ignoring bloom-sleep-timer with an unknown choice" << argument;
            }
        }
    }
    // Script-driven trickplay handlers were retired with the native overlay migration.
}

void PlayerController::setOverlayMetadata(const QString &title,
                                          const QString &subtitle,
                                          const QString &backdropUrl,
                                          const QString &logoUrl)
{
    const QString normalizedTitle = title.trimmed();
    const QString normalizedSubtitle = subtitle.trimmed();
    const QString normalizedBackdropUrl = backdropUrl.trimmed();
    const QString normalizedLogoUrl = logoUrl.trimmed();
    if (m_overlayTitle == normalizedTitle
        && m_overlaySubtitle == normalizedSubtitle
        && m_overlayBackdropUrl == normalizedBackdropUrl
        && m_overlayLogoUrl == normalizedLogoUrl) {
        return;
    }

    m_overlayTitle = normalizedTitle;
    m_overlaySubtitle = normalizedSubtitle;
    m_overlayBackdropUrl = normalizedBackdropUrl;
    m_overlayLogoUrl = normalizedLogoUrl;
    emit overlayMetadataChanged();
}

void PlayerController::clearOverlayMetadata()
{
    if (m_overlayTitle.isEmpty() && m_overlaySubtitle.isEmpty() && m_overlayBackdropUrl.isEmpty()
        && m_overlayLogoUrl.isEmpty()) {
        return;
    }

    m_overlayTitle.clear();
    m_overlaySubtitle.clear();
    m_overlayBackdropUrl.clear();
    m_overlayLogoUrl.clear();
    emit overlayMetadataChanged();
}

void PlayerController::clearPlaybackChapters()
{
    const bool hadChapters = !m_playbackChapters.isEmpty();
    const bool hadCurrentIndex = m_currentPlaybackChapterIndex != -1;
    m_playbackChapters.clear();
    m_currentPlaybackChapterIndex = -1;
    if (hadChapters) {
        emit playbackChaptersChanged();
    }
    if (hadCurrentIndex) {
        emit currentPlaybackChapterIndexChanged();
    }
}

void PlayerController::updateCurrentPlaybackChapterIndex()
{
    int nextIndex = -1;
    for (int index = 0; index < m_playbackChapters.size(); ++index) {
        const QVariantMap chapter = m_playbackChapters.at(index).toMap();
        if (chapter.value(QStringLiteral("startMs")).toLongLong()
            <= qRound64(m_currentPosition * 1000.0)) {
            nextIndex = index;
        } else {
            break;
        }
    }
    if (nextIndex != m_currentPlaybackChapterIndex) {
        m_currentPlaybackChapterIndex = nextIndex;
        emit currentPlaybackChapterIndexChanged();
    }
}

void PlayerController::onChaptersLoaded(const QString &connectionId,
                                         const QString &itemId,
                                         const QVariantList &chapters)
{
    if (itemId != m_currentItemId
        || (!connectionId.isEmpty()
            && connectionId != m_libraryService->getActiveConnectionId())) {
        return;
    }

    QVariantList chapterModels;
    chapterModels.reserve(chapters.size());
    qCInfo(lcPlayback) << "PlayerController: Loaded canonical playback chapters for item" << itemId
            << "count" << chapters.size();
    for (qsizetype index = 0; index < chapters.size(); ++index) {
        QVariantMap chapter = chapters.at(index).toMap();
        const QVariantMap artwork = chapter.value(QStringLiteral("artwork")).toMap();
        const QString thumbnailUrl = artwork.isEmpty()
            ? QString()
            : m_libraryService->getCachedArtworkUrlFromRef(artwork, 640);
        chapter.insert(QStringLiteral("index"), index);
        chapter.insert(QStringLiteral("thumbnailUrl"), thumbnailUrl);
        chapterModels.append(chapter);
    }

    m_playbackChapters = chapterModels;
    updateCurrentPlaybackChapterIndex();
    emit playbackChaptersChanged();
}

void PlayerController::onChaptersFailed(const QString &connectionId, const QString &itemId, const QString &error)
{
    if (!connectionId.isEmpty()
        && connectionId != m_libraryService->getActiveConnectionId()) {
        return;
    }
    if (itemId == m_currentItemId) {
        qCDebug(lcPlayback) << "PlayerController: Chapter metadata unavailable for item" << itemId << error;
        clearPlaybackChapters();
    }
}

void PlayerController::onMediaSegmentsLoaded(const QString &itemId, const QList<MediaSegmentInfo> &segments)
{
    if (itemId != activePlaybackSegmentItemId()) {
        qCDebug(lcPlayback) << "PlayerController: Ignoring segments for different item:" << itemId;
        return;
    }
    
    qCDebug(lcPlayback) << "PlayerController: Received" << segments.size() << "segments for item:" << itemId;
    m_currentSegments = segments;
    updateSkipSegmentState();

    // Handle early intro/outro segments that can be loaded slightly after playback starts.
    // This keeps auto-skip reliable for intros that begin at/near 0s.
    if (m_playbackState != Paused && m_playbackState != Idle && m_playbackState != Error) {
        const qint64 positionMs = qRound64(m_currentPosition * 1000.0);
        const auto skipEarly = [this, positionMs](MediaSegmentType type) {
            for (const PlayerPolicy::SkipRange &range : skipRanges(type)) {
                const QString key = segmentSkipKey(type, range);
                if (range.startMs <= kSkipAheadGraceMs && positionMs < range.endMs
                    && !m_autoSkippedSegments.contains(key)) {
                    m_autoSkippedSegments.insert(key);
                    return true;
                }
            }
            return false;
        };
        if (m_config->getAutoSkipIntro() && skipEarly(MediaSegmentType::Intro)) {
            skipIntro();
        } else if (m_config->getAutoSkipOutro() && skipEarly(MediaSegmentType::Outro)) {
            skipOutro();
        }
    }
    
    // Segment metadata is kept in controller state for native overlay handling.
    for (const auto &segment : segments) {
        const double startSeconds = static_cast<double>(segment.startMs) / 1000.0;
        const double endSeconds = static_cast<double>(segment.endMs) / 1000.0;

        if (segment.type == MediaSegmentType::Intro) {
            qCDebug(lcPlayback) << "PlayerController: Intro segment:" << startSeconds << "->" << endSeconds;
        } else if (segment.type == MediaSegmentType::Outro) {
            qCDebug(lcPlayback) << "PlayerController: Outro segment:" << startSeconds << "->" << endSeconds;
        }
    }
}

void PlayerController::onTrickplayInfoLoaded(const QString &itemId, const QMap<int, TrickplayTileInfo> &trickplayInfo)
{
    if (itemId != activePlaybackSegmentItemId() || m_terminalTransitionActive) {
        qCDebug(lcPlayback) << "PlayerController: Ignoring trickplay info for different item:" << itemId;
        return;
    }

    const bool wasTrickplayReady = hasTrickplay();
    if (trickplayInfo.isEmpty()
        && (m_useLocalTrickplay || m_hasTrickplayInfo || m_trickplayProcessor->isProcessing())) return;
    m_useLocalTrickplay = false;
    m_localSeekPreview->clear();
    
    if (trickplayInfo.isEmpty()) {
        qCDebug(lcPlayback) << "PlayerController: No trickplay info available for item:" << itemId;
        m_hasTrickplayInfo = false;
        m_currentTrickplayInfo = TrickplayTileInfo{};
        m_trickplayBinaryPath.clear();
        m_currentTrickplayFrameIndex = -1;
        m_hasTrickplayPreviewPositionOverride = false;
        m_trickplayPreviewPositionOverrideSeconds = 0.0;
        clearTrickplayPreview();
        emit trickplayStateChanged();
        enableLocalTrickplay();
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
        qCWarning(lcPlayback) << "PlayerController: Ignoring invalid trickplay info for item:" << itemId
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
        enableLocalTrickplay();
        return;
    }
    
    qCDebug(lcPlayback) << "PlayerController: Received trickplay info for item:" << itemId
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
    if (itemId != activePlaybackSegmentItemId() || m_terminalTransitionActive) {
        qCDebug(lcPlayback) << "PlayerController: Ignoring trickplay processing result for different item:" << itemId;
        return;
    }
    
    qCDebug(lcPlayback) << "PlayerController: Trickplay processing complete for item:" << itemId
             << "count:" << count
             << "interval:" << intervalMs << "ms"
             << "size:" << width << "x" << height
             << "file:" << filePath;

    if (count <= 0 || intervalMs <= 0 || width <= 0 || height <= 0 || filePath.isEmpty() || !QFile::exists(filePath)) {
        qCWarning(lcPlayback) << "PlayerController: Trickplay processing result is invalid, disabling trickplay for item:" << itemId;
        m_hasTrickplayInfo = false;
        m_trickplayBinaryPath.clear();
        m_currentTrickplayFrameIndex = -1;
        m_hasTrickplayPreviewPositionOverride = false;
        m_trickplayPreviewPositionOverrideSeconds = 0.0;
        clearTrickplayPreview();
        emit trickplayStateChanged();
        enableLocalTrickplay();
        return;
    }

    m_currentTrickplayInfo.thumbnailCount = count;
    m_useLocalTrickplay = false;
    m_localSeekPreview->clear();
    m_currentTrickplayInfo.interval = intervalMs;
    m_currentTrickplayInfo.width = width;
    m_currentTrickplayInfo.height = height;
    m_trickplayBinaryPath = filePath;
    m_trickplayFrameUrlCache.clear();
    m_trickplayFrameUrlOrder.clear();
    m_trickplayFrameCachePath = filePath;
    m_hasTrickplayInfo = true;
    m_currentTrickplayFrameIndex = -1;
    updateTrickplayPreviewForPosition(
        m_hasTrickplayPreviewPositionOverride ? m_trickplayPreviewPositionOverrideSeconds : m_currentPosition
    );
    emit trickplayStateChanged();
}

void PlayerController::onTrickplayProcessingFailed(const QString &itemId, const QString &error)
{
    if (itemId != activePlaybackSegmentItemId() || m_terminalTransitionActive) {
        return;
    }
    
    qCWarning(lcPlayback) << "PlayerController: Trickplay processing failed for item:" << itemId << "error:" << error;
    m_trickplayProcessor->clear();
    // Trickplay thumbnails won't be available, but playback continues normally
    m_hasTrickplayInfo = false;
    m_trickplayBinaryPath.clear();
    m_currentTrickplayFrameIndex = -1;
    m_hasTrickplayPreviewPositionOverride = false;
    m_trickplayPreviewPositionOverrideSeconds = 0.0;
    clearTrickplayPreview();
    emit trickplayStateChanged();
    enableLocalTrickplay();
}

void PlayerController::enableLocalTrickplay()
{
    if (m_useLocalTrickplay) return;
    clearTrickplayPreview();
    if (!m_config->getLocalSeekPreviewsEnabled()) {
        qCInfo(lcPlayback) << "Local seek previews disabled in settings";
        emit trickplayStateChanged();
        return;
    }
    const QVariantMap segment = activePlaybackSegment();
    const auto hdrKind = PlayerPolicy::classifyMediaSourceHdr(
        segment.value(QStringLiteral("mediaSource")).toMap());
    // The software preview path cannot convert Dolby Vision's IPT base layer.
    if (hdrKind == PlayerPolicy::HdrContentKind::DolbyVisionUnsupported) {
        emit trickplayStateChanged();
        return;
    }
    m_localSeekPreview->setSource(
        segment.value(QStringLiteral("descriptor")).value<Bloom::PlaybackDescriptor>(),
        PlayerPolicy::isHdr(hdrKind));
    if (!embeddedLinuxTrickplayAllowed(m_playerBackend) || !m_localSeekPreview->isAvailable()) {
        emit trickplayStateChanged();
        return;
    }
    // Frames are generated only when the filmstrip asks for them.
    m_useLocalTrickplay = true;
    emit trickplayStateChanged();
}

void PlayerController::updateLocalSeekPreviewSuspension()
{
    if (m_playbackState != Playing && m_playbackState != Paused) {
        m_localSeekPreviewResumeTimer->stop();
        m_localSeekPreview->setSuspended(true);
    } else if (m_localSeekPreview->isSuspended() && !m_localSeekPreviewResumeTimer->isActive()) {
        m_localSeekPreviewResumeTimer->start();
    }
}

void PlayerController::requestTrickplayInfo(const QString &itemId)
{
    if (m_config->getServerTrickplayEnabled()) {
        m_playbackService->getTrickplayInfo(itemId, m_mediaSourceId);
        return;
    }
    // Treat disabled server trickplay as a server without previews, delivered
    // asynchronously like a real lookup so the local fallback sees a settled segment.
    qCInfo(lcPlayback) << "Server trickplay disabled in settings";
    QTimer::singleShot(0, this, [this, itemId] {
        onTrickplayInfoLoaded(itemId, {});
    });
}

void PlayerController::setTrickplayPreviewPositionSeconds(double seconds)
{
    m_hasTrickplayPreviewPositionOverride = true;
    m_trickplayPreviewPositionOverrideSeconds = qMax(0.0, seconds);
    if (hasTrickplay()) {
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
    if (hasTrickplay()) {
        updateTrickplayPreviewForPosition(m_currentPosition);
    }
}

void PlayerController::updateTrickplayPreviewForPosition(double seconds)
{
    if (m_useLocalTrickplay) {
        // Position updates only show existing frames; the filmstrip requests new ones.
        const QString url = m_localSeekPreview->frameUrlAt(
            seconds - static_cast<double>(m_activePlaybackSegmentOffsetMs) / 1000.0);
        if (m_trickplayPreviewUrl != url) {
            m_trickplayPreviewUrl = url;
            emit trickplayPreviewChanged();
        }
        return;
    }
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
        enableLocalTrickplay();
        return;
    }

    const double segmentSeconds = seconds - static_cast<double>(m_activePlaybackSegmentOffsetMs) / 1000.0;
    const int requestedMs = qMax(0, static_cast<int>(segmentSeconds * 1000.0));
    int frameIndex = requestedMs / m_currentTrickplayInfo.interval;
    frameIndex = qBound(0, frameIndex, m_currentTrickplayInfo.thumbnailCount - 1);
    if (frameIndex == m_currentTrickplayFrameIndex && !m_trickplayPreviewUrl.isEmpty()) {
        return;
    }

    const QString previewUrl = cachedTrickplayFrameUrl(frameIndex);

    if (previewUrl.isEmpty()) {
        qCWarning(lcPlayback) << "PlayerController: Failed to load trickplay frame" << frameIndex << "for item" << m_currentItemId;
        m_hasTrickplayInfo = false;
        m_trickplayBinaryPath.clear();
        m_currentTrickplayFrameIndex = -1;
        m_hasTrickplayPreviewPositionOverride = false;
        m_trickplayPreviewPositionOverrideSeconds = 0.0;
        clearTrickplayPreview();
        emit trickplayStateChanged();
        enableLocalTrickplay();
        return;
    }

    m_currentTrickplayFrameIndex = frameIndex;
    if (m_trickplayPreviewUrl != previewUrl) {
        m_trickplayPreviewUrl = previewUrl;
        emit trickplayPreviewChanged();
    }
}

QString PlayerController::cachedTrickplayFrameUrl(int frameIndex)
{
    // Filmstrip scrubbing re-requests neighbouring frames on every step; keep
    // a small window of encoded frames so each step encodes about one frame.
    static constexpr int kMaxCachedFrames = 32;
    if (m_trickplayFrameCachePath != m_trickplayBinaryPath) {
        m_trickplayFrameUrlCache.clear();
        m_trickplayFrameUrlOrder.clear();
        m_trickplayFrameCachePath = m_trickplayBinaryPath;
    }
    const auto cached = m_trickplayFrameUrlCache.constFind(frameIndex);
    if (cached != m_trickplayFrameUrlCache.constEnd()) {
        return cached.value();
    }

    const QString url = buildTrickplayPreviewDataUrl(m_trickplayBinaryPath,
                                                     frameIndex,
                                                     m_currentTrickplayInfo.width,
                                                     m_currentTrickplayInfo.height);
    if (url.isEmpty()) {
        return url;
    }
    if (m_trickplayFrameUrlOrder.size() >= kMaxCachedFrames) {
        m_trickplayFrameUrlCache.remove(m_trickplayFrameUrlOrder.takeFirst());
    }
    m_trickplayFrameUrlCache.insert(frameIndex, url);
    m_trickplayFrameUrlOrder.append(frameIndex);
    return url;
}

QString PlayerController::trickplayPreviewUrlAt(double seconds, quint64 revision)
{
    Q_UNUSED(revision); // QML binding dependency for asynchronously generated frames.
    seconds -= static_cast<double>(m_activePlaybackSegmentOffsetMs) / 1000.0;
    if (m_useLocalTrickplay) {
        // Only the visible filmstrip calls this, so each call is a frame the user is looking at.
        m_localSeekPreview->request(seconds);
        return m_localSeekPreview->frameUrlAt(seconds);
    }
    if (!m_hasTrickplayInfo
        || m_trickplayBinaryPath.isEmpty()
        || m_currentTrickplayInfo.interval <= 0
        || m_currentTrickplayInfo.thumbnailCount <= 0
        || m_currentTrickplayInfo.width <= 0
        || m_currentTrickplayInfo.height <= 0
        || seconds < 0.0) {
        return QString();
    }
    const int requestedMs = static_cast<int>(seconds * 1000.0);
    const int frameIndex = requestedMs / m_currentTrickplayInfo.interval;
    if (frameIndex >= m_currentTrickplayInfo.thumbnailCount) {
        return QString();
    }
    return cachedTrickplayFrameUrl(frameIndex);
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

// ---- Still watching / sleep timer ----

bool PlayerController::eventFilter(QObject *watched, QEvent *event)
{
    switch (event->type()) {
    case QEvent::KeyPress:
    // A key that matches a Shortcut (Main.qml's playback keys) arrives only
    // as ShortcutOverride and Shortcut; Qt never sends its KeyPress.
    case QEvent::ShortcutOverride:
    case QEvent::Shortcut:
    case QEvent::MouseButtonPress:
    case QEvent::Wheel:
    case QEvent::TouchBegin:
    case QEvent::TabletPress:
        notePlaybackUserInput();
        break;
    case QEvent::Enter:
        // The pointer entering a window (or a window appearing under it) is
        // not movement; it becomes the reference point.
        if (const auto *enter = dynamic_cast<QEnterEvent *>(event)) {
            m_lastPointerGlobalPosition = enter->globalPosition();
            m_lastPointerPositionKnown = true;
        } else {
            m_lastPointerPositionKnown = false;
        }
        break;
    case QEvent::WindowActivate:
        // Positions seen before another window had focus are stale.
        m_lastPointerPositionKnown = false;
        break;
    case QEvent::MouseMove: {
        // Scenes resend hover moves under a resting pointer and sensors
        // jitter by a pixel or two; only travel of a few logical pixels from
        // the reference point is movement. The first move only records it.
        const QPointF position = static_cast<QMouseEvent *>(event)->globalPosition();
        if (!m_lastPointerPositionKnown) {
            m_lastPointerGlobalPosition = position;
            m_lastPointerPositionKnown = true;
        } else if (QLineF(m_lastPointerGlobalPosition, position).length() >= kPointerMovementThreshold) {
            m_lastPointerGlobalPosition = position;
            notePlaybackUserInput();
        }
        break;
    }
    default:
        break;
    }
    return QObject::eventFilter(watched, event);
}

void PlayerController::notePlaybackUserInput()
{
    if (m_autoplayChainCount != PlayerPolicy::autoplayChainCountAfterUserInput()) {
        qCDebug(lcPlayback) << "User input restarts the still-watching count"
                            << "previousCount=" << m_autoplayChainCount;
        m_autoplayChainCount = PlayerPolicy::autoplayChainCountAfterUserInput();
    }
    if (m_sleepTimerGraceActive) {
        cancelSleepTimerGrace();
    }
}

bool PlayerController::shouldAskStillWatchingForUpNext() const
{
    return PlayerPolicy::shouldAskStillWatching(m_autoplayChainCount,
                                                m_config->getStillWatchingEpisodeThreshold());
}

void PlayerController::setStillWatchingPromptActive(bool active)
{
    if (m_stillWatchingPromptActive == active) {
        return;
    }
    m_stillWatchingPromptActive = active;
    emit stillWatchingPromptActiveChanged();
}

QVariantList PlayerController::sleepTimerChoices() const
{
    QVariantList choices;
    for (const int choice : PlayerPolicy::sleepTimerChoices()) {
        choices.append(choice);
    }
    return choices;
}

void PlayerController::setSleepTimer(int choice)
{
    armSleepTimer(choice, false);
    qCInfo(lcPlayback) << "Sleep timer set"
                       << "choice=" << m_sleepTimerChoice
                       << "remainingSeconds=" << m_sleepTimerRemainingSeconds;

    QString text;
    switch (PlayerPolicy::sleepTimerKind(m_sleepTimerChoice)) {
    case PlayerPolicy::SleepTimerKind::Off:
        text = tr("Sleep timer off");
        break;
    case PlayerPolicy::SleepTimerKind::Duration:
        text = tr("Sleep timer: %1 min").arg(m_sleepTimerChoice);
        break;
    case PlayerPolicy::SleepTimerKind::EndOfItem:
        text = tr("Sleep timer: %1").arg(sleepTimerEndOfItemLabel());
        break;
    }
    showSleepTimerOsd(text, 2000);
}

QString PlayerController::sleepTimerEndOfItemLabel() const
{
    if (m_currentContentKind == PlayerPolicy::PlaybackContentKind::Extra) {
        return tr("End of this video");
    }
    return m_currentSeriesId.isEmpty() ? tr("End of this movie") : tr("End of this episode");
}

void PlayerController::cycleSleepTimer()
{
    const QList<int> choices = PlayerPolicy::sleepTimerChoices();
    const int index = choices.indexOf(m_sleepTimerChoice);
    setSleepTimer(choices.at((index + 1) % choices.size()));
}

void PlayerController::armSleepTimer(int choice, bool keepRemaining)
{
    m_sleepTimerChoice = PlayerPolicy::normalizeSleepTimerChoice(choice);
    if (!keepRemaining) {
        m_sleepTimerRemainingSeconds = PlayerPolicy::sleepTimerDurationSeconds(m_sleepTimerChoice);
    }
    m_sleepTimerGraceActive = false;
    m_sleepTimerGraceRemainingSeconds = 0;
    m_sleepTimerPauseRequested = false;
    m_sleepTimerStopRequested = false;
    updateSleepTimerTickTimer();
    emit sleepTimerChanged();
}

void PlayerController::clearSleepTimer()
{
    if (m_sleepTimerChoice == PlayerPolicy::kSleepTimerOff && !m_sleepTimerGraceActive
        && m_sleepTimerRemainingSeconds == 0) {
        m_sleepTimerPauseRequested = false;
        m_sleepTimerStopRequested = false;
        updateSleepTimerTickTimer();
        return;
    }
    armSleepTimer(PlayerPolicy::kSleepTimerOff, false);
}

void PlayerController::updateSleepTimerTickTimer()
{
    // A duration counts playing time only, so nothing ticks between items.
    const bool needsTick = m_sleepTimerGraceActive
        || (isPlaybackActive()
            && PlayerPolicy::sleepTimerKind(m_sleepTimerChoice) == PlayerPolicy::SleepTimerKind::Duration);
    if (!m_sleepTimerTickTimer) {
        return;
    }
    if (!needsTick) {
        m_sleepTimerTickTimer->stop();
    } else if (!m_sleepTimerTickTimer->isActive()) {
        m_sleepTimerTickTimer->start();
    }
}

void PlayerController::onSleepTimerTick()
{
    if (m_sleepTimerGraceActive) {
        m_sleepTimerGraceRemainingSeconds = std::max(0, m_sleepTimerGraceRemainingSeconds - 1);
        emit sleepTimerChanged();
        if (m_sleepTimerGraceRemainingSeconds <= 0) {
            finishSleepTimerGrace();
            return;
        }
        showSleepTimerOsd(tr("Sleep timer: playback stops in %1 s. Resume to keep watching.")
                              .arg(m_sleepTimerGraceRemainingSeconds),
                          1500);
        return;
    }

    if (PlayerPolicy::sleepTimerKind(m_sleepTimerChoice) != PlayerPolicy::SleepTimerKind::Duration) {
        updateSleepTimerTickTimer();
        return;
    }
    // Only playing time counts: pauses, buffering and Up Next do not use it up.
    if (m_playbackState != Playing) {
        return;
    }
    m_sleepTimerRemainingSeconds = std::max(0, m_sleepTimerRemainingSeconds - 1);
    emit sleepTimerChanged();
    if (m_sleepTimerRemainingSeconds <= 0) {
        fireSleepTimer();
    }
}

void PlayerController::fireSleepTimer()
{
    qCInfo(lcPlayback) << "Sleep timer fired; pausing for the grace period"
                       << "itemId=" << m_currentItemId
                       << "graceSeconds=" << PlayerPolicy::kSleepTimerGraceSeconds;
    m_sleepTimerGraceActive = true;
    m_sleepTimerGraceRemainingSeconds = PlayerPolicy::kSleepTimerGraceSeconds;
    if (m_playbackState == Playing || m_playbackState == Buffering) {
        m_sleepTimerPauseRequested = true;
        m_playerBackend->sendCommand({"set", "pause", "yes"});
    }
    updateSleepTimerTickTimer();
    emit sleepTimerChanged();
    showSleepTimerOsd(tr("Sleep timer: playback stops in %1 s. Resume to keep watching.")
                          .arg(m_sleepTimerGraceRemainingSeconds),
                      1500);
}

void PlayerController::cancelSleepTimerGrace()
{
    if (!m_sleepTimerGraceActive) {
        return;
    }
    qCInfo(lcPlayback) << "Sleep timer grace cancelled by user input";
    clearSleepTimer();
    showSleepTimerOsd(tr("Sleep timer cancelled"), 2000);
}

void PlayerController::finishSleepTimerGrace()
{
    qCInfo(lcPlayback) << "Sleep timer grace ended; stopping playback"
                       << "itemId=" << m_currentItemId;
    clearSleepTimer();
    m_sleepTimerStopRequested = true;
    stop();
    if (!m_terminalTransitionActive) {
        m_sleepTimerStopRequested = false;
    }
}

bool PlayerController::applySleepTimerAtTerminal(TerminalReason reason)
{
    // The timer already ran out and was waiting for input when playback
    // ended (the item reached its end before the pause, or mpv closed):
    // the timer ended this playback.
    const bool stoppedByTimer = std::exchange(m_sleepTimerStopRequested, false) || m_sleepTimerGraceActive;
    PlayerPolicy::SleepTimerPlaybackEnd end = PlayerPolicy::SleepTimerPlaybackEnd::Failed;
    if (stoppedByTimer || reason == TerminalReason::Stop) {
        end = PlayerPolicy::SleepTimerPlaybackEnd::Stopped;
    } else if (reason == TerminalReason::PlaybackEnd) {
        end = PlayerPolicy::SleepTimerPlaybackEnd::Finished;
    }

    const bool firesNow = PlayerPolicy::sleepTimerFiresAtPlaybackEnd(m_sleepTimerChoice, end);
    if (firesNow) {
        qCInfo(lcPlayback) << "Sleep timer reached the end of the item; skipping Up Next"
                           << "itemId=" << m_currentItemId;
    }
    if (stoppedByTimer || !PlayerPolicy::sleepTimerSurvivesPlaybackEnd(m_sleepTimerChoice, end)) {
        clearSleepTimer();
    }
    return stoppedByTimer || firesNow;
}

void PlayerController::showSleepTimerOsd(const QString &text, int durationMs)
{
    // External mpv covers Bloom's window, so the timer speaks through mpv's
    // OSD there; embedded backends show EmbeddedPlaybackOverlay instead.
    if (!m_playerBackend || m_playerBackend->supportsEmbeddedVideo() || !m_playerBackend->isRunning()) {
        return;
    }
    m_playerBackend->sendVariantCommand({QStringLiteral("show-text"), text, durationMs});
}
