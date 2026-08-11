#include <QtTest/QtTest>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>

#include "updates/GitHubReleaseUpdateProvider.h"
#include "updates/IUpdateApplier.h"
#include "updates/IUpdateProvider.h"
#include "updates/UpdateNetworkPolicy.h"
#include "updates/UpdateService.h"
#include "updates/WindowsNsisUpdateApplier.h"
#include "utils/ConfigManager.h"

#include "optional/monocypher-ed25519.h"

#include <utility>

namespace
{

struct TestSigningMaterial
{
    QByteArray secretKey;
    QByteArray publicKey;
};

const TestSigningMaterial &testSigningMaterial()
{
    static const TestSigningMaterial material = []()
    {
        QByteArray seed(32, '\0');
        for (int index = 0; index < seed.size(); ++index)
        {
            seed[index] = static_cast<char>(index + 1);
        }
        TestSigningMaterial result{QByteArray(64, '\0'), QByteArray(32, '\0')};
        crypto_ed25519_key_pair(reinterpret_cast<uint8_t *>(result.secretKey.data()),
                                reinterpret_cast<uint8_t *>(result.publicKey.data()),
                                reinterpret_cast<uint8_t *>(seed.data()));
        return result;
    }();
    return material;
}

QList<TrustedUpdateManifestKey> testTrustedKeys()
{
    return {{QStringLiteral("test-key"), testSigningMaterial().publicKey}};
}

QByteArray signedEnvelope(const QJsonObject &payloadObject, const QString &keyId = QStringLiteral("test-key"))
{
    const QByteArray payload = QJsonDocument(payloadObject).toJson(QJsonDocument::Compact);
    QByteArray signature(64, '\0');
    crypto_ed25519_sign(reinterpret_cast<uint8_t *>(signature.data()),
                        reinterpret_cast<const uint8_t *>(testSigningMaterial().secretKey.constData()),
                        reinterpret_cast<const uint8_t *>(payload.constData()), static_cast<size_t>(payload.size()));
    const QJsonObject envelope{
        {QStringLiteral("schema"), 2},
        {QStringLiteral("key_id"), keyId},
        {QStringLiteral("payload"), QString::fromLatin1(payload.toBase64())},
        {QStringLiteral("signature"), QString::fromLatin1(signature.toBase64())},
    };
    return QJsonDocument(envelope).toJson(QJsonDocument::Compact);
}

class ScriptedHttpServer final : public QTcpServer
{
  public:
    enum class Behavior
    {
        Response,
        Redirect,
        Truncated,
        Hang,
        PartialHang,
    };

    explicit ScriptedHttpServer(Behavior behavior, QByteArray body = {}, QObject *parent = nullptr)
        : QTcpServer(parent), m_behavior(behavior), m_body(std::move(body))
    {
        connect(this, &QTcpServer::newConnection, this,
                [this]()
                {
                    while (hasPendingConnections())
                    {
                        QTcpSocket *socket = nextPendingConnection();
                        connect(socket, &QTcpSocket::readyRead, socket,
                                [this, socket]()
                                {
                                    QByteArray request = socket->property("request").toByteArray();
                                    request.append(socket->readAll());
                                    socket->setProperty("request", request);
                                    if (!request.contains("\r\n\r\n") || socket->property("responded").toBool())
                                    {
                                        return;
                                    }
                                    socket->setProperty("responded", true);
                                    if (m_behavior == Behavior::Hang)
                                    {
                                        return;
                                    }
                                    if (m_behavior == Behavior::PartialHang)
                                    {
                                        socket->write("HTTP/1.1 200 OK\r\nContent-Length: 1024\r\n"
                                                      "Connection: keep-alive\r\n\r\npartial");
                                        socket->flush();
                                        return;
                                    }
                                    if (m_behavior == Behavior::Redirect)
                                    {
                                        socket->write("HTTP/1.1 302 Found\r\n"
                                                      "Location: https://evil.example/update\r\n"
                                                      "Content-Length: 0\r\nConnection: close\r\n\r\n");
                                    }
                                    else
                                    {
                                        const qint64 declaredLength =
                                            m_behavior == Behavior::Truncated ? m_body.size() + 10 : m_body.size();
                                        socket->write(QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Length: ") +
                                                      QByteArray::number(declaredLength) +
                                                      QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + m_body);
                                    }
                                    socket->disconnectFromHost();
                                });
                    }
                });
    }

