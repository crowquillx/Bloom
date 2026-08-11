#include "PlayerProcessManager.h"

#include "../utils/BloomLogging.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QStandardPaths>
#include <QUuid>
#include <QtMath>

#include <algorithm>
#include <utility>

#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

namespace {

QString stripQuotesFromArgValue(const QString &arg)
{
    const int equalsPosition = arg.indexOf('=');
    if (equalsPosition == -1) {
        return arg;
    }

    const QString key = arg.left(equalsPosition + 1);
    QString value = arg.mid(equalsPosition + 1);
    if ((value.startsWith('"') && value.endsWith('"'))
        || (value.startsWith('\'') && value.endsWith('\''))) {
        value = value.mid(1, value.length() - 2);
    }
    return key + value;
}

QString userIdentityToken()
{
#ifdef Q_OS_UNIX
    const QByteArray identity = QByteArray::number(static_cast<qulonglong>(geteuid()));
#else
    QByteArray identity = qEnvironmentVariable("USERNAME").toUtf8();
    if (identity.isEmpty()) {
        identity = qEnvironmentVariable("USER").toUtf8();
    }
    if (identity.isEmpty()) {
        identity = QStandardPaths::writableLocation(
                       QStandardPaths::HomeLocation).toUtf8();
    }
#endif
    return QString::fromLatin1(
        QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex().left(10));
}

#ifdef Q_OS_UNIX
bool preparePrivateRuntimeDirectory(const QString &path)
{
    if (path.isEmpty() || !QDir().mkpath(path)) {
        return false;
    }
    const QFileInfo info(path);
    if (!info.isDir() || info.ownerId() != static_cast<uint>(geteuid())) {
        return false;
    }
    return QFile::setPermissions(path,
                                 QFileDevice::ReadOwner
                                     | QFileDevice::WriteOwner
                                     | QFileDevice::ExeOwner);
}

QString privateRuntimeDirectory(const QString &instanceToken)
{
    QStringList candidates;
    const QString runtimeLocation =
        QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (!runtimeLocation.isEmpty()) {
        candidates.append(QDir(runtimeLocation).filePath(QStringLiteral("bloom")));
    }

    const QString cacheLocation =
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (!cacheLocation.isEmpty()) {
        candidates.append(QDir(cacheLocation).filePath(QStringLiteral("runtime")));
    }

    const QString temporaryBase = QStandardPaths::writableLocation(
        QStandardPaths::TempLocation);
    if (!temporaryBase.isEmpty()) {
        candidates.append(QDir(temporaryBase).filePath(
            QStringLiteral("bloom-%1-%2")
                .arg(static_cast<qulonglong>(geteuid()))
                .arg(instanceToken)));
    }

    for (const QString &candidate : std::as_const(candidates)) {
        const QString longestEndpoint = QDir(candidate).filePath(
            QStringLiteral("mpv-%1-18446744073709551615.sock").arg(instanceToken));
        if (longestEndpoint.toUtf8().size() <= 100
            && preparePrivateRuntimeDirectory(candidate)) {
            return candidate;
        }
    }
    return {};
}
#endif

bool isFiniteNumber(const QJsonValue &value)
{
    return value.isDouble() && qIsFinite(value.toDouble());
}

} // namespace

PlayerProcessManager::PlayerProcessManager(QObject *parent)
    : PlayerProcessManager(PlayerProcessOptions{}, parent)
{
}

