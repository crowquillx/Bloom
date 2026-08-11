#pragma once

#include <QElapsedTimer>
#include <QJsonObject>
#include <QLocalSocket>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <optional>

struct PlayerProcessOptions {
    int gracefulQuitTimeoutMs = 750;
    int terminateTimeoutMs = 750;
    int ipcRetryIntervalMs = 100;
    int ipcConnectionDeadlineMs = 5000;
    int maximumIpcAttempts = 50;
    int maximumPendingCommands = 256;
    qint64 maximumIpcMessageBytes = 1024 * 1024;
    qint64 maximumBufferedWriteBytes = 256 * 1024;
};

class PlayerProcessManager : public QObject
{
    Q_OBJECT
public:
    explicit PlayerProcessManager(QObject *parent = nullptr);
    explicit PlayerProcessManager(const PlayerProcessOptions &options,
                                  QObject *parent = nullptr);
    ~PlayerProcessManager() override;

    void startMpv(const QString &mpvBin,
                  const QStringList &args,
                  const QString &mediaUrl);
    void appendUrlsToPlaylist(const QStringList &mediaUrls);
    void stopMpv();
    bool isRunning() const;

    void sendCommand(const QStringList &command);
    void sendVariantCommand(const QVariantList &command);

    QString ipcPath() const;
    QVariantMap diagnostics() const;

signals:
    void stateChanged(bool running);
    void processFinished(int exitCode, bool crashed);
    void errorOccurred(const QString &error);
    void startupLatencyMeasured(qint64 milliseconds);
    void ipcConnectionLatencyMeasured(qint64 milliseconds);
    void positionChanged(double seconds);
    void durationChanged(double seconds);
    void pauseChanged(bool paused);
    void pausedForCacheChanged(bool paused);
    void playbackEnded();
    void cacheEndChanged(double seconds);
    void volumeChanged(int volume);
    void muteChanged(bool muted);
    void playlistPositionChanged(int index);

    // Track change notifications use raw mpv track IDs (1-based per track type,
    // -1 for none/off).
    void audioTrackChanged(int trackIndex);
    void subtitleTrackChanged(int trackIndex);

    // Each entry is a QVariantMap with name and description fields.
    void audioDeviceListChanged(const QVariantList &devices);

    // Script message from mpv scripts/extensions (via client-message event).
    void scriptMessage(const QString &messageName, const QStringList &args);

private slots:
    void onProcessStarted();
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onProcessError(QProcess::ProcessError error);
    void onSocketConnected();
    void onSocketDisconnected();
    void onSocketReadyRead();

private:
    struct LaunchRequest {
        QString executable;
        QStringList arguments;
        QString mediaUrl;
    };

    enum class StopStage {
        None,
        GracefulQuit,
        Terminating,
        Killing,
    };

    QString createIpcPath() const;
    QString ipcServerName() const;
    void removeIpcEndpoint();

    void launchPending();
    void beginStop();
    void escalateToTerminate();
    void escalateToKill();
    void finishProcessLifecycle(int exitCode, QProcess::ExitStatus exitStatus);

    void beginIpcConnectionWindow();
    void attemptIpcConnection();
    void retryIpcConnection();
    void failIpcConnection(const QString &reason);
    void resetIpcConnection();

    bool serializeCommand(const QVariantList &command,
                          QByteArray *payload,
                          QString *commandName) const;
    bool writeCommand(const QVariantList &command);
    void enqueueCommand(const QVariantList &command);
    void flushPendingCommands();
    void handleIpcObject(const QJsonObject &object);
    void recordInvalidIpcMessage(const QString &reason);

    PlayerProcessOptions m_options;
    QProcess *m_process = nullptr;
    QLocalSocket *m_ipcSocket = nullptr;
    QTimer m_gracefulQuitTimer;
    QTimer m_terminateTimer;
    QTimer m_ipcRetryTimer;
    QTimer m_ipcDeadlineTimer;
    QElapsedTimer m_startupElapsed;
    QElapsedTimer m_ipcElapsed;
    std::optional<LaunchRequest> m_pendingLaunch;
    QString m_ipcPath;
    QString m_instanceToken;
    quint64 m_launchSequence = 0;
    StopStage m_stopStage = StopStage::None;
    bool m_launchActive = false;
    bool m_runningReported = false;
    bool m_isConnected = false;
    bool m_ipcFailureReported = false;
    int m_ipcAttempts = 0;
    QList<QVariantList> m_pendingCommands;
    QByteArray m_ipcReadBuffer;
    quint64 m_droppedPendingCommands = 0;
    quint64 m_invalidIpcMessages = 0;
    quint64 m_terminateEscalations = 0;
    quint64 m_killEscalations = 0;
    qint64 m_lastStartupLatencyMs = -1;
    qint64 m_lastIpcLatencyMs = -1;
    int m_playlistPosition = -1;
    int m_playlistCount = 0;
};
