#include <QtTest/QtTest>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <memory>

#include "models/SubtitleAppearance.h"
#include "network/LibraryService.h"
#include "network/SubtitleAppearanceSync.h"
#include "providers/jellyfin/JellyfinCatalogProvider.h"
#include "providers/jellyfin/JellyfinProviderAdapter.h"
#include "providers/silo/SiloCatalogProvider.h"
#include "providers/silo/SiloProviderAdapter.h"
#include "providers/silo/SiloSubtitleAppearanceMapper.h"
#include "utils/ConfigManager.h"

using Bloom::SubtitleAppearance;

namespace {

class ScopedConfigIsolation
{
public:
    explicit ScopedConfigIsolation(const QString &path)
        : m_previousConfigHome(qgetenv("XDG_CONFIG_HOME"))
        , m_previousAppData(qgetenv("APPDATA"))
        , m_previousHome(qgetenv("HOME"))
    {
        QStandardPaths::setTestModeEnabled(true);
        qputenv("XDG_CONFIG_HOME", path.toUtf8());
        qputenv("APPDATA", path.toUtf8());
        qputenv("HOME", path.toUtf8());
        QDir().mkpath(path + QStringLiteral("/Library/Preferences"));
    }

    ~ScopedConfigIsolation()
    {
        qputenv("XDG_CONFIG_HOME", m_previousConfigHome);
        qputenv("APPDATA", m_previousAppData);
        qputenv("HOME", m_previousHome);
        QStandardPaths::setTestModeEnabled(false);
    }

private:
    QByteArray m_previousConfigHome;
    QByteArray m_previousAppData;
    QByteArray m_previousHome;
};

QString effectiveBody(const QJsonObject &document, bool deviceOverride = false)
{
    return QString::fromUtf8(QJsonDocument(QJsonObject{
        {QStringLiteral("key"), QStringLiteral("subtitle_appearance")},
        {QStringLiteral("profile_id"), QStringLiteral("1")},
        {QStringLiteral("global_value"), QString()},
        {QStringLiteral("effective_value"),
         document.isEmpty() ? QString()
                            : QString::fromUtf8(QJsonDocument(document).toJson(QJsonDocument::Compact))},
        {QStringLiteral("has_device_override"), deviceOverride},
    }).toJson(QJsonDocument::Compact));
}

// Silo's web player default document.
QJsonObject webDefaultDocument()
{
    return QJsonObject{
        {QStringLiteral("fontSize"), QStringLiteral("large")},
        {QStringLiteral("fontFamily"), QStringLiteral("sans-serif")},
        {QStringLiteral("fontColor"), QStringLiteral("#ffffff")},
        {QStringLiteral("textOpacity"), 100},
        {QStringLiteral("backgroundColor"), QStringLiteral("#000000")},
        {QStringLiteral("backgroundStyle"), QStringLiteral("box")},
        {QStringLiteral("backgroundOpacity"), 75},
        {QStringLiteral("textOutline"), false},
        {QStringLiteral("textOutlineColor"), QStringLiteral("#000000")},
        {QStringLiteral("position"), QStringLiteral("bottom")},
    };
}

// LibraryService stand-in: a settable scope and handlers answered by hand.
// It keeps "a read succeeded" per scope as LibraryService keeps the document.
class FakeLibraryService final : public LibraryService
{
public:
    FakeLibraryService() : LibraryService(nullptr) {}

    QString subtitleAppearanceScope() const override { return scope; }

    bool hasSubtitleAppearanceDocument(const QString &candidate) const override
    {
        return readScopes.contains(candidate);
    }

    void getSubtitleAppearance(SubtitleAppearanceHandler handler) override
    {
        const QString readScope = scope;
        requestLog.append(QStringLiteral("read ") + readScope);
        reads.append([this, readScope, handler](bool ok, const QVariantMap &result) {
            if (ok) {
                readScopes.insert(readScope);
            }
            handler(ok, result);
        });
    }

    void setSubtitleAppearance(const QVariantMap &appearance,
                               SubtitleAppearanceHandler handler) override
    {
        requestLog.append(QStringLiteral("write ") + scope);
        writtenAppearances.append(appearance);
        writes.append(handler);
    }

    void resetSubtitleAppearance(SubtitleAppearanceHandler handler) override
    {
        const QString resetScope = scope;
        requestLog.append(QStringLiteral("remove ") + resetScope);
        resets.append([this, resetScope, handler](bool ok, const QVariantMap &result) {
            if (ok) {
                readScopes.remove(resetScope);
            }
            handler(ok, result);
        });
    }