PlayerProcessManager::PlayerProcessManager(const PlayerProcessOptions &options,
                                           QObject *parent)
    : QObject(parent)
    , m_options(options)
    , m_process(new QProcess(this))
    , m_ipcSocket(new QLocalSocket(this))
{
    m_options.gracefulQuitTimeoutMs = std::max(1, m_options.gracefulQuitTimeoutMs);
    m_options.terminateTimeoutMs = std::max(1, m_options.terminateTimeoutMs);
    m_options.ipcRetryIntervalMs = std::max(1, m_options.ipcRetryIntervalMs);
    m_options.ipcConnectionDeadlineMs = std::max(1, m_options.ipcConnectionDeadlineMs);
    m_options.maximumIpcAttempts = std::max(1, m_options.maximumIpcAttempts);
    m_options.maximumPendingCommands = std::max(1, m_options.maximumPendingCommands);
    m_options.maximumIpcMessageBytes = std::max<qint64>(128,
                                                        m_options.maximumIpcMessageBytes);
    m_options.maximumBufferedWriteBytes = std::max<qint64>(128,
                                                           m_options.maximumBufferedWriteBytes);

    const QString uniquePart = QUuid::createUuid().toString(QUuid::WithoutBraces).left(12);
    m_instanceToken = QStringLiteral("%1-%2-%3")
                          .arg(userIdentityToken())
                          .arg(QCoreApplication::applicationPid())
                          .arg(uniquePart);
    m_ipcPath = createIpcPath();

    m_gracefulQuitTimer.setSingleShot(true);
    m_terminateTimer.setSingleShot(true);
    m_ipcRetryTimer.setSingleShot(true);
    m_ipcDeadlineTimer.setSingleShot(true);

    connect(&m_gracefulQuitTimer, &QTimer::timeout,
            this, &PlayerProcessManager::escalateToTerminate);
    connect(&m_terminateTimer, &QTimer::timeout,
            this, &PlayerProcessManager::escalateToKill);
    connect(&m_ipcRetryTimer, &QTimer::timeout,
            this, &PlayerProcessManager::retryIpcConnection);
    connect(&m_ipcDeadlineTimer, &QTimer::timeout, this, [this]() {
        failIpcConnection(QStringLiteral("connection deadline exceeded"));
    });

    connect(m_process, &QProcess::started,
            this, &PlayerProcessManager::onProcessStarted);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &PlayerProcessManager::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred,
            this, &PlayerProcessManager::onProcessError);
    connect(m_process, &QProcess::readyReadStandardOutput,
            m_process, [process = m_process]() { process->readAllStandardOutput(); });
    connect(m_process, &QProcess::readyReadStandardError,
            m_process, [process = m_process]() { process->readAllStandardError(); });

    connect(m_ipcSocket, &QLocalSocket::connected,
            this, &PlayerProcessManager::onSocketConnected);
    connect(m_ipcSocket, &QLocalSocket::disconnected,
            this, &PlayerProcessManager::onSocketDisconnected);
    connect(m_ipcSocket, &QLocalSocket::readyRead,
            this, &PlayerProcessManager::onSocketReadyRead);
    connect(m_ipcSocket, &QLocalSocket::errorOccurred, this,
            [this](QLocalSocket::LocalSocketError) {
                if (m_stopStage == StopStage::None && isRunning()
                    && !m_isConnected && !m_ipcRetryTimer.isActive()) {
                    m_ipcRetryTimer.start(m_options.ipcRetryIntervalMs);
                }
            });
}

PlayerProcessManager::~PlayerProcessManager()
{
    m_pendingLaunch.reset();
    m_gracefulQuitTimer.stop();
    m_terminateTimer.stop();
    resetIpcConnection();
    if (m_process->state() != QProcess::NotRunning) {
        // QProcess::~QProcess waits for a live child. Detach the process object
        // before killing so manager destruction never turns into a UI-thread
        // wait; it deletes itself after the asynchronous finished event.
        QProcess *process = m_process;
        QObject::disconnect(process, nullptr, this, nullptr);
        process->setParent(nullptr);
        connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                process, &QObject::deleteLater);
        process->kill();
        m_process = nullptr;
    }
    removeIpcEndpoint();
}

QString PlayerProcessManager::createIpcPath() const
{
    const QString endpoint = QStringLiteral("mpv-%1-%2")
                                 .arg(m_instanceToken)
                                 .arg(m_launchSequence);
#ifdef Q_OS_WIN
    return QStringLiteral("\\\\.\\pipe\\bloom-%1").arg(endpoint);
#else
    const QString runtimeDirectory = privateRuntimeDirectory(m_instanceToken);
    if (runtimeDirectory.isEmpty()) {
        return {};
    }
    return QDir(runtimeDirectory).filePath(endpoint + QStringLiteral(".sock"));
#endif
}

