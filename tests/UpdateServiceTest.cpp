#include <QtTest/QtTest>

#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QStandardPaths>

#include "updates/GitHubReleaseUpdateProvider.h"
#include "updates/IUpdateApplier.h"
#include "updates/IUpdateProvider.h"
#include "updates/UpdateService.h"
#include "utils/ConfigManager.h"

namespace {

class FakeUpdateProvider final : public IUpdateProvider
{
    Q_OBJECT

public:
    explicit FakeUpdateProvider(QObject *parent = nullptr)
        : IUpdateProvider(parent)
    {
    }

    std::optional<UpdateManifest> nextManifest;
    QString nextError;
    QString lastChannel;

    void fetchManifest(const QString &channel,
                       QObject *context,
                       FetchManifestCallback completion) override
    {
        lastChannel = channel;
        if (context) {
            completion(nextManifest, nextError);
        }
    }
};

class FakeUpdateApplier final : public IUpdateApplier
{
    Q_OBJECT

public:
    explicit FakeUpdateApplier(UpdateApplySupport support, QObject *parent = nullptr)
        : IUpdateApplier(parent)
        , m_support(support)
    {
    }

    InstallEligibility detectEligibility() const override
    {
        return {m_support, m_support == UpdateApplySupport::Supported
                              ? QString()
                              : QStringLiteral("notify-only")};
    }

    void downloadAndInstall(const UpdateManifest &, const QString &) override
    {
    }

private:
    UpdateApplySupport m_support = UpdateApplySupport::NotifyOnly;
};

UpdateManifest makeManifest(const QString &channel,
                            const QString &version,
                            const QString &buildId = QStringLiteral("build-2"))
{
    UpdateManifest manifest;
    manifest.channel = channel;
    manifest.version = version;
    manifest.buildId = buildId;
    manifest.releaseTag = channel == QStringLiteral("dev") ? QStringLiteral("dev-latest") : QStringLiteral("v") + version;
    manifest.publishedAt = QStringLiteral("2026-03-27T00:00:00Z");
    manifest.notes = QStringLiteral("Notes");
    manifest.installer = UpdateAsset{
        QStringLiteral("https://example.invalid/Bloom-Setup.exe"),
        QStringLiteral("Bloom-Setup.exe"),
        QStringLiteral("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")
    };
    manifest.portable = UpdateAsset{
        QStringLiteral("https://example.invalid/Bloom-Windows.zip"),
        QStringLiteral("Bloom-Windows.zip"),
        QStringLiteral("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb")
    };
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
        {QStringLiteral("installer"), QJsonObject{
            {QStringLiteral("url"), manifest.installer.url},
            {QStringLiteral("filename"), manifest.installer.filename},
            {QStringLiteral("sha256"), manifest.installer.sha256}
        }},
        {QStringLiteral("portable"), QJsonObject{
            {QStringLiteral("url"), manifest.portable.url},
            {QStringLiteral("filename"), manifest.portable.filename},
            {QStringLiteral("sha256"), manifest.portable.sha256}
        }}
    };
}

void clearTestConfig()
{
    const QString configDir = ConfigManager::getConfigDir();
    if (QDir(configDir).exists()) {
        QDir(configDir).removeRecursively();
    }
}

} // namespace

class UpdateServiceTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void parseManifestBytes_acceptsValidJson();
    void parseManifestBytes_rejectsInvalidJson();
    void startupCheck_showsPopupForNewUpdate();
    void startupCheck_throttlesRecentCheck();
    void manualCheck_bypassesThrottle();
    void dismissStartupPopup_persistsMarker();
};

void UpdateServiceTest::init()
{
    QStandardPaths::setTestModeEnabled(true);
    clearTestConfig();
}