    QString scope;
    QSet<QString> readScopes;
    QList<SubtitleAppearanceHandler> reads;
    QList<SubtitleAppearanceHandler> writes;
    QList<SubtitleAppearanceHandler> resets;
    QList<QVariantMap> writtenAppearances;
    QStringList requestLog;
};

QVariantMap readResult(const QVariantMap &appearance)
{
    return {{QStringLiteral("appearance"), appearance},
            {QStringLiteral("hasValue"), true},
            {QStringLiteral("hasDeviceOverride"), false}};
}

const QVariantMap kNoValue{{QStringLiteral("hasValue"), false}};

}  // namespace

class SubtitleAppearanceTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void normalizesAndValidatesValues();
    void jsonRoundTrip();

    void configPersistsAndMarksUserChanges();
    void configAdoptsOnlyWhileUntouched();
    void configResetForgetsUserChange();

    void siloDocumentMapsSharedFields();
    void siloWriteKeepsUnmappedFields();
    void siloResponsesParse();
    void siloRequestsFollowTheContract();
    void jellyfinHasNoSubtitleAppearance();

    void syncAdoptsOnlyWhenUntouched();
    void syncWritesOnlyOnUserChange();
    void syncReadsBeforeFirstWrite();
    void syncKeepsLocalChangeWhenWriteFails();
    void syncRetriesLostWriteAfterRestart();
    void syncEditOnAnotherProfileWritesThatProfile();
    void syncKeepsWriteForItsScope();
    void syncResetRemovesOverrideAndAdoptsAgain();
    void syncFailedRemovalIsRetriedFirst();
    void syncResetWaitsForWriteInFlight();
    void syncLooksAgainAfterStaleAnswer();
    void syncStaysLocalWithoutCapability();

private:
    // Every ConfigManager here reads and writes a throwaway config home.
    QTemporaryDir m_configHome;
    std::unique_ptr<ScopedConfigIsolation> m_isolation;
};

void SubtitleAppearanceTest::initTestCase()
{
    QVERIFY(m_configHome.isValid());
    m_isolation = std::make_unique<ScopedConfigIsolation>(m_configHome.path());
}

void SubtitleAppearanceTest::cleanupTestCase()
{
    m_isolation.reset();
}

void SubtitleAppearanceTest::normalizesAndValidatesValues()
{
    const SubtitleAppearance defaults;
    QCOMPARE(defaults.withValues({{"sizePercent", 10}}).sizePercent, 75);
    QCOMPARE(defaults.withValues({{"sizePercent", 999}}).sizePercent, 200);
    QCOMPARE(defaults.withValues({{"sizePercent", 112}}).sizePercent, 110);
    QCOMPARE(defaults.withValues({{"sizePercent", 127.6}}).sizePercent, 130);
    QCOMPARE(defaults.withValues({{"sizePercent", "150"}}).sizePercent, 100);
    QCOMPARE(defaults.withValues({{"textColor", "#abcdef"}}).textColor, QStringLiteral("#ABCDEF"));
    QCOMPARE(defaults.withValues({{"textColor", "red"}}).textColor, QStringLiteral("default"));
    QCOMPARE(defaults.withValues({{"textColor", "#12345"}}).textColor, QStringLiteral("default"));
    // "Default" and an explicit White are different values.
    QCOMPARE(defaults.textColor, QStringLiteral("default"));
    QCOMPARE(defaults.withValues({{"textColor", "#FFFFFF"}}).textColor, QStringLiteral("#FFFFFF"));
    QCOMPARE(defaults.withValues({{"textColor", "#FFFFFF"}}).withValues({{"textColor", "Default"}}).textColor,
             QStringLiteral("default"));
    QCOMPARE(defaults.withValues({{"backgroundColor", "default"}}).backgroundColor,
             QStringLiteral("#000000"));
    QCOMPARE(defaults.withValues({{"font", "Comic Sans"}}).font, QStringLiteral("default"));
    QCOMPARE(defaults.withValues({{"outline", "huge"}}).outline, QStringLiteral("default"));
    QCOMPARE(defaults.withValues({{"background", "BOX"}}).background, QStringLiteral("box"));
    QCOMPARE(defaults.withValues({{"backgroundOpacity", 3}}).backgroundOpacity, 25);
    QCOMPARE(defaults.withValues({{"backgroundOpacity", 101}}).backgroundOpacity, 100);
    QCOMPARE(defaults.withValues({{"positionPercent", -5}}).positionPercent, 0);
    QCOMPARE(defaults.withValues({{"positionPercent", 90}}).positionPercent, 40);
    QCOMPARE(defaults.withValues({{"applyToStyled", 1}}).applyToStyled, false);
    QCOMPARE(defaults.withValues({{"unknown", 5}}), defaults);
}

