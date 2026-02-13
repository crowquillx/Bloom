#pragma once

#include <QObject>
#include <QProcess>
#include <QLocalSocket>
#include <QString>
#include <QJsonObject>

class PlayerProcessManager : public QObject
{
    Q_OBJECT
public:
    explicit PlayerProcessManager(QObject *parent = nullptr);
    ~PlayerProcessManager();

    void startMpv(const QString &mpvBin, const QStringList &args, const QString &mediaUrl);
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
    
    // Track change notifications (mpv track IDs are 1-indexed, we convert to 0-indexed for Jellyfin)
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
};
