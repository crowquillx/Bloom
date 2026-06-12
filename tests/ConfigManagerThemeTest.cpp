#include <QtTest/QtTest>

#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "utils/ConfigManager.h"
#include "utils/MpvArgFilter.h"

class ConfigManagerThemeTest : public QObject
{
    Q_OBJECT

private slots:
    void defaultsIncludeThemeVariants();
    void defaultsContainNewBuiltinMpvProfiles();
    void themeVariantSettersPersistAndEmit();
    void v19MigrationAddsThemeVariantSettings();
    void v20MigrationRenamesDefaultProfileAssignments();
    void v21MigrationAddsArtProfilesWithoutChangingAssignments();
    void hdrPolicyDefaultsToMatchContent();
    void hdrMpvArgsRespectOutputMode();
    void builtinMpvProfilesEmitExpectedQualityArgs();
    void artMpvProfilesEmitExpectedShaderAndDebandArgs();
    void mpvProfileHdrAndWindowsOutputFieldsDefaultAndPersist();
    void hdrMpvArgsUseProfileMetadataMode();
    void v23MigrationPreservesCustomizedBuiltInProfiles();
    void startupBufferingModesDefaultNormalizeAndPersist();
    void bundledMpvShadersAreCopied();
    void bundledMpvFontsAreCopied();
    void importMpvConfigCreatesProfile();
    void importMpvConfigNormalizesQuotedShaderArgs();
    void existingMpvProfileArgsAreSanitizedOnLoad();
    void setMpvProfileNormalizesExtraArgs();
    void mpvProfileWindowsRenderApiDefaultsAndNormalizes();
    void setMpvProfilePreservesWindowsRenderApiValues();
    void renameMpvProfilePreservesProfileAndAssignments();
    void renameMpvProfileRejectsInvalidRequests();
    void deleteMpvProfileRejectsBuiltins();
    void importMpvConfigFiltersBloomManagedOptions();
    void importMpvConfigIgnoresProfileSections();
    void importMpvConfigRejectsDuplicateOrEmptyNames();
    void importMpvConfigMissingFileReturnsError();
    void embeddedMpvShaderPartitionPreservesOrder();
    void resolveMpvPortablePathExpandsConfigDirPrefix();
    void joinMpvPathListOptionValueUsesPlatformSeparator();
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

QJsonObject minimalV20ConfigWithDefaultAssignments()
{
    QJsonObject defaultProfile;
    defaultProfile[QStringLiteral("hwdec_enabled")] = true;
    defaultProfile[QStringLiteral("hwdec_method")] = QStringLiteral("auto");
    defaultProfile[QStringLiteral("deinterlace")] = false;
    defaultProfile[QStringLiteral("deinterlace_method")] = QString();
    defaultProfile[QStringLiteral("video_output")] = QStringLiteral("gpu-next");
    defaultProfile[QStringLiteral("interpolation")] = false;
    defaultProfile[QStringLiteral("windows_render_api")] = QStringLiteral("auto");
    defaultProfile[QStringLiteral("extra_args")] = QJsonArray{QStringLiteral("--fullscreen")};

    QJsonObject highQuality = defaultProfile;
    highQuality[QStringLiteral("extra_args")] = QJsonArray{
        QStringLiteral("--fullscreen"),
        QStringLiteral("--profile=high-quality")
    };

    QJsonObject customDefault = defaultProfile;
    customDefault[QStringLiteral("extra_args")] = QJsonArray{
        QStringLiteral("--fullscreen"),
        QStringLiteral("--sub-auto=fuzzy")
    };

    QJsonObject profiles;
    profiles[QStringLiteral("Default")] = customDefault;
    profiles[QStringLiteral("High Quality")] = highQuality;

    QJsonObject settings;
    settings[QStringLiteral("playback")] = QJsonObject{{QStringLiteral("completion_threshold"), 90}};
    settings[QStringLiteral("mpv_profiles")] = profiles;
    settings[QStringLiteral("default_profile")] = QStringLiteral("Default");
    settings[QStringLiteral("library_profiles")] = QJsonObject{{QStringLiteral("library-1"), QStringLiteral("Default")}};
    settings[QStringLiteral("series_profiles")] = QJsonObject{{QStringLiteral("series-1"), QStringLiteral("Default")}};

    QJsonObject config;
    config[QStringLiteral("version")] = 20;
    config[QStringLiteral("settings")] = settings;
    return config;
}

QJsonObject minimalV21ConfigWithAssignments()
{
    QJsonObject config = minimalV20ConfigWithDefaultAssignments();
    QJsonObject settings = config.value(QStringLiteral("settings")).toObject();
    QJsonObject profiles = ConfigManager::defaultMpvProfiles();
    profiles.remove(QStringLiteral("ArtCNN"));
    profiles.remove(QStringLiteral("ArtCNN-Deband"));
    profiles.remove(QStringLiteral("nnedi3"));
    profiles.remove(QStringLiteral("nnedi3-deband"));
    settings[QStringLiteral("mpv_profiles")] = profiles;
    settings[QStringLiteral("default_profile")] = QStringLiteral("High Quality");
    settings[QStringLiteral("library_profiles")] = QJsonObject{{QStringLiteral("library-1"), QStringLiteral("Low Quality")}};
    settings[QStringLiteral("series_profiles")] = QJsonObject{{QStringLiteral("series-1"), QStringLiteral("High Quality")}};
    config[QStringLiteral("version")] = 21;
    config[QStringLiteral("settings")] = settings;
    return config;
}

QJsonObject minimalV22ConfigWithCustomizedAnimeProfile()
{
    QJsonObject profiles = ConfigManager::defaultMpvProfiles();
    for (const QString &name : profiles.keys()) {
        QJsonObject profile = profiles.value(name).toObject();
        profile.remove(QStringLiteral("hdr_metadata_mode"));
        profile.remove(QStringLiteral("windows_10bit_output"));
        if (name == QStringLiteral("ArtCNN") || name == QStringLiteral("ArtCNN-Deband")
            || name == QStringLiteral("nnedi3") || name == QStringLiteral("nnedi3-deband")) {
            profile[QStringLiteral("windows_render_api")] = QStringLiteral("auto");
        }
        profiles[name] = profile;
    }

    QJsonObject customizedArt = profiles.value(QStringLiteral("ArtCNN")).toObject();
    QJsonArray extraArgs = customizedArt.value(QStringLiteral("extra_args")).toArray();
    extraArgs.append(QStringLiteral("--sub-auto=fuzzy"));
    customizedArt[QStringLiteral("extra_args")] = extraArgs;
    profiles[QStringLiteral("ArtCNN")] = customizedArt;

    QJsonObject settings;
    settings[QStringLiteral("playback")] = QJsonObject{{QStringLiteral("completion_threshold"), 90}};
    settings[QStringLiteral("mpv_profiles")] = profiles;
    settings[QStringLiteral("default_profile")] = QStringLiteral("Medium Quality");
    settings[QStringLiteral("library_profiles")] = QJsonObject();
    settings[QStringLiteral("series_profiles")] = QJsonObject();

    QJsonObject config;
    config[QStringLiteral("version")] = 22;
    config[QStringLiteral("settings")] = settings;
    return config;
}

QString writeMpvConf(const QString &dirPath, const QString &contents)
{
    const QString path = QDir(dirPath).filePath(QStringLiteral("mpv.conf"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return QString();
    }
    file.write(contents.toUtf8());
    file.close();
    return path;
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

void ConfigManagerThemeTest::defaultsContainNewBuiltinMpvProfiles()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ScopedConfigIsolation configIsolation(tempDir.path());

    ConfigManager config;
    config.load();

    QCOMPARE(config.getDefaultProfileName(), QStringLiteral("Medium Quality"));
    const QStringList names = config.getMpvProfileNames();
    QVERIFY(names.contains(QStringLiteral("Low Quality")));
    QVERIFY(names.contains(QStringLiteral("Medium Quality")));
    QVERIFY(names.contains(QStringLiteral("High Quality")));
    QVERIFY(names.contains(QStringLiteral("ArtCNN")));
    QVERIFY(names.contains(QStringLiteral("ArtCNN-Deband")));
    QVERIFY(names.contains(QStringLiteral("nnedi3")));
    QVERIFY(names.contains(QStringLiteral("nnedi3-deband")));
    QVERIFY(!names.contains(QStringLiteral("Default")));
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
    QCOMPARE(migrated.value(QStringLiteral("version")).toInt(), 24);
    const QJsonObject ui = migrated.value(QStringLiteral("settings")).toObject().value(QStringLiteral("ui")).toObject();
    QVERIFY(ui.contains(QStringLiteral("theme_flavor")));
    QCOMPARE(ui.value(QStringLiteral("theme_color_scheme")).toString(), QStringLiteral("blue"));
}

void ConfigManagerThemeTest::v21MigrationAddsArtProfilesWithoutChangingAssignments()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ScopedConfigIsolation configIsolation(tempDir.path());

    QVERIFY(QDir().mkpath(ConfigManager::getConfigDir()));
    QFile configFile(ConfigManager::getConfigPath());
    QVERIFY(configFile.open(QIODevice::WriteOnly));
    configFile.write(QJsonDocument(minimalV21ConfigWithAssignments()).toJson(QJsonDocument::Indented));
    configFile.close();

    ConfigManager config;
    config.load();

    QCOMPARE(config.getDefaultProfileName(), QStringLiteral("High Quality"));
    QCOMPARE(config.getLibraryProfile(QStringLiteral("library-1")), QStringLiteral("Low Quality"));
    QCOMPARE(config.getSeriesProfile(QStringLiteral("series-1")), QStringLiteral("High Quality"));

    const QStringList names = config.getMpvProfileNames();
    QVERIFY(names.contains(QStringLiteral("ArtCNN")));
    QVERIFY(names.contains(QStringLiteral("ArtCNN-Deband")));
    QVERIFY(names.contains(QStringLiteral("nnedi3")));
    QVERIFY(names.contains(QStringLiteral("nnedi3-deband")));
}

void ConfigManagerThemeTest::v20MigrationRenamesDefaultProfileAssignments()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ScopedConfigIsolation configIsolation(tempDir.path());

