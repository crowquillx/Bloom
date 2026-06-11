#include <QtTest/QtTest>

#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "utils/ConfigManager.h"

class ConfigManagerThemeTest : public QObject
{
    Q_OBJECT

private slots:
    void defaultsIncludeThemeVariants();
    void themeVariantSettersPersistAndEmit();
    void v19MigrationAddsThemeVariantSettings();
    void hdrPolicyDefaultsToMatchContent();
    void hdrMpvArgsRespectOutputMode();
};

namespace {

class ScopedConfigIsolation
{
public:
    explicit ScopedConfigIsolation(const QString &path)
        : m_previousConfigHome(qgetenv("XDG_CONFIG_HOME"))
        , m_previousAppData(qgetenv("APPDATA"))
        , m_previousHome(qgetenv("HOME"))
        , m_hadPreviousConfigHome(!m_previousConfigHome.isNull())
        , m_hadPreviousAppData(!m_previousAppData.isNull())
        , m_hadPreviousHome(!m_previousHome.isNull())
    {
        QStandardPaths::setTestModeEnabled(true);
        qputenv("XDG_CONFIG_HOME", path.toUtf8());
        qputenv("APPDATA", path.toUtf8());
        qputenv("HOME", path.toUtf8());
        QDir().mkpath(path + QStringLiteral("/Library/Preferences"));
    }

    ~ScopedConfigIsolation()
    {
        restoreEnv("XDG_CONFIG_HOME", m_previousConfigHome, m_hadPreviousConfigHome);
        restoreEnv("APPDATA", m_previousAppData, m_hadPreviousAppData);
        restoreEnv("HOME", m_previousHome, m_hadPreviousHome);
        QStandardPaths::setTestModeEnabled(false);
    }

private:
    static void restoreEnv(const char *name, const QByteArray &value, bool hadPrevious)
    {
        if (hadPrevious) {
            qputenv(name, value);
        } else {
            qunsetenv(name);
        }
    }

    QByteArray m_previousConfigHome;
    QByteArray m_previousAppData;
    QByteArray m_previousHome;
    bool m_hadPreviousConfigHome = false;
    bool m_hadPreviousAppData = false;
    bool m_hadPreviousHome = false;
};

QJsonObject minimalV19Config()
{
    QJsonObject playback;
    playback["completion_threshold"] = 90;

    QJsonObject ui;
    ui["theme"] = QStringLiteral("Jellyfin");

    QJsonObject settings;
    settings["playback"] = playback;
    settings["ui"] = ui;

    QJsonObject config;
    config["version"] = 19;
    config["settings"] = settings;
    return config;
}

} // namespace

void ConfigManagerThemeTest::defaultsIncludeThemeVariants()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ScopedConfigIsolation configIsolation(tempDir.path());

    ConfigManager config;
    config.load();

    QCOMPARE(config.getTheme(), QStringLiteral("Jellyfin"));
    QCOMPARE(config.getThemeFlavor(), QString());
    QCOMPARE(config.getThemeColorScheme(), QStringLiteral("blue"));
}

void ConfigManagerThemeTest::themeVariantSettersPersistAndEmit()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ScopedConfigIsolation configIsolation(tempDir.path());

    ConfigManager config;
    config.load();

    QSignalSpy flavorSpy(&config, &ConfigManager::themeFlavorChanged);
    QSignalSpy colorSchemeSpy(&config, &ConfigManager::themeColorSchemeChanged);

    config.setThemeColorScheme(QStringLiteral("   "));
    QCOMPARE(config.getThemeColorScheme(), QStringLiteral("blue"));
    QCOMPARE(colorSchemeSpy.count(), 0);

    config.setThemeFlavor(QStringLiteral(" Mocha "));
    config.setThemeColorScheme(QStringLiteral(" Mauve "));

    QCOMPARE(config.getThemeFlavor(), QStringLiteral("mocha"));
    QCOMPARE(config.getThemeColorScheme(), QStringLiteral("mauve"));
    QCOMPARE(flavorSpy.count(), 1);
    QCOMPARE(colorSchemeSpy.count(), 1);

    config.setThemeFlavor(QStringLiteral("   "));
    QCOMPARE(config.getThemeFlavor(), QStringLiteral("mocha"));
    QCOMPARE(flavorSpy.count(), 1);

    ConfigManager reloaded;
    reloaded.load();
    QCOMPARE(reloaded.getThemeFlavor(), QStringLiteral("mocha"));
    QCOMPARE(reloaded.getThemeColorScheme(), QStringLiteral("mauve"));
}

