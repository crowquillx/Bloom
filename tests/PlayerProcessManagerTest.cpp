#include <QtTest/QtTest>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPointer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>

#include "player/PlayerProcessManager.h"

#ifdef Q_OS_UNIX
#include <csignal>
#include <unistd.h>
#endif

namespace {

QString optionValue(const QStringList &arguments, const QString &prefix)
{
    for (const QString &argument : arguments) {
        if (argument.startsWith(prefix)) {
            return argument.mid(prefix.size());
        }
    }
    return {};
}

QString localServerName(QString ipcPath)
{
#ifdef Q_OS_WIN
    const QString prefix = QStringLiteral("\\\\.\\pipe\\");
    if (ipcPath.startsWith(prefix)) {
        ipcPath.remove(0, prefix.size());
    }
#endif
    return ipcPath;
}

void appendLaunchRecord(const QString &path, const QString &mediaUrl)
{
    if (path.isEmpty()) {
        return;
    }
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        file.write(mediaUrl.toUtf8());
        file.write("\n");
    }
}

int runFakeMpv(const QStringList &arguments)
{
#ifdef Q_OS_UNIX
    if (arguments.contains(QStringLiteral("--fake-ignore-terminate"))) {
        std::signal(SIGTERM, SIG_IGN);
    }
#endif

    const QString mediaUrl = arguments.isEmpty() ? QString() : arguments.last();
    appendLaunchRecord(optionValue(arguments, QStringLiteral("--fake-log=")), mediaUrl);

    if (arguments.contains(QStringLiteral("--fake-no-ipc"))) {
        QTimer::singleShot(30000, QCoreApplication::instance(), &QCoreApplication::quit);
        return QCoreApplication::exec();
    }

    const QString ipcPath = optionValue(arguments, QStringLiteral("--input-ipc-server="));
    if (ipcPath.isEmpty()) {
        return 40;
    }

    QLocalServer server;
    if (!server.listen(localServerName(ipcPath))) {
        return 41;
    }

    const bool ignoreQuit = arguments.contains(QStringLiteral("--fake-ignore-quit"));
    const bool sendEvents = arguments.contains(QStringLiteral("--fake-send-events"));
    const bool sendBurst = arguments.contains(QStringLiteral("--fake-send-burst"));
    const bool acknowledgeCommands = arguments.contains(
        QStringLiteral("--fake-ack-commands"));
    const bool disconnectOnce = arguments.contains(QStringLiteral("--fake-disconnect-once"));
    int connectionCount = 0;
    QObject::connect(&server, &QLocalServer::newConnection, &server, [&]() {
        while (server.hasPendingConnections()) {
            QLocalSocket *socket = server.nextPendingConnection();
            ++connectionCount;
            QObject::connect(socket, &QLocalSocket::readyRead, socket,
                             [socket, ignoreQuit, acknowledgeCommands]() {
                QByteArray buffer = socket->property("buffer").toByteArray();
                buffer.append(socket->readAll());
                while (true) {
                    const qsizetype newline = buffer.indexOf('\n');
                    if (newline < 0) {
                        break;
                    }
                    const QByteArray line = buffer.left(newline);
                    buffer.remove(0, newline + 1);
                    const QJsonDocument document = QJsonDocument::fromJson(line);
                    if (!document.isObject()) {
                        continue;
                    }
                    const QJsonArray command = document.object()
                                                   .value(QStringLiteral("command"))
                                                   .toArray();
                    if (!ignoreQuit && !command.isEmpty()
                        && command.first().toString() == QStringLiteral("quit")) {
                        QCoreApplication::quit();
                        return;
                    }
                    if (acknowledgeCommands && !command.isEmpty()
                        && command.first().toString() == QStringLiteral("script-message")) {
                        const QJsonObject event{
                            {QStringLiteral("event"), QStringLiteral("client-message")},
                            {QStringLiteral("args"),
                             QJsonArray{QStringLiteral("command-ack")}},
                        };
                        socket->write(QJsonDocument(event).toJson(QJsonDocument::Compact));
                        socket->write("\n");
                    }
                }
                socket->setProperty("buffer", buffer);
            });

            if (sendEvents) {
                socket->write("{bad-json}\n");
                socket->write("[]\n");
                QJsonObject propertyEvent{
                    {QStringLiteral("event"), QStringLiteral("property-change")},
                    {QStringLiteral("name"), QStringLiteral("time-pos")},
                    {QStringLiteral("data"), 12.5},
                };
                socket->write(QJsonDocument(propertyEvent).toJson(QJsonDocument::Compact));
                socket->write("\n");
                socket->flush();
            }
            if (sendBurst) {
                for (int index = 0; index < 100; ++index) {
                    const QJsonObject event{
                        {QStringLiteral("event"), QStringLiteral("property-change")},
                        {QStringLiteral("name"), QStringLiteral("time-pos")},
                        {QStringLiteral("data"), index},
                    };
                    socket->write(QJsonDocument(event).toJson(QJsonDocument::Compact));
                    socket->write("\n");
                }
                socket->flush();
            }
            if (disconnectOnce && connectionCount == 1) {
                QTimer::singleShot(20, socket, &QLocalSocket::disconnectFromServer);
            }
        }
    });

    return QCoreApplication::exec();
}