    const QString bloomDir = ConfigManager::getConfigDir();
    QVERIFY(QDir().mkpath(bloomDir));

    QFile configFile(ConfigManager::getConfigPath());
    QVERIFY(configFile.open(QIODevice::WriteOnly));
    configFile.write(QJsonDocument(minimalV20ConfigWithDefaultAssignments()).toJson(QJsonDocument::Indented));
    configFile.close();

    ConfigManager config;
    config.load();

    QCOMPARE(config.getDefaultProfileName(), QStringLiteral("Medium Quality"));
    QCOMPARE(config.getLibraryProfile(QStringLiteral("library-1")), QStringLiteral("Medium Quality"));
    QCOMPARE(config.getSeriesProfile(QStringLiteral("series-1")), QStringLiteral("Medium Quality"));
    QVERIFY(!config.getMpvProfileNames().contains(QStringLiteral("Default")));
    QVERIFY(config.getMpvProfileNames().contains(QStringLiteral("Default (custom backup)")));
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

    QStringList args = config.getMpvArgsForProfile(QStringLiteral("Medium Quality"), true);
    QVERIFY(args.contains(QStringLiteral("--target-colorspace-hint=no")));
    QVERIFY(args.contains(QStringLiteral("--tone-mapping=auto")));

    config.setEnableHDR(true);
    args = config.getMpvArgsForProfile(QStringLiteral("Medium Quality"), false);
    QVERIFY(!args.contains(QStringLiteral("--target-colorspace-hint=auto")));

    args = config.getMpvArgsForProfile(QStringLiteral("Medium Quality"), true);
    QVERIFY(args.contains(QStringLiteral("--vo=gpu-next")));
    QVERIFY(args.contains(QStringLiteral("--target-colorspace-hint=auto")));
    QVERIFY(args.contains(QStringLiteral("--target-colorspace-hint-mode=target")));
    QVERIFY(!args.contains(QStringLiteral("--gpu-api=d3d11")));
    QVERIFY(!args.contains(QStringLiteral("--gpu-context=d3d11")));

    args = config.getMpvArgsForProfile(QStringLiteral("Medium Quality"), true, true);
    QVERIFY(args.contains(QStringLiteral("--target-colorspace-hint=no")));
    QVERIFY(args.contains(QStringLiteral("--tone-mapping=auto")));
    QVERIFY(args.contains(QStringLiteral("--hdr-compute-peak=auto")));

    config.setHDROutputMode(QStringLiteral("tone-map-to-sdr"));
    args = config.getMpvArgsForProfile(QStringLiteral("Medium Quality"), true);
    QVERIFY(args.contains(QStringLiteral("--target-colorspace-hint=no")));
    QVERIFY(args.contains(QStringLiteral("--tone-mapping=auto")));
    QVERIFY(args.contains(QStringLiteral("--hdr-compute-peak=auto")));
}

