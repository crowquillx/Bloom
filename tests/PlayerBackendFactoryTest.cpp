#include <QtTest/QtTest>
#include <QtGlobal>
#include <array>
#include <memory>

#include <QByteArray>
#include <QDir>

#include "player/backend/ExternalMpvBackend.h"
#include "player/backend/IPlayerBackend.h"
#include "player/backend/MpvLibmpvRuntime.h"
#include "player/backend/MpvNodeUtils.h"
#include "player/backend/PlayerBackendFactory.h"
#include "utils/ConfigManager.h"
#if defined(Q_OS_WIN)
#include "player/backend/WindowsMpvBackend.h"
#endif

class PlayerBackendFactoryTest : public QObject
{
    Q_OBJECT
    struct EnvironmentSnapshot
    {
        const char *name;
        bool existed = false;
        QByteArray value;
    };

    std::array<EnvironmentSnapshot, 4> m_environment{{
        {"XDG_SESSION_TYPE"},
        {"WAYLAND_DISPLAY"},
        {"BLOOM_PLAYER_BACKEND"},
        {"BLOOM_ENABLE_WAYLAND_LIBMPV"},
    }};

private slots:
    void init();
    void cleanup();
    void createsPlatformDefaultBackend();
    void waylandDefaultsToExternalBackend();
    void waylandLibmpvOptInUsesEmbeddedBackendWhenSupported();
    void backendStartsInStoppedState();
    void createByNameSupportsExternal();
    void createByNameLinuxSelectionBehavior();
    void createByNameWindowsSelectionBehavior();
    void createByNameResolvesUnknownToExternal();
    void envOverrideSelectsExternalBackend();
    void envOverrideSelectsLinuxBackendWhenAvailable();
    void envOverrideSelectsWindowsBackendWhenAvailable();
    void envOverrideUnknownResolvesToExternal();
    void configPreferenceSelectsExternalWhenNoEnvOverride();
    void envOverrideTakesPrecedenceOverConfigPreference();
    void windowsEmbeddedSanitizerFiltersRenderBackendOverrides();
    void windowsEmbeddedRenderApiProfilesApplyExpectedOptions();
    void windowsEmbeddedShaderListUsesResolvedGlslShadersOption();
    void embeddedVideoFilterListOperationsAreDeferred();
    void linkedRuntimePreservesVideoFilterOrder();
    void externalBackendRuntimeIsUnknownUntilProbed();
};

void PlayerBackendFactoryTest::init()
{
    for (EnvironmentSnapshot &snapshot : m_environment) {
        snapshot.existed = qEnvironmentVariableIsSet(snapshot.name);
        snapshot.value = qgetenv(snapshot.name);
    }
}

void PlayerBackendFactoryTest::cleanup()
{
    for (const EnvironmentSnapshot &snapshot : m_environment) {
        if (snapshot.existed) {
            qputenv(snapshot.name, snapshot.value);
        } else {
            qunsetenv(snapshot.name);
        }
    }
}

void PlayerBackendFactoryTest::createsPlatformDefaultBackend()
{
    qunsetenv("BLOOM_PLAYER_BACKEND");
    std::unique_ptr<IPlayerBackend> backend = PlayerBackendFactory::create();

    QVERIFY(backend != nullptr);
#if defined(Q_OS_LINUX)
    QVERIFY(backend->backendName() == QStringLiteral("linux-libmpv-opengl")
            || backend->backendName() == QStringLiteral("external-mpv-ipc"));
#elif defined(Q_OS_WIN)
    QCOMPARE(backend->backendName(), QStringLiteral("win-libmpv"));
#else
    QCOMPARE(backend->backendName(), QStringLiteral("external-mpv-ipc"));
#endif
}

void PlayerBackendFactoryTest::waylandDefaultsToExternalBackend()
{
#if defined(Q_OS_LINUX)
    qunsetenv("BLOOM_PLAYER_BACKEND");
    qunsetenv("BLOOM_ENABLE_WAYLAND_LIBMPV");
    qputenv("XDG_SESSION_TYPE", "wayland");
    qunsetenv("WAYLAND_DISPLAY");

    const std::unique_ptr<IPlayerBackend> backend = PlayerBackendFactory::create();

    qunsetenv("XDG_SESSION_TYPE");

    QVERIFY(backend != nullptr);
    QCOMPARE(backend->backendName(), QStringLiteral("external-mpv-ipc"));
#else
    QSKIP("Wayland backend policy is only compiled on Linux");
#endif
}

