#include <QtTest/QtTest>

#include <QDir>
#include <QHostAddress>
#include <QNetworkRequest>
#include <QPointer>
#include <QQuickImageResponse>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QUrlQuery>
#include <QUuid>

#include "network/AuthenticationService.h"
#include "network/HttpTransport.h"
#include "models/MediaModels.h"
#include "providers/IArtworkProvider.h"
#include "providers/ServerConnection.h"
#include "providers/jellyfin/JellyfinArtworkProvider.h"
#include "providers/jellyfin/JellyfinProviderAdapter.h"
#include "ui/ImageCacheProvider.h"
#include "utils/ConfigManager.h"

namespace {

class ScriptedHttpServer final : public QTcpServer
{
public:
    QList<int> statuses;
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
                    const QByteArray reason = status == 401
                        ? QByteArrayLiteral("Unauthorized")
                        : (status == 403 ? QByteArrayLiteral("Forbidden")
                                         : QByteArrayLiteral("Internal Server Error"));
                    socket->write(QByteArrayLiteral("HTTP/1.1 ")
                                  + QByteArray::number(status) + QByteArrayLiteral(" ")
                                  + reason
                                  + QByteArrayLiteral("\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"));
                    socket->disconnectFromHost();
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

class ScopedConfigIsolation
{
public:
    explicit ScopedConfigIsolation(const QString &path)
        : m_previousConfigHome(qgetenv("XDG_CONFIG_HOME"))
        , m_previousAppData(qgetenv("APPDATA"))
        , m_previousHome(qgetenv("HOME"))
        , m_hadConfigHome(!m_previousConfigHome.isNull())
        , m_hadAppData(!m_previousAppData.isNull())
        , m_hadHome(!m_previousHome.isNull())
    {
        QStandardPaths::setTestModeEnabled(true);
        qputenv("XDG_CONFIG_HOME", path.toUtf8());
        qputenv("APPDATA", path.toUtf8());
        qputenv("HOME", path.toUtf8());
        QDir().mkpath(path + QStringLiteral("/Library/Preferences"));
    }

    ~ScopedConfigIsolation()
    {
        restore("XDG_CONFIG_HOME", m_previousConfigHome, m_hadConfigHome);
        restore("APPDATA", m_previousAppData, m_hadAppData);
        restore("HOME", m_previousHome, m_hadHome);
        QStandardPaths::setTestModeEnabled(false);
    }

private:
    static void restore(const char *name, const QByteArray &value, bool existed)
    {
        if (existed) {
            qputenv(name, value);
        } else {
            qunsetenv(name);
        }
    }

    QByteArray m_previousConfigHome;
    QByteArray m_previousAppData;
    QByteArray m_previousHome;
    bool m_hadConfigHome;
    bool m_hadAppData;
    bool m_hadHome;
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

} // namespace

class ArtworkRefreshTest : public QObject
{
    Q_OBJECT

private slots:
    void signedUrlIsNotPartOfCacheIdentity();
    void authorizationFailureRefreshesExactlyOnce_data();
    void authorizationFailureRefreshesExactlyOnce();
    void jellyfinRefreshKeepsExistingResolvedRequest();
};

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
    QVERIFY(server.requestTargets.at(0).startsWith(
        QStringLiteral("/old.jpg?X-Amz-Signature=expired")));
    QVERIFY(server.requestTargets.at(1).startsWith(
        QStringLiteral("/new.jpg?X-Amz-Signature=fresh")));
    QVERIFY(!response->errorString().isEmpty());

    QTest::qWait(20);
    QCOMPARE(artworkProvider.refreshCount, 1);
    QCOMPARE(finishedSpy.count(), 1);

    delete response;
}

void ArtworkRefreshTest::jellyfinRefreshKeepsExistingResolvedRequest()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    ScopedConfigIsolation isolation(temporaryDirectory.path());

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

QTEST_MAIN(ArtworkRefreshTest)
#include "ArtworkRefreshTest.moc"