void ConfigManagerThemeTest::v19MigrationAddsThemeVariantSettings()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ScopedConfigIsolation configIsolation(tempDir.path());

    const QString bloomDir = ConfigManager::getConfigDir();
    QVERIFY(QDir().mkpath(bloomDir));

    QFile configFile(bloomDir + QStringLiteral("/app.json"));
    QVERIFY(configFile.open(QIODevice::WriteOnly));
    configFile.write(QJsonDocument(minimalV19Config()).toJson(QJsonDocument::Indented));
    configFile.close();

    ConfigManager config;
    config.load();

    QCOMPARE(config.getTheme(), QStringLiteral("Jellyfin"));
    QCOMPARE(config.getThemeFlavor(), QString());
    QCOMPARE(config.getThemeColorScheme(), QStringLiteral("blue"));
    config.save();

    QFile migratedFile(ConfigManager::getConfigPath());
    QVERIFY(migratedFile.open(QIODevice::ReadOnly));
    const QJsonObject migrated = QJsonDocument::fromJson(migratedFile.readAll()).object();
    QCOMPARE(migrated.value(QStringLiteral("version")).toInt(), 20);
    const QJsonObject ui = migrated.value(QStringLiteral("settings")).toObject().value(QStringLiteral("ui")).toObject();
    QVERIFY(ui.contains(QStringLiteral("theme_flavor")));
    QCOMPARE(ui.value(QStringLiteral("theme_color_scheme")).toString(), QStringLiteral("blue"));
}

void ConfigManagerThemeTest::hdrPolicyDefaultsToMatchContent()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ScopedConfigIsolation configIsolation(tempDir.path());

    ConfigManager config;
    config.load();

    QCOMPARE(config.getEnableHDR(), false);
    QCOMPARE(config.getHDROutputMode(), QStringLiteral("match-content"));
    QCOMPARE(config.getDolbyVisionFallbackMode(), QStringLiteral("prefer-compatible-hdr"));

    QSignalSpy hdrModeSpy(&config, &ConfigManager::hdrOutputModeChanged);
    QSignalSpy dvFallbackSpy(&config, &ConfigManager::dolbyVisionFallbackModeChanged);

    config.setHDROutputMode(QStringLiteral("ToneMapToSdr"));
    config.setDolbyVisionFallbackMode(QStringLiteral("ExperimentalDirectPlay"));

    QCOMPARE(config.getHDROutputMode(), QStringLiteral("tone-map-to-sdr"));
    QCOMPARE(config.getDolbyVisionFallbackMode(), QStringLiteral("experimental-direct-play"));
    QCOMPARE(hdrModeSpy.count(), 1);
    QCOMPARE(dvFallbackSpy.count(), 1);

    ConfigManager reloaded;
    reloaded.load();
    QCOMPARE(reloaded.getHDROutputMode(), QStringLiteral("tone-map-to-sdr"));
    QCOMPARE(reloaded.getDolbyVisionFallbackMode(), QStringLiteral("experimental-direct-play"));
}

void ConfigManagerThemeTest::hdrMpvArgsRespectOutputMode()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ScopedConfigIsolation configIsolation(tempDir.path());

    ConfigManager config;
    config.load();

    QStringList args = config.getMpvArgsForProfile(QStringLiteral("Default"), true);
    QVERIFY(args.contains(QStringLiteral("--target-colorspace-hint=no")));
    QVERIFY(args.contains(QStringLiteral("--tone-mapping=auto")));

    config.setEnableHDR(true);
    args = config.getMpvArgsForProfile(QStringLiteral("Default"), false);
    QVERIFY(!args.contains(QStringLiteral("--target-colorspace-hint=auto")));

    args = config.getMpvArgsForProfile(QStringLiteral("Default"), true);
    QVERIFY(args.contains(QStringLiteral("--vo=gpu-next")));
    QVERIFY(args.contains(QStringLiteral("--target-colorspace-hint=auto")));
    QVERIFY(args.contains(QStringLiteral("--target-colorspace-hint-mode=target")));
    QVERIFY(!args.contains(QStringLiteral("--gpu-api=d3d11")));
    QVERIFY(!args.contains(QStringLiteral("--gpu-context=d3d11")));

    args = config.getMpvArgsForProfile(QStringLiteral("Default"), true, true);
    QVERIFY(args.contains(QStringLiteral("--target-colorspace-hint=no")));
    QVERIFY(args.contains(QStringLiteral("--tone-mapping=auto")));
    QVERIFY(args.contains(QStringLiteral("--hdr-compute-peak=auto")));

    config.setHDROutputMode(QStringLiteral("tone-map-to-sdr"));
    args = config.getMpvArgsForProfile(QStringLiteral("Default"), true);
    QVERIFY(args.contains(QStringLiteral("--target-colorspace-hint=no")));
    QVERIFY(args.contains(QStringLiteral("--tone-mapping=auto")));
    QVERIFY(args.contains(QStringLiteral("--hdr-compute-peak=auto")));
}

QTEST_MAIN(ConfigManagerThemeTest)
#include "ConfigManagerThemeTest.moc"