void PlayerBackendFactoryTest::waylandLibmpvOptInUsesEmbeddedBackendWhenSupported()
{
#if defined(Q_OS_LINUX)
    qunsetenv("BLOOM_PLAYER_BACKEND");
    qputenv("BLOOM_ENABLE_WAYLAND_LIBMPV", "1");
    qputenv("XDG_SESSION_TYPE", "wayland");
    qunsetenv("WAYLAND_DISPLAY");

    const std::unique_ptr<IPlayerBackend> backend = PlayerBackendFactory::create();

    qunsetenv("BLOOM_ENABLE_WAYLAND_LIBMPV");
    qunsetenv("XDG_SESSION_TYPE");

    QVERIFY(backend != nullptr);
    QVERIFY(backend->backendName() == QStringLiteral("linux-libmpv-opengl")
            || backend->backendName() == QStringLiteral("external-mpv-ipc"));
#else
    QSKIP("Wayland backend policy is only compiled on Linux");
#endif
}

void PlayerBackendFactoryTest::backendStartsInStoppedState()
{
    qunsetenv("BLOOM_PLAYER_BACKEND");
    std::unique_ptr<IPlayerBackend> backend = PlayerBackendFactory::create();

    QVERIFY(backend != nullptr);
    QVERIFY(!backend->isRunning());
}

void PlayerBackendFactoryTest::createByNameSupportsExternal()
{
    std::unique_ptr<IPlayerBackend> backend = PlayerBackendFactory::createByName(QStringLiteral("external-mpv-ipc"));

    QVERIFY(backend != nullptr);
#if defined(Q_OS_WIN)
    QCOMPARE(backend->backendName(), QStringLiteral("win-libmpv"));
#else
    QCOMPARE(backend->backendName(), QStringLiteral("external-mpv-ipc"));
#endif
}

void PlayerBackendFactoryTest::createByNameLinuxSelectionBehavior()
{
    std::unique_ptr<IPlayerBackend> backend = PlayerBackendFactory::createByName(QStringLiteral("linux-libmpv-opengl"));

    QVERIFY(backend != nullptr);
#if defined(Q_OS_LINUX)
    QVERIFY(backend->backendName() == QStringLiteral("linux-libmpv-opengl")
            || backend->backendName() == QStringLiteral("external-mpv-ipc"));
#elif defined(Q_OS_WIN)
    QCOMPARE(backend->backendName(), QStringLiteral("win-libmpv"));
#else
    QCOMPARE(backend->backendName(), QStringLiteral("external-mpv-ipc"));
#endif
}

void PlayerBackendFactoryTest::createByNameWindowsSelectionBehavior()
{
    std::unique_ptr<IPlayerBackend> backend = PlayerBackendFactory::createByName(QStringLiteral("win-libmpv"));

    QVERIFY(backend != nullptr);
#if defined(Q_OS_WIN)
    QCOMPARE(backend->backendName(), QStringLiteral("win-libmpv"));
#else
    QCOMPARE(backend->backendName(), QStringLiteral("external-mpv-ipc"));
#endif
}

void PlayerBackendFactoryTest::createByNameResolvesUnknownToExternal()
{
    std::unique_ptr<IPlayerBackend> backend = PlayerBackendFactory::createByName(QStringLiteral("unknown-backend"));

    QVERIFY(backend != nullptr);
#if defined(Q_OS_WIN)
    QCOMPARE(backend->backendName(), QStringLiteral("win-libmpv"));
#else
    QCOMPARE(backend->backendName(), QStringLiteral("external-mpv-ipc"));
#endif
}

void PlayerBackendFactoryTest::envOverrideSelectsExternalBackend()
{
    qputenv("BLOOM_PLAYER_BACKEND", "external-mpv-ipc");

    std::unique_ptr<IPlayerBackend> backend = PlayerBackendFactory::create();

    QVERIFY(backend != nullptr);
#if defined(Q_OS_WIN)
    QCOMPARE(backend->backendName(), QStringLiteral("win-libmpv"));
#else
    QCOMPARE(backend->backendName(), QStringLiteral("external-mpv-ipc"));
#endif

    qunsetenv("BLOOM_PLAYER_BACKEND");
}

