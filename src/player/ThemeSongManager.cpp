#include "ThemeSongManager.h"

#include <QtMultimedia/QAudioOutput>
#include <QtMultimedia/QMediaPlayer>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <QUrl>
#include <QAbstractAnimation>
#include <algorithm>

#include "../network/LibraryService.h"
#include "../utils/ConfigManager.h"
#include "PlayerController.h"

ThemeSongManager::ThemeSongManager(LibraryService *libraryService,
                                   ConfigManager *config,
                                   PlayerController *playerController,
                                   QObject *parent)
    : QObject(parent)
    , m_libraryService(libraryService)
    , m_config(config)
    , m_playerController(playerController)
    , m_mediaPlayer(std::make_unique<QMediaPlayer>())
    , m_audioOutput(std::make_unique<QAudioOutput>())
    , m_fadeAnimation(std::make_unique<QPropertyAnimation>())
{
    m_mediaPlayer->setAudioOutput(m_audioOutput.get());
    applyLoopFromConfig();
    applyVolumeFromConfig();
    
    m_fadeAnimation->setTargetObject(m_audioOutput.get());
    m_fadeAnimation->setPropertyName("volume");
    m_fadeAnimation->setDuration(450);
    m_fadeAnimation->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_fadeAnimation.get(), &QPropertyAnimation::finished, this, &ThemeSongManager::handleFadeFinished);
    
    connect(m_mediaPlayer.get(), &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState state) {
        bool playing = state == QMediaPlayer::PlayingState;
        if (m_isPlaying != playing) {
            m_isPlaying = playing;
            emit isPlayingChanged();
        }
        if (state == QMediaPlayer::StoppedState && !m_isFading) {
            resetState(false);
        }
    });
    
    if (m_libraryService) {
        connect(m_libraryService, &LibraryService::themeSongsLoaded, this, &ThemeSongManager::handleThemeSongsLoaded);
    }
    if (m_config) {
        connect(m_config, &ConfigManager::themeSongVolumeChanged, this, &ThemeSongManager::handleConfigVolumeChanged);
        connect(m_config, &ConfigManager::themeSongLoopChanged, this, &ThemeSongManager::handleConfigLoopChanged);
    }
    if (m_playerController) {
        connect(m_playerController, &PlayerController::isPlaybackActiveChanged, this, &ThemeSongManager::handlePlaybackActiveChanged);
    }
}

void ThemeSongManager::play(const QString &seriesId)
{
    if (!m_libraryService || !m_config) return;
    if (seriesId.isEmpty()) {
        fadeOutAndStop();
        return;
    }
    
    applyLoopFromConfig();
    applyVolumeFromConfig();
    
    if (m_config->getThemeSongVolume() <= 0) {
        fadeOutAndStop();
        return;
    }
    
    // If already playing for this series, just ensure settings are applied
    if (m_isPlaying && seriesId == m_currentSeriesId && !m_isFading) {
        return;
    }
    
    m_pendingSeriesId = seriesId;
    m_libraryService->getThemeSongs(seriesId);
}

void ThemeSongManager::stop()
{
    if (m_fadeAnimation->state() == QAbstractAnimation::Running) {
        m_fadeAnimation->stop();
    }
    m_isFading = false;
    resetState(true);
}

void ThemeSongManager::fadeOutAndStop()
{
    if (!m_mediaPlayer) return;
    
    if (!m_isPlaying || m_mediaPlayer->playbackState() == QMediaPlayer::StoppedState) {
        stop();
        return;
    }
    
    if (m_fadeAnimation->state() == QAbstractAnimation::Running) {
        m_fadeAnimation->stop();
    }
    
    m_isFading = true;
    m_fadeAnimation->setStartValue(m_audioOutput->volume());
    m_fadeAnimation->setEndValue(0.0);
    m_fadeAnimation->start();
}

void ThemeSongManager::setLoopEnabled(bool enabled)
{
    if (m_loopEnabled == enabled) return;
    m_loopEnabled = enabled;
    if (m_mediaPlayer) {
        m_mediaPlayer->setLoops(m_loopEnabled ? QMediaPlayer::Infinite : 1);
    }
    emit loopEnabledChanged();
}