    bool start()
    {
        return listen(QHostAddress::LocalHost, 0);
    }

    QString baseUrl() const
    {
        return QStringLiteral("http://127.0.0.1:%1").arg(serverPort());
    }

  private:
    Behavior m_behavior;
    QByteArray m_body;
};

class FakeUpdateProvider final : public IUpdateProvider
{
    Q_OBJECT

  public:
    explicit FakeUpdateProvider(QObject *parent = nullptr) : IUpdateProvider(parent)
    {
    }

    std::optional<UpdateManifest> nextManifest;
    QString nextError;
    QString lastChannel;

    void fetchManifest(const QString &channel, QObject *context, FetchManifestCallback completion) override
    {
        lastChannel = channel;
        if (context)
        {
            completion(nextManifest, nextError);
        }
    }
};

class FakeUpdateApplier final : public IUpdateApplier
{
    Q_OBJECT

  public:
    explicit FakeUpdateApplier(UpdateApplySupport support, QObject *parent = nullptr)
        : IUpdateApplier(parent), m_support(support)
    {
    }

    InstallEligibility detectEligibility() const override
    {
        return {m_support, m_support == UpdateApplySupport::Supported ? QString() : QStringLiteral("notify-only")};
    }

    void downloadAndInstall(const UpdateManifest &, const QString &) override
    {
    }

  private:
    UpdateApplySupport m_support = UpdateApplySupport::NotifyOnly;
};

UpdateManifest makeManifest(const QString &channel, const QString &version,
                            const QString &buildId = QStringLiteral("build-2"))
{
    UpdateManifest manifest;
    manifest.channel = channel;
    manifest.version = version;
    manifest.buildId = buildId;
    manifest.releaseTag =
        channel == QStringLiteral("dev") ? QStringLiteral("dev-latest") : QStringLiteral("v") + version;
    manifest.publishedAt = QStringLiteral("2026-03-27T00:00:00Z");
    manifest.notes = QStringLiteral("Notes");
    manifest.installer =
        UpdateAsset{QStringLiteral("https://github.com/crowquillx/Bloom/releases/download/"
                                   "v99.99.99/Bloom-Setup.exe"),
                    QStringLiteral("Bloom-Setup.exe"),
                    QStringLiteral("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")};
    manifest.portable = UpdateAsset{QStringLiteral("https://github.com/crowquillx/Bloom/releases/download/"
                                                   "v99.99.99/Bloom-Windows.zip"),
                                    QStringLiteral("Bloom-Windows.zip"),
                                    QStringLiteral("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb")};
    return manifest;
}

QJsonObject manifestToJsonObject(const UpdateManifest &manifest)
{
    return QJsonObject{
        {QStringLiteral("channel"), manifest.channel},
        {QStringLiteral("version"), manifest.version},
        {QStringLiteral("build_id"), manifest.buildId},
        {QStringLiteral("release_tag"), manifest.releaseTag},
        {QStringLiteral("published_at"), manifest.publishedAt},
        {QStringLiteral("notes"), manifest.notes},
        {QStringLiteral("installer"), QJsonObject{{QStringLiteral("url"), manifest.installer.url},
                                                  {QStringLiteral("filename"), manifest.installer.filename},
                                                  {QStringLiteral("sha256"), manifest.installer.sha256}}},
        {QStringLiteral("portable"), QJsonObject{{QStringLiteral("url"), manifest.portable.url},
                                                 {QStringLiteral("filename"), manifest.portable.filename},
                                                 {QStringLiteral("sha256"), manifest.portable.sha256}}}};
}

void clearTestConfig()
{
    const QString configDir = ConfigManager::getConfigDir();
    if (QDir(configDir).exists())
    {
        QDir(configDir).removeRecursively();
    }
}

void clearTestUpdateDownloads()
{
    const QString updatesDirectory = QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
                                         .filePath(QStringLiteral("updates"));
    if (QDir(updatesDirectory).exists())
    {
        QDir(updatesDirectory).removeRecursively();
    }
}

} // namespace

class UpdateServiceTest : public QObject
{
    Q_OBJECT

