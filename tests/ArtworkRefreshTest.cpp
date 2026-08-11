#include <QtTest/QtTest>
#include <QtConcurrent>

#include <memory>
#include <QBuffer>
#include <QHostAddress>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QNetworkRequest>
#include <QPointer>
#include <QQuickImageResponse>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrlQuery>
#include <QUuid>

#include "network/AuthenticationService.h"
#include "network/HttpTransport.h"
#include "network/LibraryService.h"
#include "models/MediaModels.h"
#include "providers/IArtworkProvider.h"
#include "providers/silo/SiloArtworkProvider.h"
#include "providers/jellyfin/JellyfinArtworkProvider.h"
#include "providers/jellyfin/JellyfinProviderAdapter.h"
#include "ui/ImageCacheProvider.h"
#include "utils/ConfigManager.h"

#include "TestConfigIsolation.h"

namespace {

class ScriptedHttpServer final : public QTcpServer
{
public:
    QList<int> statuses;
    QList<QByteArray> bodies;
    QList<int> delaysMs;
    QStringList requestTargets;

    bool start()
    {
        connect(this, &QTcpServer::newConnection, this, [this]() {
            while (hasPendingConnections()) {
                QTcpSocket *socket = nextPendingConnection();
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket]() {
                    if (socket->property("responded").toBool()) {
                        socket->readAll();
                        return;
                    }
                    QByteArray request = socket->property("requestBuffer").toByteArray();
                    request += socket->readAll();
                    if (!request.contains(QByteArrayLiteral("\r\n\r\n"))) {
                        socket->setProperty("requestBuffer", request);
                        return;
                    }
                    socket->setProperty("responded", true);
                    const qsizetype lineEnd = request.indexOf("\r\n");
                    const QList<QByteArray> requestLine = request.left(lineEnd).split(' ');
                    if (requestLine.size() >= 2) {
                        requestTargets.append(QString::fromUtf8(requestLine.at(1)));
                    }
                    const int status = statuses.isEmpty() ? 500 : statuses.takeFirst();
                    const QByteArray body = bodies.isEmpty() ? QByteArray() : bodies.takeFirst();
                    const int delayMs = delaysMs.isEmpty() ? 0 : delaysMs.takeFirst();
                    const QByteArray reason = status == 200
                        ? QByteArrayLiteral("OK")
                        : (status == 401
                               ? QByteArrayLiteral("Unauthorized")
                               : (status == 403
                                      ? QByteArrayLiteral("Forbidden")
                                      : QByteArrayLiteral("Internal Server Error")));
                    const QPointer<QTcpSocket> socketGuard(socket);
                    const auto respond = [socketGuard, status, reason, body]() {
                        if (!socketGuard) {
                            return;
                        }
                        socketGuard->write(QByteArrayLiteral("HTTP/1.1 ")
                                           + QByteArray::number(status)
                                           + QByteArrayLiteral(" ") + reason
                                           + QByteArrayLiteral("\r\nContent-Type: image/png\r\nContent-Length: ")
                                           + QByteArray::number(body.size())
                                           + QByteArrayLiteral("\r\nConnection: close\r\n\r\n")
                                           + body);
                        socketGuard->disconnectFromHost();
                    };
                    if (delayMs > 0) {
                        QTimer::singleShot(delayMs, this, respond);
                    } else {
                        respond();
                    }
                });
            }
        });
        return listen(QHostAddress::LocalHost, 0);
    }

    QUrl url(const QString &target) const
    {
        return QUrl(QStringLiteral("http://127.0.0.1:%1%2")
                        .arg(serverPort()).arg(target));
    }
};

class RefreshingArtworkProvider final : public IArtworkProvider
{
public:
    explicit RefreshingArtworkProvider(ScriptedHttpServer *server)
        : m_server(server)
    {
    }

    std::optional<QNetworkRequest> resolveArtwork(
        const Bloom::ArtworkRef &artwork) const override
    {
        resolvedArtwork = artwork;
        return QNetworkRequest(m_server->url(
            QStringLiteral("/old.jpg?X-Amz-Signature=expired&X-Amz-Expires=1")));
    }

    void refreshArtwork(const Bloom::ArtworkRef &artwork,
                        RefreshCallback callback) const override
    {
        ++refreshCount;
        refreshedArtwork = artwork;
        callback(QNetworkRequest(m_server->url(
            QStringLiteral("/new.jpg?X-Amz-Signature=fresh&X-Amz-Expires=900"))));
    }