PlayerProcessOptions fastOptions()
{
    PlayerProcessOptions options;
    options.gracefulQuitTimeoutMs = 80;
    options.terminateTimeoutMs = 80;
    options.ipcRetryIntervalMs = 15;
    options.ipcConnectionDeadlineMs = 500;
    options.maximumIpcAttempts = 40;
    options.maximumPendingCommands = 3;
    options.maximumIpcMessageBytes = 4096;
    options.maximumBufferedWriteBytes = 64 * 1024;
    return options;
}

QStringList fakeArguments(const QString &logPath = {},
                          const QStringList &extra = {})
{
    QStringList arguments{QStringLiteral("--fake-mpv")};
    if (!logPath.isEmpty()) {
        arguments.append(QStringLiteral("--fake-log=%1").arg(logPath));
    }
    arguments.append(extra);
    return arguments;
}

QStringList launchRecords(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    QStringList records;
    for (const QByteArray &line : file.readAll().split('\n')) {
        if (!line.isEmpty()) {
            records.append(QString::fromUtf8(line));
        }
    }
    return records;
}

} // namespace

class PlayerProcessManagerTest : public QObject
{
    Q_OBJECT

private slots:
    void endpointsAreUniqueAndPrivate();
    void replacementWaitsForFinishedWithoutBlocking();
    void gracefulStopEscalatesWithoutBlocking();
    void destructionDoesNotBlockRunningProcess();
    void killEscalationIsBounded();
    void ipcConnectionDeadlineIsBounded();
    void ipcReconnectAttemptsAreBounded();
    void pendingCommandsAreBounded();
    void validCommandLargerThanInitialWriteLimitIsDelivered();
    void batchedIpcMessagesAreFullyDrained();
    void disconnectedIpcReconnectsWithinDeadline();
    void malformedIpcIsIgnoredBeforeValidEvent();
    void startupFailureIsClear();
};

void PlayerProcessManagerTest::endpointsAreUniqueAndPrivate()
{
    PlayerProcessManager first;
    PlayerProcessManager second;
    QVERIFY(!first.ipcPath().isEmpty());
    QVERIFY(!second.ipcPath().isEmpty());
    QVERIFY(first.ipcPath() != second.ipcPath());

#ifdef Q_OS_UNIX
    const QFileInfo directory(QFileInfo(first.ipcPath()).absolutePath());
    QVERIFY(directory.isDir());
    QCOMPARE(directory.ownerId(), static_cast<uint>(geteuid()));
    const QFileDevice::Permissions groupOrOther =
        QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup
        | QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther;
    QVERIFY(!(directory.permissions() & groupOrOther));
#endif
}