void ConfigManagerThemeTest::builtinMpvProfilesEmitExpectedQualityArgs()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ScopedConfigIsolation configIsolation(tempDir.path());

    ConfigManager config;
    config.load();

    const QStringList lowArgs = config.getMpvArgsForProfile(QStringLiteral("Low Quality"));
    QVERIFY(lowArgs.contains(QStringLiteral("--profile=fast")));

    const QStringList mediumArgs = config.getMpvArgsForProfile(QStringLiteral("Medium Quality"));
    QVERIFY(mediumArgs.contains(QStringLiteral("--profile=high-quality")));

    const QStringList highArgs = config.getMpvArgsForProfile(QStringLiteral("High Quality"));
    const QString clearArg = QStringLiteral("--glsl-shaders-clr");
    const QString fsrcnnx = QStringLiteral("--glsl-shaders-append=~~/shaders/FSRCNNX_x2_8-0-4-1.glsl");
    const QString krig = QStringLiteral("--glsl-shaders-append=~~/shaders/KrigBilateral.glsl");
    const QString ssim = QStringLiteral("--glsl-shaders-append=~~/shaders/SSimDownscaler.glsl");

    QVERIFY(highArgs.contains(QStringLiteral("--profile=high-quality")));
    QVERIFY(highArgs.indexOf(clearArg) >= 0);
    QVERIFY(highArgs.indexOf(clearArg) < highArgs.indexOf(fsrcnnx));
    QVERIFY(highArgs.indexOf(fsrcnnx) < highArgs.indexOf(QStringLiteral("--scale=ewa_lanczos")));
    QVERIFY(highArgs.indexOf(QStringLiteral("--scale=ewa_lanczos")) < highArgs.indexOf(krig));
    QVERIFY(highArgs.indexOf(krig) < highArgs.indexOf(QStringLiteral("--cscale=ewa_lanczos")));
    QVERIFY(highArgs.indexOf(QStringLiteral("--cscale=ewa_lanczos")) < highArgs.indexOf(ssim));
    QVERIFY(highArgs.indexOf(ssim) < highArgs.indexOf(QStringLiteral("--dscale=mitchell")));
    QVERIFY(highArgs.contains(QStringLiteral("--linear-downscaling=no")));
    QVERIFY(highArgs.contains(QStringLiteral("--correct-downscaling=yes")));
}

void ConfigManagerThemeTest::artMpvProfilesEmitExpectedShaderAndDebandArgs()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ScopedConfigIsolation configIsolation(tempDir.path());

    ConfigManager config;
    config.load();

    const auto countArg = [](const QStringList &args, const QString &arg) {
        return args.count(arg);
    };
    const auto verifyProfile = [&](const QString &profileName, const QString &shaderArg, bool deband) {
        const QStringList args = config.getMpvArgsForProfile(profileName);
        QCOMPARE(config.getMpvProfile(profileName).value(QStringLiteral("windowsRenderApi")).toString(),
                 QStringLiteral("vulkan"));
        QCOMPARE(countArg(args, shaderArg), 1);
        QVERIFY(args.contains(QStringLiteral("--vo=gpu-next")));
        QVERIFY(args.contains(QStringLiteral("--hwdec=no")));
        QVERIFY(args.contains(QStringLiteral("--profile=high-quality")));
        QVERIFY(args.contains(QStringLiteral("--sub-fonts-dir=~~/fonts")));
        QVERIFY(args.contains(QStringLiteral("--sub-ass-override=no")));
        QVERIFY(args.contains(QStringLiteral("--sub-font=Gandhi Sans Bold")));
        QVERIFY(args.contains(QStringLiteral("--blend-subtitles=video")));
        QVERIFY(args.contains(QStringLiteral("--secondary-sub-ass-override=yes")));
        if (deband) {
            QVERIFY(args.contains(QStringLiteral("--deband=yes")));
            QVERIFY(args.contains(QStringLiteral("--deband-iterations=4")));
            QVERIFY(args.contains(QStringLiteral("--deband-threshold=48")));
            QVERIFY(args.contains(QStringLiteral("--deband-range=64")));
            QVERIFY(args.contains(QStringLiteral("--deband-grain=12")));
        } else {
            QVERIFY(args.contains(QStringLiteral("--deband=no")));
            QVERIFY(!args.contains(QStringLiteral("--deband-iterations=4")));
            QVERIFY(!args.contains(QStringLiteral("--deband-threshold=48")));
            QVERIFY(!args.contains(QStringLiteral("--deband-range=64")));
            QVERIFY(!args.contains(QStringLiteral("--deband-grain=12")));
        }
    };

    verifyProfile(QStringLiteral("ArtCNN"),
                  QStringLiteral("--glsl-shaders-append=~~/shaders/ArtCNN_C4F16.glsl"),
                  false);
    verifyProfile(QStringLiteral("ArtCNN-Deband"),
                  QStringLiteral("--glsl-shaders-append=~~/shaders/ArtCNN_C4F16.glsl"),
                  true);
    verifyProfile(QStringLiteral("nnedi3"),
                  QStringLiteral("--glsl-shaders-append=~~/shaders/nnedi3-nns32-win8x6.hook"),
                  false);
    verifyProfile(QStringLiteral("nnedi3-deband"),
                  QStringLiteral("--glsl-shaders-append=~~/shaders/nnedi3-nns32-win8x6.hook"),
                  true);
}

void ConfigManagerThemeTest::mpvProfileHdrAndWindowsOutputFieldsDefaultAndPersist()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ScopedConfigIsolation configIsolation(tempDir.path());

    ConfigManager config;
    config.load();

    const QVariantMap medium = config.getMpvProfile(QStringLiteral("Medium Quality"));
    QCOMPARE(medium.value(QStringLiteral("hdrMetadataMode")).toString(), QStringLiteral("target"));
    QCOMPARE(medium.value(QStringLiteral("windows10BitOutput")).toBool(), false);

    config.setMpvProfile(QStringLiteral("HDR Test"), QVariantMap{
        {QStringLiteral("hwdecEnabled"), true},
        {QStringLiteral("hwdecMethod"), QStringLiteral("auto")},
        {QStringLiteral("deinterlace"), false},
        {QStringLiteral("deinterlaceMethod"), QString()},
        {QStringLiteral("videoOutput"), QStringLiteral("gpu-next")},
        {QStringLiteral("interpolation"), false},
        {QStringLiteral("windowsRenderApi"), QStringLiteral("d3d11")},
        {QStringLiteral("hdrMetadataMode"), QStringLiteral("source-dynamic")},
        {QStringLiteral("windows10BitOutput"), true},
        {QStringLiteral("extraArgs"), QStringList{}}
    });

    const QVariantMap saved = config.getMpvProfile(QStringLiteral("HDR Test"));
    QCOMPARE(saved.value(QStringLiteral("hdrMetadataMode")).toString(), QStringLiteral("source-dynamic"));
    QCOMPARE(saved.value(QStringLiteral("windows10BitOutput")).toBool(), true);
}

