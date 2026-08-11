#include <QtTest/QtTest>

#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "utils/ConfigManager.h"
#include "utils/DisplayManager.h"

namespace {

QString shellCommand(const QString &script)
{
    QString escaped = script;
    escaped.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return QStringLiteral("/bin/sh -c \"%1\"").arg(escaped);
}

DisplayManagerOptions fastOptions()
{
    DisplayManagerOptions options;
    options.commandDeadlineMs = 250;
    options.maximumCommandOutputBytes = 1024;
    return options;
}

QString readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

} // namespace

class DisplayManagerTest : public QObject
{
    Q_OBJECT

private slots:
    void hdrCommandIsNonBlockingAndDeadlineBounded();
    void commandOutputCaptureIsBounded();
    void replacementCancelsStaleCompletion();
    void compatibleRefreshMultipleSkipsCommand();
    void refreshCommandSubstitutesFractionalAndIntegerRates();
    void destructionDoesNotWaitForCommand();
};

void DisplayManagerTest::hdrCommandIsNonBlockingAndDeadlineBounded()
{
#ifdef Q_OS_WIN
    QSKIP("Linux custom-command integration test");
#else
    ConfigManager config;
    config.setLinuxHDRCommand(shellCommand(QStringLiteral("sleep 2")));
    DisplayManager manager(&config, fastOptions());
    QSignalSpy hdrSpy(&manager, &DisplayManager::hdrChangeFinished);
    QSignalSpy operationSpy(&manager, &DisplayManager::displayOperationMeasured);

    QElapsedTimer timer;
    timer.start();
    manager.setHDRAsync(true);
    QVERIFY2(timer.elapsed() < 50, "setHDRAsync blocked the caller");

    QTRY_COMPARE_WITH_TIMEOUT(hdrSpy.count(), 1, 2000);
    QCOMPARE(hdrSpy.first().at(0).toBool(), true);
    QCOMPARE(hdrSpy.first().at(1).toBool(), false);
    QCOMPARE(operationSpy.count(), 1);
    QCOMPARE(operationSpy.first().at(3).toBool(), true);
    QCOMPARE(manager.diagnostics().value(QStringLiteral("operationsTimedOut"))
                 .toULongLong(), quint64(1));
    config.setLinuxHDRCommand(shellCommand(QStringLiteral("true")));
    manager.setHDRAsync(false);
    QTRY_COMPARE_WITH_TIMEOUT(hdrSpy.count(), 2, 1000);
#endif
}

void DisplayManagerTest::commandOutputCaptureIsBounded()
{
#ifdef Q_OS_WIN
    QSKIP("Linux custom-command integration test");
#else
    ConfigManager config;
    config.setLinuxHDRCommand(shellCommand(
        QStringLiteral("yes output | head -c 100000; "
                       "yes error | head -c 100000 >&2; exit 7")));
    DisplayManager manager(&config, fastOptions());
    QSignalSpy hdrSpy(&manager, &DisplayManager::hdrChangeFinished);

    manager.setHDRAsync(true);
    QTRY_COMPARE_WITH_TIMEOUT(hdrSpy.count(), 1, 2000);
    QCOMPARE(hdrSpy.first().at(1).toBool(), false);
    const QVariantMap diagnostics = manager.diagnostics();
    QVERIFY(diagnostics.value(QStringLiteral("capturedStdoutBytes")).toLongLong()
            == fastOptions().maximumCommandOutputBytes);
    QVERIFY(diagnostics.value(QStringLiteral("capturedStderrBytes")).toLongLong()
            == fastOptions().maximumCommandOutputBytes);
    QVERIFY(!manager.needsHdrRestore());
#endif
}