    mutable int refreshCount = 0;
    mutable Bloom::ArtworkRef resolvedArtwork;
    mutable Bloom::ArtworkRef refreshedArtwork;

private:
    ScriptedHttpServer *m_server;
};

class MissingArtworkProvider final : public IArtworkProvider
{
public:
    std::optional<QNetworkRequest> resolveArtwork(
        const Bloom::ArtworkRef &) const override
    {
        ++resolveCount;
        return std::nullopt;
    }

    void refreshArtwork(const Bloom::ArtworkRef &,
                        RefreshCallback callback) const override
    {
        ++refreshCount;
        if (callback) {
            callback(std::nullopt);
        }
    }

    mutable int resolveCount = 0;
    mutable int refreshCount = 0;
};

class HangingArtworkProvider final : public IArtworkProvider
{
public:
    explicit HangingArtworkProvider(ScriptedHttpServer *server)
        : m_server(server)
    {
    }

    std::optional<QNetworkRequest> resolveArtwork(
        const Bloom::ArtworkRef &) const override
    {
        return QNetworkRequest(m_server->url(QStringLiteral("/expired.jpg")));
    }

    void refreshArtwork(const Bloom::ArtworkRef &,
                        RefreshCallback) const override
    {
        ++refreshCount;
    }

    mutable int refreshCount = 0;

private:
    ScriptedHttpServer *m_server;
};

class ExposedJellyfinAuthenticationService final : public AuthenticationService
{
public:
    ExposedJellyfinAuthenticationService(HttpTransport *transport,
                                         IProviderAdapter *adapter)
        : AuthenticationService(nullptr, transport, adapter)
    {
    }

    void seed()
    {
        seedSession(QStringLiteral("https://jellyfin.example.test"),
                    QStringLiteral("user-1"),
                    QStringLiteral("token-1"),
                    QStringLiteral("Alice"));
    }
};

Bloom::ArtworkRef artworkRef()
{
    Bloom::ArtworkRef artwork;
    artwork.connectionId = QStringLiteral("connection-silo");
    artwork.itemId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    artwork.kind = Bloom::ArtworkKind::Primary;
    artwork.ownerKind = Bloom::ArtworkOwnerKind::MediaItem;
    artwork.requestedWidth = 640;
    return artwork;
}

QByteArray pngBytes(const QSize &size = QSize(8, 8))
{
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor(QStringLiteral("#3daee9")));
    QByteArray bytes;
    QBuffer buffer(&bytes);
    Q_ASSERT(buffer.open(QIODevice::WriteOnly));
    QImageWriter writer(&buffer, QByteArrayLiteral("png"));
    const bool written = writer.write(image);
    Q_ASSERT_X(written, "pngBytes", qPrintable(writer.errorString()));
    return bytes;
}

} // namespace

class ArtworkRefreshTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void webpDecoderIsAvailable();
    void widthAdjustedReferenceRetainsTransientSource();
    void transientSourcesSurviveLargeCatalogAndClearExplicitly();
    void missingArtworkDegradesWithoutRefresh();
    void tokenFreeCacheMissRetainsTransientSourceUrl();
    void signedUrlIsNotPartOfCacheIdentity();
    void identicalRequestsCoalesceAcrossCancellation();
    void concurrentRequestRegistrationCoalesces();
    void terminalSignalAllowsImmediateResubscribe();
    void lastSubscriberCancellationClearsPendingState();
    void clearCancelsJobsWithoutStaleWrites();
    void clearCancelsRoundedGenerationWithoutStaleWrites();
    void networkDeadlineIsBounded();
    void artworkRefreshDeadlineIsBounded();
    void networkResponseSizeIsBounded();
    void decodedMemoryIsBounded();
    void authorizationFailureRefreshesExactlyOnce_data();
    void authorizationFailureRefreshesExactlyOnce();
    void jellyfinRefreshKeepsExistingResolvedRequest();
    void siloRefreshCompletesAfterAuthenticationServiceDestruction();

private:
    QTemporaryDir m_temporaryDirectory;
    std::unique_ptr<ScopedConfigIsolation> m_configIsolation;
};

void ArtworkRefreshTest::initTestCase()
{
    QVERIFY(m_temporaryDirectory.isValid());
    m_configIsolation = std::make_unique<ScopedConfigIsolation>(
        m_temporaryDirectory.path());
}

