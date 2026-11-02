#include <QtTest/QtTest>

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickView>
#include <QTemporaryDir>

#include <memory>

namespace {

// Stand-in for UserRatingState: records rate() and holds the write pending
// until the test finishes it.
class StubRatingState : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString itemId MEMBER itemId CONSTANT)
    Q_PROPERTY(QString connectionId MEMBER connectionId CONSTANT)
    Q_PROPERTY(QString scale MEMBER scale CONSTANT)
    Q_PROPERTY(bool supported MEMBER supported CONSTANT)
    Q_PROPERTY(int value MEMBER value NOTIFY valueChanged)
    Q_PROPERTY(bool pending MEMBER pending NOTIFY pendingChanged)

public:
    Q_INVOKABLE void rate(int choice)
    {
        rates.append(choice);
        value = choice == value ? 0 : choice;
        pending = true;
        emit valueChanged();
        emit pendingChanged();
    }

    void finish()
    {
        pending = false;
        emit pendingChanged();
    }

    QString itemId = QStringLiteral("movie-1");
    QString connectionId;
    QString scale = QStringLiteral("stars");
    bool supported = true;
    int value = 0;
    bool pending = false;
    QList<int> rates;

signals:
    void valueChanged();
    void pendingChanged();
};

constexpr auto kScene = R"QML(
import QtQuick
import BloomUI

Item {
    width: 800
    height: 600

    property alias control: control

    UserRatingControl {
        id: control
        objectName: "control"
        ratingState: RatingState
        focus: true
    }
}
)QML";

} // namespace

class UserRatingControlTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void ratingKeepsFocusWhilePending();

private:
    QString focusedLabel() const;

    QTemporaryDir m_moduleRoot;
    StubRatingState m_state;
    std::unique_ptr<QQuickView> m_view;
};

void UserRatingControlTest::initTestCase()
{
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QVERIFY(m_moduleRoot.isValid());

    QDir root(m_moduleRoot.path());
    QVERIFY(root.mkdir(QStringLiteral("BloomUI")));
    const QDir module(root.filePath(QStringLiteral("BloomUI")));
    const QDir sourceDir(QStringLiteral(BLOOM_UI_SOURCE_DIR));
    for (const auto &file : {QStringLiteral("Theme.qml"), QStringLiteral("Icons.qml"),
                             QStringLiteral("FocusRing.qml"), QStringLiteral("ActionButton.qml"),
                             QStringLiteral("SourceRouter.qml"),
                             QStringLiteral("UserRatingControl.qml")}) {
        QVERIFY2(QFile::copy(sourceDir.filePath(file), module.filePath(file)), qPrintable(file));
    }
    QFile qmldir(module.filePath(QStringLiteral("qmldir")));
    QVERIFY(qmldir.open(QIODevice::WriteOnly | QIODevice::Text));
    qmldir.write("module BloomUI\n"
                 "singleton Theme 1.0 Theme.qml\n"
                 "singleton Icons 1.0 Icons.qml\n"
                 "FocusRing 1.0 FocusRing.qml\n"
                 "ActionButton 1.0 ActionButton.qml\n"
                 "SourceRouter 1.0 SourceRouter.qml\n"
                 "UserRatingControl 1.0 UserRatingControl.qml\n");
    qmldir.close();

    QFile scene(root.filePath(QStringLiteral("Scene.qml")));
    QVERIFY(scene.open(QIODevice::WriteOnly | QIODevice::Text));
    scene.write(kScene);
    scene.close();
}

void UserRatingControlTest::init()
{
    m_state.value = 0;
    m_state.pending = false;
    m_state.rates.clear();

    m_view = std::make_unique<QQuickView>();
    m_view->engine()->addImportPath(m_moduleRoot.path());
    m_view->engine()->rootContext()->setContextProperty(QStringLiteral("RatingState"), &m_state);
    m_view->setSource(QUrl::fromLocalFile(QDir(m_moduleRoot.path()).filePath(QStringLiteral("Scene.qml"))));
    QVERIFY2(m_view->status() == QQuickView::Ready,
             qPrintable(m_view->errors().isEmpty() ? QString() : m_view->errors().first().toString()));
    m_view->show();
    m_view->requestActivate();
    QVERIFY(QTest::qWaitForWindowActive(m_view.get()));
    QTRY_COMPARE(focusedLabel(), QStringLiteral("Rate 1 star"));
}

void UserRatingControlTest::cleanup()
{
    m_view.reset();
}

QString UserRatingControlTest::focusedLabel() const
{
    QQuickItem *item = m_view ? m_view->activeFocusItem() : nullptr;
    return item ? item->property("accessibleLabel").toString() : QString();
}

void UserRatingControlTest::ratingKeepsFocusWhilePending()
{
    QTest::keyClick(m_view.get(), Qt::Key_Right);
    QTest::keyClick(m_view.get(), Qt::Key_Right);
    QCOMPARE(focusedLabel(), QStringLiteral("Rate 3 stars"));
    QQuickItem *rated = m_view->activeFocusItem();

    // The press rates at once; the button keeps focus and stays enabled
    // while the write is in flight.
    QTest::keyClick(m_view.get(), Qt::Key_Return);
    QCOMPARE(m_state.rates, QList<int>{3});
    QVERIFY(m_state.pending);
    QCoreApplication::processEvents();
    QCOMPARE(m_view->activeFocusItem(), rated);
    QVERIFY(rated->isEnabled());

    // Presses while pending are ignored, but focus still moves.
    QTest::keyClick(m_view.get(), Qt::Key_Return);
    QCOMPARE(m_state.rates, QList<int>{3});
    QTest::keyClick(m_view.get(), Qt::Key_Right);
    QCOMPARE(focusedLabel(), QStringLiteral("Rate 4 stars"));

    // The answer leaves focus where it is.
    m_state.finish();
    QCoreApplication::processEvents();
    QCOMPARE(focusedLabel(), QStringLiteral("Rate 4 stars"));
    QTest::keyClick(m_view.get(), Qt::Key_Left);
    QTest::keyClick(m_view.get(), Qt::Key_Return);
    QCOMPARE(m_state.rates, (QList<int>{3, 3}));
    QCOMPARE(m_state.value, 0);
}

QTEST_MAIN(UserRatingControlTest)
#include "UserRatingControlTest.moc"
