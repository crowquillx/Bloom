#include "player/LocalSeekPreview.h"

#include <QFile>
#include <QImage>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>
#include <limits>
#ifdef Q_OS_LINUX
#include <locale.h>
#endif

namespace {
Bloom::PlaybackDescriptor source(const QUrl &url)
{
    Bloom::PlaybackDescriptor descriptor;
    descriptor.media = { QStringLiteral("connection"), QStringLiteral("item") };
    descriptor.mediaVersionId = QStringLiteral("version");
    descriptor.stream.url = url;
    descriptor.stream.method = Bloom::PlaybackMethod::DirectPlay;
    descriptor.durationMs = 41000;
    return descriptor;
}

bool writeVideo(const QString &path, int lumaOffset = 0, int frameCount = 41)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    file.write("YUV4MPEG2 W64 H36 F1:1 Ip A1:1 C420jpeg\n");
    for (int frame = 0; frame < frameCount; ++frame) {
        file.write("FRAME\n");
        file.write(QByteArray(64 * 36, static_cast<char>(40 + frame * (frameCount > 70 ? 1 : 3) + lumaOffset)));
        file.write(QByteArray(64 * 36 / 2, static_cast<char>(128)));
    }
    return file.error() == QFile::NoError;
}

LocalSeekPreview::Options helperOptions(int timeoutMs = 10000)
{
    static QTemporaryDir cacheRoot;
    return { QString::fromUtf8(BLOOM_SEEK_PREVIEW_HELPER), timeoutMs, cacheRoot.path() };
}
}

class LocalSeekPreviewTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
#if !defined(BLOOM_HAS_LIBMPV)
        QSKIP("Local preview decoding requires libmpv.");
#endif
    }
    void generatesEntireTimelineWithoutSeeking();
    void rejectsUnsupportedSources();
    void changingSourceDropsOldImages();
    void decoderFailureIsNonFatal();
    void stalledStreamHasDeadline();
    void stalledStreamHasDeadline_data();
    void authenticatedHttpRanges();
    void rejectsHeaderInjection();
    void toneMapsHdrPreviews();
    void helperWorksWithUtf8Locale();
    void streamRefreshKeepsCachedFrames();
    void failedFramesKeepEarlierPreviews();
};

void LocalSeekPreviewTest::generatesEntireTimelineWithoutSeeking()
{
    QTemporaryDir directory;
    const QString video = directory.filePath(QStringLiteral("sample.y4m"));
    QVERIFY(writeVideo(video, 0, 121));
    LocalSeekPreview previews(helperOptions());
    QSignalSpy ready(&previews, &LocalSeekPreview::framesChanged);
    QSignalSpy failed(&previews, &LocalSeekPreview::unavailable);
    auto descriptor = source(QUrl::fromLocalFile(video));
    descriptor.durationMs = 121000;
    previews.setSource(descriptor, false);
    QVERIFY(previews.isAvailable());
    QVERIFY(previews.frameUrlAt(0).isEmpty());
    QList<int> arrivals;
    QSet<int> seen;
    connect(&previews, &LocalSeekPreview::framesChanged, this, [&] {
        for (int second = 0; second <= 120; second += 10) {
            if (!seen.contains(second) && !previews.frameUrlAt(second).isEmpty()) {
                seen.insert(second);
                arrivals.append(second);
            }
        }
    });
    previews.start();
    previews.start();
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 13, 15000);
    QCOMPARE(arrivals.first(), 0);
    QCOMPARE(arrivals.at(1), 120);
    QCOMPARE(failed.count(), 0);
    int previousBrightness = -1;
    for (int seconds = 0; seconds <= 120; seconds += 10) {
        const QString path = QUrl(previews.frameUrlAt(seconds)).toLocalFile();
        const QImage image(path);
        QVERIFY(!image.isNull());
        QCOMPARE(image.size(), QSize(320, 180));
        const int brightness = image.pixelColor(160, 90).red();
        QVERIFY2(previousBrightness < 0 || brightness > previousBrightness + 5,
                 qPrintable(QStringLiteral("Wrong frame at %1 s: %2").arg(seconds).arg(brightness)));
        previousBrightness = brightness;
    }
    QCOMPARE(previews.frameUrlAt(9.9), previews.frameUrlAt(0));
    QVERIFY(previews.frameUrlAt(-1).isEmpty());
    QVERIFY(previews.frameUrlAt(121).isEmpty());
    QVERIFY(previews.frameUrlAt(std::numeric_limits<double>::quiet_NaN()).isEmpty());
    const QString cached = QUrl(previews.frameUrlAt(120)).toLocalFile();
    previews.clear();
    QTRY_VERIFY(!QFileInfo::exists(cached));
    QVERIFY(!previews.isAvailable());
}

