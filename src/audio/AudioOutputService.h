#pragma once

#include "models/AudioTypes.h"
#include <QObject>
#include <QFutureWatcher>
#include <QTimer>
#include <QVariantList>
#include <functional>

class ConfigManager;

namespace Bloom {
class AudioOutputService : public QObject
{
    Q_OBJECT
public:
    using Probe = std::function<AudioOutputCapabilities(const QString &)>;
    explicit AudioOutputService(ConfigManager *config, QObject *parent = nullptr);
    AudioOutputService(ConfigManager *config, Probe probe, int deadlineMs, QObject *parent = nullptr);
    void prepare(std::function<void()> completion);
    void refresh();
    AudioOutputPlan planForSource(const QVariantMap &source, int track) const;
    // `allowExclusive=false` plans shared-mode output after an exclusive failure.
    AudioOutputPlan planForMetadata(const AudioSourceMetadata &source, bool allowExclusive = true) const;
    AudioOutputCapabilities capabilities() const;
    // Saved preference matched against mpv's device list for this session.
    AudioDeviceResolution deviceResolution() const;
    QString activeDriver() const;
    QVariantList mpvDevices() const { return m_mpvDeviceList; }
    QVariantList outputOptions() const;
    void setMpvDevices(const QVariantList &devices);
    // Driver mpv reports through current-ao; "null" and empty values are ignored.
    void setRuntimeDriver(const QString &driver);
    // Treat `device` as unusable until the device list or the saved choice changes.
    void markDeviceFailed(const QString &device);
    void clearDeviceFailures();
    // Playback holds the endpoint exclusively (exclusive PCM or an encoded carrier).
    void setEndpointHeld(bool held) { m_endpointHeld = held; }
signals:
    void capabilitiesChanged();
    // Device list, resolution or driver changed (selector contents).
    void devicesChanged();
    // QtMultimedia reported an OS output change.
    void systemOutputsChanged();
private:
    void invalidate();
    void finish(const AudioOutputCapabilities &capabilities);
    ConfigManager *m_config;
    Probe m_probeFunction;
    QVariantList m_mpvDeviceList;
    bool m_mpvDevicesKnown = false;
    QString m_runtimeDriver;
    QStringList m_failedDevices;
    bool m_endpointHeld = false;
    AudioOutputCapabilities m_capabilities;
    QFutureWatcher<AudioOutputCapabilities> m_probe;
    QTimer m_deadline;
    QTimer m_refreshTimer;
    QList<std::function<void()>> m_waiters;
    QString m_requestedDevice;
    quint64 m_generation = 0;
    quint64 m_probeGeneration = 0;
    bool m_probeActive = false;
    bool m_pendingRefresh = false;
    qint64 m_lastRefreshMs = 0;
};
}
