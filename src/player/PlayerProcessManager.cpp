#include "PlayerProcessManager.h"
#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDebug>
#include <QThread>
#include <QTimer>
#include <QtMath>

PlayerProcessManager::PlayerProcessManager(QObject *parent)
    : QObject(parent)
    , m_process(new QProcess(this))
    , m_ipcSocket(new QLocalSocket(this))
{
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &PlayerProcessManager::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred,
            this, &PlayerProcessManager::onProcessError);

    connect(m_ipcSocket, &QLocalSocket::connected, this, &PlayerProcessManager::onSocketConnected);
    connect(m_ipcSocket, &QLocalSocket::readyRead, this, &PlayerProcessManager::onSocketReadyRead);
    
    // Determine IPC path once
    m_ipcPath = getIpcPath();
}

PlayerProcessManager::~PlayerProcessManager()
{
    stopMpv();
}

QString PlayerProcessManager::getIpcPath() const
{
#ifdef Q_OS_WIN
    return "\\\\.\\pipe\\bloom-mpv-socket";
#else
    return QDir::tempPath() + "/bloom-mpv-socket";
#endif
}

// Helper to strip unnecessary quotes from argument values
// Users often copy args like --glsl-shader="path with spaces" but QProcess handles quoting automatically
static QString stripQuotesFromArgValue(const QString &arg)
{
    int eqPos = arg.indexOf('=');
    if (eqPos == -1) {
        return arg;  // No '=' means no value to unquote
    }
    
    QString key = arg.left(eqPos + 1);  // Include the '='
    QString value = arg.mid(eqPos + 1);
    
    // Strip surrounding quotes from value (both " and ')
    if ((value.startsWith('"') && value.endsWith('"')) ||
        (value.startsWith('\'') && value.endsWith('\''))) {
        value = value.mid(1, value.length() - 2);
    }
    
    return key + value;
}

void PlayerProcessManager::startMpv(const QString &mpvBin, const QStringList &args, const QString &mediaUrl)
{
    if (isRunning()) {
        stopMpv();
        // Give it a moment to close? Or just force kill?
        // For now, assume stopMpv() handles it or we wait.
        // But QProcess::kill is async.
        m_process->waitForFinished(1000);
    }

    QStringList finalArgs;
    // Strip quotes from argument values - QProcess handles quoting automatically,
    // so user-provided quotes like --glsl-shader="path" cause issues
    for (const QString &arg : args) {
        finalArgs << stripQuotesFromArgValue(arg);
    }
    
    // Add IPC server argument
    // On Windows, mpv expects just the pipe name for --input-ipc-server=\\.\pipe\name
    // On Linux, it expects the path.
    // QLocalSocket serverName on Windows is just "name".
    
    QString ipcArg = m_ipcPath;
    finalArgs << "--input-ipc-server=" + ipcArg;
    
    // Add media
    finalArgs << mediaUrl;

    // Observe properties for playback reporting
    // Note: --observe-property is not a valid CLI arg, we must send it via IPC once connected.
    
    qInfo() << "Starting mpv:" << mpvBin;
    m_process->start(mpvBin, finalArgs);
    
    // Try to connect IPC after a short delay or retry loop
    // We'll use a simple retry mechanism in connectIpc
    QTimer::singleShot(500, this, &PlayerProcessManager::connectIpc);
    
    emit stateChanged(true);
}

void PlayerProcessManager::stopMpv()
{
    if (m_process->state() != QProcess::NotRunning) {
        // Try to quit gracefully via IPC first
        sendCommand({"quit"});
        
        if (!m_process->waitForFinished(1000)) {
            m_process->kill();
        }
    }
    m_ipcSocket->abort();
    m_isConnected = false;
    m_pendingCommands.clear();
}

bool PlayerProcessManager::isRunning() const
{
    return m_process->state() != QProcess::NotRunning;
}

void PlayerProcessManager::connectIpc()
{
    if (!isRunning()) return;
    
    if (m_ipcSocket->state() == QLocalSocket::ConnectedState) return;

    // On Windows, QLocalSocket connects to "bloom-mpv-socket" if path is "\\.\pipe\bloom-mpv-socket"
    QString serverName = m_ipcPath;
#ifdef Q_OS_WIN
    if (serverName.startsWith("\\\\.\\pipe\\")) {
        serverName = serverName.mid(9);
    }
#endif

    m_ipcSocket->connectToServer(serverName);
    
    // If failed, retry in a bit if process is still running
    if (!m_ipcSocket->waitForConnected(100)) {
         if (isRunning()) {
             QTimer::singleShot(500, this, &PlayerProcessManager::connectIpc);
         }
    }
}

void PlayerProcessManager::sendCommand(const QStringList &command)
{
    QVariantList variantCmd;
    for (const QString &arg : command) {
        variantCmd.append(arg);
    }
    sendVariantCommand(variantCmd);
}

void PlayerProcessManager::sendVariantCommand(const QVariantList &command)
{
    if (m_ipcSocket->state() != QLocalSocket::ConnectedState) {
        // Queue the command for later if we're starting up
        if (isRunning()) {
            qDebug() << "IPC: Queueing command (not connected yet):" << command;
            m_pendingCommands.append(command);
        } else {
            qDebug() << "IPC: Not connected, dropping command:" << command;
        }
        return;
    }

    QJsonArray cmdArray;
    for (const QVariant &arg : command) {
        cmdArray.append(QJsonValue::fromVariant(arg));
    }
    
    QJsonObject jsonObj;
    jsonObj["command"] = cmdArray;
    
    QJsonDocument doc(jsonObj);
    qDebug() << "IPC >" << doc.toJson(QJsonDocument::Compact);
    m_ipcSocket->write(doc.toJson(QJsonDocument::Compact));
    m_ipcSocket->write("\n");
    m_ipcSocket->flush();
}