void ConfigManagerThemeTest::hdrMpvArgsUseProfileMetadataMode()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ScopedConfigIsolation configIsolation(tempDir.path());

    ConfigManager config;
    config.load();
    config.setEnableHDR(true);

    QStringList args = config.getMpvArgsForProfile(QStringLiteral("Medium Quality"), true);
    QVERIFY(args.contains(QStringLiteral("--target-colorspace-hint=auto")));
    QVERIFY(args.contains(QStringLiteral("--target-colorspace-hint-mode=target")));

    QVariantMap profile = config.getMpvProfile(QStringLiteral("Medium Quality"));
    profile[QStringLiteral("hdrMetadataMode")] = QStringLiteral("source-dynamic");
    config.setMpvProfile(QStringLiteral("Medium Quality"), profile);

    args = config.getMpvArgsForProfile(QStringLiteral("Medium Quality"), true);
    QVERIFY(args.contains(QStringLiteral("--target-colorspace-hint=auto")));
    QVERIFY(args.contains(QStringLiteral("--target-colorspace-hint-mode=source-dynamic")));

    args = config.getMpvArgsForProfile(QStringLiteral("Medium Quality"), true, true);
    QVERIFY(args.contains(QStringLiteral("--target-colorspace-hint=no")));
    QVERIFY(!args.contains(QStringLiteral("--target-colorspace-hint-mode=source-dynamic")));
}

void ConfigManagerThemeTest::v23MigrationPreservesCustomizedBuiltInProfiles()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ScopedConfigIsolation configIsolation(tempDir.path());

    QVERIFY(QDir().mkpath(ConfigManager::getConfigDir()));
    QFile configFile(ConfigManager::getConfigPath());
    QVERIFY(configFile.open(QIODevice::WriteOnly));
    configFile.write(QJsonDocument(minimalV22ConfigWithCustomizedAnimeProfile()).toJson(QJsonDocument::Indented));
    configFile.close();

    ConfigManager config;
    config.load();

    const QVariantMap art = config.getMpvProfile(QStringLiteral("ArtCNN"));
    QCOMPARE(art.value(QStringLiteral("windowsRenderApi")).toString(), QStringLiteral("auto"));
    QCOMPARE(art.value(QStringLiteral("hdrMetadataMode")).toString(), QStringLiteral("target"));
    QCOMPARE(art.value(QStringLiteral("windows10BitOutput")).toBool(), false);
    QVERIFY(art.value(QStringLiteral("extraArgs")).toStringList().contains(QStringLiteral("--sub-auto=fuzzy")));

    QCOMPARE(config.getMpvProfile(QStringLiteral("ArtCNN-Deband")).value(QStringLiteral("windowsRenderApi")).toString(),
             QStringLiteral("vulkan"));
    QCOMPARE(config.getMpvProfile(QStringLiteral("nnedi3")).value(QStringLiteral("windowsRenderApi")).toString(),
             QStringLiteral("vulkan"));
    QCOMPARE(config.getMpvProfile(QStringLiteral("nnedi3-deband")).value(QStringLiteral("windowsRenderApi")).toString(),
	             QStringLiteral("vulkan"));
}

void ConfigManagerThemeTest::startupBufferingModesDefaultNormalizeAndPersist()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ScopedConfigIsolation configIsolation(tempDir.path());

    ConfigManager config;
    config.load();

    QCOMPARE(config.getStartupBufferingMode(), QStringLiteral("normal"));
    QCOMPARE(config.resolveStartupBufferingModeForItem(QStringLiteral("library-1")), QStringLiteral("normal"));
    QCOMPARE(config.getLibraryStartupBufferingMode(QStringLiteral("library-1")), QString());

    QSignalSpy globalSpy(&config, &ConfigManager::startupBufferingModeChanged);
    QSignalSpy librarySpy(&config, &ConfigManager::libraryStartupBufferingModesChanged);

    config.setStartupBufferingMode(QStringLiteral("remote_mount"));
    QCOMPARE(config.getStartupBufferingMode(), QStringLiteral("remote-mount"));
    QCOMPARE(globalSpy.count(), 1);

    config.setLibraryStartupBufferingMode(QStringLiteral("library-1"), QStringLiteral("normal"));
    QCOMPARE(config.getLibraryStartupBufferingMode(QStringLiteral("library-1")), QStringLiteral("normal"));
    QCOMPARE(config.resolveStartupBufferingModeForItem(QStringLiteral("library-1")), QStringLiteral("normal"));
    QCOMPARE(librarySpy.count(), 1);

    config.setLibraryStartupBufferingMode(QStringLiteral("library-1"), QStringLiteral("use-default"));
    QCOMPARE(config.getLibraryStartupBufferingMode(QStringLiteral("library-1")), QString());
    QCOMPARE(config.resolveStartupBufferingModeForItem(QStringLiteral("library-1")), QStringLiteral("remote-mount"));
    QCOMPARE(librarySpy.count(), 2);

    ConfigManager reloaded;
    reloaded.load();
    QCOMPARE(reloaded.getStartupBufferingMode(), QStringLiteral("remote-mount"));
    QCOMPARE(reloaded.getLibraryStartupBufferingMode(QStringLiteral("library-1")), QString());
}

void ConfigManagerThemeTest::bundledMpvShadersAreCopied()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ScopedConfigIsolation configIsolation(tempDir.path());

    ConfigManager config;
    config.load();

    const QDir shaderDir(QDir(ConfigManager::getMpvConfigDir()).filePath(QStringLiteral("shaders")));
    for (const QString &shaderName : QStringList{
             QStringLiteral("FSRCNNX_x2_8-0-4-1.glsl"),
             QStringLiteral("KrigBilateral.glsl"),
             QStringLiteral("SSimDownscaler.glsl"),
             QStringLiteral("ArtCNN_C4F16.glsl"),
             QStringLiteral("nnedi3-nns32-win8x6.hook")}) {
        QFile copied(shaderDir.filePath(shaderName));
        QVERIFY2(copied.exists(), qPrintable(shaderName));
        QVERIFY(copied.open(QIODevice::ReadOnly));
        QVERIFY(!copied.readAll().isEmpty());
    }
}