  private slots:
    void init();
    void parseManifestBytes_acceptsValidSignature();
    void ed25519Verifier_matchesRfc8032Vector();
    void parseManifestBytes_rejectsInvalidSignature();
    void parseManifestBytes_rejectsUntrustedAssetOrigin();
    void networkPolicy_rejectsInsecureAndForeignOrigins();
    void manifestFetch_rejectsRedirect();
    void manifestFetch_rejectsOversizeResponse();
    void manifestFetch_rejectsTruncation();
    void manifestFetch_timesOut();
    void manifestFetch_callbackCanDestroyProvider();
    void installerDownload_rejectsRedirectAndCleansPartial();
    void installerDownload_rejectsOversizeAndTruncation();
    void installerDownload_timesOutAndCleansPartial();
    void installerDownload_finalizesAtomicallyAndCleansObsolete();
    void autoUpdateCheckDefaultsOff();
    void startupCheck_skipsWhenAutoCheckDefaultOff();
    void startupCheck_showsPopupForNewUpdate();
    void startupCheck_throttlesRecentCheck();
    void manualCheck_bypassesThrottle();
    void dismissStartupPopup_persistsMarker();
};

void UpdateServiceTest::init()
{
    clearTestConfig();
    clearTestUpdateDownloads();
}

void UpdateServiceTest::parseManifestBytes_acceptsValidSignature()
{
    const QJsonObject root{
        {QStringLiteral("channel"), QStringLiteral("stable")},
        {QStringLiteral("version"), QStringLiteral("99.99.99")},
        {QStringLiteral("build_id"), QStringLiteral("99.99.99")},
        {QStringLiteral("release_tag"), QStringLiteral("v99.99.99")},
        {QStringLiteral("published_at"), QStringLiteral("2026-03-27T00:00:00Z")},
        {QStringLiteral("notes"), QStringLiteral("Release notes")},
        {QStringLiteral("installer"),
         QJsonObject{{QStringLiteral("url"), QStringLiteral("https://github.com/crowquillx/Bloom/"
                                                            "releases/download/v99.99.99/setup.exe")},
                     {QStringLiteral("filename"), QStringLiteral("Bloom-Setup-99.99.99.exe")},
                     {QStringLiteral("sha256"), QStringLiteral("ccccccccccccccccccccccccccccccccccccccccccc"
                                                               "ccccccccccccccccccccc")}}},
        {QStringLiteral("portable"),
         QJsonObject{{QStringLiteral("url"), QStringLiteral("https://github.com/crowquillx/Bloom/releases/"
                                                            "download/v99.99.99/Bloom-Windows.zip")},
                     {QStringLiteral("filename"), QStringLiteral("Bloom-Windows.zip")},
                     {QStringLiteral("sha256"), QStringLiteral("ddddddddddddddddddddddddddddddddddddddddddddddddddd"
                                                               "ddddddddddddd")}}}};

    QString error;
    const std::optional<UpdateManifest> manifest =
        GitHubReleaseUpdateProvider::parseManifestBytes(signedEnvelope(root), testTrustedKeys(), &error);

    QVERIFY2(manifest.has_value(), qPrintable(error));
    QCOMPARE(manifest->channel, QStringLiteral("stable"));
    QCOMPARE(manifest->version, QStringLiteral("99.99.99"));
    QCOMPARE(manifest->buildId, QStringLiteral("99.99.99"));
    QCOMPARE(manifest->installer.filename, QStringLiteral("Bloom-Setup-99.99.99.exe"));
}

void UpdateServiceTest::ed25519Verifier_matchesRfc8032Vector()
{
    const QByteArray publicKey =
        QByteArray::fromHex(QByteArrayLiteral("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a"));
    const QByteArray signature = QByteArray::fromHex(QByteArrayLiteral("e5564300c360ac729086e2cc806e828a"
                                                                       "84877f1eb8e5d974d873e06522490155"
                                                                       "5fb8821590a33bacc61e39701cf9b46b"
                                                                       "d25bf5f0595bbe24655141438e7a100b"));
    QCOMPARE(crypto_ed25519_check(reinterpret_cast<const uint8_t *>(signature.constData()),
                                  reinterpret_cast<const uint8_t *>(publicKey.constData()), nullptr, 0),
             0);
}

