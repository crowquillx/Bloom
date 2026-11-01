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
    void keyboardShortcutsFollowPlaybackBindings();
    void defaultControllerPlaybackNavigationBindings();
    void controllerNavigationAndPlaybackBindingsDoNotConflict();
    void crossContextBindingsDoNotConflict();
    void menuActionDefaults();
    void controllerStartFollowsAnyPlayback();
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

void InputBindingManagerTest::keyboardShortcutsFollowPlaybackBindings()
{
    BindingFixture fixture;
    InputBindingManager manager(nullptr, &fixture.config);

    const auto shortcutAction = [&manager](const QString &sequence) {
        for (const QVariant &entry : manager.keyboardShortcuts(QStringLiteral("playback"))) {
            const QVariantMap shortcut = entry.toMap();
            if (shortcut.value(QStringLiteral("sequence")).toString() == sequence) {
                return shortcut.value(QStringLiteral("actionId")).toString();
            }
        }
        return QString();
    };

    // S and K belong to the subtitle actions, not stop and pause.
    QCOMPARE(shortcutAction(QStringLiteral("S")), QStringLiteral("playback.subtitleSelector"));
    QCOMPARE(shortcutAction(QStringLiteral("K")), QStringLiteral("playback.subtitleOverride"));
    QCOMPARE(shortcutAction(QStringLiteral("Space")), QStringLiteral("playback.playPause"));
    QCOMPARE(shortcutAction(QStringLiteral("J")), QStringLiteral("playback.seekBack"));
    QCOMPARE(shortcutAction(QStringLiteral("L")), QStringLiteral("playback.seekForward"));
    QCOMPARE(shortcutAction(QStringLiteral("I")), QStringLiteral("playback.stats"));
    QCOMPARE(shortcutAction(QStringLiteral("Shift+I")), QStringLiteral("playback.statsOnce"));
    QCOMPARE(shortcutAction(QStringLiteral("+")), QStringLiteral("playback.volumeUp"));
    QVERIFY(shortcutAction(QStringLiteral("Esc")).isEmpty());

    // Rebinding moves the shortcut; an unassigned action has none.
    QVERIFY(manager.setBindingsForAction(QStringLiteral("keyboard"),
                                         QStringLiteral("playback.playPause"),
                                         {QStringLiteral("key:p")}));
    QVERIFY(manager.setBindingsForAction(QStringLiteral("keyboard"),
                                         QStringLiteral("playback.subtitleOverride"),
                                         {}));
    QCOMPARE(shortcutAction(QStringLiteral("P")), QStringLiteral("playback.playPause"));
    QVERIFY(shortcutAction(QStringLiteral("Space")).isEmpty());
    QVERIFY(shortcutAction(QStringLiteral("K")).isEmpty());
    QCOMPARE(manager.actionForKeyboardEvent(Qt::Key_P, Qt::NoModifier, QStringLiteral("playback")),
             QStringLiteral("playback.playPause"));
}

void InputBindingManagerTest::defaultControllerPlaybackNavigationBindings()
{
    BindingFixture fixture;
    InputBindingManager manager(nullptr, &fixture.config);

    const auto expectBinding = [&manager](const QString &actionId, const QString &binding) {
        const QVariantList bindings = manager.bindingsForAction(QStringLiteral("gamepad"), actionId);
        QVERIFY2(bindings.contains(binding), qPrintable(QStringLiteral("%1 missing %2").arg(actionId, binding)));
    };

    expectBinding(QStringLiteral("playback.navigateUp"), QStringLiteral("gamepad:dpad_up"));
    expectBinding(QStringLiteral("playback.navigateUp"), QStringLiteral("gamepad:left_stick_up"));
    expectBinding(QStringLiteral("playback.navigateDown"), QStringLiteral("gamepad:dpad_down"));
    expectBinding(QStringLiteral("playback.navigateLeft"), QStringLiteral("gamepad:dpad_left"));
    expectBinding(QStringLiteral("playback.navigateRight"), QStringLiteral("gamepad:dpad_right"));
    expectBinding(QStringLiteral("playback.select"), QStringLiteral("gamepad:a"));
    expectBinding(QStringLiteral("playback.back"), QStringLiteral("gamepad:b"));
}