void ArtworkRefreshTest::webpDecoderIsAvailable()
{
    const QList<QByteArray> formats = QImageReader::supportedImageFormats();
    QVERIFY2(formats.contains(QByteArrayLiteral("webp")),
             "Bloom's runtime must ship Qt's WebP image-format plugin");
}

void ArtworkRefreshTest::widthAdjustedReferenceRetainsTransientSource()
{
    Bloom::ArtworkRef original = artworkRef();
    original.ownerKind = Bloom::ArtworkOwnerKind::Person;
    original.requestedWidth = 0;
    original.sourceUrl = QStringLiteral(
        "https://images.example.test/person.webp?X-Amz-Signature=transient");
    const QVariantMap emitted = original.toVariantMap();

    LibraryService service(nullptr);
    const QString cachedUrl = service.getCachedArtworkUrlFromRef(emitted, 640);
    const QString prefix = QStringLiteral("image://cached/");
    QVERIFY(cachedUrl.startsWith(prefix));
    const QString adjustedKey = QUrl::fromPercentEncoding(
        cachedUrl.mid(prefix.size()).toUtf8());
    const Bloom::ArtworkRef adjusted = Bloom::ArtworkRef::fromCacheKey(adjustedKey);

    QVERIFY(adjusted.isValid());
    QCOMPARE(adjusted.ownerKind, Bloom::ArtworkOwnerKind::Person);
    QCOMPARE(adjusted.requestedWidth, 640);
    QCOMPARE(Bloom::ArtworkRef::transientSourceUrlForCacheKey(adjustedKey),
             original.sourceUrl);
}

void ArtworkRefreshTest::transientSourcesSurviveLargeCatalogAndClearExplicitly()
{
    Bloom::ArtworkRef first = artworkRef();
    first.itemId = QStringLiteral("item-0");
    first.sourceUrl = QStringLiteral("https://images.example.test/item-0.webp");
    const QString firstKey = first.cacheKey();

    for (int index = 1; index < 1024; ++index) {
        Bloom::ArtworkRef artwork = first;
        artwork.itemId = QStringLiteral("item-%1").arg(index);
        artwork.sourceUrl = QStringLiteral("https://images.example.test/item-%1.webp")
                                .arg(index);
        artwork.cacheKey();
    }

    QCOMPARE(Bloom::ArtworkRef::transientSourceUrlForCacheKey(firstKey),
             first.sourceUrl);
    Bloom::ArtworkRef::clearTransientSourceUrls();
    QVERIFY(Bloom::ArtworkRef::transientSourceUrlForCacheKey(firstKey).isEmpty());
}

void ArtworkRefreshTest::missingArtworkDegradesWithoutRefresh()
{
    MissingArtworkProvider artworkProvider;
    ImageCacheProvider cache(1, &artworkProvider);
    cache.setRoundedPreprocessEnabled(false);

    const Bloom::ArtworkRef artwork = artworkRef();
    QPointer<QQuickImageResponse> response(
        cache.requestImageResponse(artwork.cacheKey(), QSize()));
    QVERIFY(response);
    QSignalSpy finishedSpy(response, &QQuickImageResponse::finished);
    QVERIFY(finishedSpy.isValid());

    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 3000);
    QCOMPARE(artworkProvider.resolveCount, 1);
    QCOMPARE(artworkProvider.refreshCount, 0);
    QVERIFY(!response->errorString().isEmpty());

    delete response;
}

void ArtworkRefreshTest::tokenFreeCacheMissRetainsTransientSourceUrl()
{
    ScriptedHttpServer server;
    server.statuses = {500};
    QVERIFY(server.start());

    RefreshingArtworkProvider artworkProvider(&server);
    ImageCacheProvider cache(1, &artworkProvider);
    cache.setRoundedPreprocessEnabled(false);

    Bloom::ArtworkRef artwork = artworkRef();
    artwork.sourceUrl = server.url(
        QStringLiteral("/artwork/poster.jpg?X-Amz-Signature=transient")).toString();
    const QVariantMap emitted = artwork.toVariantMap();
    QVERIFY(!emitted.contains(QStringLiteral("sourceUrl")));
    QVERIFY(!artwork.cacheKey().contains(QStringLiteral("X-Amz-Signature")));

    QPointer<QQuickImageResponse> response(
        cache.requestImageResponse(artwork.cacheKey(), QSize()));
    QVERIFY(response);
    QCOMPARE(artworkProvider.resolvedArtwork.sourceUrl, artwork.sourceUrl);

    QSignalSpy finishedSpy(response, &QQuickImageResponse::finished);
    QVERIFY(finishedSpy.isValid());
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 3000);
    QVERIFY(!response->errorString().isEmpty());
    delete response;
}