void ConfigManagerThemeTest::bundledMpvFontsAreCopied()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ScopedConfigIsolation configIsolation(tempDir.path());

    ConfigManager config;
    config.load();

    const QDir fontsDir(QDir(ConfigManager::getMpvConfigDir()).filePath(QStringLiteral("fonts")));
    for (const QString &fontName : QStringList{
             QStringLiteral("GandhiSans-Regular.otf"),
             QStringLiteral("GandhiSans-Bold.otf"),
             QStringLiteral("GandhiSans-Italic.otf"),
             QStringLiteral("GandhiSans-BoldItalic.otf"),
             QStringLiteral("LICENSE-GandhiSans.txt")}) {
        QFile copied(fontsDir.filePath(fontName));
        QVERIFY2(copied.exists(), qPrintable(fontName));
        QVERIFY(copied.open(QIODevice::ReadOnly));
        QVERIFY(!copied.readAll().isEmpty());
    }
}

void ConfigManagerThemeTest::importMpvConfigCreatesProfile()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ScopedConfigIsolation configIsolation(tempDir.path());

    const QString confPath = writeMpvConf(tempDir.path(), QStringLiteral(
        "# global options\n"
        "profile-cond=p[\"video-params/primaries\"] == \"bt.2020\"\n"
        "--save-position-on-quit\n"
        "sub-font=\"Noto Sans\"\n"));
    QVERIFY(!confPath.isEmpty());

    ConfigManager config;
    config.load();

    const QVariantMap result = config.importMpvConfigAsProfile(confPath, QStringLiteral("Imported"));
    QVERIFY(result.value(QStringLiteral("success")).toBool());
    QCOMPARE(result.value(QStringLiteral("importedCount")).toInt(), 3);

    const QVariantMap profile = config.getMpvProfile(QStringLiteral("Imported"));
    const QStringList extraArgs = profile.value(QStringLiteral("extraArgs")).toStringList();
    QCOMPARE(extraArgs, QStringList({
        QStringLiteral("--profile-cond=p[\"video-params/primaries\"] == \"bt.2020\""),
        QStringLiteral("--save-position-on-quit"),
        QStringLiteral("--sub-font=Noto Sans")
    }));
}

void ConfigManagerThemeTest::importMpvConfigNormalizesQuotedShaderArgs()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ScopedConfigIsolation configIsolation(tempDir.path());

    const QString confPath = writeMpvConf(tempDir.path(), QStringLiteral(
        "glsl-shader=\"C:\\path with spaces\\ArtCNN.glsl\"\n"
        "glsl-shader-append='C:\\filters\\Other.glsl'\n"
        "glsl-shaders-append=\"C:\\filters\\Third.glsl\"\n"
        "glsl-shaders-clr\n"
        "sub-font=\"Noto Sans\"\n"));
    QVERIFY(!confPath.isEmpty());

    ConfigManager config;
    config.load();

    const QVariantMap result = config.importMpvConfigAsProfile(confPath, QStringLiteral("Shaders"));
    QVERIFY(result.value(QStringLiteral("success")).toBool());
    QCOMPARE(result.value(QStringLiteral("importedCount")).toInt(), 4);

    const QStringList extraArgs = config.getMpvProfile(QStringLiteral("Shaders"))
                                      .value(QStringLiteral("extraArgs"))
                                      .toStringList();
    QCOMPARE(extraArgs, QStringList({
        QStringLiteral("--glsl-shaders-append=C:\\path with spaces\\ArtCNN.glsl"),
        QStringLiteral("--glsl-shaders-append=C:\\filters\\Other.glsl"),
        QStringLiteral("--glsl-shaders-append=C:\\filters\\Third.glsl"),
        QStringLiteral("--sub-font=Noto Sans")
    }));

    const QStringList args = config.getMpvArgsForProfile(QStringLiteral("Shaders"));
    QVERIFY(args.contains(QStringLiteral("--glsl-shaders-clr")));
    QVERIFY(args.contains(QStringLiteral("--glsl-shaders-append=C:\\path with spaces\\ArtCNN.glsl")));
    QVERIFY(args.contains(QStringLiteral("--glsl-shaders-append=C:\\filters\\Other.glsl")));
    QVERIFY(args.contains(QStringLiteral("--glsl-shaders-append=C:\\filters\\Third.glsl")));
}

void ConfigManagerThemeTest::existingMpvProfileArgsAreSanitizedOnLoad()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ScopedConfigIsolation configIsolation(tempDir.path());

    ConfigManager initialConfig;
    initialConfig.load();

    QFile configFile(ConfigManager::getConfigPath());
    QVERIFY(configFile.open(QIODevice::ReadOnly));
    QJsonObject configJson = QJsonDocument::fromJson(configFile.readAll()).object();
    configFile.close();

    QJsonObject settings = configJson.value(QStringLiteral("settings")).toObject();
    QJsonObject profiles = settings.value(QStringLiteral("mpv_profiles")).toObject();
    QJsonObject artcnn;
    artcnn[QStringLiteral("hwdec_enabled")] = true;
    artcnn[QStringLiteral("hwdec_method")] = QStringLiteral("auto");
    artcnn[QStringLiteral("deinterlace")] = false;
    artcnn[QStringLiteral("deinterlace_method")] = QString();
    artcnn[QStringLiteral("video_output")] = QStringLiteral("gpu-next");
    artcnn[QStringLiteral("interpolation")] = false;
    artcnn[QStringLiteral("extra_args")] = QJsonArray{
        QStringLiteral("--glsl-shader=\"C:\\path with spaces\\ArtCNN.glsl\""),
        QStringLiteral("--glsl-shaders=C:\\filters\\KrigBilateral.glsl"),
        QStringLiteral("--glsl-shaders-append=C:\\filters\\SSimDownscaler.glsl"),
        QStringLiteral("--glsl-shaders-clr")
    };
    profiles[QStringLiteral("artcnn")] = artcnn;
    settings[QStringLiteral("mpv_profiles")] = profiles;
    configJson[QStringLiteral("settings")] = settings;

    QVERIFY(configFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    configFile.write(QJsonDocument(configJson).toJson());
    configFile.close();

    ConfigManager config;
    config.load();

    const QStringList extraArgs = config.getMpvProfile(QStringLiteral("artcnn"))
                                      .value(QStringLiteral("extraArgs"))
                                      .toStringList();
    QCOMPARE(extraArgs, QStringList({
        QStringLiteral("--glsl-shaders-append=C:\\path with spaces\\ArtCNN.glsl"),
        QStringLiteral("--glsl-shaders-append=C:\\filters\\KrigBilateral.glsl"),
        QStringLiteral("--glsl-shaders-append=C:\\filters\\SSimDownscaler.glsl")
    }));

    const QStringList args = config.getMpvArgsForProfile(QStringLiteral("artcnn"));
    const int clearIndex = args.indexOf(QStringLiteral("--glsl-shaders-clr"));
    QVERIFY(clearIndex >= 0);
    QVERIFY(clearIndex < args.indexOf(QStringLiteral("--glsl-shaders-append=C:\\path with spaces\\ArtCNN.glsl")));
    QVERIFY(args.indexOf(QStringLiteral("--glsl-shaders-append=C:\\path with spaces\\ArtCNN.glsl"))
            < args.indexOf(QStringLiteral("--glsl-shaders-append=C:\\filters\\KrigBilateral.glsl")));
    QVERIFY(args.indexOf(QStringLiteral("--glsl-shaders-append=C:\\filters\\KrigBilateral.glsl"))
            < args.indexOf(QStringLiteral("--glsl-shaders-append=C:\\filters\\SSimDownscaler.glsl")));
}