void PlayerProcessManager::flushPendingCommands()
{
    if (m_pendingCommands.isEmpty()) return;
    
    qDebug() << "IPC: Flushing" << m_pendingCommands.size() << "pending commands";
    QList<QVariantList> commands = m_pendingCommands;
    m_pendingCommands.clear();
    
    for (const QVariantList &cmd : commands) {
        sendVariantCommand(cmd);
    }
}

void PlayerProcessManager::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (exitStatus == QProcess::CrashExit || exitCode != 0) {
        qWarning() << "mpv process exited abnormally: exitCode=" << exitCode 
                   << "status=" << (exitStatus == QProcess::CrashExit ? "crashed" : "normal");
    } else {
        qInfo() << "mpv process exited normally";
    }
    m_ipcSocket->abort();
    m_isConnected = false;
    emit stateChanged(false);
}

void PlayerProcessManager::onProcessError(QProcess::ProcessError error)
{
    QString errorStr;
    switch (error) {
    case QProcess::FailedToStart: errorStr = QStringLiteral("FailedToStart"); break;
    case QProcess::Crashed: errorStr = QStringLiteral("Crashed"); break;
    case QProcess::Timedout: errorStr = QStringLiteral("Timedout"); break;
    case QProcess::WriteError: errorStr = QStringLiteral("WriteError"); break;
    case QProcess::ReadError: errorStr = QStringLiteral("ReadError"); break;
    default: errorStr = QStringLiteral("Unknown"); break;
    }
    qWarning() << "mpv process error:" << errorStr;
    emit errorOccurred("mpv process error");
}

void PlayerProcessManager::onSocketConnected()
{
    qInfo() << "mpv IPC connected";
    m_isConnected = true;
    
    // Start observing properties
    // Use integers for IDs as mpv expects
    sendVariantCommand(QVariantList{"observe_property", 1, "time-pos"});
    sendVariantCommand(QVariantList{"observe_property", 2, "duration"});
    sendVariantCommand(QVariantList{"observe_property", 3, "pause"});
    sendVariantCommand(QVariantList{"observe_property", 4, "aid"});  // Audio track ID
    sendVariantCommand(QVariantList{"observe_property", 5, "sid"});  // Subtitle track ID
    sendVariantCommand(QVariantList{"observe_property", 6, "paused-for-cache"});  // Buffering state
    sendVariantCommand(QVariantList{"observe_property", 7, "volume"});
    sendVariantCommand(QVariantList{"observe_property", 8, "mute"});
    
    // Flush any commands that were queued while connecting
    flushPendingCommands();
}

void PlayerProcessManager::onSocketReadyRead()
{
    // Handle events if needed
    while (m_ipcSocket->canReadLine()) {
        QByteArray line = m_ipcSocket->readLine();
        qDebug() << "IPC <" << line;
        
        QJsonDocument doc = QJsonDocument::fromJson(line);
        QJsonObject obj = doc.object();
        
        if (obj["event"].toString() == "property-change") {
            QString name = obj["name"].toString();
            if (name == "time-pos") {
                if (!obj["data"].isNull()) {
                    emit positionChanged(obj["data"].toDouble());
                }
            } else if (name == "duration") {
                if (!obj["data"].isNull()) {
                    emit durationChanged(obj["data"].toDouble());
                }
            } else if (name == "pause") {
                if (!obj["data"].isNull()) {
                    emit pauseChanged(obj["data"].toBool());
                }
            } else if (name == "aid") {
                // mpv track IDs are 1-indexed, convert to 0-indexed for Jellyfin
                if (!obj["data"].isNull()) {
                    int mpvTrackId = obj["data"].toInt();
                    emit audioTrackChanged(mpvTrackId > 0 ? mpvTrackId - 1 : -1);
                }
            } else if (name == "sid") {
                // mpv track IDs are 1-indexed, convert to 0-indexed for Jellyfin
                // "no" or false means no subtitles (-1)
                if (obj["data"].isBool() && !obj["data"].toBool()) {
                    emit subtitleTrackChanged(-1);
                } else if (!obj["data"].isNull() && obj["data"].isDouble()) {
                    int mpvTrackId = obj["data"].toInt();
                    emit subtitleTrackChanged(mpvTrackId > 0 ? mpvTrackId - 1 : -1);
                }
            } else if (name == "paused-for-cache") {
                if (!obj["data"].isNull()) {
                    emit pausedForCacheChanged(obj["data"].toBool());
                }
            } else if (name == "volume") {
                if (!obj["data"].isNull()) {
                    emit volumeChanged(qRound(obj["data"].toDouble()));
                }
            } else if (name == "mute") {
                if (!obj["data"].isNull()) {
                    emit muteChanged(obj["data"].toBool());
                }
            }
        } else if (obj["event"].toString() == "end-file") {
            // Emit playbackEnded so PlayerController can report stop
            emit playbackEnded();
        } else if (obj["event"].toString() == "client-message") {
            // Handle client-message events from mpv scripts/extensions (if any user scripts are loaded)
            // The "args" array contains the message name followed by any arguments
            QJsonArray argsArray = obj["args"].toArray();
            if (!argsArray.isEmpty()) {
                QString messageName = argsArray.at(0).toString();
                QStringList messageArgs;
                for (int i = 1; i < argsArray.size(); ++i) {
                    messageArgs.append(argsArray.at(i).toString());
                }
                qDebug() << "IPC: client-message received:" << messageName << messageArgs;
                emit scriptMessage(messageName, messageArgs);
            }
        }
    }
}