void ArtworkRefreshTest::signedUrlIsNotPartOfCacheIdentity()
{
    Bloom::ArtworkRef first = artworkRef();
    first.sourceUrl = QStringLiteral(
        "https://images.example.test/poster.jpg?X-Amz-Signature=old&X-Amz-Expires=1");
    Bloom::ArtworkRef refreshed = first;
    refreshed.sourceUrl = QStringLiteral(
        "https://images.example.test/poster.jpg?X-Amz-Signature=new&X-Amz-Expires=900");

    QCOMPARE(first.cacheKey(), refreshed.cacheKey());
    QCOMPARE(first, refreshed);
    QVERIFY(!first.cacheKey().contains(QStringLiteral("Signature")));
    QVERIFY(!first.cacheKey().contains(QStringLiteral("images.example.test")));

    const Bloom::ArtworkRef decoded = Bloom::ArtworkRef::fromCacheKey(first.cacheKey());
    QVERIFY(decoded.isValid());
    QCOMPARE(decoded.connectionId, first.connectionId);
    QCOMPARE(decoded.itemId, first.itemId);
    QCOMPARE(decoded.kind, first.kind);
    QCOMPARE(decoded.ownerKind, first.ownerKind);
    QCOMPARE(decoded.requestedWidth, first.requestedWidth);
    QVERIFY(decoded.sourceUrl.isEmpty());
    QCOMPARE(Bloom::artworkOwnerKindFromName(QStringLiteral("mediaItem")),
             Bloom::ArtworkOwnerKind::MediaItem);
    QCOMPARE(Bloom::artworkOwnerKindFromName(QStringLiteral("unknown-owner")),
             Bloom::ArtworkOwnerKind::MediaItem);
}

void ArtworkRefreshTest::identicalRequestsCoalesceAcrossCancellation()
{
    ScriptedHttpServer server;
    server.statuses = {200};
    server.bodies = {pngBytes()};
    QVERIFY(server.start());

    ImageCacheProvider cache(1);
    cache.setDefaultRoundedParams(16, QSize(32, 32));
    const QString identity = server.url(QStringLiteral("/coalesced.png")).toString();
    QPointer<QQuickImageResponse> cancelled(
        cache.requestImageResponse(identity, QSize()));
    QPointer<QQuickImageResponse> active(
        cache.requestImageResponse(identity, QSize()));
    QVERIFY(cancelled);
    QVERIFY(active);
    QSignalSpy cancelledSpy(cancelled, &QQuickImageResponse::finished);
    QSignalSpy activeSpy(active, &QQuickImageResponse::finished);
    cancelled->cancel();

    QTRY_COMPARE_WITH_TIMEOUT(activeSpy.count(), 1, 3000);
    QCOMPARE(cancelledSpy.count(), 1);
    QVERIFY(cancelled->errorString().contains(QStringLiteral("cancelled")));
    QVERIFY(active->errorString().isEmpty());
    QCOMPARE(server.requestTargets.size(), 1);

    const QVariantMap stats = cache.cacheStats();
    QCOMPARE(stats.value(QStringLiteral("networkLoads")).toULongLong(), quint64(1));
    QCOMPARE(stats.value(QStringLiteral("coalescedRequests")).toULongLong(), quint64(1));
    QCOMPARE(stats.value(QStringLiteral("decodeAttempts")).toULongLong(), quint64(1));
    QCOMPARE(stats.value(QStringLiteral("decodedImages")).toULongLong(), quint64(1));

    QPointer<QQuickImageResponse> cached(
        cache.requestImageResponse(identity, QSize()));
    QSignalSpy cachedSpy(cached, &QQuickImageResponse::finished);
    QTRY_COMPARE_WITH_TIMEOUT(cachedSpy.count(), 1, 1000);
    QCOMPARE(server.requestTargets.size(), 1);
    QCOMPARE(cache.cacheStats().value(QStringLiteral("imageHits")).toULongLong(),
             quint64(1));

    for (int index = 0; index < 8; ++index) {
        cache.requestRoundedImage(identity, 16, 32, 32);
    }
    QTRY_COMPARE_WITH_TIMEOUT(
        cache.cacheStats().value(QStringLiteral("roundedGenerations")).toULongLong(),
        quint64(1), 3000);

    delete cancelled;
    delete active;
    delete cached;
}

