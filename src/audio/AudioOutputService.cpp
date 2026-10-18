#include "AudioOutputService.h"
#include "AudioEndpointProbe.h"
#include "player/AudioPolicy.h"
#include "utils/ConfigManager.h"
#include <QDateTime>
#include <QMediaDevices>
#include <QThreadPool>
#include <QtConcurrentRun>
#include <utility>

namespace {
QThreadPool *nativeAudioThreadPool()
{
    // Native driver calls cannot be cancelled. Keep this serialized pool alive
    // for the process lifetime so QObject teardown and Qt shutdown never join it.
    static QThreadPool *pool = [] {
        auto *created = new QThreadPool;
        created->setMaxThreadCount(1);
        created->setExpiryTimeout(-1);
        return created;
    }();
    return pool;
}
}

namespace Bloom {
AudioOutputService::AudioOutputService(ConfigManager *config, QObject *parent)
    : AudioOutputService(config, probeAudioEndpoint, 1500, parent)
{
}

AudioOutputService::AudioOutputService(ConfigManager *config, Probe probe, int deadlineMs, QObject *parent)
    : QObject(parent), m_config(config), m_probeFunction(std::move(probe))
{
    m_deadline.setSingleShot(true);
    m_deadline.setInterval(deadlineMs);
    connect(&m_deadline, &QTimer::timeout, this, [this] {
        ++m_generation;
        AudioOutputCapabilities unknown;
        unknown.device = m_config ? m_config->getAudioOutputDevice() : QStringLiteral("auto");
        finish(unknown);
    });
    connect(&m_probe, &QFutureWatcher<AudioOutputCapabilities>::finished, this, [this] {
        m_probeActive = false;
        if (m_probeGeneration == m_generation) {
            m_deadline.stop();
            finish(m_probe.result());
        }
        if (std::exchange(m_pendingRefresh, false)) refresh();
    });
    auto *devices = new QMediaDevices(this);
    connect(devices, &QMediaDevices::audioOutputsChanged, this, [this] { invalidate(); refresh(); });
    if (config) {
        connect(config, &ConfigManager::audioOutputDeviceChanged, this, [this] { invalidate(); refresh(); });
        connect(config, &ConfigManager::audioPassthroughChanged, this, [this] {
            emit capabilitiesChanged();
        });
    }
    m_lastRefreshMs = QDateTime::currentMSecsSinceEpoch();
    m_refreshTimer.setInterval(5000);
    connect(&m_refreshTimer, &QTimer::timeout, this, [this] {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (m_lastRefreshMs && now - m_lastRefreshMs > 12000) {
            invalidate();
        }
        m_lastRefreshMs = now;
        refresh();
    });
    m_refreshTimer.start();
    QTimer::singleShot(0, this, &AudioOutputService::refresh);
}

void AudioOutputService::prepare(std::function<void()> completion)
{
    // A driver that hangs must not accumulate more workers or playback waiters.
    if (m_waiters.size() >= 32) {
        completion();
        return;
    }
    m_waiters.append(std::move(completion));
    refresh();
}

void AudioOutputService::refresh()
{
    const QString device = m_config ? m_config->getAudioOutputDevice() : QStringLiteral("auto");
    if (m_probeActive) {
        if (device != m_requestedDevice) {
            ++m_generation;
            m_pendingRefresh = true;
        }
        if (!m_deadline.isActive()) m_deadline.start();
        return;
    }
    m_probeActive = true;
    m_requestedDevice = device;
    m_probeGeneration = ++m_generation;
    m_deadline.start();
    const auto probe = m_probeFunction;
    m_probe.setFuture(QtConcurrent::run(nativeAudioThreadPool(), [device, probe] { return probe(device); }));
}

void AudioOutputService::invalidate()
{
    ++m_generation;
    if (m_probeActive) m_pendingRefresh = true;
    m_capabilities = {};
    m_capabilities.device = m_config ? m_config->getAudioOutputDevice() : QStringLiteral("auto");
    emit capabilitiesChanged();
}

void AudioOutputService::finish(const AudioOutputCapabilities &capabilities)
{
    const bool changed = m_capabilities.device != capabilities.device
        || m_capabilities.endpointId != capabilities.endpointId
        || m_capabilities.passthroughCodecs != capabilities.passthroughCodecs
        || m_capabilities.pcmLayouts != capabilities.pcmLayouts
        || m_capabilities.evidence != capabilities.evidence
        || m_capabilities.endpointAvailable != capabilities.endpointAvailable;
    m_capabilities = capabilities;
    if (changed) emit capabilitiesChanged();
    const auto waiters = std::exchange(m_waiters, {});
    for (const auto &completion : waiters) completion();
}

AudioOutputCapabilities AudioOutputService::capabilities() const
{
    auto result = m_capabilities;
    if (m_config && result.device != m_config->getAudioOutputDevice()) {
        result = {};
        result.device = m_config->getAudioOutputDevice();
    }
    if (result.device != QStringLiteral("auto") && m_mpvDevicesKnown
        && !m_mpvDevices.contains(result.device)) result.endpointAvailable = false;
    return result;
}

void AudioOutputService::setMpvDevices(const QVariantList &devices)
{
    const auto previous = capabilities();
    m_mpvDevices.clear();
    for (const auto &device : devices) m_mpvDevices.append(device.toMap().value(QStringLiteral("name")).toString());
    m_mpvDevicesKnown = true;
    if (previous.endpointAvailable != capabilities().endpointAvailable) emit capabilitiesChanged();
    refresh();
}

AudioOutputPlan AudioOutputService::planForSource(const QVariantMap &source, int track) const
{
    return planForMetadata(AudioPolicy::selectedSource(source, track));
}

AudioOutputPlan AudioOutputService::planForMetadata(const AudioSourceMetadata &source) const
{
    AudioPreferences preferences;
    if (m_config) {
        const QString mode = m_config->getAudioPassthroughMode();
        preferences.mode = mode == "disabled" ? AudioPassthroughMode::Disabled
                         : mode == "custom" ? AudioPassthroughMode::Custom : AudioPassthroughMode::Auto;
        preferences.customCodecs = m_config->getAudioPassthroughCodecs();
    }
    return AudioPolicy::plan(source, capabilities(), preferences);
}
}