void LocalSeekPreviewTest::rejectsUnsupportedSources()
{
    LocalSeekPreview previews(helperOptions());
    auto descriptor = source(QUrl(QStringLiteral("https://media.test/video.mkv")));
    descriptor.stream.method = Bloom::PlaybackMethod::Transcode;
    previews.setSource(descriptor, false);
    QVERIFY(!previews.isAvailable());
    descriptor.stream.method = Bloom::PlaybackMethod::DirectPlay;
    descriptor.durationMs = 0;
    previews.setSource(descriptor, false);
    QVERIFY(!previews.isAvailable());
    descriptor.durationMs = 24LL * 60 * 60 * 1000 + 1;
    previews.setSource(descriptor, false);
    QVERIFY(!previews.isAvailable());
    descriptor = source(QUrl(QStringLiteral("https://user:password@media.test/video")));
    previews.setSource(descriptor, false);
    QVERIFY(!previews.isAvailable());
    descriptor = source(QUrl(QStringLiteral("exec://invalid")));
    previews.setSource(descriptor, false);
    QVERIFY(!previews.isAvailable());
}

void LocalSeekPreviewTest::changingSourceDropsOldImages()
{
    QTemporaryDir directory;
    const QString first = directory.filePath(QStringLiteral("first.y4m"));
    const QString second = directory.filePath(QStringLiteral("second.y4m"));
    QVERIFY(writeVideo(first));
    QVERIFY(writeVideo(second, 30));
    LocalSeekPreview previews(helperOptions());
    QSignalSpy ready(&previews, &LocalSeekPreview::framesChanged);
    QSignalSpy failed(&previews, &LocalSeekPreview::unavailable);
    previews.setSource(source(QUrl::fromLocalFile(first)), false);
    previews.start();
    QTRY_VERIFY_WITH_TIMEOUT(ready.count() >= 1, 15000);
    const QString old = QUrl(previews.frameUrlAt(0)).toLocalFile();
    const int oldBrightness = QImage(old).pixelColor(160, 90).red();
    auto replacement = source(QUrl::fromLocalFile(second));
    replacement.media.connectionId = QStringLiteral("another-connection");
    replacement.mediaVersionId = QStringLiteral("another-version");
    previews.setSource(replacement, false);
    QVERIFY(previews.frameUrlAt(0).isEmpty());
    ready.clear();
    previews.start();
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 5, 15000);
    QTRY_VERIFY(!QFileInfo::exists(old));
    QCOMPARE(failed.count(), 0);
    const QImage image(QUrl(previews.frameUrlAt(0)).toLocalFile());
    QVERIFY(image.pixelColor(160, 90).red() > oldBrightness + 20);
}

void LocalSeekPreviewTest::decoderFailureIsNonFatal()
{
    LocalSeekPreview previews(helperOptions());
    QSignalSpy failed(&previews, &LocalSeekPreview::unavailable);
    previews.setSource(source(QUrl::fromLocalFile(QStringLiteral("/missing/bloom-test-video.mkv"))), false);
    previews.start();
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 15000);
    QVERIFY(!previews.isAvailable());
    QVERIFY(previews.frameUrlAt(0).isEmpty());
}

void LocalSeekPreviewTest::stalledStreamHasDeadline()
{
    QFETCH(qint64, durationMs);
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    LocalSeekPreview previews(helperOptions(250));
    QSignalSpy failed(&previews, &LocalSeekPreview::unavailable);
    auto descriptor = source(QUrl(QStringLiteral("http://127.0.0.1:%1/stalled.mkv")
        .arg(server.serverPort())));
    descriptor.durationMs = durationMs;
    previews.setSource(descriptor, false);
    QTimer heartbeat;
    heartbeat.start(10);
    QSignalSpy ticks(&heartbeat, &QTimer::timeout);
    previews.start();
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 3000);
    QVERIFY(ticks.count() > 0);
    QVERIFY(!previews.isAvailable());
    previews.clear();
}

