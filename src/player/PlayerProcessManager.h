#pragma once

#include <QObject>
#include <QJsonObject>
#include <QLocalSocket>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QVariantList>

class PlayerProcessManager : public QObject
{
    Q_OBJECT
public:
    explicit PlayerProcessManager(QObject *parent = nullptr);
    ~PlayerProcessManager();

    void startMpv(const QString &mpvBin, const QStringList &args, const QString &mediaUrl);
    void appendUrlsToPlaylist(const QStringList &mediaUrls);
    void stopMpv();
    bool isRunning() const;

    void sendCommand(const QStringList &command);
    void sendVariantCommand(const QVariantList &command);

signals:
    void stateChanged(bool running);
    void errorOccurred(const QString &error);
    void positionChanged(double seconds);
    void durationChanged(double seconds);
    void pauseChanged(bool paused);
    void pausedForCacheChanged(bool paused);
    void playbackEnded();
    void volumeChanged(int volume);
    void muteChanged(bool muted);
    void playlistPositionChanged(int index);
    
    // Track change notifications use raw mpv track IDs (1-based per track type, -1 for none/off).
    void audioTrackChanged(int trackIndex);
    void subtitleTrackChanged(int trackIndex);
    
    // Script message from mpv scripts/extensions (via client-message event)
    void scriptMessage(const QString &messageName, const QStringList &args);

private slots:
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onProcessError(QProcess::ProcessError error);
    void onSocketConnected();
    void onSocketReadyRead();

private:
    QString getIpcPath() const;
    void connectIpc();
    void flushPendingCommands();

    QProcess *m_process = nullptr;
    QLocalSocket *m_ipcSocket = nullptr;
    QString m_ipcPath;
    bool m_isConnected = false;
    QList<QVariantList> m_pendingCommands;
    int m_playlistPosition = -1;
    int m_playlistCount = 0;
};
