#include "AudioOutputService.h"
#include "AudioEndpointProbe.h"
#include "player/AudioPolicy.h"
#include "utils/ConfigManager.h"
#include <QDateTime>
#include <QMediaDevices>
#include <QThreadPool>
#include <QtConcurrentRun>
#include <algorithm>
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

QString platformDefaultDriver()
{
#if defined(Q_OS_WIN)
    // mpv autoprobes WASAPI first on Windows.
    return QStringLiteral("wasapi");
#else
    return {};
#endif
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
        unknown.device = deviceResolution().device;
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
    connect(devices, &QMediaDevices::audioOutputsChanged, this, [this] {
        invalidate();
        refresh();
        emit systemOutputsChanged();
    });
    if (config) {
        connect(config, &ConfigManager::audioOutputDeviceChanged, this, [this] {
            // A new choice is a new attempt; earlier open failures no longer apply.
            m_failedDevices.clear();
            invalidate();
            refresh();
            emit devicesChanged();
        });
        connect(config, &ConfigManager::audioPassthroughChanged, this, [this] {
            emit capabilitiesChanged();
        });
        connect(config, &ConfigManager::audioExclusiveModeChanged, this, [this] {
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
    const QString device = deviceResolution().device;
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
    m_capabilities.device = deviceResolution().device;
    emit capabilitiesChanged();
}

void AudioOutputService::finish(const AudioOutputCapabilities &result)
{
    AudioOutputCapabilities capabilities = result;
    // A driver may reject every format query on an endpoint this process holds
    // exclusively. An all-negative answer for that endpoint is not evidence that
    // its formats changed, and acting on it would reopen audio in a loop.
    if (m_endpointHeld && capabilities.endpointAvailable
        && capabilities.endpointId == m_capabilities.endpointId
        && capabilities.evidence == m_capabilities.evidence
        && capabilities.passthroughCodecs.isEmpty() && capabilities.pcmLayouts.isEmpty()) {
        capabilities.passthroughCodecs = m_capabilities.passthroughCodecs;
        capabilities.pcmLayouts = m_capabilities.pcmLayouts;
    }
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

AudioDeviceResolution AudioOutputService::deviceResolution() const
{
    if (!m_config) return {};
    return AudioPolicy::resolveDevice(m_config->getAudioOutputDevice(),
                                      m_config->getAudioOutputDeviceDescription(),
                                      m_mpvDeviceList, m_mpvDevicesKnown, m_failedDevices, m_runtimeDriver);
}

QString AudioOutputService::activeDriver() const
{
    const QString driver = AudioPolicy::activeDriver(m_mpvDeviceList, m_runtimeDriver);
    return driver.isEmpty() ? platformDefaultDriver() : driver;
}

QVariantList AudioOutputService::outputOptions() const
{
    return AudioPolicy::outputDeviceOptions(m_mpvDeviceList, activeDriver(), deviceResolution(),
                                            m_config ? m_config->getAudioOutputDeviceDescription()
                                                     : QString());
}

AudioOutputCapabilities AudioOutputService::capabilities() const
{
    const QString device = deviceResolution().device;
    auto result = m_capabilities;
    if (result.device != device) {
        result = {};
        result.device = device;
    }
    if (result.device != QStringLiteral("auto") && m_mpvDevicesKnown) {
        const bool listed = std::any_of(m_mpvDeviceList.cbegin(), m_mpvDeviceList.cend(),
                                        [&device](const QVariant &value) {
                                            return value.toMap().value(QStringLiteral("name")).toString() == device;
                                        });
        if (!listed) result.endpointAvailable = false;
    }
    const QString driver = AudioPolicy::deviceDriver(device);
    result.exclusiveModeSupported = AudioPolicy::driverSupportsExclusive(
        driver.isEmpty() ? activeDriver() : driver);
    return result;
}

void AudioOutputService::setMpvDevices(const QVariantList &devices)
{
    const auto previous = capabilities();
    const bool listChanged = !m_mpvDevicesKnown || m_mpvDeviceList != devices;
    m_mpvDeviceList = devices;
    m_mpvDevicesKnown = true;
    // Hotplug is the signal to retry endpoints that failed to open earlier.
    if (listChanged) m_failedDevices.clear();
    const auto current = capabilities();
    if (previous.endpointAvailable != current.endpointAvailable || previous.device != current.device
        || previous.exclusiveModeSupported != current.exclusiveModeSupported)
        emit capabilitiesChanged();
    if (listChanged) emit devicesChanged();
    refresh();
}

void AudioOutputService::setRuntimeDriver(const QString &driver)
{
    if (driver.isEmpty() || driver == QStringLiteral("null") || driver == m_runtimeDriver) return;
    m_runtimeDriver = driver;
    emit devicesChanged();
}

void AudioOutputService::markDeviceFailed(const QString &device)
{
    if (device.isEmpty() || device == QStringLiteral("auto") || m_failedDevices.contains(device)) return;
    m_failedDevices.append(device);
    invalidate();
    refresh();
    emit devicesChanged();
}

void AudioOutputService::clearDeviceFailures()
{
    if (m_failedDevices.isEmpty()) return;
    m_failedDevices.clear();
    invalidate();
    refresh();
    emit devicesChanged();
}

AudioOutputPlan AudioOutputService::planForSource(const QVariantMap &source, int track) const
{
    return planForMetadata(AudioPolicy::selectedSource(source, track));
}

AudioOutputPlan AudioOutputService::planForMetadata(const AudioSourceMetadata &source, bool allowExclusive) const
{
    AudioPreferences preferences;
    if (m_config) {
        const QString mode = m_config->getAudioPassthroughMode();
        preferences.mode = mode == "disabled" ? AudioPassthroughMode::Disabled
                         : mode == "custom" ? AudioPassthroughMode::Custom : AudioPassthroughMode::Auto;
        preferences.customCodecs = m_config->getAudioPassthroughCodecs();
        preferences.exclusive = allowExclusive && m_config->getAudioExclusiveMode();
    }
    return AudioPolicy::plan(source, capabilities(), preferences);
}
}