QString PlayerProcessManager::ipcServerName() const
{
#ifdef Q_OS_WIN
    constexpr auto pipePrefix = "\\\\.\\pipe\\";
    if (m_ipcPath.startsWith(QLatin1StringView(pipePrefix))) {
        return m_ipcPath.mid(int(std::char_traits<char>::length(pipePrefix)));
    }
#endif
    return m_ipcPath;
}

void PlayerProcessManager::removeIpcEndpoint()
{
#ifndef Q_OS_WIN
    if (!m_ipcPath.isEmpty()) {
        QFile::remove(m_ipcPath);
    }
#endif
}

void PlayerProcessManager::startMpv(const QString &mpvBin,
                                    const QStringList &args,
                                    const QString &mediaUrl)
{
    if (mpvBin.trimmed().isEmpty() || mediaUrl.isEmpty()) {
        emit errorOccurred(QStringLiteral("Cannot start mpv: executable or media URL is empty"));
        return;
    }

    LaunchRequest request;
    request.executable = mpvBin;
    request.mediaUrl = mediaUrl;
    request.arguments.reserve(args.size());
    for (const QString &arg : args) {
        request.arguments.append(stripQuotesFromArgValue(arg));
    }
    m_pendingLaunch = std::move(request);

    if (isRunning()) {
        qCInfo(lcPlaybackIpc) << "Queueing replacement mpv playback after process exit";
        beginStop();
        return;
    }
    launchPending();
}

void PlayerProcessManager::launchPending()
{
    if (!m_pendingLaunch.has_value() || m_process->state() != QProcess::NotRunning
        || m_launchActive) {
        return;
    }

    LaunchRequest request = std::move(*m_pendingLaunch);
    m_pendingLaunch.reset();
    ++m_launchSequence;
    removeIpcEndpoint();
    m_ipcPath = createIpcPath();
    if (m_ipcPath.isEmpty()) {
        emit errorOccurred(QStringLiteral("Cannot start mpv: no private IPC runtime directory"));
        return;
    }

    resetIpcConnection();
    m_ipcFailureReported = false;
    m_stopStage = StopStage::None;
    m_playlistPosition = -1;
    m_playlistCount = 0;
    m_pendingCommands.clear();

    QStringList finalArguments = std::move(request.arguments);
    finalArguments.append(QStringLiteral("--input-ipc-server=%1").arg(m_ipcPath));
    finalArguments.append(request.mediaUrl);

    m_launchActive = true;
    m_startupElapsed.restart();
    qCInfo(lcPlaybackIpc) << "Starting external mpv process:" << request.executable;
    m_process->start(request.executable, finalArguments);
}

void PlayerProcessManager::appendUrlsToPlaylist(const QStringList &mediaUrls)
{
    for (const QString &mediaUrl : mediaUrls) {
        if (!mediaUrl.isEmpty()) {
            sendVariantCommand(QVariantList{QStringLiteral("loadfile"),
                                            mediaUrl,
                                            QStringLiteral("append-play")});
        }
    }
}

void PlayerProcessManager::stopMpv()
{
    m_pendingLaunch.reset();
    beginStop();
}

void PlayerProcessManager::beginStop()
{
    if (m_process->state() == QProcess::NotRunning) {
        if (m_launchActive) {
            finishProcessLifecycle(m_process->exitCode(), m_process->exitStatus());
        }
        return;
    }
    if (m_stopStage != StopStage::None) {
        return;
    }

    m_stopStage = StopStage::GracefulQuit;
    m_ipcRetryTimer.stop();
    m_ipcDeadlineTimer.stop();
    m_pendingCommands.clear();
    if (m_ipcSocket->state() == QLocalSocket::ConnectedState) {
        writeCommand(QVariantList{QStringLiteral("quit")});
    }
    qCInfo(lcPlaybackIpc) << "Requested graceful mpv shutdown";
    m_gracefulQuitTimer.start(m_options.gracefulQuitTimeoutMs);
}

void PlayerProcessManager::escalateToTerminate()
{
    if (m_process->state() == QProcess::NotRunning) {
        return;
    }
    m_stopStage = StopStage::Terminating;
    ++m_terminateEscalations;
    qCWarning(lcPlaybackIpc) << "mpv did not quit within the grace period; terminating";
    m_process->terminate();
    m_terminateTimer.start(m_options.terminateTimeoutMs);
}