void SubtitleAppearanceTest::jsonRoundTrip()
{
    const SubtitleAppearance appearance = SubtitleAppearance{}.withValues({
        {"sizePercent", 140}, {"font", "monospace"}, {"textColor", "#22C55E"},
        {"outline", "thin"}, {"shadow", "soft"}, {"positionPercent", 10},
        {"applyToStyled", true}});
    QCOMPARE(SubtitleAppearance::fromJson(appearance.toJson()), appearance);
    QCOMPARE(appearance.toJson().value(QStringLiteral("size_percent")).toInt(), 140);
    QCOMPARE(SubtitleAppearance::fromJson(QJsonObject{{"size_percent", "big"}}), SubtitleAppearance{});
}

void SubtitleAppearanceTest::configPersistsAndMarksUserChanges()
{
    {
        ConfigManager config;
        config.load();
        QCOMPARE(config.getSubtitleAppearance(), SubtitleAppearance{});
        QVERIFY(!config.isSubtitleAppearanceUserModified());

        QSignalSpy changed(&config, &ConfigManager::subtitleAppearanceChanged);
        QSignalSpy edited(&config, &ConfigManager::subtitleAppearanceEdited);
        config.setSubtitleAppearanceValue(QStringLiteral("sizePercent"), 135);
        config.setSubtitleAppearanceValue(QStringLiteral("textColor"), QStringLiteral("#facc15"));
        QCOMPARE(changed.count(), 2);
        QCOMPARE(edited.count(), 2);
        QVERIFY(config.isSubtitleAppearanceUserModified());

        // Invalid values and unchanged ones are no change.
        config.setSubtitleAppearanceValue(QStringLiteral("textColor"), QStringLiteral("yellow"));
        config.setSubtitleAppearanceValue(QStringLiteral("sizePercent"), 135);
        config.setSubtitleAppearanceValue(QStringLiteral("bogus"), 1);
        QCOMPARE(changed.count(), 2);
        QCOMPARE(config.getSubtitleAppearanceMap().value(QStringLiteral("textColor")).toString(),
                 QStringLiteral("#FACC15"));
        config.save();
    }
    ConfigManager reloaded;
    reloaded.load();
    QCOMPARE(reloaded.getSubtitleAppearance().sizePercent, 135);
    QCOMPARE(reloaded.getSubtitleAppearance().textColor, QStringLiteral("#FACC15"));
    QVERIFY(reloaded.isSubtitleAppearanceUserModified());
}

void SubtitleAppearanceTest::configAdoptsOnlyWhileUntouched()
{
    ConfigManager config;
    QSignalSpy edited(&config, &ConfigManager::subtitleAppearanceEdited);
    const SubtitleAppearance server = SubtitleAppearance{}.withValues({{"sizePercent", 150}});
    QVERIFY(config.adoptSubtitleAppearance(server));
    QCOMPARE(config.getSubtitleAppearance(), server);
    QVERIFY(!config.isSubtitleAppearanceUserModified());
    QCOMPARE(edited.count(), 0);

    config.setSubtitleAppearanceValue(QStringLiteral("font"), QStringLiteral("serif"));
    QVERIFY(!config.adoptSubtitleAppearance(SubtitleAppearance{}));
    QCOMPARE(config.getSubtitleAppearance().font, QStringLiteral("serif"));
    QCOMPARE(config.getSubtitleAppearance().sizePercent, 150);
}

void SubtitleAppearanceTest::configResetForgetsUserChange()
{
    ConfigManager config;
    config.setSubtitleAppearanceValue(QStringLiteral("background"), QStringLiteral("box"));
    QSignalSpy reset(&config, &ConfigManager::subtitleAppearanceReset);
    QSignalSpy edited(&config, &ConfigManager::subtitleAppearanceEdited);
    config.resetSubtitleAppearance();
    QCOMPARE(reset.count(), 1);
    QCOMPARE(edited.count(), 0);
    QCOMPARE(config.getSubtitleAppearance(), SubtitleAppearance{});
    QVERIFY(!config.isSubtitleAppearanceUserModified());
    QVERIFY(config.adoptSubtitleAppearance(SubtitleAppearance{}.withValues({{"sizePercent", 80}})));
}

