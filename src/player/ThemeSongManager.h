#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <memory>

#include <QtMultimedia/QMediaPlayer>
#include <QtMultimedia/QAudioOutput>
#include <QPropertyAnimation>

class LibraryService;
class ConfigManager;
class PlayerController;

/**
 * @brief Handles playback of series theme songs with volume control and fade-out.
 * 
 * Plays theme songs fetched from Jellyfin when entering a series context and
 * stops (with optional fade) when leaving or when playback starts.
 */
class ThemeSongManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool isPlaying READ isPlaying NOTIFY isPlayingChanged)
    Q_PROPERTY(QString currentSeriesId READ currentSeriesId NOTIFY currentSeriesIdChanged)
    Q_PROPERTY(bool loopEnabled READ loopEnabled NOTIFY loopEnabledChanged)

public:
    explicit ThemeSongManager(LibraryService *libraryService,
                              ConfigManager *config,
                              PlayerController *playerController,
                              QObject *parent = nullptr);

    Q_INVOKABLE void play(const QString &seriesId);
    Q_INVOKABLE void stop();
    Q_INVOKABLE void fadeOutAndStop();
    Q_INVOKABLE void setLoopEnabled(bool enabled);

    bool isPlaying() const { return m_isPlaying; }
    QString currentSeriesId() const { return m_currentSeriesId; }
    bool loopEnabled() const { return m_loopEnabled; }

signals:
    void isPlayingChanged();
    void currentSeriesIdChanged();
    void loopEnabledChanged();

private slots:
    void handleThemeSongsLoaded(const QString &seriesId, const QStringList &urls);
    void handlePlaybackActiveChanged();
    void handleConfigVolumeChanged();
    void handleConfigLoopChanged();
    void handleFadeFinished();

private:
    void startPlayback(const QStringList &urls, const QString &seriesId);
    void applyVolumeFromConfig();
    void applyLoopFromConfig();
    double volumeForLevel(int level) const;
    void resetState(bool clearSeriesId);

    LibraryService *m_libraryService;
    ConfigManager *m_config;
    PlayerController *m_playerController;

    QString m_pendingSeriesId;
    QString m_currentSeriesId;
    bool m_isPlaying = false;
    bool m_loopEnabled = false;
    int m_volumeLevel = 0;
    bool m_isFading = false;

    std::unique_ptr<QMediaPlayer> m_mediaPlayer;
    std::unique_ptr<QAudioOutput> m_audioOutput;
    std::unique_ptr<QPropertyAnimation> m_fadeAnimation;
};