void ArtworkRefreshTest::concurrentRequestRegistrationCoalesces()
{
    ScriptedHttpServer server;
    server.statuses = {200};
    server.bodies = {pngBytes()};
    server.delaysMs = {100};
    QVERIFY(server.start());

    ImageCacheProvider cache(1);
    const QString identity = server.url(QStringLiteral("/concurrent.png")).toString();
    auto firstFuture = QtConcurrent::run([&cache, identity]() {
        return cache.requestImageResponse(identity, QSize(16, 16));
    });
    auto secondFuture = QtConcurrent::run([&cache, identity]() {
        return cache.requestImageResponse(identity, QSize(16, 16));
    });
    QTRY_VERIFY_WITH_TIMEOUT(firstFuture.isFinished() && secondFuture.isFinished(), 1000);

    QPointer<QQuickImageResponse> first(firstFuture.result());
    QPointer<QQuickImageResponse> second(secondFuture.result());
    QVERIFY(first);
    QVERIFY(second);
    QSignalSpy firstSpy(first, &QQuickImageResponse::finished);
    QSignalSpy secondSpy(second, &QQuickImageResponse::finished);
    QTRY_COMPARE_WITH_TIMEOUT(firstSpy.count(), 1, 3000);
    QTRY_COMPARE_WITH_TIMEOUT(secondSpy.count(), 1, 3000);
    QCOMPARE(server.requestTargets.size(), 1);
    QCOMPARE(cache.cacheStats().value(QStringLiteral("coalescedRequests")).toULongLong(),
             quint64(1));

    delete first;
    delete second;
}

void ArtworkRefreshTest::terminalSignalAllowsImmediateResubscribe()
{
    ScriptedHttpServer server;
    server.statuses = {200};
    server.bodies = {pngBytes()};
    QVERIFY(server.start());

    ImageCacheProvider cache(1);
    const QString identity = server.url(QStringLiteral("/reentrant.png")).toString();
    QPointer<QQuickImageResponse> first(
        cache.requestImageResponse(identity, QSize()));
    QPointer<QQuickImageResponse> nested;
    int nestedFinished = 0;
    connect(first, &QQuickImageResponse::finished, this, [&]() {
        nested = cache.requestImageResponse(identity, QSize());
        connect(nested, &QQuickImageResponse::finished, this, [&]() {
            ++nestedFinished;
        });
    });

    QTRY_COMPARE_WITH_TIMEOUT(nestedFinished, 1, 3000);
    QVERIFY(nested);
    QVERIFY(nested->errorString().isEmpty());
    QCOMPARE(server.requestTargets.size(), 1);
    QCOMPARE(cache.cacheStats().value(QStringLiteral("inFlightImageJobs")).toInt(), 0);

    delete first;
    delete nested;
}

void ArtworkRefreshTest::lastSubscriberCancellationClearsPendingState()
{
    ScriptedHttpServer server;
    server.statuses = {200, 200};
    server.bodies = {pngBytes(), pngBytes()};
    server.delaysMs = {250, 0};
    QVERIFY(server.start());

    ImageCacheProvider cache(1);
    const QString identity = server.url(QStringLiteral("/cancelled.png")).toString();
    QCOMPARE(cache.requestRoundedImage(identity, 16, 32, 32), QString());
    QPointer<QQuickImageResponse> cancelled(
        cache.requestImageResponse(identity, QSize()));
    QTRY_COMPARE_WITH_TIMEOUT(server.requestTargets.size(), 1, 1000);
    cancelled->cancel();
    QTRY_COMPARE_WITH_TIMEOUT(
        cache.cacheStats().value(QStringLiteral("inFlightImageJobs")).toInt(), 0, 1000);
    QCOMPARE(cache.cacheStats().value(QStringLiteral("pendingRoundedRequests")).toInt(), 0);
    delete cancelled;

    QPointer<QQuickImageResponse> retry(
        cache.requestImageResponse(identity, QSize()));
    QSignalSpy retrySpy(retry, &QQuickImageResponse::finished);
    QTRY_COMPARE_WITH_TIMEOUT(retrySpy.count(), 1, 3000);
    QVERIFY(retry->errorString().isEmpty());
    QCOMPARE(server.requestTargets.size(), 2);
    QCOMPARE(cache.cacheStats().value(QStringLiteral("inFlightImageJobs")).toInt(), 0);
    delete retry;
}