void SubtitleAppearanceTest::siloDocumentMapsSharedFields()
{
    QVariantMap fields = SiloSubtitleAppearanceMapper::canonicalFields(webDefaultDocument());
    QCOMPARE(fields.value("sizePercent").toInt(), 100);
    QCOMPARE(fields.value("font").toString(), QStringLiteral("default"));
    QCOMPARE(fields.value("textColor").toString(), QStringLiteral("#FFFFFF"));
    QCOMPARE(fields.value("background").toString(), QStringLiteral("box"));
    QCOMPARE(fields.value("backgroundOpacity").toInt(), 75);
    QCOMPARE(fields.value("positionPercent").toInt(), 0);
    QVERIFY(!fields.contains("textOpacity"));
    QVERIFY(!fields.contains("outline"));

    QJsonObject outlined = webDefaultDocument();
    outlined["backgroundStyle"] = "outline";
    outlined["fontSize"] = "xxlarge";
    outlined["fontFamily"] = "serif";
    outlined["textOutlineColor"] = "#374151";
    outlined["position"] = "lower-third";
    fields = SiloSubtitleAppearanceMapper::canonicalFields(outlined);
    QCOMPARE(fields.value("sizePercent").toInt(), 150);
    QCOMPARE(fields.value("font").toString(), QStringLiteral("serif"));
    QCOMPARE(fields.value("background").toString(), QStringLiteral("off"));
    QCOMPARE(fields.value("outline").toString(), QStringLiteral("medium"));
    QCOMPARE(fields.value("outlineColor").toString(), QStringLiteral("#374151"));
    QCOMPARE(fields.value("positionPercent").toInt(), 5);

    QJsonObject plain = webDefaultDocument();
    plain["backgroundStyle"] = "none";
    plain["position"] = "top";
    plain["fontColor"] = "not a colour";
    fields = SiloSubtitleAppearanceMapper::canonicalFields(plain);
    QCOMPARE(fields.value("outline").toString(), QStringLiteral("off"));
    QCOMPARE(fields.value("shadow").toString(), QStringLiteral("off"));
    QVERIFY(!fields.contains("positionPercent"));
    QVERIFY(!fields.contains("textColor"));
}

void SubtitleAppearanceTest::siloWriteKeepsUnmappedFields()
{
    QJsonObject base = webDefaultDocument();
    base["textOpacity"] = 60;
    base["position"] = "top";
    base["futureField"] = "kept";
    const SubtitleAppearance appearance = SubtitleAppearance{}.withValues({
        {"sizePercent", 125}, {"font", "gandhi-sans"}, {"textColor", "#FACC15"},
        {"shadow", "strong"}, {"outline", "thin"}, {"positionPercent", 10}});
    const QJsonObject document = SiloSubtitleAppearanceMapper::document(appearance, base);
    QCOMPARE(document.value("fontSize").toString(), QStringLiteral("xlarge"));
    QCOMPARE(document.value("fontFamily").toString(), QStringLiteral("sans-serif"));
    QCOMPARE(document.value("fontColor").toString(), QStringLiteral("#facc15"));
    QCOMPARE(document.value("backgroundStyle").toString(), QStringLiteral("shadow"));
    QCOMPARE(document.value("textOutline").toBool(), true);
    QCOMPARE(document.value("position").toString(), QStringLiteral("lower-third"));
    QCOMPARE(document.value("textOpacity").toInt(), 60);
    QCOMPARE(document.value("futureField").toString(), QStringLiteral("kept"));

    const QJsonObject body = QJsonDocument::fromJson(SiloSubtitleAppearanceMapper::requestBody(
        SubtitleAppearance{}, QString::fromUtf8(QJsonDocument(base).toJson()))).object();
    QCOMPARE(body.keys(), QStringList{QStringLiteral("value")});
    const QJsonObject value = SiloSubtitleAppearanceMapper::parseDocument(body.value("value").toString());
    QCOMPARE(value.value("backgroundStyle").toString(), QStringLiteral("outline"));
    QCOMPARE(value.value("fontSize").toString(), QStringLiteral("large"));
    QCOMPARE(value.value("futureField").toString(), QStringLiteral("kept"));
}

void SubtitleAppearanceTest::siloResponsesParse()
{
    ProviderCatalogResponse response = SiloSubtitleAppearanceMapper::parseResponse(
        ProviderCatalogOperation::SubtitleAppearance, effectiveBody(webDefaultDocument(), true).toUtf8());
    QVERIFY(response.valid);
    QVERIFY(response.capabilityMetadata.value("hasValue").toBool());
    QVERIFY(response.capabilityMetadata.value("hasDeviceOverride").toBool());
    QCOMPARE(response.capabilityMetadata.value("appearance").toMap().value("background").toString(),
             QStringLiteral("box"));
    QVERIFY(!response.capabilityMetadata.value("document").toString().isEmpty());

    response = SiloSubtitleAppearanceMapper::parseResponse(
        ProviderCatalogOperation::SubtitleAppearance, effectiveBody({}).toUtf8());
    QVERIFY(response.valid);
    QVERIFY(!response.capabilityMetadata.value("hasValue").toBool());

    response = SiloSubtitleAppearanceMapper::parseResponse(
        ProviderCatalogOperation::SubtitleAppearance, QByteArrayLiteral("{\"key\":1}"));
    QVERIFY(!response.valid);

    // DELETE answers 204 with no body.
    response = SiloSubtitleAppearanceMapper::parseResponse(
        ProviderCatalogOperation::SetSubtitleAppearance, QByteArray());
    QVERIFY(response.valid);

    // A value encoded into a JSON string once more still reads.
    const QString inner = QString::fromUtf8(QJsonDocument(webDefaultDocument()).toJson(QJsonDocument::Compact));
    const QString twice = QString::fromUtf8(QJsonDocument(QJsonArray{inner}).toJson(QJsonDocument::Compact));
    QCOMPARE(SiloSubtitleAppearanceMapper::parseDocument(twice.mid(1, twice.size() - 2)),
             webDefaultDocument());
    QVERIFY(SiloSubtitleAppearanceMapper::parseDocument(QStringLiteral("not json")).isEmpty());
}