void InputBindingManagerTest::controllerNavigationAndPlaybackBindingsDoNotConflict()
{
    BindingFixture fixture;
    InputBindingManager manager(nullptr, &fixture.config);

    QVERIFY(manager.conflictsForBinding(QStringLiteral("gamepad"),
                                        QStringLiteral("playback.select"),
                                        QStringLiteral("gamepad:a"),
                                        QStringLiteral("playback")).isEmpty());
    QVERIFY(manager.conflictsForBinding(QStringLiteral("gamepad"),
                                        QStringLiteral("playback.back"),
                                        QStringLiteral("gamepad:b"),
                                        QStringLiteral("playback")).isEmpty());
    QVERIFY(manager.conflictsForBinding(QStringLiteral("gamepad"),
                                        QStringLiteral("playback.navigateLeft"),
                                        QStringLiteral("gamepad:dpad_left"),
                                        QStringLiteral("playback")).isEmpty());
    QVERIFY(manager.conflictsForBinding(QStringLiteral("gamepad"),
                                        QStringLiteral("nav.select"),
                                        QStringLiteral("gamepad:a"),
                                        QStringLiteral("navigation")).isEmpty());
    QVERIFY(manager.conflictsForBinding(QStringLiteral("gamepad"),
                                        QStringLiteral("nav.back"),
                                        QStringLiteral("gamepad:b"),
                                        QStringLiteral("navigation")).isEmpty());
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

void InputBindingManagerTest::menuActionDefaults()
{
    BindingFixture fixture;
    InputBindingManager manager(nullptr, &fixture.config);

    // The context-menu key and Shift+F10 open a card's actions; the
    // controller's Start/Menu button does in navigation and stays play/pause
    // during playback.
    QCOMPARE(manager.actionForKeyboardEvent(Qt::Key_Menu, Qt::NoModifier, QStringLiteral("navigation")),
             QStringLiteral("nav.menu"));
    QCOMPARE(manager.actionForKeyboardEvent(Qt::Key_F10, Qt::ShiftModifier, QStringLiteral("navigation")),
             QStringLiteral("nav.menu"));
    QVERIFY(manager.actionForKeyboardEvent(Qt::Key_F10, Qt::NoModifier, QStringLiteral("navigation")).isEmpty());
    QVERIFY(manager.bindingsForAction(QStringLiteral("gamepad"), QStringLiteral("nav.menu"))
                .contains(QStringLiteral("gamepad:start")));
    QVERIFY(manager.conflictsForBinding(QStringLiteral("gamepad"),
                                        QStringLiteral("nav.menu"),
                                        QStringLiteral("gamepad:start"),
                                        QStringLiteral("navigation")).isEmpty());
    QVERIFY(manager.conflictsForBinding(QStringLiteral("keyboard"),
                                        QStringLiteral("nav.menu"),
                                        QStringLiteral("key:menu"),
                                        QStringLiteral("navigation")).isEmpty());
}

void InputBindingManagerTest::controllerStartFollowsAnyPlayback()
{
    BindingFixture fixture;
    InputBindingManager manager(nullptr, &fixture.config);

    // Main.qml binds the runtime context to this for embedded and external
    // mpv alike, so Start stays play/pause and never opens the card menu
    // on the page behind the player.
    QCOMPARE(manager.runtimeContextFor(true, false), QStringLiteral("playback"));
    QCOMPARE(manager.runtimeContextFor(false, false), QStringLiteral("navigation"));
    // The screensaver over paused playback navigates.
    QCOMPARE(manager.runtimeContextFor(true, true), QStringLiteral("navigation"));

    manager.setCurrentRuntimeContext(manager.runtimeContextFor(true, false));
    QCOMPARE(manager.actionForGamepadBinding(QStringLiteral("gamepad:start")),
             QStringLiteral("playback.playPause"));
    QVERIFY(manager.actionForGamepadBinding(QStringLiteral("gamepad:dpad_up"))
                .startsWith(QStringLiteral("playback.")));

    manager.setCurrentRuntimeContext(manager.runtimeContextFor(false, false));
    QCOMPARE(manager.actionForGamepadBinding(QStringLiteral("gamepad:start")),
             QStringLiteral("nav.menu"));
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