void PlayerBackendFactoryTest::envOverrideSelectsLinuxBackendWhenAvailable()
{
    qputenv("BLOOM_PLAYER_BACKEND", "linux-libmpv-opengl");

    std::unique_ptr<IPlayerBackend> backend = PlayerBackendFactory::create();

    QVERIFY(backend != nullptr);
#if defined(Q_OS_LINUX)
    QVERIFY(backend->backendName() == QStringLiteral("linux-libmpv-opengl")
            || backend->backendName() == QStringLiteral("external-mpv-ipc"));
#elif defined(Q_OS_WIN)
    QCOMPARE(backend->backendName(), QStringLiteral("win-libmpv"));
#else
    QCOMPARE(backend->backendName(), QStringLiteral("external-mpv-ipc"));
#endif

    qunsetenv("BLOOM_PLAYER_BACKEND");
}

void PlayerBackendFactoryTest::envOverrideSelectsWindowsBackendWhenAvailable()
{
    qputenv("BLOOM_PLAYER_BACKEND", "win-libmpv");

    std::unique_ptr<IPlayerBackend> backend = PlayerBackendFactory::create();

    QVERIFY(backend != nullptr);
#if defined(Q_OS_WIN)
    QCOMPARE(backend->backendName(), QStringLiteral("win-libmpv"));
#else
    QCOMPARE(backend->backendName(), QStringLiteral("external-mpv-ipc"));
#endif

    qunsetenv("BLOOM_PLAYER_BACKEND");
}

void PlayerBackendFactoryTest::envOverrideUnknownResolvesToExternal()
{
    qputenv("BLOOM_PLAYER_BACKEND", "unknown-backend");

    std::unique_ptr<IPlayerBackend> backend = PlayerBackendFactory::create();

    QVERIFY(backend != nullptr);
#if defined(Q_OS_WIN)
    QCOMPARE(backend->backendName(), QStringLiteral("win-libmpv"));
#else
    QCOMPARE(backend->backendName(), QStringLiteral("external-mpv-ipc"));
#endif

    qunsetenv("BLOOM_PLAYER_BACKEND");
}

void PlayerBackendFactoryTest::configPreferenceSelectsExternalWhenNoEnvOverride()
{
    qunsetenv("BLOOM_PLAYER_BACKEND");
    std::unique_ptr<IPlayerBackend> backend = PlayerBackendFactory::create(QStringLiteral("external-mpv-ipc"));

    QVERIFY(backend != nullptr);
#if defined(Q_OS_WIN)
    QCOMPARE(backend->backendName(), QStringLiteral("win-libmpv"));
#else
    QCOMPARE(backend->backendName(), QStringLiteral("external-mpv-ipc"));
#endif
}

void PlayerBackendFactoryTest::envOverrideTakesPrecedenceOverConfigPreference()
{
#if defined(Q_OS_WIN)
    qputenv("BLOOM_PLAYER_BACKEND", "win-libmpv");
    const QString configuredPreference = QStringLiteral("external-mpv-ipc");
#else
    qputenv("BLOOM_PLAYER_BACKEND", "external-mpv-ipc");
    const QString configuredPreference = QStringLiteral("win-libmpv");
#endif

    std::unique_ptr<IPlayerBackend> backend = PlayerBackendFactory::create(configuredPreference);

    QVERIFY(backend != nullptr);
#if defined(Q_OS_WIN)
    QCOMPARE(backend->backendName(), QStringLiteral("win-libmpv"));
#else
    QCOMPARE(backend->backendName(), QStringLiteral("external-mpv-ipc"));
#endif

    qunsetenv("BLOOM_PLAYER_BACKEND");
}

void PlayerBackendFactoryTest::windowsEmbeddedSanitizerFiltersRenderBackendOverrides()
{
#if defined(Q_OS_WIN)
    WindowsMpvBackend backend;
    const QStringList args = {
        QStringLiteral("--target-colorspace-hint=auto"),
        QStringLiteral("--target-colorspace-hint-mode=target"),
        QStringLiteral("--gpu-api=vulkan"),
        QStringLiteral("--gpu-context=winvk"),
        QStringLiteral("--vulkan-device=GPU-1"),
        QStringLiteral("--wid=12345"),
        QStringLiteral("--profile=fast"),
        QStringLiteral("--profile=high-quality"),
        QStringLiteral("--profile=custom")
    };

    const QStringList sanitized = backend.sanitizeStartupArgsForTest(args);

    QVERIFY(sanitized.contains(QStringLiteral("--target-colorspace-hint=auto")));
    QVERIFY(sanitized.contains(QStringLiteral("--target-colorspace-hint-mode=target")));
    QVERIFY(sanitized.contains(QStringLiteral("--profile=fast")));
    QVERIFY(sanitized.contains(QStringLiteral("--profile=high-quality")));
    QVERIFY(!sanitized.contains(QStringLiteral("--profile=custom")));
    QVERIFY(!sanitized.contains(QStringLiteral("--gpu-api=vulkan")));
    QVERIFY(!sanitized.contains(QStringLiteral("--gpu-context=winvk")));
    QVERIFY(!sanitized.contains(QStringLiteral("--vulkan-device=GPU-1")));
    QVERIFY(!sanitized.contains(QStringLiteral("--wid=12345")));
#else
    QSKIP("Windows embedded sanitizer is only compiled on Windows");
#endif
}

