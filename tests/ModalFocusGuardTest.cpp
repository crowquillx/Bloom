#include <QtTest/QtTest>

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickView>
#include <QTemporaryDir>

#include <memory>

namespace {

// A page item behind a modal dialog, and a second popup that can open above it.
constexpr auto kScene = R"QML(
import QtQuick
import QtQuick.Controls
import BloomUI

Item {
    id: scene
    width: 800
    height: 600

    property alias dialog: dialog
    property alias topPopup: topPopup
    property alias page: page
    property alias first: first
    property alias second: second
    property alias topButton: topButton

    Button {
        id: page
        objectName: "page"
        focus: true
    }

    Dialog {
        id: dialog
        parent: Overlay.overlay
        modal: true
        focus: true

        contentItem: Column {
            Button { id: first; objectName: "first" }
            Button { id: second; objectName: "second" }
        }

        ModalFocusGuard {
            popup: dialog
            fallbackItem: first
        }
    }

    Popup {
        id: topPopup
        parent: Overlay.overlay
        focus: true
        contentItem: Button { id: topButton; objectName: "topButton" }
    }
}
)QML";

} // namespace

class ModalFocusGuardTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void lateFocusStealReturnsToLastFocusedItem();
    void lateFocusStealBeforeAnyFocusUsesFallback();
    void closedDialogLeavesFocusAlone();
    void popupAboveTheDialogKeepsFocus();

private:
    QQuickItem *sceneItem(const char *name) const;
    QObject *sceneObject(const char *name) const;
    QString focusedName() const;
    void openDialog();

    QTemporaryDir m_moduleRoot;
    std::unique_ptr<QQuickView> m_view;
};

void ModalFocusGuardTest::initTestCase()
{
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QVERIFY(m_moduleRoot.isValid());

    QDir root(m_moduleRoot.path());
    QVERIFY(root.mkdir(QStringLiteral("BloomUI")));
    const QDir module(root.filePath(QStringLiteral("BloomUI")));
    const QDir sourceDir(QStringLiteral(BLOOM_UI_SOURCE_DIR));
    QVERIFY(QFile::copy(sourceDir.filePath(QStringLiteral("ModalFocusGuard.qml")),
                        module.filePath(QStringLiteral("ModalFocusGuard.qml"))));
    QFile qmldir(module.filePath(QStringLiteral("qmldir")));
    QVERIFY(qmldir.open(QIODevice::WriteOnly | QIODevice::Text));
    qmldir.write("module BloomUI\n"
                 "ModalFocusGuard 1.0 ModalFocusGuard.qml\n");
    qmldir.close();

    QFile scene(root.filePath(QStringLiteral("Scene.qml")));
    QVERIFY(scene.open(QIODevice::WriteOnly | QIODevice::Text));
    scene.write(kScene);
    scene.close();
}

void ModalFocusGuardTest::init()
{
    m_view = std::make_unique<QQuickView>();
    m_view->engine()->addImportPath(m_moduleRoot.path());
    m_view->setSource(QUrl::fromLocalFile(QDir(m_moduleRoot.path()).filePath(QStringLiteral("Scene.qml"))));
    QVERIFY2(m_view->status() == QQuickView::Ready,
             qPrintable(m_view->errors().isEmpty() ? QString() : m_view->errors().first().toString()));
    m_view->show();
    m_view->requestActivate();
    QVERIFY(QTest::qWaitForWindowActive(m_view.get()));
    QTRY_COMPARE(focusedName(), QStringLiteral("page"));
}

void ModalFocusGuardTest::cleanup()
{
    m_view.reset();
}

QQuickItem *ModalFocusGuardTest::sceneItem(const char *name) const
{
    return m_view->rootObject()->property(name).value<QQuickItem *>();
}

QObject *ModalFocusGuardTest::sceneObject(const char *name) const
{
    return m_view->rootObject()->property(name).value<QObject *>();
}

QString ModalFocusGuardTest::focusedName() const
{
    QQuickItem *focused = m_view->activeFocusItem();
    return focused ? focused->objectName() : QString();
}

void ModalFocusGuardTest::openDialog()
{
    QObject *dialog = sceneObject("dialog");
    QVERIFY(dialog);
    QMetaObject::invokeMethod(dialog, "open");
    QTRY_VERIFY(dialog->property("opened").toBool());
}

void ModalFocusGuardTest::lateFocusStealReturnsToLastFocusedItem()
{
    openDialog();
    sceneItem("second")->forceActiveFocus();
    QCOMPARE(focusedName(), QStringLiteral("second"));

    // A page restoring its focus after the dialog opened.
    sceneItem("page")->forceActiveFocus();
    QTRY_COMPARE(focusedName(), QStringLiteral("second"));
}

void ModalFocusGuardTest::lateFocusStealBeforeAnyFocusUsesFallback()
{
    openDialog();
    sceneItem("page")->forceActiveFocus();
    QTRY_COMPARE(focusedName(), QStringLiteral("first"));
}

void ModalFocusGuardTest::closedDialogLeavesFocusAlone()
{
    openDialog();
    sceneItem("second")->forceActiveFocus();
    QObject *dialog = sceneObject("dialog");
    QMetaObject::invokeMethod(dialog, "close");
    QTRY_VERIFY(!dialog->property("visible").toBool());

    sceneItem("page")->forceActiveFocus();
    QTest::qWait(50);
    QCOMPARE(focusedName(), QStringLiteral("page"));
}

void ModalFocusGuardTest::popupAboveTheDialogKeepsFocus()
{
    openDialog();
    sceneItem("second")->forceActiveFocus();

    QObject *topPopup = sceneObject("topPopup");
    QMetaObject::invokeMethod(topPopup, "open");
    QTRY_VERIFY(topPopup->property("opened").toBool());
    sceneItem("topButton")->forceActiveFocus();
    QTest::qWait(50);
    QCOMPARE(focusedName(), QStringLiteral("topButton"));
}

QTEST_MAIN(ModalFocusGuardTest)
#include "ModalFocusGuardTest.moc"