void SubtitleAppearanceTest::siloRequestsFollowTheContract()
{
    const SiloCatalogProvider provider;
    ProviderCatalogRequest request =
        provider.createRequest(ProviderCatalogOperation::SubtitleAppearance, {});
    QVERIFY(request.supported);
    QCOMPARE(request.method, ProviderHttpMethod::Get);
    QCOMPARE(request.relativeEndpoint,
             QStringLiteral("/api/v2/settings/subtitle-appearance/effective"));
    QCOMPARE(request.retrySafety, ProviderCatalogRetrySafety::Idempotent);

    ProviderCatalogQuery query;
    query.stateValue = true;
    query.subtitleAppearance = SubtitleAppearance{}.withValues({{"sizePercent", 150}}).toVariantMap();
    query.subtitleAppearanceDocument = QStringLiteral("{\"textOpacity\":40}");
    request = provider.createRequest(ProviderCatalogOperation::SetSubtitleAppearance, query);
    QVERIFY(request.supported);
    QCOMPARE(request.method, ProviderHttpMethod::Put);
    QCOMPARE(request.relativeEndpoint, QStringLiteral("/api/v2/settings/device/subtitle-appearance"));
    QCOMPARE(request.retrySafety, ProviderCatalogRetrySafety::Idempotent);
    const QJsonObject value = SiloSubtitleAppearanceMapper::parseDocument(
        QJsonDocument::fromJson(request.body).object().value("value").toString());
    QCOMPARE(value.value("fontSize").toString(), QStringLiteral("xxlarge"));
    QCOMPARE(value.value("textOpacity").toInt(), 40);

    query.stateValue = false;
    request = provider.createRequest(ProviderCatalogOperation::SetSubtitleAppearance, query);
    QCOMPARE(request.method, ProviderHttpMethod::Delete);
    QVERIFY(request.body.isEmpty());
    QCOMPARE(request.retrySafety, ProviderCatalogRetrySafety::Idempotent);

    QVERIFY(SiloProviderAdapter().supportsCapability(ProviderCapability::SubtitleAppearance));
}

void SubtitleAppearanceTest::jellyfinHasNoSubtitleAppearance()
{
    const JellyfinCatalogProvider provider;
    QVERIFY(!provider.createRequest(ProviderCatalogOperation::SubtitleAppearance, {}).supported);
    QVERIFY(!provider.createRequest(ProviderCatalogOperation::SetSubtitleAppearance, {}).supported);
    QVERIFY(!JellyfinProviderAdapter().supportsCapability(ProviderCapability::SubtitleAppearance));
}

void SubtitleAppearanceTest::syncAdoptsOnlyWhenUntouched()
{
    ConfigManager config;
    FakeLibraryService library;
    library.scope = QStringLiteral("silo\n1");
    SubtitleAppearanceSync sync(&config, &library);

    sync.refresh();
    QCOMPARE(library.reads.size(), 1);
    library.reads.takeFirst()(true, readResult({{"sizePercent", 150}, {"background", "box"}}));
    QCOMPARE(config.getSubtitleAppearance().sizePercent, 150);
    QCOMPARE(config.getSubtitleAppearance().background, QStringLiteral("box"));
    QCOMPARE(config.subtitleAppearanceSourceScope(), QStringLiteral("silo\n1"));
    QVERIFY(!config.isSubtitleAppearanceUserModified());
    // Adoption is no user change: nothing is written back.
    QVERIFY(!sync.writePending());
    QVERIFY(library.writes.isEmpty());

    // A profile with no appearance of its own gets the defaults, not the
    // look adopted from the previous profile.
    library.scope = QStringLiteral("silo\n2");
    sync.refresh();
    library.reads.takeFirst()(true, kNoValue);
    QCOMPARE(config.getSubtitleAppearance(), SubtitleAppearance{});
    QCOMPARE(config.subtitleAppearanceSourceScope(), QStringLiteral("silo\n2"));

    // After a user change, another profile's appearance is not adopted.
    config.setSubtitleAppearanceValue(QStringLiteral("sizePercent"), 90);
    QVERIFY(config.subtitleAppearanceSourceScope().isEmpty());
    library.scope = QStringLiteral("silo\n3");
    sync.refresh();
    library.reads.takeFirst()(true, readResult({{"sizePercent", 200}}));
    QCOMPARE(config.getSubtitleAppearance().sizePercent, 90);
    library.scope = QStringLiteral("silo\n4");
    sync.refresh();
    library.reads.takeFirst()(true, kNoValue);
    QCOMPARE(config.getSubtitleAppearance().sizePercent, 90);
}