void UpdateServiceTest::parseManifestBytes_acceptsValidJson()
{
    const QJsonObject root{
        {QStringLiteral("channel"), QStringLiteral("stable")},
        {QStringLiteral("version"), QStringLiteral("99.99.99")},
        {QStringLiteral("build_id"), QStringLiteral("99.99.99")},
        {QStringLiteral("release_tag"), QStringLiteral("v99.99.99")},
        {QStringLiteral("published_at"), QStringLiteral("2026-03-27T00:00:00Z")},
        {QStringLiteral("notes"), QStringLiteral("Release notes")},
        {QStringLiteral("installer"), QJsonObject{
            {QStringLiteral("url"), QStringLiteral("https://example.invalid/setup.exe")},
            {QStringLiteral("filename"), QStringLiteral("Bloom-Setup-99.99.99.exe")},
            {QStringLiteral("sha256"), QStringLiteral("cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc")}
        }},
        {QStringLiteral("portable"), QJsonObject{
            {QStringLiteral("url"), QStringLiteral("https://example.invalid/Bloom-Windows.zip")},
            {QStringLiteral("filename"), QStringLiteral("Bloom-Windows.zip")},
            {QStringLiteral("sha256"), QStringLiteral("dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd")}
        }}
    };

    QString error;
    const std::optional<UpdateManifest> manifest = GitHubReleaseUpdateProvider::parseManifestBytes(
        QJsonDocument(root).toJson(QJsonDocument::Compact), &error);

    QVERIFY2(manifest.has_value(), qPrintable(error));
    QCOMPARE(manifest->channel, QStringLiteral("stable"));
    QCOMPARE(manifest->version, QStringLiteral("99.99.99"));
    QCOMPARE(manifest->buildId, QStringLiteral("99.99.99"));
    QCOMPARE(manifest->installer.filename, QStringLiteral("Bloom-Setup-99.99.99.exe"));
}

void UpdateServiceTest::parseManifestBytes_rejectsInvalidJson()
{
    QString error;
    const std::optional<UpdateManifest> malformedManifest = GitHubReleaseUpdateProvider::parseManifestBytes(
        QByteArrayLiteral("{\"channel\":\"stable\""), &error);
    QVERIFY(!malformedManifest.has_value());
    QVERIFY(!error.trimmed().isEmpty());

    QJsonObject incompleteRoot = manifestToJsonObject(makeManifest(QStringLiteral("stable"), QStringLiteral("99.99.99")));
    incompleteRoot.remove(QStringLiteral("version"));

    error.clear();
    const std::optional<UpdateManifest> incompleteManifest = GitHubReleaseUpdateProvider::parseManifestBytes(
        QJsonDocument(incompleteRoot).toJson(QJsonDocument::Compact), &error);
    QVERIFY(!incompleteManifest.has_value());
    QVERIFY(!error.trimmed().isEmpty());
}

void UpdateServiceTest::startupCheck_showsPopupForNewUpdate()
{
    ConfigManager configManager;
    configManager.load();
    configManager.setAutoUpdateCheckEnabled(true);
    configManager.setUpdateChannel(QStringLiteral("stable"));
    configManager.setLastUpdateCheckAt(QString());

    // UpdateService adopts unparented provider/applier instances in its constructor.
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

    // UpdateService adopts unparented provider/applier instances in its constructor.
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

    // UpdateService adopts unparented provider/applier instances in its constructor.
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

    // UpdateService adopts unparented provider/applier instances in its constructor.
    auto *provider = new FakeUpdateProvider;
    provider->nextManifest = makeManifest(QStringLiteral("stable"), QStringLiteral("99.99.99"), QStringLiteral("99.99.99"));
    auto *applier = new FakeUpdateApplier(UpdateApplySupport::Supported);

    UpdateService service(&configManager, nullptr, provider, applier);
    service.performStartupCheck();
    QVERIFY(service.shouldShowStartupPopup());

    service.dismissStartupPopup();

    QVERIFY(!service.shouldShowStartupPopup());
    QCOMPARE(configManager.getSkippedUpdateVersion(), QStringLiteral("stable:99.99.99:99.99.99"));
}

QTEST_MAIN(UpdateServiceTest)
#include "UpdateServiceTest.moc"
