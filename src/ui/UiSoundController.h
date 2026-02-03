#pragma once

#include <QObject>
#include <QElapsedTimer>
#include <memory>

class QMediaPlayer;
class QAudioOutput;

class ConfigManager;

/**
 * @brief Plays short UI feedback sounds with config-based enable and volume.
 *
 * Implemented in C++ to avoid relying on the QtMultimedia QML plugin being
 * present at runtime. Backed by QSoundEffect and wired to ConfigManager.
 */
class UiSoundController : public QObject
{
    Q_OBJECT

public:
    explicit UiSoundController(ConfigManager *config, QObject *parent = nullptr);

    Q_INVOKABLE void playNavigation();
    Q_INVOKABLE void playSelect();
    Q_INVOKABLE void playBack();

private:
    void updateEnabled();
    void updateVolume();
    void play();
    float volumeForLevel(int level) const;

    ConfigManager *m_config = nullptr;
    std::unique_ptr<QMediaPlayer> m_player;
    std::unique_ptr<QAudioOutput> m_output;
    bool m_enabled = true;
    int m_volumeLevel = 3;
    QElapsedTimer m_timer;
    int m_cooldownMs = 35;
};