void PlayerProcessManagerTest::replacementWaitsForFinishedWithoutBlocking()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString logPath = directory.filePath(QStringLiteral("launches.log"));
    PlayerProcessManager manager(fastOptions());
    QSignalSpy stateSpy(&manager, &PlayerProcessManager::stateChanged);
    QSignalSpy finishedSpy(&manager, &PlayerProcessManager::processFinished);
    QSignalSpy ipcSpy(&manager, &PlayerProcessManager::ipcConnectionLatencyMeasured);

    manager.startMpv(QCoreApplication::applicationFilePath(),
                     fakeArguments(logPath), QStringLiteral("first-media"));
    QTRY_VERIFY_WITH_TIMEOUT(manager.isRunning(), 1000);
    QTRY_COMPARE_WITH_TIMEOUT(ipcSpy.count(), 1, 2000);

    QElapsedTimer callTimer;
    callTimer.start();
    manager.startMpv(QCoreApplication::applicationFilePath(),
                     fakeArguments(logPath), QStringLiteral("second-media"));
    QVERIFY2(callTimer.elapsed() < 50, "replacement start blocked the caller");

    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 2000);
    QTRY_COMPARE_WITH_TIMEOUT(launchRecords(logPath).size(), 2, 2000);
    QTRY_COMPARE_WITH_TIMEOUT(ipcSpy.count(), 2, 2000);
    QCOMPARE(launchRecords(logPath),
             QStringList({QStringLiteral("first-media"), QStringLiteral("second-media")}));

    manager.stopMpv();
    QTRY_VERIFY_WITH_TIMEOUT(!manager.isRunning(), 2000);
    QVERIFY(stateSpy.count() >= 4);
}

void PlayerProcessManagerTest::gracefulStopEscalatesWithoutBlocking()
{
    PlayerProcessManager manager(fastOptions());
    QSignalSpy stateSpy(&manager, &PlayerProcessManager::stateChanged);
    QSignalSpy ipcSpy(&manager, &PlayerProcessManager::ipcConnectionLatencyMeasured);
    QSignalSpy errorSpy(&manager, &PlayerProcessManager::errorOccurred);
    manager.startMpv(QCoreApplication::applicationFilePath(),
                     fakeArguments({}, {QStringLiteral("--fake-ignore-quit")}),
                     QStringLiteral("stubborn-media"));
    QTRY_COMPARE_WITH_TIMEOUT(ipcSpy.count(), 1, 2000);

    QElapsedTimer callTimer;
    callTimer.start();
    manager.stopMpv();
    QVERIFY2(callTimer.elapsed() < 50, "stopMpv blocked the caller");
    QTRY_VERIFY_WITH_TIMEOUT(!manager.isRunning(), 2000);
    QVERIFY(manager.diagnostics().value(QStringLiteral("terminateEscalations"))
                .toULongLong() >= 1);
    QCOMPARE(errorSpy.count(), 0);
    QVERIFY(stateSpy.count() >= 2);
}

void PlayerProcessManagerTest::destructionDoesNotBlockRunningProcess()
{
    auto *manager = new PlayerProcessManager(fastOptions());
    QSignalSpy startedSpy(manager, &PlayerProcessManager::startupLatencyMeasured);
    manager->startMpv(QCoreApplication::applicationFilePath(),
                      fakeArguments({}, {QStringLiteral("--fake-no-ipc"),
                                         QStringLiteral("--fake-ignore-terminate")}),
                      QStringLiteral("destructor-media"));
    QTRY_COMPARE_WITH_TIMEOUT(startedSpy.count(), 1, 1000);

    QElapsedTimer callTimer;
    callTimer.start();
    delete manager;
    QVERIFY2(callTimer.elapsed() < 50, "manager destruction blocked on the child process");
    QTest::qWait(50);
}

void PlayerProcessManagerTest::killEscalationIsBounded()
{
#ifndef Q_OS_UNIX
    QSKIP("QProcess::terminate cannot be ignored portably on this platform");
#else
    PlayerProcessManager manager(fastOptions());
    QSignalSpy ipcSpy(&manager, &PlayerProcessManager::ipcConnectionLatencyMeasured);
    manager.startMpv(QCoreApplication::applicationFilePath(),
                     fakeArguments({}, {QStringLiteral("--fake-ignore-quit"),
                                        QStringLiteral("--fake-ignore-terminate")}),
                     QStringLiteral("unkillable-media"));
    QTRY_COMPARE_WITH_TIMEOUT(ipcSpy.count(), 1, 2000);
    manager.stopMpv();
    QTRY_VERIFY_WITH_TIMEOUT(!manager.isRunning(), 2000);
    QVERIFY(manager.diagnostics().value(QStringLiteral("killEscalations"))
                .toULongLong() >= 1);
#endif
}