void ThemeSongManager::handleThemeSongsLoaded(const QString &seriesId, const QStringList &urls)
{
    if (seriesId != m_pendingSeriesId) {
        return;
    }
    m_pendingSeriesId.clear();
    
    if (m_config && m_config->getThemeSongVolume() <= 0) {
        fadeOutAndStop();
        return;
    }
    
    startPlayback(urls, seriesId);
}

void ThemeSongManager::handlePlaybackActiveChanged()
{
    if (m_playerController && m_playerController->isPlaybackActive()) {
        stop(); // Stop immediately when video playback starts
    }
}

void ThemeSongManager::handleConfigVolumeChanged()
{
    applyVolumeFromConfig();
}

void ThemeSongManager::handleConfigLoopChanged()
{
    applyLoopFromConfig();
}

void ThemeSongManager::handleFadeFinished()
{
    if (!m_isFading) return;
    m_isFading = false;
    if (m_mediaPlayer) {
        m_mediaPlayer->stop();
    }
    resetState(true);
    // Restore volume for the next start
    if (m_audioOutput) {
        m_audioOutput->setVolume(volumeForLevel(m_volumeLevel));
    }
}

void ThemeSongManager::startPlayback(const QStringList &urls, const QString &seriesId)
{
    if (!m_mediaPlayer || urls.isEmpty()) {
        resetState(true);
        return;
    }
    
    if (m_fadeAnimation->state() == QAbstractAnimation::Running) {
        m_fadeAnimation->stop();
    }
    m_isFading = false;
    
    double vol = volumeForLevel(m_volumeLevel);
    m_audioOutput->setVolume(vol);
    m_mediaPlayer->setLoops(m_loopEnabled ? QMediaPlayer::Infinite : 1);
    m_mediaPlayer->setSource(QUrl::fromUserInput(urls.first()));
    m_mediaPlayer->play();
    
    if (m_currentSeriesId != seriesId) {
        m_currentSeriesId = seriesId;
        emit currentSeriesIdChanged();
    }
    
    if (!m_isPlaying) {
        m_isPlaying = true;
        emit isPlayingChanged();
    }
}

void ThemeSongManager::applyVolumeFromConfig()
{
    int level = m_config ? m_config->getThemeSongVolume() : 0;
    m_volumeLevel = std::clamp(level, 0, 4);
    
    if (m_fadeAnimation->state() == QAbstractAnimation::Running) {
        m_fadeAnimation->stop();
        m_isFading = false;
    }
    
    double vol = volumeForLevel(m_volumeLevel);
    if (m_audioOutput) {
        m_audioOutput->setVolume(vol);
    }
    
    if (m_volumeLevel == 0 && m_isPlaying) {
        fadeOutAndStop();
    }
}

void ThemeSongManager::applyLoopFromConfig()
{
    bool loop = m_config ? m_config->getThemeSongLoop() : false;
    if (loop == m_loopEnabled) {
        return;
    }
    m_loopEnabled = loop;
    if (m_mediaPlayer) {
        m_mediaPlayer->setLoops(m_loopEnabled ? QMediaPlayer::Infinite : 1);
    }
    emit loopEnabledChanged();
}

double ThemeSongManager::volumeForLevel(int level) const
{
    switch (level) {
    case 1: return 0.15;
    case 2: return 0.35;
    case 3: return 0.55;
    case 4: return 0.75;
    default: return 0.0;
    }
}

void ThemeSongManager::resetState(bool clearSeriesId)
{
    if (m_mediaPlayer) {
        m_mediaPlayer->stop();
    }
    bool wasPlaying = m_isPlaying;
    m_isPlaying = false;
    m_pendingSeriesId.clear();
    if (clearSeriesId && !m_currentSeriesId.isEmpty()) {
        m_currentSeriesId.clear();
        emit currentSeriesIdChanged();
    }
    if (wasPlaying) {
        emit isPlayingChanged();
    }
}