void SubtitleAppearanceTest::syncWritesOnlyOnUserChange()
{
    ConfigManager config;
    FakeLibraryService library;
    library.scope = QStringLiteral("silo\n1");
    SubtitleAppearanceSync sync(&config, &library);
    sync.refresh();
    library.reads.takeFirst()(true, readResult({{"sizePercent", 125}}));
    QVERIFY(library.writes.isEmpty());

    config.setSubtitleAppearanceValue(QStringLiteral("sizePercent"), 130);
    config.setSubtitleAppearanceValue(QStringLiteral("sizePercent"), 140);
    QVERIFY(sync.writePending());
    QVERIFY(config.isSubtitleAppearanceWritePending(library.scope));
    QVERIFY(library.writes.isEmpty());
    // Debounced into one write of the latest appearance.
    QTRY_COMPARE_WITH_TIMEOUT(library.writes.size(), 1,
                              SubtitleAppearanceSync::kWriteDebounceMs + 2000);
    QCOMPARE(library.writtenAppearances.first().value("sizePercent").toInt(), 140);

    // A change while the write is out is sent after it.
    config.setSubtitleAppearanceValue(QStringLiteral("font"), QStringLiteral("serif"));
    sync.flushPendingWrite();
    QCOMPARE(library.writes.size(), 1);
    library.writes.takeFirst()(true, {});
    // The first answer does not clear the mark of the newer change.
    QVERIFY(config.isSubtitleAppearanceWritePending(library.scope));
    QCOMPARE(library.writes.size(), 1);
    QCOMPARE(library.writtenAppearances.last().value("font").toString(), QStringLiteral("serif"));
    library.writes.takeFirst()(true, {});
    QVERIFY(!config.isSubtitleAppearanceWritePending(library.scope));
    QVERIFY(library.writes.isEmpty());
}

void SubtitleAppearanceTest::syncReadsBeforeFirstWrite()
{
    ConfigManager config;
    FakeLibraryService library;
    library.scope = QStringLiteral("silo\n1");
    SubtitleAppearanceSync sync(&config, &library);

    // An edit before the scope's first read reads first, during the
    // debounce, so the server's unmapped fields are known to the write.
    config.setSubtitleAppearanceValue(QStringLiteral("sizePercent"), 160);
    QCOMPARE(library.reads.size(), 1);
    sync.flushPendingWrite();
    QVERIFY(library.writes.isEmpty());

    // A failed read keeps the change local and unwritten.
    library.reads.takeFirst()(false, {});
    QVERIFY(library.writes.isEmpty());
    QCOMPARE(config.getSubtitleAppearance().sizePercent, 160);
    QVERIFY(config.isSubtitleAppearanceWritePending(library.scope));

    // The next chance reads again; the answer is not adopted over the
    // change, and the write follows.
    sync.refresh();
    QCOMPARE(library.reads.size(), 1);
    library.reads.takeFirst()(true, readResult({{"sizePercent", 200}}));
    QCOMPARE(config.getSubtitleAppearance().sizePercent, 160);
    QCOMPARE(library.writes.size(), 1);
    QCOMPARE(library.writtenAppearances.first().value("sizePercent").toInt(), 160);
    QCOMPARE(library.requestLog,
             (QStringList{"read silo\n1", "read silo\n1", "write silo\n1"}));
}

void SubtitleAppearanceTest::syncKeepsLocalChangeWhenWriteFails()
{
    ConfigManager config;
    FakeLibraryService library;
    library.scope = QStringLiteral("silo\n1");
    library.readScopes.insert(library.scope);
    SubtitleAppearanceSync sync(&config, &library);
    config.setSubtitleAppearanceValue(QStringLiteral("textColor"), QStringLiteral("#22C55E"));
    sync.flushPendingWrite();
    QCOMPARE(library.writes.size(), 1);
    library.writes.takeFirst()(false, {});
    QCOMPARE(config.getSubtitleAppearance().textColor, QStringLiteral("#22C55E"));
    QVERIFY(config.isSubtitleAppearanceUserModified());
    QVERIFY(config.isSubtitleAppearanceWritePending(library.scope));
    QVERIFY(!sync.writePending());
}