void UpdateServiceTest::parseManifestBytes_rejectsInvalidSignature()
{
    QString error;
    const std::optional<UpdateManifest> unsignedManifest =
        GitHubReleaseUpdateProvider::parseManifestBytes(QByteArrayLiteral("{\"channel\":\"stable\""), &error);
    QVERIFY(!unsignedManifest.has_value());
    QVERIFY(!error.trimmed().isEmpty());

    QByteArray tampered =
        signedEnvelope(manifestToJsonObject(makeManifest(QStringLiteral("stable"), QStringLiteral("99.99.99"))));
    QJsonObject envelope = QJsonDocument::fromJson(tampered).object();
    QByteArray payload = QByteArray::fromBase64(envelope.value(QStringLiteral("payload")).toString().toLatin1());
    payload.replace("99.99.99", "98.98.98");
    envelope.insert(QStringLiteral("payload"), QString::fromLatin1(payload.toBase64()));

    error.clear();
    const auto tamperedManifest = GitHubReleaseUpdateProvider::parseManifestBytes(
        QJsonDocument(envelope).toJson(QJsonDocument::Compact), testTrustedKeys(), &error);
    QVERIFY(!tamperedManifest.has_value());
    QVERIFY(error.contains(QStringLiteral("signature"), Qt::CaseInsensitive));

    error.clear();
    const auto unknownKeyManifest = GitHubReleaseUpdateProvider::parseManifestBytes(
        signedEnvelope(manifestToJsonObject(makeManifest(QStringLiteral("stable"), QStringLiteral("99.99.99"))),
                       QStringLiteral("retired-key")),
        testTrustedKeys(), &error);
    QVERIFY(!unknownKeyManifest.has_value());
    QVERIFY(error.contains(QStringLiteral("unknown key"), Qt::CaseInsensitive));
}

void UpdateServiceTest::parseManifestBytes_rejectsUntrustedAssetOrigin()
{
    QJsonObject root = manifestToJsonObject(makeManifest(QStringLiteral("stable"), QStringLiteral("99.99.99")));
    QJsonObject installer = root.value(QStringLiteral("installer")).toObject();
    installer.insert(QStringLiteral("url"), QStringLiteral("https://evil.example/Bloom-Setup.exe"));
    root.insert(QStringLiteral("installer"), installer);

    QString error;
    const auto manifest =
        GitHubReleaseUpdateProvider::parseManifestBytes(signedEnvelope(root), testTrustedKeys(), &error);
    QVERIFY(!manifest.has_value());
    QVERIFY(error.contains(QStringLiteral("untrusted"), Qt::CaseInsensitive));
}

void UpdateServiceTest::networkPolicy_rejectsInsecureAndForeignOrigins()
{
    QVERIFY(UpdateNetworkPolicy::isAllowedManifestUrl(
        QUrl(QStringLiteral("https://raw.githubusercontent.com/crowquillx/Bloom/"
                            "update-manifests/stable.json"))));
    QVERIFY(!UpdateNetworkPolicy::isAllowedManifestUrl(
        QUrl(QStringLiteral("http://raw.githubusercontent.com/crowquillx/Bloom/"
                            "update-manifests/stable.json"))));
    QVERIFY(!UpdateNetworkPolicy::isAllowedManifestUrl(QUrl(QStringLiteral("https://evil.example/stable.json"))));

    QVERIFY(UpdateNetworkPolicy::isAllowedAssetUrl(QUrl(QStringLiteral("https://github.com/crowquillx/Bloom/releases/"
                                                                       "download/v1/Bloom.exe")),
                                                   true));
    QVERIFY(!UpdateNetworkPolicy::isAllowedAssetUrl(
        QUrl(QStringLiteral("https://github.com/another/repo/releases/download/v1/Bloom.exe")), true));
    QVERIFY(UpdateNetworkPolicy::isAllowedAssetUrl(QUrl(QStringLiteral("https://release-assets.githubusercontent.com/"
                                                                       "object?token=redacted")),
                                                   false));
    QVERIFY(!UpdateNetworkPolicy::isAllowedAssetUrl(QUrl(QStringLiteral("https://evil.example/object")), false));
}