void PlayerProcessManagerTest::ipcConnectionDeadlineIsBounded()
{
    PlayerProcessOptions options = fastOptions();
    options.ipcConnectionDeadlineMs = 120;
    PlayerProcessManager manager(options);
    QSignalSpy errorSpy(&manager, &PlayerProcessManager::errorOccurred);
    manager.startMpv(QCoreApplication::applicationFilePath(),
                     fakeArguments({}, {QStringLiteral("--fake-no-ipc")}),
                     QStringLiteral("no-ipc-media"));

    QTRY_VERIFY_WITH_TIMEOUT(errorSpy.count() >= 1, 1000);
    QVERIFY(errorSpy.first().first().toString().contains(QStringLiteral("IPC connection failed")));
    QTRY_VERIFY_WITH_TIMEOUT(!manager.isRunning(), 2000);
}

void PlayerProcessManagerTest::ipcReconnectAttemptsAreBounded()
{
    PlayerProcessOptions options = fastOptions();
    options.ipcConnectionDeadlineMs = 2000;
    options.maximumIpcAttempts = 3;
    PlayerProcessManager manager(options);
    QSignalSpy errorSpy(&manager, &PlayerProcessManager::errorOccurred);
    manager.startMpv(QCoreApplication::applicationFilePath(),
                     fakeArguments({}, {QStringLiteral("--fake-no-ipc")}),
                     QStringLiteral("attempt-limit-media"));

    QTRY_VERIFY_WITH_TIMEOUT(errorSpy.count() >= 1, 1000);
    QVERIFY(errorSpy.first().first().toString().contains(
        QStringLiteral("maximum reconnect attempts")));
    QCOMPARE(manager.diagnostics().value(QStringLiteral("ipcAttempts")).toInt(), 3);
    QTRY_VERIFY_WITH_TIMEOUT(!manager.isRunning(), 2000);
}

void PlayerProcessManagerTest::pendingCommandsAreBounded()
{
    PlayerProcessManager manager(fastOptions());
    QSignalSpy startedSpy(&manager, &PlayerProcessManager::startupLatencyMeasured);
    manager.startMpv(QCoreApplication::applicationFilePath(),
                     fakeArguments({}, {QStringLiteral("--fake-no-ipc")}),
                     QStringLiteral("queued-command-media"));
    QTRY_COMPARE_WITH_TIMEOUT(startedSpy.count(), 1, 1000);

    manager.sendVariantCommand({});
    manager.sendVariantCommand(QVariantList{42, QStringLiteral("not-a-command")});
    for (int index = 0; index < 10; ++index) {
        manager.sendVariantCommand(QVariantList{QStringLiteral("set_property"),
                                                QStringLiteral("volume"), index});
    }
    const QVariantMap diagnostics = manager.diagnostics();
    QCOMPARE(diagnostics.value(QStringLiteral("pendingCommands")).toInt(), 3);
    QCOMPARE(diagnostics.value(QStringLiteral("droppedPendingCommands")).toULongLong(),
             quint64(7));
    manager.stopMpv();
    QTRY_VERIFY_WITH_TIMEOUT(!manager.isRunning(), 2000);
}