void ArtworkRefreshTest::clearCancelsJobsWithoutStaleWrites()
{
    ScriptedHttpServer server;
    server.statuses = {200, 200};
    server.bodies = {pngBytes(), pngBytes()};
    server.delaysMs = {200, 0};
    QVERIFY(server.start());

    ImageCacheProvider cache(1);
    cache.setRoundedPreprocessEnabled(false);
    const QString identity = server.url(QStringLiteral("/clear-active.png")).toString();
    QPointer<QQuickImageResponse> cleared(
        cache.requestImageResponse(identity, QSize()));
    QSignalSpy clearedSpy(cleared, &QQuickImageResponse::finished);
    QTRY_COMPARE_WITH_TIMEOUT(server.requestTargets.size(), 1, 1000);

    cache.clearCache();
    QCOMPARE(clearedSpy.count(), 1);
    QVERIFY(cleared->errorString().contains(QStringLiteral("shutting down")));
    QCOMPARE(cache.cacheStats().value(QStringLiteral("inFlightImageJobs")).toInt(), 0);
    QCOMPARE(cache.currentCacheSize(), qint64(0));

    QPointer<QQuickImageResponse> retry(
        cache.requestImageResponse(identity, QSize()));
    QSignalSpy retrySpy(retry, &QQuickImageResponse::finished);
    QTRY_COMPARE_WITH_TIMEOUT(retrySpy.count(), 1, 3000);
    QVERIFY(retry->errorString().isEmpty());
    QCOMPARE(server.requestTargets.size(), 2);
    QTest::qWait(250);
    QCOMPARE(cache.cacheStats().value(QStringLiteral("writes")).toULongLong(), quint64(1));
    QVERIFY(cache.currentCacheSize() > 0);

    delete cleared;
    delete retry;
}

void ArtworkRefreshTest::clearCancelsRoundedGenerationWithoutStaleWrites()
{
    ScriptedHttpServer server;
    server.statuses = {200};
    server.bodies = {pngBytes(QSize(512, 512))};
    QVERIFY(server.start());

    ImageCacheProvider cache(20);
    cache.setRoundedPreprocessEnabled(false);
    const QString identity = server.url(QStringLiteral("/rounded-clear.png")).toString();
    QPointer<QQuickImageResponse> base(
        cache.requestImageResponse(identity, QSize()));
    QSignalSpy baseSpy(base, &QQuickImageResponse::finished);
    QTRY_COMPARE_WITH_TIMEOUT(baseSpy.count(), 1, 3000);
    QVERIFY(base->errorString().isEmpty());
    QVERIFY(cache.currentCacheSize() > 0);

    cache.setRoundedPreprocessEnabled(true);
    QSignalSpy roundedSpy(&cache, &ImageCacheProvider::roundedImageReady);
    QCOMPARE(cache.requestRoundedImage(identity, 24, 1536, 1536), QString());
    QTRY_COMPARE_WITH_TIMEOUT(
        cache.cacheStats().value(QStringLiteral("inFlightRoundedJobs")).toInt(), 1, 1000);
    QTest::qWait(10);
    cache.clearCache();
    QTRY_COMPARE_WITH_TIMEOUT(
        cache.cacheStats().value(QStringLiteral("activeRoundedTasks")).toULongLong(),
        quint64(0), 3000);

    QCOMPARE(cache.currentCacheSize(), qint64(0));
    QCOMPARE(roundedSpy.count(), 0);
    QCOMPARE(cache.cacheStats().value(QStringLiteral("inFlightRoundedJobs")).toInt(), 0);
    delete base;
}

void ArtworkRefreshTest::networkDeadlineIsBounded()
{
    ScriptedHttpServer server;
    server.statuses = {200};
    server.bodies = {pngBytes()};
    server.delaysMs = {250};
    QVERIFY(server.start());

    ImageRequestLimits limits;
    limits.networkDeadlineMs = 40;
    ImageCacheProvider cache(1, nullptr, limits);
    QPointer<QQuickImageResponse> response(cache.requestImageResponse(
        server.url(QStringLiteral("/slow.png")).toString(), QSize()));
    QSignalSpy finishedSpy(response, &QQuickImageResponse::finished);

    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 1000);
    QVERIFY(response->errorString().contains(QStringLiteral("timed out")));
    QCOMPARE(server.requestTargets.size(), 1);
    delete response;
}