void PlayerProcessManager::escalateToKill()
{
    if (m_process->state() == QProcess::NotRunning) {
        return;
    }
    m_stopStage = StopStage::Killing;
    ++m_killEscalations;
    qCWarning(lcPlaybackIpc) << "mpv did not terminate within the deadline; killing";
    m_process->kill();
}

bool PlayerProcessManager::isRunning() const
{
    return m_launchActive || m_process->state() != QProcess::NotRunning;
}

QString PlayerProcessManager::ipcPath() const
{
    return m_ipcPath;
}

QVariantMap PlayerProcessManager::diagnostics() const
{
    QString stopStage;
    switch (m_stopStage) {
    case StopStage::None:
        stopStage = QStringLiteral("none");
        break;
    case StopStage::GracefulQuit:
        stopStage = QStringLiteral("graceful-quit");
        break;
    case StopStage::Terminating:
        stopStage = QStringLiteral("terminating");
        break;
    case StopStage::Killing:
        stopStage = QStringLiteral("killing");
        break;
    }
    return {
        {QStringLiteral("ipcPath"), m_ipcPath},
        {QStringLiteral("processState"), int(m_process->state())},
        {QStringLiteral("stopStage"), stopStage},
        {QStringLiteral("pendingCommands"), m_pendingCommands.size()},
        {QStringLiteral("droppedPendingCommands"),
         QVariant::fromValue(m_droppedPendingCommands)},
        {QStringLiteral("invalidIpcMessages"), QVariant::fromValue(m_invalidIpcMessages)},
        {QStringLiteral("ipcAttempts"), m_ipcAttempts},
        {QStringLiteral("terminateEscalations"),
         QVariant::fromValue(m_terminateEscalations)},
        {QStringLiteral("killEscalations"), QVariant::fromValue(m_killEscalations)},
        {QStringLiteral("lastStartupLatencyMs"), m_lastStartupLatencyMs},
        {QStringLiteral("lastIpcLatencyMs"), m_lastIpcLatencyMs},
    };
}

void PlayerProcessManager::onProcessStarted()
{
    if (!m_launchActive) {
        return;
    }
    m_lastStartupLatencyMs = m_startupElapsed.elapsed();
    qCInfo(lcPlaybackIpc) << "mpv process started in" << m_lastStartupLatencyMs << "ms";
    emit startupLatencyMeasured(m_lastStartupLatencyMs);
    if (!m_runningReported) {
        m_runningReported = true;
        emit stateChanged(true);
    }
    if (m_stopStage == StopStage::None) {
        beginIpcConnectionWindow();
    }
}

void PlayerProcessManager::onProcessFinished(int exitCode,
                                             QProcess::ExitStatus exitStatus)
{
    finishProcessLifecycle(exitCode, exitStatus);
}

void PlayerProcessManager::finishProcessLifecycle(int exitCode,
                                                  QProcess::ExitStatus exitStatus)
{
    if (!m_launchActive) {
        return;
    }

    m_launchActive = false;
    m_gracefulQuitTimer.stop();
    m_terminateTimer.stop();
    resetIpcConnection();
    removeIpcEndpoint();
    m_pendingCommands.clear();
    m_playlistPosition = -1;
    m_playlistCount = 0;
    m_stopStage = StopStage::None;

    const bool crashed = exitStatus == QProcess::CrashExit;
    if (crashed || exitCode != 0) {
        qCWarning(lcPlaybackIpc) << "mpv process exited abnormally"
                                 << "exitCode=" << exitCode
                                 << "crashed=" << crashed;
    } else {
        qCInfo(lcPlaybackIpc) << "mpv process exited normally";
    }

    emit processFinished(exitCode, crashed);
    if (m_runningReported) {
        m_runningReported = false;
        emit stateChanged(false);
    }

    if (m_pendingLaunch.has_value()) {
        QMetaObject::invokeMethod(this, &PlayerProcessManager::launchPending,
                                  Qt::QueuedConnection);
    }
}

