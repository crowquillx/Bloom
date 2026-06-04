#include <QtTest/QtTest>

#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "utils/ConfigManager.h"

class ConfigManagerThemeTest : public QObject
{
    Q_OBJECT

private slots:
    void defaultsIncludeThemeVariants();
    void themeVariantSettersPersistAndEmit();
    void v19MigrationAddsThemeVariantSettings();
};

namespace {

void useConfigHome(const QString &path)
{
    qputenv("XDG_CONFIG_HOME", path.toUtf8());
}

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
    useConfigHome(tempDir.path());

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
    useConfigHome(tempDir.path());

    ConfigManager config;
    config.load();

    QSignalSpy flavorSpy(&config, &ConfigManager::themeFlavorChanged);
    QSignalSpy colorSchemeSpy(&config, &ConfigManager::themeColorSchemeChanged);

    config.setThemeFlavor(QStringLiteral(" Mocha "));
    config.setThemeColorScheme(QStringLiteral(" Mauve "));

    QCOMPARE(config.getThemeFlavor(), QStringLiteral("mocha"));
    QCOMPARE(config.getThemeColorScheme(), QStringLiteral("mauve"));
    QCOMPARE(flavorSpy.count(), 1);
    QCOMPARE(colorSchemeSpy.count(), 1);

    ConfigManager reloaded;
    reloaded.load();
    QCOMPARE(reloaded.getThemeFlavor(), QStringLiteral("mocha"));
    QCOMPARE(reloaded.getThemeColorScheme(), QStringLiteral("mauve"));
}

void ConfigManagerThemeTest::v19MigrationAddsThemeVariantSettings()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    useConfigHome(tempDir.path());

    const QString bloomDir = tempDir.filePath(QStringLiteral("Bloom"));
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

QTEST_MAIN(ConfigManagerThemeTest)
#include "ConfigManagerThemeTest.moc"
