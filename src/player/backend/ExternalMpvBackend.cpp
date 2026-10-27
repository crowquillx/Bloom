#include "ExternalMpvBackend.h"
#include "player/AudioPolicy.h"

#include <QTimer>

#include "utils/BloomLogging.h"

#include <algorithm>
#include <memory>

namespace PlayerPolicy = Bloom::PlaybackPolicy;

namespace {
// First snapshot once the first frames have been rendered (vo-passes is
// populated per rendered frame); second one for steady-state drop counters.
constexpr int kStartupSnapshotDelayMs = 1500;
constexpr int kSteadySnapshotDelayMs = 8000;
// `--audio-device=help` lists every driver's endpoints and exits; bound both.
constexpr int kDeviceListDeadlineMs = 4000;
constexpr qsizetype kDeviceListOutputLimit = 256 * 1024;
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
    connect(m_processManager, &PlayerProcessManager::fileLoaded,
            this, &ExternalMpvBackend::fileLoaded);
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

ExternalMpvBackend::~ExternalMpvBackend()
{
    finishDeviceListProbe(m_deviceListProcess, {});
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

void ExternalMpvBackend::refreshAudioDevices()
{
    if (isRunning() || m_deviceListProcess || m_mpvBin.isEmpty()) {
        return;
    }
    auto *process = new QProcess(this);
    m_deviceListProcess = process;
    process->setProcessChannelMode(QProcess::MergedChannels);
    auto output = std::make_shared<QByteArray>();
    const auto readBounded = [process, output]() {
        const QByteArray chunk = process->readAll();
        output->append(chunk.left(std::max<qsizetype>(0, kDeviceListOutputLimit - output->size())));
    };
    connect(process, &QProcess::readyReadStandardOutput, this, readBounded);
    connect(process, &QProcess::finished, this,
            [this, process, output, readBounded](int exitCode, QProcess::ExitStatus status) {
                readBounded();
                const bool ok = status == QProcess::NormalExit && exitCode == 0;
                finishDeviceListProbe(process, ok ? Bloom::AudioPolicy::parseDeviceHelp(
                                                        QString::fromUtf8(*output))
                                                  : QVariantList{});
            });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        // Crashes are followed by finished(); only a failed start ends here.
        if (error == QProcess::FailedToStart) {
            finishDeviceListProbe(process, {});
        }
    });
    // Owned by the process, so a stale deadline can never match a newer probe.
    auto *deadline = new QTimer(process);
    deadline->setSingleShot(true);
    connect(deadline, &QTimer::timeout, this, [this, process]() {
        qCWarning(lcPlayback) << "mpv audio device enumeration exceeded" << kDeviceListDeadlineMs << "ms";
        finishDeviceListProbe(process, {});
    });
    deadline->start(kDeviceListDeadlineMs);
    // --no-config keeps user scripts and profiles out of a list-only query.
    process->start(m_mpvBin, {QStringLiteral("--no-config"), QStringLiteral("--audio-device=help")});
}

void ExternalMpvBackend::finishDeviceListProbe(QProcess *process, const QVariantList &devices)
{
    if (!process || process != m_deviceListProcess) {
        return;
    }
    m_deviceListProcess = nullptr;
    process->disconnect(this);
    if (process->state() != QProcess::NotRunning) {
        // Reap without blocking the UI thread on QProcess destruction.
        process->setParent(nullptr);
        connect(process, &QProcess::finished, process, &QObject::deleteLater);
        process->kill();
    } else {
        process->deleteLater();
    }
    // A session that started meanwhile publishes its own live list.
    if (!devices.isEmpty() && !isRunning()) {
        emit audioDeviceListChanged(devices);
    }
}

void ExternalMpvBackend::prepareRuntime(const QString &mpvBin)
{
    m_mpvBin = mpvBin.trimmed();
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
