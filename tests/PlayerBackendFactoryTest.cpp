#include <QtTest/QtTest>
#include <QtGlobal>
#include <memory>

#include "player/backend/IPlayerBackend.h"
#include "player/backend/PlayerBackendFactory.h"

class PlayerBackendFactoryTest : public QObject
{
    Q_OBJECT

private slots:
    void createsExternalBackendByDefault();
    void backendStartsInStoppedState();
    void createByNameSupportsExternal();
    void createByNameLinuxSelectionBehavior();
    void createByNameFallsBackForUnknown();
    void envOverrideSelectsExternalBackend();
    void envOverrideSelectsLinuxBackendWhenAvailable();
};

void PlayerBackendFactoryTest::createsExternalBackendByDefault()
{
    qunsetenv("BLOOM_PLAYER_BACKEND");
    std::unique_ptr<IPlayerBackend> backend = PlayerBackendFactory::create();

    QVERIFY(backend != nullptr);
#if defined(Q_OS_LINUX)
    QVERIFY(backend->backendName() == QStringLiteral("linux-libmpv-opengl")
            || backend->backendName() == QStringLiteral("external-mpv-ipc"));
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
    QCOMPARE(backend->backendName(), QStringLiteral("external-mpv-ipc"));
}

void PlayerBackendFactoryTest::createByNameLinuxSelectionBehavior()
{
    std::unique_ptr<IPlayerBackend> backend = PlayerBackendFactory::createByName(QStringLiteral("linux-libmpv-opengl"));

    QVERIFY(backend != nullptr);
#if defined(Q_OS_LINUX)
    QVERIFY(backend->backendName() == QStringLiteral("linux-libmpv-opengl")
            || backend->backendName() == QStringLiteral("external-mpv-ipc"));
#else
    QCOMPARE(backend->backendName(), QStringLiteral("external-mpv-ipc"));
#endif
}

void PlayerBackendFactoryTest::createByNameFallsBackForUnknown()
{
    std::unique_ptr<IPlayerBackend> backend = PlayerBackendFactory::createByName(QStringLiteral("unknown-backend"));

    QVERIFY(backend != nullptr);
    QCOMPARE(backend->backendName(), QStringLiteral("external-mpv-ipc"));
}

void PlayerBackendFactoryTest::envOverrideSelectsExternalBackend()
{
    qputenv("BLOOM_PLAYER_BACKEND", "external-mpv-ipc");

    std::unique_ptr<IPlayerBackend> backend = PlayerBackendFactory::create();

    QVERIFY(backend != nullptr);
    QCOMPARE(backend->backendName(), QStringLiteral("external-mpv-ipc"));

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
#else
    QCOMPARE(backend->backendName(), QStringLiteral("external-mpv-ipc"));
#endif

    qunsetenv("BLOOM_PLAYER_BACKEND");
}

QTEST_MAIN(PlayerBackendFactoryTest)
#include "PlayerBackendFactoryTest.moc"
