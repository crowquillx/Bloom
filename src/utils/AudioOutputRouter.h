#pragma once

#include <QObject>

class QAudioOutput;
class QMediaDevices;
class ConfigManager;

/**
 * @brief Keeps a QtMultimedia QAudioOutput pointed at the desired audio device.
 *
 * QtMultimedia audio (interface sounds, series theme songs) does not follow the
 * system default output device or honor Bloom's audio output device preference
 * on its own, and a QAudioOutput bound at construction time will not move when a
 * device is hotplugged (e.g. a Bluetooth headset connecting after launch).
 *
 * AudioOutputRouter watches both QMediaDevices (for hotplug) and
 * ConfigManager::audioOutputDevice (for the user's preference), and reassigns the
 * QAudioOutput's device whenever either changes. When the preference is "auto"
 * (the default) it follows the current system default device; otherwise it makes
 * a best-effort match of the saved mpv audio-device id to a Qt audio device and
 * falls back to the system default when no match exists.
 */
class AudioOutputRouter : public QObject
{
    Q_OBJECT

public:
    AudioOutputRouter(QAudioOutput *output, ConfigManager *config, QObject *parent = nullptr);

private:
    void updateDevice();

    QAudioOutput *m_output = nullptr;
    ConfigManager *m_config = nullptr;
    QMediaDevices *m_mediaDevices = nullptr;
};