void PlayerProcessManager::onProcessError(QProcess::ProcessError error)
{
    if (error == QProcess::FailedToStart) {
        const QString message = QStringLiteral("Failed to start mpv: %1")
                                    .arg(m_process->errorString());
        qCWarning(lcPlaybackIpc) << message;
        emit errorOccurred(message);
        finishProcessLifecycle(-1, QProcess::CrashExit);
        return;
    }

    if (error == QProcess::Crashed && m_stopStage != StopStage::None) {
        // terminate()/kill() report Crashed even though this is the expected
        // result of Bloom's bounded shutdown escalation.
        return;
    }

    QString errorName;
    switch (error) {
    case QProcess::Crashed:
        errorName = QStringLiteral("crashed");
        break;
    case QProcess::Timedout:
        errorName = QStringLiteral("timed out");
        break;
    case QProcess::WriteError:
        errorName = QStringLiteral("write failed");
        break;
    case QProcess::ReadError:
        errorName = QStringLiteral("read failed");
        break;
    case QProcess::UnknownError:
        errorName = QStringLiteral("unknown error");
        break;
    case QProcess::FailedToStart:
        Q_UNREACHABLE();
    }
    const QString message = QStringLiteral("mpv process %1: %2")
                                .arg(errorName, m_process->errorString());
    qCWarning(lcPlaybackIpc) << message;
    emit errorOccurred(message);
}

void PlayerProcessManager::beginIpcConnectionWindow()
{
    resetIpcConnection();
    m_ipcFailureReported = false;
    m_ipcElapsed.restart();
    m_ipcDeadlineTimer.start(m_options.ipcConnectionDeadlineMs);
    attemptIpcConnection();
}

void PlayerProcessManager::attemptIpcConnection()
{
    if (!isRunning() || m_stopStage != StopStage::None || m_isConnected) {
        return;
    }
    if (m_ipcAttempts >= m_options.maximumIpcAttempts) {
        failIpcConnection(QStringLiteral("maximum reconnect attempts reached"));
        return;
    }

    ++m_ipcAttempts;
    if (m_ipcSocket->state() != QLocalSocket::UnconnectedState) {
        m_ipcSocket->abort();
    }
    m_ipcSocket->connectToServer(ipcServerName(), QIODevice::ReadWrite);
    m_ipcRetryTimer.start(m_options.ipcRetryIntervalMs);
}

void PlayerProcessManager::retryIpcConnection()
{
    if (m_isConnected || !isRunning() || m_stopStage != StopStage::None) {
        return;
    }
    if (m_ipcSocket->state() != QLocalSocket::UnconnectedState) {
        m_ipcSocket->abort();
    }
    attemptIpcConnection();
}

void PlayerProcessManager::failIpcConnection(const QString &reason)
{
    if (m_ipcFailureReported || !isRunning() || m_stopStage != StopStage::None) {
        return;
    }
    m_ipcFailureReported = true;
    m_ipcRetryTimer.stop();
    m_ipcDeadlineTimer.stop();
    const QString message = QStringLiteral("mpv IPC connection failed: %1")
                                .arg(reason);
    qCWarning(lcPlaybackIpc) << message << "attempts=" << m_ipcAttempts;
    emit errorOccurred(message);
    beginStop();
}

void PlayerProcessManager::resetIpcConnection()
{
    m_ipcRetryTimer.stop();
    m_ipcDeadlineTimer.stop();
    m_ipcSocket->abort();
    m_isConnected = false;
    m_ipcAttempts = 0;
    m_ipcReadBuffer.clear();
}