void UpdateServiceTest::manifestFetch_rejectsRedirect()
{
    ScriptedHttpServer server(ScriptedHttpServer::Behavior::Redirect);
    QVERIFY(server.start());
    QNetworkAccessManager network;
    GitHubReleaseUpdateProviderOptions options;
    options.manifestBaseUrl = server.baseUrl();
    options.deadlineMs = 1000;
    options.urlValidator = [](const QUrl &url) { return url.host() == QStringLiteral("127.0.0.1"); };
    options.trustedKeys = testTrustedKeys();
    GitHubReleaseUpdateProvider provider(&network, options);

    bool completed = false;
    QString error;
    provider.fetchManifest(QStringLiteral("stable"), &provider,
                           [&](std::optional<UpdateManifest> manifest, const QString &message)
                           {
                               QVERIFY(!manifest.has_value());
                               error = message;
                               completed = true;
                           });
    QTRY_VERIFY_WITH_TIMEOUT(completed, 2000);
    QVERIFY(error.contains(QStringLiteral("redirect"), Qt::CaseInsensitive));
}

void UpdateServiceTest::manifestFetch_rejectsOversizeResponse()
{
    ScriptedHttpServer server(ScriptedHttpServer::Behavior::Response, QByteArray(256, 'x'));
    QVERIFY(server.start());
    QNetworkAccessManager network;
    GitHubReleaseUpdateProviderOptions options;
    options.manifestBaseUrl = server.baseUrl();
    options.maximumResponseBytes = 64;
    options.urlValidator = [](const QUrl &url) { return url.host() == QStringLiteral("127.0.0.1"); };
    GitHubReleaseUpdateProvider provider(&network, options);

    bool completed = false;
    QString error;
    provider.fetchManifest(QStringLiteral("stable"), &provider,
                           [&](std::optional<UpdateManifest>, const QString &message)
                           {
                               error = message;
                               completed = true;
                           });
    QTRY_VERIFY_WITH_TIMEOUT(completed, 2000);
    QVERIFY(error.contains(QStringLiteral("size"), Qt::CaseInsensitive));
}

void UpdateServiceTest::manifestFetch_rejectsTruncation()
{
    ScriptedHttpServer server(ScriptedHttpServer::Behavior::Truncated, QByteArrayLiteral("short"));
    QVERIFY(server.start());
    QNetworkAccessManager network;
    GitHubReleaseUpdateProviderOptions options;
    options.manifestBaseUrl = server.baseUrl();
    options.urlValidator = [](const QUrl &url) { return url.host() == QStringLiteral("127.0.0.1"); };
    GitHubReleaseUpdateProvider provider(&network, options);

    bool completed = false;
    QString error;
    provider.fetchManifest(QStringLiteral("stable"), &provider,
                           [&](std::optional<UpdateManifest>, const QString &message)
                           {
                               error = message;
                               completed = true;
                           });
    QTRY_VERIFY_WITH_TIMEOUT(completed, 2000);
    QVERIFY(error.contains(QStringLiteral("truncated"), Qt::CaseInsensitive));
}

void UpdateServiceTest::manifestFetch_timesOut()
{
    ScriptedHttpServer server(ScriptedHttpServer::Behavior::Hang);
    QVERIFY(server.start());
    QNetworkAccessManager network;
    GitHubReleaseUpdateProviderOptions options;
    options.manifestBaseUrl = server.baseUrl();
    options.deadlineMs = 50;
    options.transferTimeoutMs = 1000;
    options.urlValidator = [](const QUrl &url) { return url.host() == QStringLiteral("127.0.0.1"); };
    GitHubReleaseUpdateProvider provider(&network, options);

    bool completed = false;
    QString error;
    provider.fetchManifest(QStringLiteral("stable"), &provider,
                           [&](std::optional<UpdateManifest>, const QString &message)
                           {
                               error = message;
                               completed = true;
                           });
    QTRY_VERIFY_WITH_TIMEOUT(completed, 2000);
    QVERIFY(error.contains(QStringLiteral("timed out"), Qt::CaseInsensitive));
}