void ConfigManagerThemeTest::setMpvProfileNormalizesExtraArgs()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ScopedConfigIsolation configIsolation(tempDir.path());

    ConfigManager config;
    config.load();
    config.setMpvProfile(QStringLiteral("Edited"), QVariantMap{
        {QStringLiteral("hwdecEnabled"), true},
        {QStringLiteral("hwdecMethod"), QStringLiteral("auto")},
        {QStringLiteral("deinterlace"), false},
        {QStringLiteral("deinterlaceMethod"), QString()},
        {QStringLiteral("videoOutput"), QStringLiteral("gpu-next")},
        {QStringLiteral("interpolation"), false},
        {QStringLiteral("windowsRenderApi"), QStringLiteral("vulkan")},
        {QStringLiteral("extraArgs"), QStringList{
            QStringLiteral("--glsl-shader=\"C:\\shader dir\\ArtCNN.glsl\""),
            QStringLiteral("--gpu-api=vulkan"),
            QStringLiteral("--gpu-context=winvk"),
            QStringLiteral("--profile=fast"),
            QStringLiteral("--profile=custom"),
            QStringLiteral("--sub-font=\"Noto Sans\"")
        }}
    });

    const QStringList extraArgs = config.getMpvProfile(QStringLiteral("Edited"))
                                      .value(QStringLiteral("extraArgs"))
                                      .toStringList();
    QCOMPARE(extraArgs, QStringList({
        QStringLiteral("--glsl-shaders-append=C:\\shader dir\\ArtCNN.glsl"),
        QStringLiteral("--profile=fast"),
        QStringLiteral("--sub-font=Noto Sans")
    }));
}

void ConfigManagerThemeTest::mpvProfileWindowsRenderApiDefaultsAndNormalizes()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ScopedConfigIsolation configIsolation(tempDir.path());

    ConfigManager initialConfig;
    initialConfig.load();

    QFile configFile(ConfigManager::getConfigPath());
    QVERIFY(configFile.open(QIODevice::ReadOnly));
    QJsonObject configJson = QJsonDocument::fromJson(configFile.readAll()).object();
    configFile.close();

    QJsonObject settings = configJson.value(QStringLiteral("settings")).toObject();
    QJsonObject profiles = settings.value(QStringLiteral("mpv_profiles")).toObject();

    QJsonObject missingRenderApi;
    missingRenderApi[QStringLiteral("hwdec_enabled")] = true;
    missingRenderApi[QStringLiteral("hwdec_method")] = QStringLiteral("auto");
    missingRenderApi[QStringLiteral("deinterlace")] = false;
    missingRenderApi[QStringLiteral("deinterlace_method")] = QString();
    missingRenderApi[QStringLiteral("video_output")] = QStringLiteral("gpu-next");
    missingRenderApi[QStringLiteral("interpolation")] = false;
    missingRenderApi[QStringLiteral("extra_args")] = QJsonArray{};
    profiles[QStringLiteral("Missing Render API")] = missingRenderApi;

    QJsonObject invalidRenderApi = missingRenderApi;
    invalidRenderApi[QStringLiteral("windows_render_api")] = QStringLiteral("metal");
    profiles[QStringLiteral("Invalid Render API")] = invalidRenderApi;

    settings[QStringLiteral("mpv_profiles")] = profiles;
    configJson[QStringLiteral("settings")] = settings;

    QVERIFY(configFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    configFile.write(QJsonDocument(configJson).toJson());
    configFile.close();

    ConfigManager config;
    config.load();

    QCOMPARE(config.getMpvProfile(QStringLiteral("Missing Render API")).value(QStringLiteral("windowsRenderApi")).toString(),
             QStringLiteral("auto"));
    QCOMPARE(config.getMpvProfile(QStringLiteral("Invalid Render API")).value(QStringLiteral("windowsRenderApi")).toString(),
             QStringLiteral("auto"));
}

void ConfigManagerThemeTest::setMpvProfilePreservesWindowsRenderApiValues()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ScopedConfigIsolation configIsolation(tempDir.path());

    ConfigManager config;
    config.load();

    const QStringList values{
        QStringLiteral("auto"),
        QStringLiteral("d3d11"),
        QStringLiteral("vulkan")
    };

    for (const QString &value : values) {
        const QString profileName = QStringLiteral("Render API %1").arg(value);
        config.setMpvProfile(profileName, QVariantMap{
            {QStringLiteral("hwdecEnabled"), true},
            {QStringLiteral("hwdecMethod"), QStringLiteral("auto")},
            {QStringLiteral("deinterlace"), false},
            {QStringLiteral("deinterlaceMethod"), QString()},
            {QStringLiteral("videoOutput"), QStringLiteral("gpu-next")},
            {QStringLiteral("interpolation"), false},
            {QStringLiteral("windowsRenderApi"), value},
            {QStringLiteral("extraArgs"), QStringList{}}
        });
        QCOMPARE(config.getMpvProfile(profileName).value(QStringLiteral("windowsRenderApi")).toString(), value);
    }
}