void PlayerProcessManager::onSocketConnected()
{
    if (!isRunning() || m_stopStage != StopStage::None) {
        m_ipcSocket->abort();
        return;
    }

    m_isConnected = true;
    m_ipcRetryTimer.stop();
    m_ipcDeadlineTimer.stop();
    m_lastIpcLatencyMs = m_ipcElapsed.elapsed();
    qCInfo(lcPlaybackIpc) << "mpv IPC connected in" << m_lastIpcLatencyMs
                          << "ms after" << m_ipcAttempts << "attempts";
    emit ipcConnectionLatencyMeasured(m_lastIpcLatencyMs);

    sendVariantCommand(QVariantList{QStringLiteral("observe_property"), 1,
                                    QStringLiteral("time-pos")});
    sendVariantCommand(QVariantList{QStringLiteral("observe_property"), 2,
                                    QStringLiteral("duration")});
    sendVariantCommand(QVariantList{QStringLiteral("observe_property"), 3,
                                    QStringLiteral("pause")});
    sendVariantCommand(QVariantList{QStringLiteral("observe_property"), 4,
                                    QStringLiteral("aid")});
    sendVariantCommand(QVariantList{QStringLiteral("observe_property"), 5,
                                    QStringLiteral("sid")});
    sendVariantCommand(QVariantList{QStringLiteral("observe_property"), 6,
                                    QStringLiteral("paused-for-cache")});
    sendVariantCommand(QVariantList{QStringLiteral("observe_property"), 7,
                                    QStringLiteral("volume")});
    sendVariantCommand(QVariantList{QStringLiteral("observe_property"), 8,
                                    QStringLiteral("mute")});
    sendVariantCommand(QVariantList{QStringLiteral("observe_property"), 9,
                                    QStringLiteral("playlist-pos")});
    sendVariantCommand(QVariantList{QStringLiteral("observe_property"), 10,
                                    QStringLiteral("playlist-count")});
    sendVariantCommand(QVariantList{QStringLiteral("observe_property"), 11,
                                    QStringLiteral("demuxer-cache-time")});
    sendVariantCommand(QVariantList{QStringLiteral("observe_property"), 12,
                                    QStringLiteral("audio-device-list")});
    flushPendingCommands();
}

void PlayerProcessManager::onSocketDisconnected()
{
    const bool wasConnected = std::exchange(m_isConnected, false);
    if (!wasConnected || !isRunning() || m_stopStage != StopStage::None) {
        return;
    }
    qCWarning(lcPlaybackIpc) << "mpv IPC disconnected; reconnecting within deadline";
    // QLocalSocket is still unwinding its disconnect notification here. Reusing
    // it synchronously can corrupt its internal write buffer, so reconnect only
    // after control returns to the event loop.
    QMetaObject::invokeMethod(this, [this]() {
        if (!m_isConnected && isRunning() && m_stopStage == StopStage::None) {
            beginIpcConnectionWindow();
        }
    }, Qt::QueuedConnection);
}

void PlayerProcessManager::sendCommand(const QStringList &command)
{
    QVariantList variantCommand;
    variantCommand.reserve(command.size());
    for (const QString &argument : command) {
        variantCommand.append(argument);
    }
    sendVariantCommand(variantCommand);
}

bool PlayerProcessManager::serializeCommand(const QVariantList &command,
                                            QByteArray *payload,
                                            QString *commandName) const
{
    if (command.isEmpty() || command.first().metaType().id() != QMetaType::QString
        || command.first().toString().trimmed().isEmpty()) {
        return false;
    }

    QJsonArray commandArray;
    for (const QVariant &argument : command) {
        const QJsonValue value = QJsonValue::fromVariant(argument);
        if (value.isUndefined()) {
            return false;
        }
        commandArray.append(value);
    }

    QJsonObject object;
    object.insert(QStringLiteral("command"), commandArray);
    QByteArray serialized = QJsonDocument(object).toJson(QJsonDocument::Compact);
    if (serialized.size() + 1 > m_options.maximumIpcMessageBytes) {
        return false;
    }
    serialized.append('\n');
    if (payload) {
        *payload = std::move(serialized);
    }
    if (commandName) {
        *commandName = command.first().toString();
    }
    return true;
}

bool PlayerProcessManager::writeCommand(const QVariantList &command)
{
    QByteArray payload;
    QString commandName;
    if (!serializeCommand(command, &payload, &commandName)) {
        qCWarning(lcPlaybackIpc) << "Rejected invalid or oversized mpv IPC command";
        return false;
    }
    if (m_ipcSocket->state() != QLocalSocket::ConnectedState) {
        return false;
    }
    if (m_ipcSocket->bytesToWrite() + payload.size()
        > m_options.maximumBufferedWriteBytes) {
        qCWarning(lcPlaybackIpc) << "Dropping mpv IPC command because the write buffer is full"
                                 << "command=" << commandName;
        return false;
    }
    if (m_ipcSocket->write(payload) != payload.size()) {
        return false;
    }
    qCDebug(lcPlaybackIpc) << "IPC command sent:" << commandName;
    return true;
}