void UpdateServiceTest::manifestFetch_callbackCanDestroyProvider()
{
    const QByteArray envelope =
        signedEnvelope(manifestToJsonObject(makeManifest(QStringLiteral("stable"), QStringLiteral("99.99.99"))));
    ScriptedHttpServer server(ScriptedHttpServer::Behavior::Response, envelope);
    QVERIFY(server.start());
    QNetworkAccessManager network;
    GitHubReleaseUpdateProviderOptions options;
    options.manifestBaseUrl = server.baseUrl();
    options.urlValidator = [](const QUrl &url) { return url.host() == QStringLiteral("127.0.0.1"); };
    options.trustedKeys = testTrustedKeys();
    auto *provider = new GitHubReleaseUpdateProvider(&network, options);

    bool completed = false;
    provider->fetchManifest(QStringLiteral("stable"), provider,
                            [&](std::optional<UpdateManifest> manifest, const QString &error)
                            {
                                QVERIFY2(manifest.has_value(), qPrintable(error));
                                delete provider;
                                provider = nullptr;
                                completed = true;
                            });

    QTRY_VERIFY_WITH_TIMEOUT(completed, 2000);
    QVERIFY(provider == nullptr);
}

void UpdateServiceTest::installerDownload_rejectsRedirectAndCleansPartial()
{
    ScriptedHttpServer server(ScriptedHttpServer::Behavior::Redirect);
    QVERIFY(server.start());
    QNetworkAccessManager network;
    UpdateDownloadOptions options;
    options.urlValidator = [](const QUrl &url, bool) { return url.host() == QStringLiteral("127.0.0.1"); };
    WindowsNsisUpdateApplier applier(&network, options);
    QSignalSpy finishedSpy(&applier, &IUpdateApplier::installFinished);
    UpdateManifest manifest = makeManifest(QStringLiteral("stable"), QStringLiteral("99.99.99"));
    manifest.installer.url = server.baseUrl() + QStringLiteral("/installer.exe");

    applier.downloadAndInstall(manifest, QStringLiteral("stable"));
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 2000);
    QVERIFY(finishedSpy.first().at(1).toString().contains(QStringLiteral("redirect"), Qt::CaseInsensitive));

    const QDir directory(QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
                             .filePath(QStringLiteral("updates/stable")));
    QCOMPARE(directory.entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot).size(), 0);
}

void UpdateServiceTest::installerDownload_rejectsOversizeAndTruncation()
{
    const auto runFailure = [&](ScriptedHttpServer::Behavior behavior, const QString &expectedError)
    {
        ScriptedHttpServer server(behavior, QByteArrayLiteral("12345678"));
        QVERIFY(server.start());
        QNetworkAccessManager network;
        UpdateDownloadOptions options;
        options.maximumBytes = behavior == ScriptedHttpServer::Behavior::Response ? 4 : 1024;
        options.urlValidator = [](const QUrl &url, bool) { return url.host() == QStringLiteral("127.0.0.1"); };
        WindowsNsisUpdateApplier applier(&network, options);
        QSignalSpy finishedSpy(&applier, &IUpdateApplier::installFinished);
        UpdateManifest manifest = makeManifest(QStringLiteral("stable"), QStringLiteral("99.99.99"));
        manifest.installer.url = server.baseUrl() + QStringLiteral("/installer.exe");
        applier.downloadAndInstall(manifest, QStringLiteral("stable"));
        QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 2000);
        QVERIFY(finishedSpy.first().at(1).toString().contains(expectedError, Qt::CaseInsensitive));
    };

    runFailure(ScriptedHttpServer::Behavior::Response, QStringLiteral("size"));
    clearTestConfig();
    runFailure(ScriptedHttpServer::Behavior::Truncated, QStringLiteral("truncated"));
}

void UpdateServiceTest::installerDownload_timesOutAndCleansPartial()
{
    ScriptedHttpServer server(ScriptedHttpServer::Behavior::PartialHang);
    QVERIFY(server.start());
    QNetworkAccessManager network;
    UpdateDownloadOptions options;
    options.totalDeadlineMs = 50;
    options.transferTimeoutMs = 1000;
    options.urlValidator = [](const QUrl &url, bool) { return url.host() == QStringLiteral("127.0.0.1"); };
    WindowsNsisUpdateApplier applier(&network, options);
    QSignalSpy finishedSpy(&applier, &IUpdateApplier::installFinished);
    UpdateManifest manifest = makeManifest(QStringLiteral("stable"), QStringLiteral("99.99.99"));
    manifest.installer.url = server.baseUrl() + QStringLiteral("/installer.exe");

    applier.downloadAndInstall(manifest, QStringLiteral("stable"));
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 2000);
    QVERIFY(finishedSpy.first().at(1).toString().contains(QStringLiteral("timed out"), Qt::CaseInsensitive));

    const QDir directory(QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
                             .filePath(QStringLiteral("updates/stable")));
    QCOMPARE(directory.entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot).size(), 0);
}

