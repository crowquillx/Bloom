#include "AudioOutputRouter.h"

#include <QAudioDevice>
#include <QAudioOutput>
#include <QMediaDevices>

#include "ConfigManager.h"

namespace {
// Normalize an identifier for loose comparison: lowercase and drop the braces
// that appear in WASAPI-style ids/names so "wasapi/{GUID}" can be matched
// against a Qt audio device id that contains the same GUID.
QString normalizeDeviceKey(const QString &value)
{
    QString normalized = value.toLower();
    normalized.remove('{');
    normalized.remove('}');
    return normalized;
}
} // namespace

AudioOutputRouter::AudioOutputRouter(QAudioOutput *output, ConfigManager *config, QObject *parent)
    : QObject(parent)
    , m_output(output)
    , m_config(config)
    , m_mediaDevices(new QMediaDevices(this))
{
    // React to devices appearing/disappearing (e.g. Bluetooth headset hotplug)
    // and to the system default device changing underneath us.
    connect(m_mediaDevices, &QMediaDevices::audioOutputsChanged,
            this, &AudioOutputRouter::updateDevice);

    if (m_config) {
        connect(m_config, &ConfigManager::audioOutputDeviceChanged,
                this, &AudioOutputRouter::updateDevice);
    }

    updateDevice();
}

void AudioOutputRouter::updateDevice()
{
    if (!m_output) {
        return;
    }

    const QString desired = m_config ? m_config->getAudioOutputDevice() : QStringLiteral("auto");

    QAudioDevice target = QMediaDevices::defaultAudioOutput();

    if (!desired.isEmpty() && desired != QStringLiteral("auto")) {
        // Bloom stores the mpv audio-device id (e.g. "wasapi/{GUID}",
        // "pulse/<name>"). Try to resolve it to a Qt audio device by matching the
        // portion after the output-driver prefix; fall back to the system default.
        const QString key = normalizeDeviceKey(desired.section('/', 1));
        if (!key.isEmpty()) {
            const QList<QAudioDevice> outputs = QMediaDevices::audioOutputs();
            for (const QAudioDevice &device : outputs) {
                const QString id = normalizeDeviceKey(QString::fromUtf8(device.id()));
                const QString description = normalizeDeviceKey(device.description());
                if ((!id.isEmpty() && (id.contains(key) || key.contains(id)))
                    || description == key) {
                    target = device;
                    break;
                }
            }
        }
    }

    if (target.isNull()) {
        return;
    }

    if (m_output->device() != target) {
        m_output->setDevice(target);
    }
}