void SubtitleAppearanceTest::syncRetriesLostWriteAfterRestart()
{
    const QString scope = QStringLiteral("restart-write\n1");
    {
        ConfigManager config;
        config.load();
        FakeLibraryService library;
        library.scope = scope;
        library.readScopes.insert(scope);
        SubtitleAppearanceSync sync(&config, &library);
        config.setSubtitleAppearanceValue(QStringLiteral("sizePercent"), 170);
        sync.flushPendingWrite();
        library.writes.takeFirst()(false, {});
        config.save();
    }
    ConfigManager config;
    config.load();
    QVERIFY(config.isSubtitleAppearanceWritePending(scope));
    FakeLibraryService library;
    library.scope = scope;
    SubtitleAppearanceSync sync(&config, &library);
    sync.refresh();
    // A read for the document, never adopted over the change, then the write.
    QCOMPARE(library.reads.size(), 1);
    library.reads.takeFirst()(true, readResult({{"sizePercent", 80}}));
    QCOMPARE(config.getSubtitleAppearance().sizePercent, 170);
    QCOMPARE(library.writes.size(), 1);
    QCOMPARE(library.writtenAppearances.first().value("sizePercent").toInt(), 170);
    library.writes.takeFirst()(true, {});
    QVERIFY(!config.isSubtitleAppearanceWritePending(scope));
    // That read was the refresh's read too.
    QVERIFY(library.reads.isEmpty());
}

void SubtitleAppearanceTest::syncEditOnAnotherProfileWritesThatProfile()
{
    ConfigManager config;
    FakeLibraryService library;
    library.scope = QStringLiteral("silo\n1");
    SubtitleAppearanceSync sync(&config, &library);
    sync.refresh();
    library.reads.takeFirst()(true, readResult({{"sizePercent", 125}}));
    config.setSubtitleAppearanceValue(QStringLiteral("sizePercent"), 130);
    sync.flushPendingWrite();
    library.writes.takeFirst()(true, {});

    library.scope = QStringLiteral("silo\n2");
    sync.refresh();
    library.reads.takeFirst()(true, readResult({{"sizePercent", 200}}));
    QCOMPARE(config.getSubtitleAppearance().sizePercent, 130);

    // The device's appearance, with this edit, becomes profile 2's override.
    config.setSubtitleAppearanceValue(QStringLiteral("font"), QStringLiteral("serif"));
    sync.flushPendingWrite();
    QCOMPARE(library.writes.size(), 1);
    QCOMPARE(library.requestLog.constLast(), QStringLiteral("write silo\n2"));
    QCOMPARE(library.writtenAppearances.last().value("sizePercent").toInt(), 130);
    QCOMPARE(library.writtenAppearances.last().value("font").toString(), QStringLiteral("serif"));
    library.writes.takeFirst()(true, {});
    QVERIFY(!config.isSubtitleAppearanceWritePending(QStringLiteral("silo\n1")));
    QVERIFY(!config.isSubtitleAppearanceWritePending(QStringLiteral("silo\n2")));
}

void SubtitleAppearanceTest::syncKeepsWriteForItsScope()
{
    ConfigManager config;
    FakeLibraryService library;
    library.scope = QStringLiteral("silo\n1");
    library.readScopes = {QStringLiteral("silo\n1"), QStringLiteral("silo\n2")};
    SubtitleAppearanceSync sync(&config, &library);
    config.setSubtitleAppearanceValue(QStringLiteral("sizePercent"), 160);
    QVERIFY(sync.writePending());

    // Not sent to another profile...
    library.scope = QStringLiteral("silo\n2");
    sync.flushPendingWrite();
    QVERIFY(library.writes.isEmpty());
    QVERIFY(config.isSubtitleAppearanceWritePending(QStringLiteral("silo\n1")));

    // ...but once profile 1 is back.
    library.scope = QStringLiteral("silo\n1");
    sync.refresh();
    QCOMPARE(library.writes.size(), 1);
    QCOMPARE(library.requestLog.constLast(), QStringLiteral("write silo\n1"));
}

void SubtitleAppearanceTest::syncResetRemovesOverrideAndAdoptsAgain()
{
    ConfigManager config;
    FakeLibraryService library;
    library.scope = QStringLiteral("silo\n1");
    library.readScopes.insert(library.scope);
    SubtitleAppearanceSync sync(&config, &library);
    config.setSubtitleAppearanceValue(QStringLiteral("sizePercent"), 160);
    QVERIFY(sync.writePending());

    config.resetSubtitleAppearance();
    QVERIFY(!sync.writePending());
    QVERIFY(!config.isSubtitleAppearanceWritePending(library.scope));
    QVERIFY(config.isSubtitleAppearanceDeletePending(library.scope));
    QCOMPARE(library.resets.size(), 1);
    library.resets.takeFirst()(true, {});
    QVERIFY(!config.isSubtitleAppearanceDeletePending(library.scope));
    // The profile-wide appearance is read and becomes the starting point.
    QCOMPARE(library.reads.size(), 1);
    library.reads.takeFirst()(true, readResult({{"font", "monospace"}}));
    QCOMPARE(config.getSubtitleAppearance().font, QStringLiteral("monospace"));
    QVERIFY(!config.isSubtitleAppearanceUserModified());
    QVERIFY(library.writes.isEmpty());
}