void UpdateServiceTest::installerDownload_finalizesAtomicallyAndCleansObsolete()
{
    const QByteArray installerBytes = QByteArrayLiteral("verified-installer");
    ScriptedHttpServer server(ScriptedHttpServer::Behavior::Response, installerBytes);
    QVERIFY(server.start());

    const QString directoryPath = QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
                                      .filePath(QStringLiteral("updates/stable"));
    QVERIFY(QDir().mkpath(directoryPath));
    QFile obsolete(QDir(directoryPath).filePath(QStringLiteral("old.exe")));
    QVERIFY(obsolete.open(QIODevice::WriteOnly));
    QCOMPARE(obsolete.write("old"), qint64(3));
    obsolete.close();

    QNetworkAccessManager network;
    UpdateDownloadOptions options;
    options.urlValidator = [](const QUrl &url, bool) { return url.host() == QStringLiteral("127.0.0.1"); };
    WindowsNsisUpdateApplier applier(&network, options);
    QSignalSpy finishedSpy(&applier, &IUpdateApplier::installFinished);
    UpdateManifest manifest = makeManifest(QStringLiteral("stable"), QStringLiteral("99.99.99"));
    manifest.installer.url = server.baseUrl() + QStringLiteral("/installer.exe");
    manifest.installer.sha256 =
        QString::fromLatin1(QCryptographicHash::hash(installerBytes, QCryptographicHash::Sha256).toHex());

    applier.downloadAndInstall(manifest, QStringLiteral("stable"));
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 2000);
#ifndef Q_OS_WIN
    QVERIFY(!finishedSpy.first().at(0).toBool());
    QVERIFY(finishedSpy.first().at(1).toString().contains(QStringLiteral("Windows"), Qt::CaseInsensitive));
#endif

    const QDir directory(directoryPath);
    QVERIFY(!QFileInfo::exists(directory.filePath(QStringLiteral("old.exe"))));
    const QString finalPath = directory.filePath(manifest.installer.filename);
    QFile finalFile(finalPath);
    QVERIFY(finalFile.open(QIODevice::ReadOnly));
    QCOMPARE(finalFile.readAll(), installerBytes);
    const QStringList files = directory.entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot);
    QCOMPARE(files, QStringList{manifest.installer.filename});
}

void UpdateServiceTest::autoUpdateCheckDefaultsOff()
{
    ConfigManager configManager;
    configManager.load();

    QVERIFY(!configManager.getAutoUpdateCheckEnabled());

    configManager.setAutoUpdateCheckEnabled(true);
    QVERIFY(configManager.getAutoUpdateCheckEnabled());
}

void UpdateServiceTest::startupCheck_skipsWhenAutoCheckDefaultOff()
{
    ConfigManager configManager;
    configManager.load();
    configManager.setLastUpdateCheckAt(QString());

    auto *provider = new FakeUpdateProvider;
    provider->nextManifest = makeManifest(QStringLiteral("stable"), QStringLiteral("99.99.99"));
    auto *applier = new FakeUpdateApplier(UpdateApplySupport::Supported);

    UpdateService service(&configManager, nullptr, provider, applier);
    service.performStartupCheck();

    QVERIFY(provider->lastChannel.isEmpty());
    QVERIFY(!service.updateAvailable());
    QVERIFY(!service.shouldShowStartupPopup());
}