void PlayerBackendFactoryTest::windowsEmbeddedRenderApiProfilesApplyExpectedOptions()
{
#if defined(Q_OS_WIN)
    WindowsMpvBackend backend;

    QCOMPARE(backend.renderApiStartupArgsForTest({
                 QStringLiteral("--bloom-windows-render-api=auto")
             }),
             QStringList({QStringLiteral("--vo=gpu-next")}));

    QCOMPARE(backend.renderApiStartupArgsForTest({
                 QStringLiteral("--bloom-windows-render-api=d3d11"),
                 QStringLiteral("--bloom-windows-10bit-output=yes")
             }),
             QStringList({
                 QStringLiteral("--vo=gpu-next"),
                 QStringLiteral("--gpu-api=d3d11"),
                 QStringLiteral("--gpu-context=d3d11"),
                 QStringLiteral("--d3d11-output-format=rgb10_a2")
             }));

    QCOMPARE(backend.renderApiStartupArgsForTest({
                 QStringLiteral("--bloom-windows-render-api=vulkan"),
                 QStringLiteral("--bloom-windows-10bit-output=yes")
             }),
             QStringList({
                 QStringLiteral("--vo=gpu-next"),
                 QStringLiteral("--gpu-api=vulkan"),
                 QStringLiteral("--gpu-context=winvk")
             }));

    QCOMPARE(backend.renderApiStartupArgsForTest({
                 QStringLiteral("--bloom-windows-render-api=auto"),
                 QStringLiteral("--bloom-windows-10bit-output=yes")
             }),
             QStringList({QStringLiteral("--vo=gpu-next")}));
#else
    QSKIP("Windows embedded render API profiles are only compiled on Windows");
#endif
}

void PlayerBackendFactoryTest::windowsEmbeddedShaderListUsesResolvedGlslShadersOption()
{
#if defined(Q_OS_WIN)
    const QString mpvConfigDir = ConfigManager::getMpvConfigDir();
    WindowsMpvBackend backend;

    const QStringList highQualityArgs{
        QStringLiteral("--glsl-shaders-clr"),
        QStringLiteral("--glsl-shaders-append=~~/shaders/FSRCNNX_x2_8-0-4-1.glsl"),
        QStringLiteral("--glsl-shaders-append=~~/shaders/KrigBilateral.glsl"),
        QStringLiteral("--glsl-shaders-append=~~/shaders/SSimDownscaler.glsl"),
    };

    const QStringList shaderPaths = backend.embeddedShaderPathsForTest(highQualityArgs);
    const QStringList expectedPaths{
        QDir(mpvConfigDir).filePath(QStringLiteral("shaders/FSRCNNX_x2_8-0-4-1.glsl")),
        QDir(mpvConfigDir).filePath(QStringLiteral("shaders/KrigBilateral.glsl")),
        QDir(mpvConfigDir).filePath(QStringLiteral("shaders/SSimDownscaler.glsl")),
    };
    QCOMPARE(shaderPaths, expectedPaths);
    QVERIFY(shaderPaths.size() == 3);
#else
    QSKIP("Windows embedded shader list helper is only compiled on Windows");
#endif
}

