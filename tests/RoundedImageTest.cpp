#include <QGuiApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

namespace {

constexpr int kReadyStatus = 1;

void processFrames(QQuickWindow &window, int count = 4)
{
    for (int frame = 0; frame < count; ++frame) {
        window.requestUpdate();
        QCoreApplication::processEvents();
        QTest::qWait(16);
    }
}

bool nearColor(const QColor &actual, const QColor &expected, int tolerance = 8)
{
    return qAbs(actual.red() - expected.red()) <= tolerance
        && qAbs(actual.green() - expected.green()) <= tolerance
        && qAbs(actual.blue() - expected.blue()) <= tolerance;
}

} // namespace

class RoundedImageTest : public QObject
{
    Q_OBJECT

private slots:
    void modesUseOneBaseLoadAndLazyPrerender();
};

void RoundedImageTest::modesUseOneBaseLoadAndLazyPrerender()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const QColor blue(QStringLiteral("#3daee9"));
    const QColor background(QStringLiteral("#d02090"));
    QImage base(64, 64, QImage::Format_ARGB32_Premultiplied);
    base.fill(blue);
    const QString basePath = directory.filePath(QStringLiteral("base.png"));
    QVERIFY(base.save(basePath));
    QImage changedBase(64, 64, QImage::Format_ARGB32_Premultiplied);
    changedBase.fill(QColor(QStringLiteral("#27ae60")));
    const QString changedBasePath =
        directory.filePath(QStringLiteral("base-changed.png"));
    QVERIFY(changedBase.save(changedBasePath));

    QImage rounded(64, 64, QImage::Format_ARGB32_Premultiplied);
    rounded.fill(Qt::transparent);
    {
        QPainter painter(&rounded);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setBrush(blue);
        painter.setPen(Qt::NoPen);
        painter.drawRoundedRect(rounded.rect(), 18, 18);
    }
    const QString roundedPath = directory.filePath(QStringLiteral("rounded.png"));
    QVERIFY(rounded.save(roundedPath));

    QFile qmlFile(QStringLiteral(BLOOM_ROUNDED_IMAGE_QML));
    QVERIFY(qmlFile.open(QIODevice::ReadOnly));
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(qmlFile.readAll(), QUrl(QStringLiteral("inmemory:/RoundedImage.qml")));
    QTRY_VERIFY_WITH_TIMEOUT(component.status() != QQmlComponent::Loading, 1000);
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> owner(component.create());
    QVERIFY2(owner, qPrintable(component.errorString()));
    auto *item = qobject_cast<QQuickItem *>(owner.get());
    QVERIFY(item);
    item->setX(8);
    item->setY(8);
    item->setWidth(64);
    item->setHeight(64);
    item->setProperty("sourceWidth", 64);
    item->setProperty("sourceHeight", 64);
    item->setProperty("radius", 18);
    item->setProperty("asynchronous", false);
    item->setProperty("source", QUrl::fromLocalFile(basePath));
    item->setProperty("preRoundedSource", QUrl::fromLocalFile(roundedPath));

    QQuickWindow window;
    window.setColor(background);
    window.resize(80, 80);
    item->setParentItem(window.contentItem());
    window.show();

    QObject *baseImage = item->findChild<QObject *>(QStringLiteral("baseImage"));
    QObject *loader = item->findChild<QObject *>(QStringLiteral("preRoundedLoader"));
    QObject *shaderSource = item->findChild<QObject *>(QStringLiteral("shaderSource"));
    QObject *fallback = item->findChild<QObject *>(QStringLiteral("fallbackClip"));
    QVERIFY(baseImage);
    QVERIFY(loader);
    QVERIFY(shaderSource);
    QVERIFY(fallback);
    QCOMPARE(item->findChildren<QObject *>(QStringLiteral("baseImage")).size(), 1);
    QCOMPARE(shaderSource->property("live").toBool(), false);

    item->setProperty("mode", QStringLiteral("shader"));
    QTRY_COMPARE(baseImage->property("status").toInt(), kReadyStatus);
    QCOMPARE(loader->property("active").toBool(), false);
    QVERIFY(!loader->property("item").value<QObject *>());
    QCOMPARE(fallback->property("visible").toBool(), true);
    processFrames(window);
    QImage fallbackCapture = window.grabWindow();
    QVERIFY(!fallbackCapture.isNull());
    QVERIFY2(nearColor(fallbackCapture.pixelColor(40, 40), blue),
             qPrintable(QStringLiteral("center=%1")
                            .arg(fallbackCapture.pixelColor(40, 40).name(QColor::HexArgb))));

    const bool shaderCapable = item->property("shaderSupported").toBool();
    if (shaderCapable) {
        QTRY_VERIFY(item->property("shaderActive").toBool());
        QVERIFY2(nearColor(fallbackCapture.pixelColor(8, 8),
                           fallbackCapture.pixelColor(0, 0)),
                 qPrintable(QStringLiteral("corner=%1")
                                .arg(fallbackCapture.pixelColor(8, 8).name(QColor::HexArgb))));
        QTRY_VERIFY(item->property("shaderRefreshCount").toInt() > 0);
        const int refreshesBeforeIdle = item->property("shaderRefreshCount").toInt();
        processFrames(window, 60);
        QCOMPARE(item->property("shaderRefreshCount").toInt(), refreshesBeforeIdle);
        QElapsedTimer updateTimer;
        updateTimer.start();
        item->setProperty("source", QUrl::fromLocalFile(changedBasePath));
        QTRY_COMPARE(baseImage->property("status").toInt(), kReadyStatus);
        processFrames(window);
        const QImage changedCapture = window.grabWindow();
        QVERIFY(nearColor(changedCapture.pixelColor(40, 40),
                          QColor(QStringLiteral("#27ae60"))));
        QVERIFY(item->property("shaderRefreshCount").toInt() > refreshesBeforeIdle);
        qInfo() << "RoundedImage static shader: 60 idle frames scheduled 0 refreshes;"
                << "source replacement rendered in" << updateTimer.elapsed() << "ms";
    }

    item->setProperty("mode", QStringLiteral("prerender"));
    QTRY_VERIFY(loader->property("active").toBool());
    QTRY_VERIFY(loader->property("item").value<QObject *>());
    QTRY_VERIFY(item->property("usePreRounded").toBool());
    processFrames(window);
    QImage prerenderCapture = window.grabWindow();
    QVERIFY(!prerenderCapture.isNull());
    QVERIFY(nearColor(prerenderCapture.pixelColor(40, 40), blue));
    QVERIFY(nearColor(prerenderCapture.pixelColor(8, 8),
                      prerenderCapture.pixelColor(0, 0)));

    QQmlComponent overlayComponent(&engine);
    overlayComponent.setData(R"(
        import QtQuick
        Rectangle {
            width: 64
            height: 64
            color: "transparent"
            z: 10
            property bool isHovered: false
            property bool isFocused: false
            border.width: isFocused ? 4 : (isHovered ? 2 : 0)
            border.color: isFocused ? "white" : "#ffcc00"
        }
    )", QUrl(QStringLiteral("inmemory:/RoundedOverlay.qml")));
    QVERIFY2(overlayComponent.isReady(), qPrintable(overlayComponent.errorString()));
    std::unique_ptr<QObject> overlayOwner(overlayComponent.create());
    QVERIFY2(overlayOwner, qPrintable(overlayComponent.errorString()));
    auto *overlay = qobject_cast<QQuickItem *>(overlayOwner.get());
    QVERIFY(overlay);
    overlay->setParentItem(item);

    overlay->setProperty("isHovered", true);
    processFrames(window);
    const QImage hoverCapture = window.grabWindow();
    QVERIFY(nearColor(hoverCapture.pixelColor(9, 40), QColor(QStringLiteral("#ffcc00"))));
    overlay->setProperty("isFocused", true);
    processFrames(window);
    const QImage focusCapture = window.grabWindow();
    QVERIFY(nearColor(focusCapture.pixelColor(9, 40), Qt::white));

    item->setProperty("mode", QStringLiteral("auto"));
    QTRY_VERIFY(item->property("usePreRounded").toBool());
    item->setProperty("preRoundedSource", QString());
    QTRY_VERIFY(!loader->property("active").toBool());
    QTRY_VERIFY(!loader->property("item").value<QObject *>());
    QTRY_COMPARE(baseImage->property("status").toInt(), kReadyStatus);
    processFrames(window);
    const QImage restoredCapture = window.grabWindow();
    QVERIFY(nearColor(restoredCapture.pixelColor(40, 40),
                      shaderCapable ? QColor(QStringLiteral("#27ae60")) : blue));
}

int main(int argc, char **argv)
{
    if (qEnvironmentVariableIsSet("BLOOM_ROUNDED_TEST_SOFTWARE")) {
        qputenv("QT_QUICK_BACKEND", QByteArrayLiteral("software"));
    }
    QGuiApplication application(argc, argv);
    RoundedImageTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "RoundedImageTest.moc"