void PlayerProcessManagerTest::validCommandLargerThanInitialWriteLimitIsDelivered()
{
    PlayerProcessOptions options = fastOptions();
    options.maximumIpcMessageBytes = 4096;
    options.maximumBufferedWriteBytes = 128;
    options.maximumPendingCommands = 16;
    PlayerProcessManager manager(options);
    QSignalSpy ipcSpy(&manager, &PlayerProcessManager::ipcConnectionLatencyMeasured);
    QSignalSpy messageSpy(&manager, &PlayerProcessManager::scriptMessage);
    manager.startMpv(QCoreApplication::applicationFilePath(),
                     fakeArguments({}, {QStringLiteral("--fake-ack-commands")}),
                     QStringLiteral("large-command-media"));
    QTRY_COMPARE_WITH_TIMEOUT(ipcSpy.count(), 1, 2000);

    for (int index = 0; index < 6; ++index) {
        manager.sendVariantCommand(QVariantList{QStringLiteral("script-message"),
                                                QString(1024, QLatin1Char('x'))});
    }
    QTRY_COMPARE_WITH_TIMEOUT(messageSpy.count(), 6, 2000);
    QCOMPARE(messageSpy.first().first().toString(), QStringLiteral("command-ack"));
    manager.stopMpv();
    QTRY_VERIFY_WITH_TIMEOUT(!manager.isRunning(), 2000);
}

void PlayerProcessManagerTest::batchedIpcMessagesAreFullyDrained()
{
    PlayerProcessOptions options = fastOptions();
    options.maximumIpcMessageBytes = 512;
    PlayerProcessManager manager(options);
    QSignalSpy positionSpy(&manager, &PlayerProcessManager::positionChanged);
    manager.startMpv(QCoreApplication::applicationFilePath(),
                     fakeArguments({}, {QStringLiteral("--fake-send-burst")}),
                     QStringLiteral("burst-media"));

    QTRY_COMPARE_WITH_TIMEOUT(positionSpy.count(), 100, 2000);
    QCOMPARE(positionSpy.last().first().toDouble(), 99.0);
    manager.stopMpv();
    QTRY_VERIFY_WITH_TIMEOUT(!manager.isRunning(), 2000);
}

void PlayerProcessManagerTest::disconnectedIpcReconnectsWithinDeadline()
{
    PlayerProcessManager manager(fastOptions());
    QSignalSpy ipcSpy(&manager, &PlayerProcessManager::ipcConnectionLatencyMeasured);
    manager.startMpv(QCoreApplication::applicationFilePath(),
                     fakeArguments({}, {QStringLiteral("--fake-disconnect-once")}),
                     QStringLiteral("reconnect-media"));

    QTRY_COMPARE_WITH_TIMEOUT(ipcSpy.count(), 2, 2000);
    QVERIFY(manager.isRunning());
    manager.stopMpv();
    QTRY_VERIFY_WITH_TIMEOUT(!manager.isRunning(), 2000);
}

void PlayerProcessManagerTest::malformedIpcIsIgnoredBeforeValidEvent()
{
    PlayerProcessManager manager(fastOptions());
    QSignalSpy positionSpy(&manager, &PlayerProcessManager::positionChanged);
    manager.startMpv(QCoreApplication::applicationFilePath(),
                     fakeArguments({}, {QStringLiteral("--fake-send-events")}),
                     QStringLiteral("event-media"));

    QTRY_COMPARE_WITH_TIMEOUT(positionSpy.count(), 1, 2000);
    QCOMPARE(positionSpy.first().first().toDouble(), 12.5);
    QVERIFY(manager.diagnostics().value(QStringLiteral("invalidIpcMessages"))
                .toULongLong() >= 2);
    manager.stopMpv();
    QTRY_VERIFY_WITH_TIMEOUT(!manager.isRunning(), 2000);
}

void PlayerProcessManagerTest::startupFailureIsClear()
{
    PlayerProcessManager manager(fastOptions());
    QSignalSpy errorSpy(&manager, &PlayerProcessManager::errorOccurred);
    manager.startMpv(QStringLiteral("/definitely/missing/bloom-mpv"), {},
                     QStringLiteral("failure-media"));
    QTRY_COMPARE_WITH_TIMEOUT(errorSpy.count(), 1, 1000);
    QVERIFY(errorSpy.first().first().toString().startsWith(QStringLiteral("Failed to start mpv:")));
    QVERIFY(!manager.isRunning());
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    const QStringList arguments = application.arguments();
    if (arguments.contains(QStringLiteral("--fake-mpv"))) {
        return runFakeMpv(arguments.mid(1));
    }

    PlayerProcessManagerTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "PlayerProcessManagerTest.moc"
