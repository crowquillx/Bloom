#include <QtTest/QtTest>

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QTemporaryDir>

#include <memory>

namespace {

// A page with two focusable items, a fallback target, and a screensaver
// overlay that takes focus while it is visible, as ScreensaverOverlay does.
constexpr auto kScene = R"QML(
import QtQuick
import BloomUI

Item {
    id: scene
    width: 800
    height: 600

    property alias memory: memory
    property alias overlay: overlay
    property alias first: first
    property alias second: second
    property alias fallbackTarget: fallbackTarget
    property int fallbackCalls: 0

    FocusScope {
        anchors.fill: parent
        focus: true
        Rectangle { id: first; objectName: "first"; width: 10; height: 10; activeFocusOnTab: true; focus: true }
        Rectangle { id: second; objectName: "second"; x: 20; width: 10; height: 10; activeFocusOnTab: true }
        Rectangle { id: fallbackTarget; objectName: "fallbackTarget"; x: 40; width: 10; height: 10 }
    }

    Item {
        id: overlay
        objectName: "overlay"
        anchors.fill: parent
        visible: false
        focus: visible
    }

    ScreensaverFocusMemory {
        id: memory
        window: scene.Window.window
        overlay: overlay
        fallback: function() {
            scene.fallbackCalls++
            fallbackTarget.forceActiveFocus()
        }
    }
}
)QML";

} // namespace

class ScreensaverFocusMemoryTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void restoresTheItemFocusedBeforeTheScreensaver();
    void overlayTakingFocusBeforeFreezeStillRestoresThePage();
    void hiddenItemFallsBackToThePage();
    void destroyedItemFallsBackToThePage();
    void focusChangesWhileFrozenAreIgnored();

private:
    QQuickItem *sceneItem(const char *name) const;
    QObject *memory() const;
    QString focusedName() const;
    void showOverlay();
    void hideOverlayAndRestore();

    QTemporaryDir m_moduleRoot;
    std::unique_ptr<QQuickView> m_view;
};

void ScreensaverFocusMemoryTest::initTestCase()
{
    QVERIFY(m_moduleRoot.isValid());

    QDir root(m_moduleRoot.path());
    QVERIFY(root.mkdir(QStringLiteral("BloomUI")));
    const QDir module(root.filePath(QStringLiteral("BloomUI")));
    const QDir sourceDir(QStringLiteral(BLOOM_UI_SOURCE_DIR));
    QVERIFY(QFile::copy(sourceDir.filePath(QStringLiteral("ScreensaverFocusMemory.qml")),
                        module.filePath(QStringLiteral("ScreensaverFocusMemory.qml"))));
    QFile qmldir(module.filePath(QStringLiteral("qmldir")));
    QVERIFY(qmldir.open(QIODevice::WriteOnly | QIODevice::Text));
    qmldir.write("module BloomUI\n"
                 "ScreensaverFocusMemory 1.0 ScreensaverFocusMemory.qml\n");
    qmldir.close();

    QFile scene(root.filePath(QStringLiteral("Scene.qml")));
    QVERIFY(scene.open(QIODevice::WriteOnly | QIODevice::Text));
    scene.write(kScene);
    scene.close();
}

void ScreensaverFocusMemoryTest::init()
{
    m_view = std::make_unique<QQuickView>();
    m_view->engine()->addImportPath(m_moduleRoot.path());
    m_view->setSource(QUrl::fromLocalFile(QDir(m_moduleRoot.path()).filePath(QStringLiteral("Scene.qml"))));
    QVERIFY2(m_view->status() == QQuickView::Ready,
             qPrintable(m_view->errors().isEmpty() ? QString() : m_view->errors().first().toString()));
    m_view->show();
    m_view->requestActivate();
    QVERIFY(QTest::qWaitForWindowActive(m_view.get()));
    QTRY_COMPARE(focusedName(), QStringLiteral("first"));
}

void ScreensaverFocusMemoryTest::cleanup()
{
    m_view.reset();
}

QQuickItem *ScreensaverFocusMemoryTest::sceneItem(const char *name) const
{
    return m_view->rootObject()->property(name).value<QQuickItem *>();
}

QObject *ScreensaverFocusMemoryTest::memory() const
{
    return m_view->rootObject()->property("memory").value<QObject *>();
}

QString ScreensaverFocusMemoryTest::focusedName() const
{
    QQuickItem *focused = m_view->activeFocusItem();
    return focused ? focused->objectName() : QString();
}

void ScreensaverFocusMemoryTest::showOverlay()
{
    sceneItem("overlay")->setVisible(true);
    QTRY_COMPARE(focusedName(), QStringLiteral("overlay"));
}

void ScreensaverFocusMemoryTest::hideOverlayAndRestore()
{
    sceneItem("overlay")->setVisible(false);
    QMetaObject::invokeMethod(memory(), "restore");
}

void ScreensaverFocusMemoryTest::restoresTheItemFocusedBeforeTheScreensaver()
{
    sceneItem("second")->forceActiveFocus();
    QMetaObject::invokeMethod(memory(), "freeze");
    showOverlay();

    hideOverlayAndRestore();
    QTRY_COMPARE(focusedName(), QStringLiteral("second"));
    QCOMPARE(m_view->rootObject()->property("fallbackCalls").toInt(), 0);
}

void ScreensaverFocusMemoryTest::overlayTakingFocusBeforeFreezeStillRestoresThePage()
{
    sceneItem("second")->forceActiveFocus();
    // The overlay's focus binding can run before the activation handler.
    showOverlay();
    QMetaObject::invokeMethod(memory(), "freeze");

    hideOverlayAndRestore();
    QTRY_COMPARE(focusedName(), QStringLiteral("second"));
}

void ScreensaverFocusMemoryTest::hiddenItemFallsBackToThePage()
{
    sceneItem("second")->forceActiveFocus();
    QMetaObject::invokeMethod(memory(), "freeze");
    showOverlay();
    // The hero rotated to a series and hid its Play button.
    sceneItem("second")->setVisible(false);

    hideOverlayAndRestore();
    QTRY_COMPARE(focusedName(), QStringLiteral("fallbackTarget"));
    QCOMPARE(m_view->rootObject()->property("fallbackCalls").toInt(), 1);
}

void ScreensaverFocusMemoryTest::destroyedItemFallsBackToThePage()
{
    sceneItem("second")->forceActiveFocus();
    QMetaObject::invokeMethod(memory(), "freeze");
    showOverlay();
    // A Home row reloaded and destroyed its delegates.
    delete sceneItem("second");

    hideOverlayAndRestore();
    QTRY_COMPARE(focusedName(), QStringLiteral("fallbackTarget"));
    QCOMPARE(m_view->rootObject()->property("fallbackCalls").toInt(), 1);
}

void ScreensaverFocusMemoryTest::focusChangesWhileFrozenAreIgnored()
{
    sceneItem("second")->forceActiveFocus();
    QMetaObject::invokeMethod(memory(), "freeze");
    showOverlay();
    // A page restoring focus behind the screensaver must not replace the
    // remembered item.
    sceneItem("first")->forceActiveFocus();
    sceneItem("overlay")->forceActiveFocus();

    hideOverlayAndRestore();
    QTRY_COMPARE(focusedName(), QStringLiteral("second"));
}

QTEST_MAIN(ScreensaverFocusMemoryTest)
#include "ScreensaverFocusMemoryTest.moc"