void ArtworkRefreshTest::artworkRefreshDeadlineIsBounded()
{
    ScriptedHttpServer server;
    server.statuses = {401};
    QVERIFY(server.start());

    HangingArtworkProvider artworkProvider(&server);
    ImageRequestLimits limits;
    limits.networkDeadlineMs = 40;
    ImageCacheProvider cache(1, &artworkProvider, limits);
    cache.setRoundedPreprocessEnabled(false);
    const Bloom::ArtworkRef artwork = artworkRef();
    QPointer<QQuickImageResponse> response(
        cache.requestImageResponse(artwork.cacheKey(), QSize()));
    QSignalSpy finishedSpy(response, &QQuickImageResponse::finished);

    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 1000);
    QCOMPARE(artworkProvider.refreshCount, 1);
    QVERIFY(response->errorString().contains(QStringLiteral("timed out")));
    QCOMPARE(cache.cacheStats().value(QStringLiteral("inFlightImageJobs")).toInt(), 0);
    delete response;
}

void ArtworkRefreshTest::networkResponseSizeIsBounded()
{
    ScriptedHttpServer server;
    server.statuses = {200, 200};
    server.bodies = {pngBytes(), pngBytes()};
    QVERIFY(server.start());

    ImageRequestLimits limits;
    limits.maximumNetworkBytes = 32;
    ImageCacheProvider cache(1, nullptr, limits);
    QPointer<QQuickImageResponse> response(cache.requestImageResponse(
        server.url(QStringLiteral("/oversize.png")).toString(), QSize()));
    QSignalSpy finishedSpy(response, &QQuickImageResponse::finished);

    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 1000);
    QVERIFY(response->errorString().contains(QStringLiteral("size limit")));
    QCOMPARE(cache.cacheStats().value(QStringLiteral("writes")).toULongLong(),
             quint64(0));
    delete response;

    QPointer<QQuickImageResponse> retry(cache.requestImageResponse(
        server.url(QStringLiteral("/oversize.png")).toString(), QSize()));
    QSignalSpy retrySpy(retry, &QQuickImageResponse::finished);
    QTRY_COMPARE_WITH_TIMEOUT(retrySpy.count(), 1, 1000);
    QCOMPARE(server.requestTargets.size(), 2);
    delete retry;
}

void ArtworkRefreshTest::decodedMemoryIsBounded()
{
    ScriptedHttpServer server;
    server.statuses = {200};
    server.bodies = {pngBytes(QSize(8, 8))};
    QVERIFY(server.start());

    ImageRequestLimits limits;
    limits.maximumDecodedBytes = 64;
    ImageCacheProvider cache(1, nullptr, limits);
    QPointer<QQuickImageResponse> response(cache.requestImageResponse(
        server.url(QStringLiteral("/decoded-limit.png")).toString(), QSize()));
    QSignalSpy finishedSpy(response, &QQuickImageResponse::finished);

    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 1000);
    QVERIFY(response->errorString().contains(QStringLiteral("memory limit")));
    QCOMPARE(cache.cacheStats().value(QStringLiteral("writes")).toULongLong(),
             quint64(0));
    delete response;
}

void ArtworkRefreshTest::authorizationFailureRefreshesExactlyOnce_data()
{
    QTest::addColumn<int>("status");
    QTest::newRow("unauthorized") << 401;
    QTest::newRow("forbidden") << 403;
}

void ArtworkRefreshTest::authorizationFailureRefreshesExactlyOnce()
{
    QFETCH(int, status);

    ScriptedHttpServer server;
    server.statuses = {status, status};
    QVERIFY(server.start());

    RefreshingArtworkProvider artworkProvider(&server);
    ImageCacheProvider cache(1, &artworkProvider);
    cache.setRoundedPreprocessEnabled(false);

    const Bloom::ArtworkRef artwork = artworkRef();
    QPointer<QQuickImageResponse> response(
        cache.requestImageResponse(artwork.cacheKey(), QSize()));
    QVERIFY(response);
    QSignalSpy finishedSpy(response, &QQuickImageResponse::finished);
    QVERIFY(finishedSpy.isValid());

    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 3000);
    QCOMPARE(artworkProvider.refreshCount, 1);
    QCOMPARE(artworkProvider.resolvedArtwork.connectionId, artwork.connectionId);
    QCOMPARE(artworkProvider.resolvedArtwork.itemId, artwork.itemId);
    QCOMPARE(artworkProvider.refreshedArtwork.connectionId, artwork.connectionId);
    QCOMPARE(artworkProvider.refreshedArtwork.itemId, artwork.itemId);
    QCOMPARE(server.requestTargets.size(), 2);
    QVERIFY(!response->errorString().isEmpty());
    QCOMPARE(server.requestTargets.at(0),
             QStringLiteral("/old.jpg?X-Amz-Signature=expired&X-Amz-Expires=1"));
    QCOMPARE(server.requestTargets.at(1),
             QStringLiteral("/new.jpg?X-Amz-Signature=fresh&X-Amz-Expires=900"));
    delete response;
}

