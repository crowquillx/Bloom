#pragma once

#include "models/AudioTypes.h"
#include <QObject>
#include <QFutureWatcher>
#include <QTimer>
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
    AudioOutputPlan planForMetadata(const AudioSourceMetadata &source) const;
    AudioOutputCapabilities capabilities() const;
    void setMpvDevices(const QVariantList &devices);
signals:
    void capabilitiesChanged();
private:
    void invalidate();
    void finish(const AudioOutputCapabilities &capabilities);
    ConfigManager *m_config;
    Probe m_probeFunction;
    QStringList m_mpvDevices;
    bool m_mpvDevicesKnown = false;
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