void LocalSeekPreviewTest::stalledStreamHasDeadline_data()
{
    QTest::addColumn<qint64>("durationMs");
    QTest::newRow("one preview") << qint64(1000);
    QTest::newRow("several previews") << qint64(41000);
}

void LocalSeekPreviewTest::authenticatedHttpRanges()
{
    QTemporaryDir directory;
    const QString video = directory.filePath(QStringLiteral("stream.y4m"));
    QVERIFY(writeVideo(video));
    QFile file(video);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray media = file.readAll();
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    int requests = 0;
    bool authenticated = true;
    connect(&server, &QTcpServer::newConnection, this, [&] {
        while (server.hasPendingConnections()) {
            QTcpSocket *socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                QByteArray input = socket->property("request").toByteArray() + socket->readAll();
                socket->setProperty("request", input);
                if (!input.contains("\r\n\r\n")) return;
                ++requests;
                authenticated = authenticated && input.contains("Authorization: Bearer test-secret,comma\r\n")
                    && input.startsWith("GET /video.y4m?opaque=preserved ");
                qint64 start = 0;
                qint64 end = media.size() - 1;
                const auto range = input.toLower().indexOf("\r\nrange: bytes=");
                if (range >= 0) {
                    const QByteArray spec = input.mid(range + 15).split('\r').first();
                    const auto parts = spec.split('-');
                    start = parts.first().toLongLong();
                    if (parts.size() > 1 && !parts.at(1).isEmpty()) end = qMin(end, parts.at(1).toLongLong());
                }
                if (start > end) {
                    socket->write("HTTP/1.1 416 Range Not Satisfiable\r\nConnection: close\r\n\r\n");
                } else {
                    const QByteArray body = media.mid(start, end - start + 1);
                    QByteArray response = range >= 0 ? "HTTP/1.1 206 Partial Content\r\n" : "HTTP/1.1 200 OK\r\n";
                    response += "Content-Type: application/octet-stream\r\nAccept-Ranges: bytes\r\nContent-Length: "
                        + QByteArray::number(body.size()) + "\r\n";
                    if (range >= 0) response += "Content-Range: bytes " + QByteArray::number(start)
                        + "-" + QByteArray::number(end) + "/" + QByteArray::number(media.size()) + "\r\n";
                    socket->write(response + "Connection: close\r\n\r\n" + body);
                }
                socket->disconnectFromHost();
            });
        }
    });
    LocalSeekPreview previews(helperOptions());
    auto descriptor = source(QUrl(QStringLiteral("http://127.0.0.1:%1/video.y4m?opaque=preserved")
        .arg(server.serverPort())));
    descriptor.stream.headers.insert(QStringLiteral("Authorization"), QStringLiteral("Bearer test-secret,comma"));
    QSignalSpy ready(&previews, &LocalSeekPreview::framesChanged);
    QSignalSpy failed(&previews, &LocalSeekPreview::unavailable);
    previews.setSource(descriptor, false);
    previews.start();
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 5, 15000);
    QCOMPARE(failed.count(), 0);
    QVERIFY(requests > 0);
    QVERIFY(authenticated);
}

void LocalSeekPreviewTest::rejectsHeaderInjection()
{
    LocalSeekPreview previews(helperOptions());
    auto descriptor = source(QUrl(QStringLiteral("https://media.test/video")));
    descriptor.stream.headers.insert(QStringLiteral("Authorization"), QStringLiteral("value\r\nInjected: yes"));
    QSignalSpy failed(&previews, &LocalSeekPreview::unavailable);
    previews.setSource(descriptor, false);
    previews.start();
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 15000);
}

void LocalSeekPreviewTest::toneMapsHdrPreviews()
{
    auto descriptor = source(QUrl::fromLocalFile(QString::fromUtf8(BLOOM_SEEK_PREVIEW_HDR_FIXTURE)));
    descriptor.durationMs = 11000;
    LocalSeekPreview unmapped(helperOptions());
    LocalSeekPreview toneMapped(helperOptions());
    QSignalSpy rawReady(&unmapped, &LocalSeekPreview::framesChanged);
    QSignalSpy mappedReady(&toneMapped, &LocalSeekPreview::framesChanged);
    unmapped.setSource(descriptor, false);
    toneMapped.setSource(descriptor, true);
    unmapped.start();
    toneMapped.start();
    QTRY_COMPARE_WITH_TIMEOUT(rawReady.count(), 2, 15000);
    QTRY_COMPARE_WITH_TIMEOUT(mappedReady.count(), 2, 15000);
    const QColor raw = QImage(QUrl(unmapped.frameUrlAt(0)).toLocalFile()).pixelColor(160, 90);
    const QColor mapped = QImage(QUrl(toneMapped.frameUrlAt(0)).toLocalFile()).pixelColor(160, 90);
    QVERIFY(qAbs(mapped.red() - raw.red()) > 10);
    QVERIFY(qAbs(mapped.red() - mapped.green()) <= 2);
    QVERIFY(qAbs(mapped.red() - mapped.blue()) <= 2);
}

