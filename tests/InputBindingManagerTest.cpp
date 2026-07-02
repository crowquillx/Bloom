#include "utils/ConfigManager.h"
#include "utils/InputBindingManager.h"

#include <QDir>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

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

class BindingFixture
{
public:
    BindingFixture()
        : isolation(tempDir.path())
    {
        config.load();
    }

    QTemporaryDir tempDir;
    ScopedConfigIsolation isolation;
    ConfigManager config;
};

} // namespace

class InputBindingManagerTest : public QObject
{
    Q_OBJECT

private slots:
    void defaultKeyboardResolutionIsContextAware();
    void crossContextBindingsDoNotConflict();
    void sameContextConflictCanBeReassigned();
    void setBindingsForActionClearsSameContextConflicts();
    void emptyBindingsIntentionallyUnassignAction();
    void resetActionRestoresDefaultBinding();
    void rejectsInvalidDeviceAndAction();
};

void InputBindingManagerTest::defaultKeyboardResolutionIsContextAware()
{
    BindingFixture fixture;
    InputBindingManager manager(nullptr, &fixture.config);

    QCOMPARE(manager.actionForKeyboardEvent(Qt::Key_Space, Qt::NoModifier, QStringLiteral("navigation")),
             QStringLiteral("nav.select"));
    QCOMPARE(manager.actionForKeyboardEvent(Qt::Key_Space, Qt::NoModifier, QStringLiteral("playback")),
             QStringLiteral("playback.playPause"));
    QCOMPARE(manager.actionForKeyboardEvent(Qt::Key_I, Qt::ShiftModifier, QStringLiteral("playback")),
             QStringLiteral("playback.statsOnce"));
}

void InputBindingManagerTest::crossContextBindingsDoNotConflict()
{
    BindingFixture fixture;
    InputBindingManager manager(nullptr, &fixture.config);

    const QVariantList conflicts = manager.conflictsForBinding(QStringLiteral("keyboard"),
                                                               QStringLiteral("playback.playPause"),
                                                               QStringLiteral("key:space"),
                                                               QStringLiteral("playback"));
    QVERIFY(conflicts.isEmpty());
}

void InputBindingManagerTest::sameContextConflictCanBeReassigned()
{
    BindingFixture fixture;
    InputBindingManager manager(nullptr, &fixture.config);

    const QVariantList originalSubtitleBindings =
        manager.bindingsForAction(QStringLiteral("keyboard"), QStringLiteral("playback.subtitleSelector"));
    QVERIFY(originalSubtitleBindings.contains(QStringLiteral("key:s")));

    QVERIFY(manager.setBindingForAction(QStringLiteral("keyboard"),
                                        QStringLiteral("playback.audioSelector"),
                                        QStringLiteral("key:s"),
                                        true));
    const QVariantList subtitleBindings = manager.bindingsForAction(QStringLiteral("keyboard"),
                                                                    QStringLiteral("playback.subtitleSelector"));
    for (const QVariant &binding : subtitleBindings) {
        QVERIFY(binding.toString() != QStringLiteral("key:s"));
    }
    QCOMPARE(manager.bindingsForAction(QStringLiteral("keyboard"),
                                       QStringLiteral("playback.audioSelector")).value(0).toString(),
             QStringLiteral("key:s"));
}

void InputBindingManagerTest::setBindingsForActionClearsSameContextConflicts()
{
    BindingFixture fixture;
    InputBindingManager manager(nullptr, &fixture.config);

    QVariantList replacementBindings;
    replacementBindings.append(QStringLiteral("key:s"));
    replacementBindings.append(QStringLiteral("key:space"));

    QVERIFY(manager.setBindingsForAction(QStringLiteral("keyboard"),
                                         QStringLiteral("playback.audioSelector"),
                                         replacementBindings));

    QCOMPARE(manager.actionForKeyboardEvent(Qt::Key_S, Qt::NoModifier, QStringLiteral("playback")),
             QStringLiteral("playback.audioSelector"));
    const QVariantList subtitleBindings = manager.bindingsForAction(QStringLiteral("keyboard"),
                                                                    QStringLiteral("playback.subtitleSelector"));
    for (const QVariant &binding : subtitleBindings) {
        QVERIFY(binding.toString() != QStringLiteral("key:s"));
    }
    QCOMPARE(manager.actionForKeyboardEvent(Qt::Key_Space, Qt::NoModifier, QStringLiteral("navigation")),
             QStringLiteral("nav.select"));
}

void InputBindingManagerTest::emptyBindingsIntentionallyUnassignAction()
{
    BindingFixture fixture;
    InputBindingManager manager(nullptr, &fixture.config);

    QVERIFY(manager.setBindingsForAction(QStringLiteral("gamepad"),
                                         QStringLiteral("playback.playPause"),
                                         QVariantList()));
    QVERIFY(manager.bindingsForAction(QStringLiteral("gamepad"),
                                      QStringLiteral("playback.playPause")).isEmpty());
}

void InputBindingManagerTest::resetActionRestoresDefaultBinding()
{
    BindingFixture fixture;
    InputBindingManager manager(nullptr, &fixture.config);

    QVERIFY(manager.setBindingsForAction(QStringLiteral("gamepad"),
                                         QStringLiteral("playback.playPause"),
                                         QVariantList()));
    manager.resetActionBindings(QStringLiteral("gamepad"), QStringLiteral("playback.playPause"));
    QCOMPARE(manager.bindingsForAction(QStringLiteral("gamepad"),
                                       QStringLiteral("playback.playPause")).value(0).toString(),
             QStringLiteral("gamepad:start"));
}

void InputBindingManagerTest::rejectsInvalidDeviceAndAction()
{
    BindingFixture fixture;
    InputBindingManager manager(nullptr, &fixture.config);

    QVERIFY(!manager.setBindingForAction(QStringLiteral("mouse"),
                                         QStringLiteral("playback.playPause"),
                                         QStringLiteral("mouse:left"),
                                         true));
    QVERIFY(!manager.setBindingForAction(QStringLiteral("keyboard"),
                                         QStringLiteral("missing.action"),
                                         QStringLiteral("key:x"),
                                         true));
}

QTEST_GUILESS_MAIN(InputBindingManagerTest)
#include "InputBindingManagerTest.moc"