void ConfigManagerThemeTest::renameMpvProfilePreservesProfileAndAssignments()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ScopedConfigIsolation configIsolation(tempDir.path());

    ConfigManager config;
    config.load();
    config.setMpvProfile(QStringLiteral("Old Name"), QVariantMap{
        {QStringLiteral("hwdecEnabled"), false},
        {QStringLiteral("hwdecMethod"), QStringLiteral("no")},
        {QStringLiteral("deinterlace"), true},
        {QStringLiteral("deinterlaceMethod"), QStringLiteral("bwdif")},
        {QStringLiteral("videoOutput"), QStringLiteral("gpu")},
        {QStringLiteral("interpolation"), true},
        {QStringLiteral("windowsRenderApi"), QStringLiteral("vulkan")},
        {QStringLiteral("extraArgs"), QStringList{QStringLiteral("--sub-auto=fuzzy")}}
    });
    config.setDefaultProfileName(QStringLiteral("Old Name"));
    config.setLibraryProfile(QStringLiteral("library-1"), QStringLiteral("Old Name"));
    config.setSeriesProfile(QStringLiteral("series-1"), QStringLiteral("Old Name"));

    QVERIFY(config.renameMpvProfile(QStringLiteral("Old Name"), QStringLiteral("New Name")));
    QVERIFY(!config.getMpvProfileNames().contains(QStringLiteral("Old Name")));
    QVERIFY(config.getMpvProfileNames().contains(QStringLiteral("New Name")));
    QCOMPARE(config.getDefaultProfileName(), QStringLiteral("New Name"));
    QCOMPARE(config.getLibraryProfile(QStringLiteral("library-1")), QStringLiteral("New Name"));
    QCOMPARE(config.getSeriesProfile(QStringLiteral("series-1")), QStringLiteral("New Name"));

    const QVariantMap profile = config.getMpvProfile(QStringLiteral("New Name"));
    QCOMPARE(profile.value(QStringLiteral("hwdecEnabled")).toBool(), false);
    QCOMPARE(profile.value(QStringLiteral("deinterlace")).toBool(), true);
    QCOMPARE(profile.value(QStringLiteral("deinterlaceMethod")).toString(), QStringLiteral("bwdif"));
    QCOMPARE(profile.value(QStringLiteral("videoOutput")).toString(), QStringLiteral("gpu"));
    QCOMPARE(profile.value(QStringLiteral("interpolation")).toBool(), true);
    QCOMPARE(profile.value(QStringLiteral("windowsRenderApi")).toString(), QStringLiteral("vulkan"));
    QCOMPARE(profile.value(QStringLiteral("extraArgs")).toStringList(), QStringList({QStringLiteral("--sub-auto=fuzzy")}));
}

void ConfigManagerThemeTest::renameMpvProfileRejectsInvalidRequests()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ScopedConfigIsolation configIsolation(tempDir.path());

    ConfigManager config;
    config.load();
    config.setMpvProfile(QStringLiteral("Custom"), QVariantMap{
        {QStringLiteral("hwdecEnabled"), true},
        {QStringLiteral("hwdecMethod"), QStringLiteral("auto")},
        {QStringLiteral("deinterlace"), false},
        {QStringLiteral("deinterlaceMethod"), QString()},
        {QStringLiteral("videoOutput"), QStringLiteral("gpu-next")},
        {QStringLiteral("interpolation"), false},
        {QStringLiteral("extraArgs"), QStringList{QStringLiteral("--sub-auto=fuzzy")}}
    });

    const QStringList namesBefore = config.getMpvProfileNames();
    QVERIFY(!config.renameMpvProfile(QStringLiteral("Low Quality"), QStringLiteral("Low Renamed")));
    QVERIFY(!config.renameMpvProfile(QStringLiteral("Medium Quality"), QStringLiteral("Medium Renamed")));
    QVERIFY(!config.renameMpvProfile(QStringLiteral("High Quality"), QStringLiteral("HQ Renamed")));
    QVERIFY(!config.renameMpvProfile(QStringLiteral("ArtCNN"), QStringLiteral("Art Renamed")));
    QVERIFY(!config.renameMpvProfile(QStringLiteral("ArtCNN-Deband"), QStringLiteral("Art Deband Renamed")));
    QVERIFY(!config.renameMpvProfile(QStringLiteral("nnedi3"), QStringLiteral("nnedi3 Renamed")));
    QVERIFY(!config.renameMpvProfile(QStringLiteral("nnedi3-deband"), QStringLiteral("nnedi3 Deband Renamed")));
    QVERIFY(!config.renameMpvProfile(QStringLiteral("Custom"), QStringLiteral("Medium Quality")));
    QVERIFY(!config.renameMpvProfile(QStringLiteral("Custom"), QStringLiteral("   ")));
    QVERIFY(!config.renameMpvProfile(QStringLiteral("Missing"), QStringLiteral("New Missing")));
    QVERIFY(!config.renameMpvProfile(QStringLiteral("Custom"), QStringLiteral("Custom")));
    QCOMPARE(config.getMpvProfileNames(), namesBefore);
}

void ConfigManagerThemeTest::deleteMpvProfileRejectsBuiltins()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ScopedConfigIsolation configIsolation(tempDir.path());

    ConfigManager config;
    config.load();

    const QStringList namesBefore = config.getMpvProfileNames();
    for (const QString &name : QStringList{
             QStringLiteral("Low Quality"),
             QStringLiteral("Medium Quality"),
             QStringLiteral("High Quality"),
             QStringLiteral("ArtCNN"),
             QStringLiteral("ArtCNN-Deband"),
             QStringLiteral("nnedi3"),
             QStringLiteral("nnedi3-deband")}) {
        QVERIFY2(!config.deleteMpvProfile(name), qPrintable(name));
    }
    QCOMPARE(config.getMpvProfileNames(), namesBefore);
}