void SubtitleAppearanceTest::syncFailedRemovalIsRetriedFirst()
{
    const QString scope = QStringLiteral("restart-remove\n1");
    {
        ConfigManager config;
        config.load();
        FakeLibraryService library;
        library.scope = scope;
        library.readScopes.insert(scope);
        SubtitleAppearanceSync sync(&config, &library);
        config.setSubtitleAppearanceValue(QStringLiteral("sizePercent"), 160);
        config.resetSubtitleAppearance();
        library.resets.takeFirst()(false, {});
        QVERIFY(config.isSubtitleAppearanceDeletePending(scope));

        // A refresh removes the override again before it reads anything,
        // so the old override cannot be read back and adopted.
        sync.refresh();
        QVERIFY(library.reads.isEmpty());
        QCOMPARE(library.resets.size(), 1);
        library.resets.takeFirst()(false, {});
        QVERIFY(library.reads.isEmpty());
        QCOMPARE(library.requestLog, (QStringList{"remove " + scope, "remove " + scope}));
    }
    // The removal survives a restart.
    ConfigManager config;
    config.load();
    QVERIFY(config.isSubtitleAppearanceDeletePending(scope));
    FakeLibraryService library;
    library.scope = scope;
    SubtitleAppearanceSync sync(&config, &library);
    sync.refresh();
    QCOMPARE(library.requestLog, QStringList{"remove " + scope});
    library.resets.takeFirst()(true, {});
    QVERIFY(!config.isSubtitleAppearanceDeletePending(scope));
    QCOMPARE(library.reads.size(), 1);
}

void SubtitleAppearanceTest::syncResetWaitsForWriteInFlight()
{
    ConfigManager config;
    FakeLibraryService library;
    library.scope = QStringLiteral("silo\n1");
    library.readScopes.insert(library.scope);
    SubtitleAppearanceSync sync(&config, &library);
    config.setSubtitleAppearanceValue(QStringLiteral("sizePercent"), 160);
    sync.flushPendingWrite();
    QCOMPARE(library.writes.size(), 1);

    // The removal waits for the write on the wire, so it reaches the server
    // last and the write cannot bring the override back.
    config.resetSubtitleAppearance();
    QVERIFY(library.resets.isEmpty());
    library.writes.takeFirst()(true, {});
    QCOMPARE(library.resets.size(), 1);

    // A change made while the removal is out is written after it, over a
    // fresh read, and the read is not adopted over it.
    config.setSubtitleAppearanceValue(QStringLiteral("font"), QStringLiteral("serif"));
    sync.flushPendingWrite();
    library.resets.takeFirst()(true, {});
    QVERIFY(library.writes.isEmpty());
    QCOMPARE(library.reads.size(), 1);
    library.reads.takeFirst()(true, readResult({{"font", "monospace"}}));
    QCOMPARE(config.getSubtitleAppearance().font, QStringLiteral("serif"));
    QCOMPARE(library.writes.size(), 1);
    QCOMPARE(library.requestLog,
             (QStringList{"write silo\n1", "remove silo\n1", "read silo\n1", "write silo\n1"}));
}

void SubtitleAppearanceTest::syncLooksAgainAfterStaleAnswer()
{
    ConfigManager config;
    FakeLibraryService library;
    library.scope = QStringLiteral("silo\n1");
    SubtitleAppearanceSync sync(&config, &library);
    sync.refresh();
    library.reads.takeFirst()(false, {{QStringLiteral("stale"), true}});
    QTRY_COMPARE_WITH_TIMEOUT(library.reads.size(), 1, 1000);
}

void SubtitleAppearanceTest::syncStaysLocalWithoutCapability()
{
    ConfigManager config;
    FakeLibraryService library;  // Jellyfin: no scope
    SubtitleAppearanceSync sync(&config, &library);
    sync.refresh();
    config.setSubtitleAppearanceValue(QStringLiteral("sizePercent"), 120);
    config.resetSubtitleAppearance();
    QVERIFY(!sync.writePending());
    QVERIFY(library.reads.isEmpty());
    QVERIFY(library.resets.isEmpty());
    QVERIFY(library.writes.isEmpty());
}

QTEST_GUILESS_MAIN(SubtitleAppearanceTest)
#include "SubtitleAppearanceTest.moc"