void DisplayManagerTest::replacementCancelsStaleCompletion()
{
#ifdef Q_OS_WIN
    QSKIP("Linux custom-command integration test");
#else
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString marker = directory.filePath(QStringLiteral("state.txt"));
    ConfigManager config;
    config.setLinuxHDRCommand(shellCommand(
        QStringLiteral("sleep 1; printf stale > %1").arg(marker)));
    DisplayManager manager(&config, fastOptions());
    QSignalSpy hdrSpy(&manager, &DisplayManager::hdrChangeFinished);

    manager.setHDRAsync(true);
    config.setLinuxHDRCommand(shellCommand(
        QStringLiteral("printf current > %1").arg(marker)));
    manager.setHDRAsync(false);

    QTRY_COMPARE_WITH_TIMEOUT(hdrSpy.count(), 1, 2000);
    QCOMPARE(hdrSpy.first().at(0).toBool(), false);
    QCOMPARE(hdrSpy.first().at(1).toBool(), true);
    QTRY_COMPARE_WITH_TIMEOUT(readFile(marker), QStringLiteral("current"), 1000);
    QTest::qWait(350);
    QCOMPARE(hdrSpy.count(), 1);
    QCOMPARE(readFile(marker), QStringLiteral("current"));
    QVERIFY(manager.diagnostics().value(QStringLiteral("operationsCanceled"))
                .toULongLong() >= 1);
#endif
}

void DisplayManagerTest::compatibleRefreshMultipleSkipsCommand()
{
#ifdef Q_OS_WIN
    QSKIP("Linux deterministic refresh-rate fixture");
#else
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString marker = directory.filePath(QStringLiteral("unexpected.txt"));
    ConfigManager config;
    config.setSkipRefreshRateOnCompatibleMultiple(true);
    config.setLinuxRefreshRateCommand(shellCommand(
        QStringLiteral("printf switched > %1").arg(marker)));
    DisplayManager manager(&config, fastOptions());
    QSignalSpy changeSpy(&manager, &DisplayManager::refreshRateChangeFinished);

    manager.setRefreshRateAsync(30.0);
    QTRY_COMPARE_WITH_TIMEOUT(changeSpy.count(), 1, 1000);
    QCOMPARE(changeSpy.first().at(1).toBool(), true);
    QVERIFY(manager.lastRefreshRateSwitchSkippedCompatibleMultiple());
    QVERIFY(!manager.lastRefreshRateSwitchChanged());
    QVERIFY(!manager.needsRefreshRestore());
    QVERIFY(!QFile::exists(marker));
#endif
}

void DisplayManagerTest::refreshCommandSubstitutesFractionalAndIntegerRates()
{
#ifdef Q_OS_WIN
    QSKIP("Linux custom-command integration test");
#else
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString marker = directory.filePath(QStringLiteral("rate.txt"));
    ConfigManager config;
    config.setLinuxRefreshRateCommand(shellCommand(
        QStringLiteral("printf {RATE}:{RATE_INT} > %1").arg(marker)));
    DisplayManager manager(&config, fastOptions());
    QSignalSpy changeSpy(&manager, &DisplayManager::refreshRateChangeFinished);
    QSignalSpy restoreSpy(&manager, &DisplayManager::refreshRateRestoreFinished);

    manager.setRefreshRateAsync(23.976);
    QTRY_COMPARE_WITH_TIMEOUT(changeSpy.count(), 1, 2000);
    QCOMPARE(changeSpy.first().at(1).toBool(), true);
    QCOMPARE(readFile(marker), QStringLiteral("23.976:24"));
    QVERIFY(manager.lastRefreshRateSwitchChanged());
    QVERIFY(manager.needsRefreshRestore());

    manager.restoreRefreshRateAsync();
    QTRY_COMPARE_WITH_TIMEOUT(restoreSpy.count(), 1, 2000);
    QCOMPARE(restoreSpy.first().at(0).toBool(), true);
    QVERIFY(!manager.needsRefreshRestore());
#endif
}

void DisplayManagerTest::destructionDoesNotWaitForCommand()
{
#ifdef Q_OS_WIN
    QSKIP("Linux custom-command integration test");
#else
    ConfigManager config;
    config.setLinuxHDRCommand(shellCommand(QStringLiteral("sleep 2")));
    auto *manager = new DisplayManager(&config, fastOptions());
    manager->setHDRAsync(true);
    config.setLinuxHDRCommand(shellCommand(QStringLiteral("true")));

    QElapsedTimer timer;
    timer.start();
    delete manager;
    QVERIFY2(timer.elapsed() < 50, "DisplayManager destruction blocked on a command");
    QTest::qWait(50);
#endif
}

QTEST_GUILESS_MAIN(DisplayManagerTest)

#include "DisplayManagerTest.moc"