void ConfigManagerThemeTest::importMpvConfigFiltersBloomManagedOptions()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ScopedConfigIsolation configIsolation(tempDir.path());

    const QString confPath = writeMpvConf(tempDir.path(), QStringLiteral(
        "vo=gpu\n"
        "hwdec=vaapi\n"
        "gpu-api=vulkan\n"
        "gpu-context=winvk\n"
        "d3d11-output-format=rgba16hf\n"
        "vulkan-device=GPU-1\n"
        "audio-file-auto=fuzzy\n"));
    QVERIFY(!confPath.isEmpty());

    ConfigManager config;
    config.load();

    const QVariantMap result = config.importMpvConfigAsProfile(confPath, QStringLiteral("Filtered"));
    QVERIFY(result.value(QStringLiteral("success")).toBool());
    QCOMPARE(result.value(QStringLiteral("importedCount")).toInt(), 1);
    QCOMPARE(result.value(QStringLiteral("filteredOptions")).toStringList(),
             QStringList({
                 QStringLiteral("--vo=gpu"),
                 QStringLiteral("--hwdec=vaapi"),
                 QStringLiteral("--gpu-api=vulkan"),
                 QStringLiteral("--gpu-context=winvk"),
                 QStringLiteral("--d3d11-output-format=rgba16hf"),
                 QStringLiteral("--vulkan-device=GPU-1")
             }));

    const QStringList extraArgs = config.getMpvProfile(QStringLiteral("Filtered"))
                                      .value(QStringLiteral("extraArgs"))
                                      .toStringList();
    QCOMPARE(extraArgs, QStringList({QStringLiteral("--audio-file-auto=fuzzy")}));
}

void ConfigManagerThemeTest::importMpvConfigIgnoresProfileSections()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ScopedConfigIsolation configIsolation(tempDir.path());

    const QString confPath = writeMpvConf(tempDir.path(), QStringLiteral(
        "sub-auto=fuzzy\n"
        "[high-quality]\n"
        "scale=ewa_lanczossharp\n"));
    QVERIFY(!confPath.isEmpty());

    ConfigManager config;
    config.load();

    const QVariantMap result = config.importMpvConfigAsProfile(confPath, QStringLiteral("Global Only"));
    QVERIFY(result.value(QStringLiteral("success")).toBool());
    QCOMPARE(result.value(QStringLiteral("importedCount")).toInt(), 1);

    const QStringList extraArgs = config.getMpvProfile(QStringLiteral("Global Only"))
                                      .value(QStringLiteral("extraArgs"))
                                      .toStringList();
    QCOMPARE(extraArgs, QStringList({QStringLiteral("--sub-auto=fuzzy")}));
}

void ConfigManagerThemeTest::importMpvConfigRejectsDuplicateOrEmptyNames()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ScopedConfigIsolation configIsolation(tempDir.path());

    const QString confPath = writeMpvConf(tempDir.path(), QStringLiteral("sub-auto=fuzzy\n"));
    QVERIFY(!confPath.isEmpty());

    ConfigManager config;
    config.load();

    const QStringList namesBefore = config.getMpvProfileNames();
    QVariantMap result = config.importMpvConfigAsProfile(confPath, QStringLiteral("   "));
    QVERIFY(!result.value(QStringLiteral("success")).toBool());
    QCOMPARE(config.getMpvProfileNames(), namesBefore);

    result = config.importMpvConfigAsProfile(confPath, QStringLiteral("Medium Quality"));
    QVERIFY(!result.value(QStringLiteral("success")).toBool());
    QCOMPARE(config.getMpvProfileNames(), namesBefore);
}

void ConfigManagerThemeTest::importMpvConfigMissingFileReturnsError()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ScopedConfigIsolation configIsolation(tempDir.path());

    ConfigManager config;
    config.load();

    const QVariantMap result = config.importMpvConfigAsProfile(
        QDir(tempDir.path()).filePath(QStringLiteral("missing.conf")),
        QStringLiteral("Missing"));
    QVERIFY(!result.value(QStringLiteral("success")).toBool());
    QVERIFY(!result.value(QStringLiteral("error")).toString().isEmpty());
    QVERIFY(!config.getMpvProfileNames().contains(QStringLiteral("Missing")));
}

void ConfigManagerThemeTest::embeddedMpvShaderPartitionPreservesOrder()
{
    const QStringList args{
        QStringLiteral("--profile=high-quality"),
        QStringLiteral("--glsl-shaders-clr"),
        QStringLiteral("--glsl-shaders-append=~~/shaders/FSRCNNX_x2_8-0-4-1.glsl"),
        QStringLiteral("--scale=ewa_lanczos"),
        QStringLiteral("--glsl-shaders-append=~~/shaders/KrigBilateral.glsl"),
        QStringLiteral("--glsl-shaders-append=~~/shaders/SSimDownscaler.glsl"),
    };

    const MpvArgFilter::ShaderArgPartition partitioned = MpvArgFilter::partitionShaderArgs(args);
    QCOMPARE(partitioned.nonShaderArgs,
             QStringList({
                 QStringLiteral("--profile=high-quality"),
                 QStringLiteral("--scale=ewa_lanczos"),
             }));
    QCOMPARE(partitioned.shaderPaths,
             QStringList({
                 QStringLiteral("~~/shaders/FSRCNNX_x2_8-0-4-1.glsl"),
                 QStringLiteral("~~/shaders/KrigBilateral.glsl"),
                 QStringLiteral("~~/shaders/SSimDownscaler.glsl"),
             }));
}

void ConfigManagerThemeTest::resolveMpvPortablePathExpandsConfigDirPrefix()
{
    const QString mpvConfigDir = QStringLiteral("/cfg/mpv");
    QCOMPARE(MpvArgFilter::resolveMpvPortablePath(QStringLiteral("~~/shaders/Foo.glsl"), mpvConfigDir),
             QDir(mpvConfigDir).filePath(QStringLiteral("shaders/Foo.glsl")));
    QCOMPARE(MpvArgFilter::resolveMpvPortablePath(QStringLiteral("C:\\filters\\ArtCNN.glsl"), mpvConfigDir),
             QStringLiteral("C:\\filters\\ArtCNN.glsl"));
}

void ConfigManagerThemeTest::joinMpvPathListOptionValueUsesPlatformSeparator()
{
    const QStringList paths{
        QStringLiteral("/first.glsl"),
        QStringLiteral("/second.glsl"),
    };
#if defined(Q_OS_WIN)
    QCOMPARE(MpvArgFilter::joinMpvPathListOptionValue(paths),
             QStringLiteral("/first.glsl;/second.glsl"));
#else
    QCOMPARE(MpvArgFilter::joinMpvPathListOptionValue(paths),
             QStringLiteral("/first.glsl:/second.glsl"));
#endif
}

QTEST_MAIN(ConfigManagerThemeTest)
#include "ConfigManagerThemeTest.moc"