void PlayerProcessManager::sendVariantCommand(const QVariantList &command)
{
    QByteArray validatedPayload;
    QString commandName;
    if (!serializeCommand(command, &validatedPayload, &commandName)) {
        qCWarning(lcPlaybackIpc) << "Rejected invalid or oversized mpv IPC command";
        return;
    }

    if (m_ipcSocket->state() == QLocalSocket::ConnectedState) {
        if (!writeCommand(command)) {
            qCWarning(lcPlaybackIpc) << "Unable to write mpv IPC command"
                                     << "command=" << commandName;
        }
        return;
    }
    if (isRunning() && m_stopStage == StopStage::None) {
        enqueueCommand(command);
    }
}

void PlayerProcessManager::enqueueCommand(const QVariantList &command)
{
    if (m_pendingCommands.size() >= m_options.maximumPendingCommands) {
        m_pendingCommands.removeFirst();
        ++m_droppedPendingCommands;
        qCWarning(lcPlaybackIpc) << "mpv IPC pending-command limit reached; dropped oldest command";
    }
    m_pendingCommands.append(command);
}

void PlayerProcessManager::flushPendingCommands()
{
    while (!m_pendingCommands.isEmpty()
           && m_ipcSocket->state() == QLocalSocket::ConnectedState) {
        const QVariantList command = m_pendingCommands.takeFirst();
        if (!writeCommand(command)) {
            m_pendingCommands.prepend(command);
            break;
        }
    }
}

void PlayerProcessManager::onSocketReadyRead()
{
    const qint64 remainingCapacity = m_options.maximumIpcMessageBytes + 1
        - m_ipcReadBuffer.size();
    if (remainingCapacity > 0) {
        m_ipcReadBuffer.append(m_ipcSocket->read(remainingCapacity));
    }
    while (true) {
        const qsizetype newline = m_ipcReadBuffer.indexOf('\n');
        if (newline < 0) {
            break;
        }
        QByteArray line = m_ipcReadBuffer.left(newline).trimmed();
        m_ipcReadBuffer.remove(0, newline + 1);
        if (line.isEmpty()) {
            continue;
        }
        if (line.size() > m_options.maximumIpcMessageBytes) {
            recordInvalidIpcMessage(QStringLiteral("message exceeds size limit"));
            continue;
        }

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            recordInvalidIpcMessage(QStringLiteral("malformed JSON object"));
            continue;
        }
        handleIpcObject(document.object());
    }

    if (m_ipcReadBuffer.size() > m_options.maximumIpcMessageBytes) {
        m_ipcReadBuffer.clear();
        recordInvalidIpcMessage(QStringLiteral("unterminated message exceeds size limit"));
        failIpcConnection(QStringLiteral("received oversized IPC message"));
    }
}

void PlayerProcessManager::recordInvalidIpcMessage(const QString &reason)
{
    ++m_invalidIpcMessages;
    qCWarning(lcPlaybackIpc) << "Ignored invalid mpv IPC message:" << reason;
}