void PlayerBackendFactoryTest::embeddedVideoFilterListOperationsAreDeferred()
{
    QStringList args{
        QStringLiteral("--vo=gpu-next"),
        QStringLiteral("--vf=lavfi=[bwdif]"),
        QStringLiteral("--vf-append=@bloom-dovi:format=dolbyvision=yes:enhancement-layer=yes"),
        QStringLiteral("--vf-pre=@user:format=colorlevels=full"),
        QStringLiteral("--vf-clr"),
        QStringLiteral("--vfoo=bar"),
        QStringLiteral("--af-append=lavfi=[loudnorm]"),
    };
    const QList<BloomBackend::VideoFilterListOperation> operations =
        BloomBackend::takeVideoFilterListOperations(&args);

    // Replacements and list edits retain their relative order.
    QCOMPARE(args, QStringList({QStringLiteral("--vo=gpu-next"),
                                QStringLiteral("--vfoo=bar"),
                                QStringLiteral("--af-append=lavfi=[loudnorm]")}));
    QCOMPARE(operations.size(), 4);
    QCOMPARE(operations.at(0).operation, QStringLiteral("set"));
    QCOMPARE(operations.at(0).value, QStringLiteral("lavfi=[bwdif]"));
    QCOMPARE(operations.at(1).operation, QStringLiteral("append"));
    QCOMPARE(operations.at(1).value,
             QStringLiteral("@bloom-dovi:format=dolbyvision=yes:enhancement-layer=yes"));
    QCOMPARE(operations.at(2).operation, QStringLiteral("pre"));
    QCOMPARE(operations.at(3).operation, QStringLiteral("clr"));
    QVERIFY(operations.at(3).value.isEmpty());

    args = {QStringLiteral("--vf-append=hflip"), QStringLiteral("--vf=vflip"),
            QStringLiteral("--vf-append=format=dolbyvision=yes")};
    const auto ordered = BloomBackend::takeVideoFilterListOperations(&args);
    QVERIFY(args.isEmpty());
    QCOMPARE(ordered.size(), 3);
    QCOMPARE(ordered.at(0).operation, QStringLiteral("append"));
    QCOMPARE(ordered.at(1).operation, QStringLiteral("set"));
    QCOMPARE(ordered.at(1).value, QStringLiteral("vflip"));
    QCOMPARE(ordered.at(2).operation, QStringLiteral("append"));
}

void PlayerBackendFactoryTest::linkedRuntimePreservesVideoFilterOrder()
{
#if defined(BLOOM_HAS_LIBMPV)
    std::unique_ptr<mpv_handle, decltype(&mpv_terminate_destroy)> handle(mpv_create(), mpv_terminate_destroy);
    QVERIFY(handle);
    QVERIFY(mpv_set_option_string(handle.get(), "config", "no") >= 0);
    QVERIFY(mpv_set_option_string(handle.get(), "vo", "null") >= 0);
    QVERIFY(mpv_set_option_string(handle.get(), "ao", "null") >= 0);
    QVERIFY(mpv_initialize(handle.get()) >= 0);
    QStringList args{QStringLiteral("--vf-append=@discard:format=gamma=pq"),
                     QStringLiteral("--vf=@user:format=gamma=bt.1886"),
                     QStringLiteral("--vf-append=@bloom-dovi:format=dolbyvision=yes")};
    const auto operations = BloomBackend::takeVideoFilterListOperations(&args);
    QVERIFY(BloomBackend::applyVideoFilterListOperations(handle.get(), operations).isEmpty());
    QVERIFY(BloomBackend::applyVideoFilterListOperations(handle.get(), {operations.last()}).isEmpty());
    mpv_node value{};
    QVERIFY(mpv_get_property(handle.get(), "vf", MPV_FORMAT_NODE, &value) >= 0);
    const auto filters = BloomBackend::mpvNodeToVariant(&value).toList();
    mpv_free_node_contents(&value);
    QCOMPARE(filters.size(), 2);
    QCOMPARE(filters.first().toMap().value(QStringLiteral("label")).toString(), QStringLiteral("user"));
    QCOMPARE(filters.last().toMap().value(QStringLiteral("label")).toString(), QStringLiteral("bloom-dovi"));
#else
    QSKIP("libmpv is unavailable");
#endif
}

void PlayerBackendFactoryTest::externalBackendRuntimeIsUnknownUntilProbed()
{
    ExternalMpvBackend backend;
    const Bloom::PlaybackPolicy::MpvRuntimeInfo info = backend.runtimeInfo();
    QCOMPARE(info.backend, QStringLiteral("external-mpv-ipc"));
    QCOMPARE(info.formatFilter, Bloom::PlaybackPolicy::FormatFilterSupport::Unknown);
    QCOMPARE(backend.rendererKind({QStringLiteral("--vo=gpu-next")}),
             Bloom::PlaybackPolicy::RendererKind::GpuNext);
    QCOMPARE(backend.rendererKind({}), Bloom::PlaybackPolicy::RendererKind::Unknown);
}

QTEST_MAIN(PlayerBackendFactoryTest)
#include "PlayerBackendFactoryTest.moc"