void LocalSeekPreviewTest::helperWorksWithUtf8Locale()
{
#ifdef Q_OS_LINUX
    locale_t locale = newlocale(LC_NUMERIC_MASK, "en_US.UTF-8", nullptr);
    QVERIFY2(locale != nullptr, "en_US.UTF-8 must be available through LOCALE_ARCHIVE for this regression test");
    freelocale(locale);
    QProcess helper;
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("LC_ALL"), QStringLiteral("en_US.UTF-8"));
    helper.setProcessEnvironment(environment);
    QSignalSpy stopped(&helper, qOverload<int, QProcess::ExitStatus>(&QProcess::finished));
    helper.start(QString::fromUtf8(BLOOM_SEEK_PREVIEW_HELPER), { QStringLiteral("--self-test") });
    QTRY_COMPARE_WITH_TIMEOUT(stopped.count(), 1, 15000);
    QCOMPARE(helper.exitCode(), 0);
    QVERIFY(helper.readAllStandardOutput().contains("ready 0\n"));
#else
    QSKIP("This regression concerns the Unix QCoreApplication locale reset.");
#endif
}

void LocalSeekPreviewTest::streamRefreshKeepsCachedFrames()
{
    QTemporaryDir directory;
    const QString first = directory.filePath(QStringLiteral("original.y4m"));
    const QString refreshed = directory.filePath(QStringLiteral("refreshed.y4m"));
    QVERIFY(writeVideo(first));
    QVERIFY(writeVideo(refreshed));
    LocalSeekPreview previews(helperOptions());
    QSignalSpy ready(&previews, &LocalSeekPreview::framesChanged);
    QSignalSpy completed(&previews, &LocalSeekPreview::finished);
    bool changed = false;
    QString firstUrl;
    connect(&previews, &LocalSeekPreview::framesChanged, this, [&] {
        if (changed || previews.frameUrlAt(0).isEmpty()) return;
        changed = true;
        firstUrl = previews.frameUrlAt(0);
        previews.updateStream(source(QUrl::fromLocalFile(refreshed)).stream);
    });
    previews.setSource(source(QUrl::fromLocalFile(first)), false);
    previews.start();
    QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 1, 15000);
    QVERIFY(completed.first().first().toBool());
    QCOMPARE(ready.count(), 5);
    QCOMPARE(previews.frameUrlAt(0), firstUrl);
    const QString lastUrl = previews.frameUrlAt(40);
    previews.updateStream(source(QUrl::fromLocalFile(QStringLiteral("/missing/audio-only-replacement"))).stream);
    QCOMPARE(previews.frameUrlAt(40), lastUrl);
    QCOMPARE(completed.count(), 1);
}

void LocalSeekPreviewTest::failedFramesKeepEarlierPreviews()
{
    QTemporaryDir directory;
    const QString video = directory.filePath(QStringLiteral("short.y4m"));
    QVERIFY(writeVideo(video));
    LocalSeekPreview previews(helperOptions(500));
    QSignalSpy completed(&previews, &LocalSeekPreview::finished);
    QSignalSpy unavailable(&previews, &LocalSeekPreview::unavailable);
    auto descriptor = source(QUrl::fromLocalFile(video));
    descriptor.durationMs = 121000;
    previews.setSource(descriptor, false);
    previews.start();
    QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 1, 15000);
    QVERIFY(!completed.first().first().toBool());
    QCOMPARE(unavailable.count(), 0);
    QVERIFY(previews.isAvailable());
    QVERIFY(!previews.frameUrlAt(0).isEmpty());
    QVERIFY(!QImage(QUrl(previews.frameUrlAt(0)).toLocalFile()).isNull());
}

QTEST_GUILESS_MAIN(LocalSeekPreviewTest)
#include "LocalSeekPreviewTest.moc"
