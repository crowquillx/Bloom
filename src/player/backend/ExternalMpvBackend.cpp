#include "ExternalMpvBackend.h"
#include "player/AudioPolicy.h"

#include <QTimer>

namespace PlayerPolicy = Bloom::PlaybackPolicy;

namespace {
// First snapshot once the first frames have been rendered (vo-passes is
// populated per rendered frame); second one for steady-state drop counters.
constexpr int kStartupSnapshotDelayMs = 1500;
constexpr int kSteadySnapshotDelayMs = 8000;
}

ExternalMpvBackend::ExternalMpvBackend(QObject *parent)
    : IPlayerBackend(parent)
    , m_processManager(new PlayerProcessManager(this))
    , m_runtimeProbe(new MpvRuntimeProbe(this))
{
    connect(m_runtimeProbe, &MpvRuntimeProbe::finished,
            this, &ExternalMpvBackend::runtimeInfoChanged);
    connect(m_processManager, &PlayerProcessManager::fileLoaded,
            this, &ExternalMpvBackend::scheduleDiagnosticSnapshots);
    connect(m_processManager, &PlayerProcessManager::propertySnapshotReady,
            this, &ExternalMpvBackend::playbackDiagnosticsAvailable);
    connect(m_processManager, &PlayerProcessManager::stateChanged, this, [this](bool running) {
        if (!running) {
            ++m_fileGeneration;
        }
    });
    connect(m_processManager, &PlayerProcessManager::stateChanged,
            this, &ExternalMpvBackend::stateChanged);
    connect(m_processManager, &PlayerProcessManager::errorOccurred,
            this, &ExternalMpvBackend::errorOccurred);
    connect(m_processManager, &PlayerProcessManager::positionChanged,
            this, &ExternalMpvBackend::positionChanged);
    connect(m_processManager, &PlayerProcessManager::durationChanged,
            this, &ExternalMpvBackend::durationChanged);
    connect(m_processManager, &PlayerProcessManager::pauseChanged,
            this, &ExternalMpvBackend::pauseChanged);
    connect(m_processManager, &PlayerProcessManager::pausedForCacheChanged,
            this, &ExternalMpvBackend::pausedForCacheChanged);
    connect(m_processManager, &PlayerProcessManager::volumeChanged,
            this, &ExternalMpvBackend::volumeChanged);
    connect(m_processManager, &PlayerProcessManager::muteChanged,
            this, &ExternalMpvBackend::muteChanged);
    connect(m_processManager, &PlayerProcessManager::playlistPositionChanged,
            this, &ExternalMpvBackend::playlistPositionChanged);
    connect(m_processManager, &PlayerProcessManager::playbackEnded,
            this, &ExternalMpvBackend::playbackEnded);
    connect(m_processManager, &PlayerProcessManager::cacheEndChanged,
            this, &ExternalMpvBackend::cacheEndChanged);
    connect(m_processManager, &PlayerProcessManager::audioTrackChanged,
            this, &ExternalMpvBackend::audioTrackChanged);
    connect(m_processManager, &PlayerProcessManager::subtitleTrackChanged,
            this, &ExternalMpvBackend::subtitleTrackChanged);
    connect(m_processManager, &PlayerProcessManager::audioDeviceListChanged,
            this, &ExternalMpvBackend::audioDeviceListChanged);
    connect(m_processManager, &PlayerProcessManager::scriptMessage,
            this, &ExternalMpvBackend::scriptMessage);
}

QString ExternalMpvBackend::backendName() const
{
        return QStringLiteral("external-mpv-ipc");
}

void ExternalMpvBackend::startMpv(const QString &mpvBin, const QStringList &args, const QString &mediaUrl)
{
    prepareRuntime(mpvBin);
    ++m_fileGeneration;
    m_processManager->startMpv(mpvBin, args, mediaUrl);
}

void ExternalMpvBackend::requestAudioDiagnostics(const QString &tag)
{
    m_processManager->requestPropertySnapshot(tag, Bloom::AudioPolicy::diagnosticProperties());
}

void ExternalMpvBackend::prepareRuntime(const QString &mpvBin)
{
    m_runtimeProbe->probe(mpvBin);
}

PlayerPolicy::MpvRuntimeInfo ExternalMpvBackend::runtimeInfo() const
{
    if (m_runtimeProbe->isFinished()) {
        return m_runtimeProbe->result();
    }
    PlayerPolicy::MpvRuntimeInfo pending;
    pending.backend = backendName();
    pending.executablePath = m_runtimeProbe->executable();
    return pending;
}

void ExternalMpvBackend::scheduleDiagnosticSnapshots()
{
    const quint64 generation = ++m_fileGeneration;
    QStringList properties = PlayerPolicy::runtimeProvenanceProperties();
    properties << PlayerPolicy::playbackDiagnosticProperties();
    const auto requestSnapshot = [this, generation, properties](const QString &stage) {
        if (generation == m_fileGeneration) {
            m_processManager->requestPropertySnapshot(stage, properties);
        }
    };
    QTimer::singleShot(kStartupSnapshotDelayMs, this,
                       [requestSnapshot]() { requestSnapshot(QStringLiteral("startup")); });
    QTimer::singleShot(kSteadySnapshotDelayMs, this,
                       [requestSnapshot]() { requestSnapshot(QStringLiteral("steady")); });
}

void ExternalMpvBackend::appendUrlsToPlaylist(const QStringList &mediaUrls)
{
    m_processManager->appendUrlsToPlaylist(mediaUrls);
}

void ExternalMpvBackend::stopMpv()
{
    m_processManager->stopMpv();
}

bool ExternalMpvBackend::isRunning() const
{
    return m_processManager->isRunning();
}

void ExternalMpvBackend::sendCommand(const QStringList &command)
{
    m_processManager->sendCommand(command);
}

void ExternalMpvBackend::sendVariantCommand(const QVariantList &command)
{
    m_processManager->sendVariantCommand(command);
}

bool ExternalMpvBackend::supportsEmbeddedVideo() const
{
        return false;
}

bool ExternalMpvBackend::attachVideoTarget(QObject *target)
{
        Q_UNUSED(target);
        return false;
}

void ExternalMpvBackend::detachVideoTarget(QObject *target)
{
        Q_UNUSED(target);
}

void ExternalMpvBackend::setVideoViewport(const QRectF &viewport)
{
        Q_UNUSED(viewport);
}