void UpdateServiceTest::startupCheck_showsPopupForNewUpdate()
{
    ConfigManager configManager;
    configManager.load();
    configManager.setAutoUpdateCheckEnabled(true);
    configManager.setUpdateChannel(QStringLiteral("stable"));
    configManager.setLastUpdateCheckAt(QString());

    // UpdateService adopts unparented provider/applier instances in its
    // constructor.
    auto *provider = new FakeUpdateProvider;
    provider->nextManifest = makeManifest(QStringLiteral("stable"), QStringLiteral("99.99.99"));
    auto *applier = new FakeUpdateApplier(UpdateApplySupport::Supported);

    UpdateService service(&configManager, nullptr, provider, applier);
    QSignalSpy popupSpy(&service, &UpdateService::startupPopupRequested);

    service.performStartupCheck();

    QCOMPARE(popupSpy.count(), 1);
    QVERIFY(service.updateAvailable());
    QVERIFY(service.shouldShowStartupPopup());
    QCOMPARE(service.availableVersion(), QStringLiteral("99.99.99"));
}

void UpdateServiceTest::startupCheck_throttlesRecentCheck()
{
    ConfigManager configManager;
    configManager.load();
    configManager.setAutoUpdateCheckEnabled(true);
    configManager.setLastUpdateCheckAt(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));

    // UpdateService adopts unparented provider/applier instances in its
    // constructor.
    auto *provider = new FakeUpdateProvider;
    provider->nextManifest = makeManifest(QStringLiteral("stable"), QStringLiteral("99.99.99"));
    auto *applier = new FakeUpdateApplier(UpdateApplySupport::NotifyOnly);

    UpdateService service(&configManager, nullptr, provider, applier);
    service.performStartupCheck();

    QVERIFY(provider->lastChannel.isEmpty());
    QVERIFY(!service.updateAvailable());
}

void UpdateServiceTest::manualCheck_bypassesThrottle()
{
    ConfigManager configManager;
    configManager.load();
    configManager.setAutoUpdateCheckEnabled(true);
    configManager.setLastUpdateCheckAt(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));

    // UpdateService adopts unparented provider/applier instances in its
    // constructor.
    auto *provider = new FakeUpdateProvider;
    provider->nextManifest = makeManifest(QStringLiteral("stable"), QStringLiteral("99.99.99"));
    auto *applier = new FakeUpdateApplier(UpdateApplySupport::NotifyOnly);

    UpdateService service(&configManager, nullptr, provider, applier);
    service.checkForUpdates(true);

    QCOMPARE(provider->lastChannel, QStringLiteral("stable"));
    QVERIFY(service.updateAvailable());
}

void UpdateServiceTest::dismissStartupPopup_persistsMarker()
{
    ConfigManager configManager;
    configManager.load();
    configManager.setAutoUpdateCheckEnabled(true);
    configManager.setLastUpdateCheckAt(QString());

    // UpdateService adopts unparented provider/applier instances in its
    // constructor.
    auto *provider = new FakeUpdateProvider;
    provider->nextManifest =
        makeManifest(QStringLiteral("stable"), QStringLiteral("99.99.99"), QStringLiteral("99.99.99"));
    auto *applier = new FakeUpdateApplier(UpdateApplySupport::Supported);

    UpdateService service(&configManager, nullptr, provider, applier);
    service.performStartupCheck();
    QVERIFY(service.shouldShowStartupPopup());

    service.dismissStartupPopup();

    QVERIFY(!service.shouldShowStartupPopup());
    QCOMPARE(configManager.getSkippedUpdateVersion(), QStringLiteral("stable:99.99.99:99.99.99"));
}

int main(int argc, char *argv[])
{
    QTemporaryDir dataRoot;
    if (!dataRoot.isValid())
    {
        return 1;
    }
    const QString dataPath = QDir(dataRoot.path()).filePath(QStringLiteral("data"));
    const QString configPath = QDir(dataRoot.path()).filePath(QStringLiteral("config"));
    const QString cachePath = QDir(dataRoot.path()).filePath(QStringLiteral("cache"));
    if (!QDir().mkpath(dataPath) || !QDir().mkpath(configPath) || !QDir().mkpath(cachePath) ||
        !qputenv("XDG_DATA_HOME", dataPath.toUtf8()) || !qputenv("XDG_CONFIG_HOME", configPath.toUtf8()) ||
        !qputenv("XDG_CACHE_HOME", cachePath.toUtf8()))
    {
        return 1;
    }
#ifdef Q_OS_WIN
    QStandardPaths::setTestModeEnabled(true);
#endif

    QGuiApplication application(argc, argv);
    UpdateServiceTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "UpdateServiceTest.moc"
