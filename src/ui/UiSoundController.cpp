#include "UiSoundController.h"

#include <QMediaPlayer>
#include <QAudioOutput>
#include "../utils/ConfigManager.h"

UiSoundController::UiSoundController(ConfigManager *config, QObject *parent)
    : QObject(parent)
    , m_config(config)
    , m_player(std::make_unique<QMediaPlayer>())
    , m_output(std::make_unique<QAudioOutput>())
{
    m_player->setAudioOutput(m_output.get());
    m_player->setSource(QUrl(QStringLiteral("qrc:/sounds/ui.opus")));
    m_player->setLoops(1);
    m_timer.start();

    updateEnabled();
    updateVolume();

    if (m_config) {
        connect(m_config, &ConfigManager::uiSoundsEnabledChanged,
                this, &UiSoundController::updateEnabled);
        connect(m_config, &ConfigManager::uiSoundsVolumeChanged,
                this, &UiSoundController::updateVolume);
    }
}

void UiSoundController::playNavigation()
{
    play();
}

void UiSoundController::playSelect()
{
    play();
}

void UiSoundController::playBack()
{
    play();
}

void UiSoundController::updateEnabled()
{
    m_enabled = m_config ? m_config->getUiSoundsEnabled() : true;
}

void UiSoundController::updateVolume()
{
    m_volumeLevel = m_config ? m_config->getUiSoundsVolume() : 3;
    if (m_output) {
        m_output->setVolume(volumeForLevel(m_volumeLevel));
    }
    if (m_volumeLevel <= 0 && m_player && m_player->playbackState() == QMediaPlayer::PlayingState) {
        m_player->stop();
    }
}

void UiSoundController::play()
{
    if (!m_enabled || m_volumeLevel <= 0) {
        return;
    }

    if (m_timer.isValid() && m_timer.elapsed() < m_cooldownMs) {
        return;
    }
    m_timer.restart();

    if (m_player && m_player->playbackState() == QMediaPlayer::PlayingState) {
        m_player->stop();
    }
    if (m_output) {
        m_output->setVolume(volumeForLevel(m_volumeLevel));
    }
    if (m_player) {
        m_player->play();
    }
}

float UiSoundController::volumeForLevel(int level) const
{
    switch (level) {
    case 1: return 0.20f;
    case 2: return 0.35f;
    case 3: return 0.55f;
    case 4: return 0.75f;
    default: return 0.0f;
    }
}