void PlayerProcessManager::handleIpcObject(const QJsonObject &object)
{
    const QJsonValue eventValue = object.value(QStringLiteral("event"));
    if (eventValue.isUndefined()) {
        // Command replies carry request_id/error but no event and need no handling.
        return;
    }
    if (!eventValue.isString() || eventValue.toString().isEmpty()) {
        recordInvalidIpcMessage(QStringLiteral("event is not a non-empty string"));
        return;
    }

    const QString event = eventValue.toString();
    if (event == QStringLiteral("property-change")) {
        const QJsonValue nameValue = object.value(QStringLiteral("name"));
        if (!nameValue.isString() || !object.contains(QStringLiteral("data"))) {
            recordInvalidIpcMessage(QStringLiteral("property change lacks name or data"));
            return;
        }
        const QString name = nameValue.toString();
        const QJsonValue data = object.value(QStringLiteral("data"));
        if (name == QStringLiteral("time-pos")) {
            if (!data.isNull() && isFiniteNumber(data)) {
                emit positionChanged(data.toDouble());
            }
        } else if (name == QStringLiteral("duration")) {
            if (!data.isNull() && isFiniteNumber(data)) {
                emit durationChanged(data.toDouble());
            }
        } else if (name == QStringLiteral("pause")) {
            if (!data.isNull() && data.isBool()) {
                emit pauseChanged(data.toBool());
            }
        } else if (name == QStringLiteral("aid")) {
            if (!data.isNull() && isFiniteNumber(data)) {
                const int trackId = data.toInt();
                emit audioTrackChanged(trackId > 0 ? trackId : -1);
            }
        } else if (name == QStringLiteral("sid")) {
            if ((data.isBool() && !data.toBool())
                || (data.isString() && data.toString() == QStringLiteral("no"))) {
                emit subtitleTrackChanged(-1);
            } else if (!data.isNull() && isFiniteNumber(data)) {
                const int trackId = data.toInt();
                emit subtitleTrackChanged(trackId > 0 ? trackId : -1);
            }
        } else if (name == QStringLiteral("paused-for-cache")) {
            if (!data.isNull() && data.isBool()) {
                emit pausedForCacheChanged(data.toBool());
            }
        } else if (name == QStringLiteral("volume")) {
            if (!data.isNull() && isFiniteNumber(data)) {
                emit volumeChanged(qRound(data.toDouble()));
            }
        } else if (name == QStringLiteral("mute")) {
            if (!data.isNull() && data.isBool()) {
                emit muteChanged(data.toBool());
            }
        } else if (name == QStringLiteral("playlist-pos")) {
            if (!data.isNull() && isFiniteNumber(data)) {
                m_playlistPosition = data.toInt(-1);
                emit playlistPositionChanged(m_playlistPosition);
            }
        } else if (name == QStringLiteral("playlist-count")) {
            if (!data.isNull() && isFiniteNumber(data)) {
                m_playlistCount = data.toInt(0);
            }
        } else if (name == QStringLiteral("demuxer-cache-time")) {
            if (!data.isNull() && isFiniteNumber(data)) {
                emit cacheEndChanged(data.toDouble());
            }
        } else if (name == QStringLiteral("audio-device-list") && data.isArray()) {
            QVariantList devices;
            const QJsonArray deviceArray = data.toArray();
            devices.reserve(deviceArray.size());
            for (const QJsonValue &entry : deviceArray) {
                if (!entry.isObject()) {
                    continue;
                }
                const QJsonObject deviceObject = entry.toObject();
                if (!deviceObject.value(QStringLiteral("name")).isString()
                    || !deviceObject.value(QStringLiteral("description")).isString()) {
                    continue;
                }
                devices.append(QVariantMap{
                    {QStringLiteral("name"),
                     deviceObject.value(QStringLiteral("name")).toString()},
                    {QStringLiteral("description"),
                     deviceObject.value(QStringLiteral("description")).toString()},
                });
            }
            emit audioDeviceListChanged(devices);
        }
        return;
    }

    if (event == QStringLiteral("end-file")) {
        const bool hasRemainingPlaylistItems = m_playlistCount > 0
            && m_playlistPosition >= 0
            && (m_playlistPosition + 1) < m_playlistCount;
        if (!hasRemainingPlaylistItems) {
            emit playbackEnded();
        }
        return;
    }

    if (event == QStringLiteral("client-message")) {
        const QJsonValue argumentsValue = object.value(QStringLiteral("args"));
        if (!argumentsValue.isArray()) {
            recordInvalidIpcMessage(QStringLiteral("client message args is not an array"));
            return;
        }
        const QJsonArray argumentArray = argumentsValue.toArray();
        if (argumentArray.isEmpty() || !argumentArray.first().isString()) {
            recordInvalidIpcMessage(QStringLiteral("client message has no string name"));
            return;
        }
        QStringList arguments;
        arguments.reserve(argumentArray.size() - 1);
        for (qsizetype index = 1; index < argumentArray.size(); ++index) {
            if (!argumentArray.at(index).isString()) {
                recordInvalidIpcMessage(QStringLiteral("client message argument is not a string"));
                return;
            }
            arguments.append(argumentArray.at(index).toString());
        }
        emit scriptMessage(argumentArray.first().toString(), arguments);
    }
}