void ArtworkRefreshTest::jellyfinRefreshKeepsExistingResolvedRequest()
{

    ConfigManager config;
    ServerConnection connection;
    connection.connectionId = QStringLiteral("connection-jellyfin");
    connection.providerKind = ProviderKind::Jellyfin;
    connection.protocolMode = ProtocolMode::Native;
    connection.baseUrl = QStringLiteral("https://jellyfin.example.test");
    connection.accountId = QStringLiteral("user-1");
    connection.credentialReference = QStringLiteral("credential-jellyfin");
    QVERIFY(connection.isValid());
    config.upsertConnection(connection, true);

    HttpTransport transport;
    JellyfinProviderAdapter adapter;
    ExposedJellyfinAuthenticationService auth(&transport, &adapter);
    auth.initialize(&config);
    auth.seed();
    QVERIFY(auth.isAuthenticated());

    JellyfinArtworkProvider provider(&auth);
    Bloom::ArtworkRef artwork;
    artwork.connectionId = connection.connectionId;
    artwork.itemId = QStringLiteral("item-1");
    artwork.kind = Bloom::ArtworkKind::Primary;
    artwork.ownerKind = Bloom::ArtworkOwnerKind::MediaItem;
    artwork.requestedWidth = 640;
    artwork.tag = QStringLiteral("stable-tag");

    const auto directlyResolved = provider.resolveArtwork(artwork);
    QVERIFY(directlyResolved.has_value());
    QCOMPARE(directlyResolved->url().path(),
             QStringLiteral("/Items/item-1/Images/Primary"));
    const QUrlQuery directQuery(directlyResolved->url());
    QCOMPARE(directQuery.queryItemValue(QStringLiteral("fillWidth")),
             QStringLiteral("640"));
    QCOMPARE(directQuery.queryItemValue(QStringLiteral("quality")),
             QStringLiteral("95"));
    QCOMPARE(directQuery.queryItemValue(QStringLiteral("tag")),
             QStringLiteral("stable-tag"));
    QVERIFY(directlyResolved->rawHeader("Authorization").contains(
        QByteArrayLiteral("Token=\"token-1\"")));

    int callbacks = 0;
    std::optional<QNetworkRequest> refreshed;
    provider.refreshArtwork(
        artwork,
        [&](std::optional<QNetworkRequest> request) {
            ++callbacks;
            refreshed = std::move(request);
        });

    QCOMPARE(callbacks, 0);
    QTRY_COMPARE_WITH_TIMEOUT(callbacks, 1, 1000);
    QVERIFY(refreshed.has_value());
    QCOMPARE(refreshed->url(), directlyResolved->url());
    QCOMPARE(refreshed->rawHeader("Authorization"),
             directlyResolved->rawHeader("Authorization"));
}
void ArtworkRefreshTest::siloRefreshCompletesAfterAuthenticationServiceDestruction()
{
    auto *auth = new AuthenticationService;
    SiloArtworkProvider provider(auth);

    int callbacks = 0;
    bool receivedEmptyRequest = false;
    provider.refreshArtwork(
        artworkRef(),
        [&callbacks, &receivedEmptyRequest](std::optional<QNetworkRequest> request) {
            ++callbacks;
            receivedEmptyRequest = !request.has_value();
        });

    QCOMPARE(callbacks, 0);
    delete auth;
    QTRY_COMPARE_WITH_TIMEOUT(callbacks, 1, 1000);
    QVERIFY(receivedEmptyRequest);
}

QTEST_MAIN(ArtworkRefreshTest)
#include "ArtworkRefreshTest.moc"
