#include <QtTest/QtTest>
#include <QtGlobal>
#include <memory>

#include "player/backend/IPlayerBackend.h"
#include "player/backend/PlayerBackendFactory.h"
#if defined(Q_OS_WIN)
#include "player/backend/WindowsMpvBackend.h"
#endif

class PlayerBackendFactoryTest : public QObject
{
    Q_OBJECT

private slots:
    void createsPlatformDefaultBackend();
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
};

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
        QStringLiteral("--profile=fast")
    };

    const QStringList sanitized = backend.sanitizeStartupArgsForTest(args);

    QVERIFY(sanitized.contains(QStringLiteral("--target-colorspace-hint=auto")));
    QVERIFY(sanitized.contains(QStringLiteral("--target-colorspace-hint-mode=target")));
    QVERIFY(sanitized.contains(QStringLiteral("--profile=fast")));
    QVERIFY(!sanitized.contains(QStringLiteral("--gpu-api=vulkan")));
    QVERIFY(!sanitized.contains(QStringLiteral("--gpu-context=winvk")));
    QVERIFY(!sanitized.contains(QStringLiteral("--vulkan-device=GPU-1")));
    QVERIFY(!sanitized.contains(QStringLiteral("--wid=12345")));
#else
    QSKIP("Windows embedded sanitizer is only compiled on Windows");
#endif
}

QTEST_MAIN(PlayerBackendFactoryTest)
#include "PlayerBackendFactoryTest.moc"
