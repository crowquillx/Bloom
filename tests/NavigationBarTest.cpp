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
#include <QtQuickTest/quicktest.h>

#include <memory>

#include "ui/ResponsiveLayoutPolicy.h"

namespace {

// Minimal stand-ins for the context objects NavigationBar.qml and Theme.qml read.

class StubLayout : public QObject
{
    Q_OBJECT
    Q_PROPERTY(qreal layoutScale MEMBER layoutScale NOTIFY changed)
    Q_PROPERTY(int gridColumns MEMBER gridColumns NOTIFY changed)
    Q_PROPERTY(int homeRowVisibleItems MEMBER homeRowVisibleItems NOTIFY changed)
    Q_PROPERTY(QString sidebarDefaultMode MEMBER sidebarDefaultMode NOTIFY changed)
    Q_PROPERTY(qreal aspectRatio MEMBER aspectRatio NOTIFY changed)
    Q_PROPERTY(int viewportWidth MEMBER viewportWidth NOTIFY changed)
    Q_PROPERTY(int viewportHeight MEMBER viewportHeight NOTIFY changed)

public:
    void apply(int width, int height)
    {
        const auto metrics = Bloom::ResponsiveLayoutPolicy::computeLayout(width, height, 1.0);
        layoutScale = metrics.layoutScale;
        gridColumns = metrics.gridColumns;
        homeRowVisibleItems = metrics.homeRowVisibleItems;
        sidebarDefaultMode = metrics.sidebarDefaultMode;
        aspectRatio = metrics.aspectRatio;
        viewportWidth = width;
        viewportHeight = height;
        emit changed();
    }

    qreal layoutScale = 1.0;
    int gridColumns = 7;
    int homeRowVisibleItems = 6;
    QString sidebarDefaultMode = QStringLiteral("rail");
    qreal aspectRatio = 16.0 / 9.0;
    int viewportWidth = 1920;
    int viewportHeight = 1080;

signals:
    void changed();
};

class StubConfig : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString theme MEMBER theme CONSTANT)
    Q_PROPERTY(QString themeFlavor MEMBER themeFlavor CONSTANT)
    Q_PROPERTY(QString themeColorScheme MEMBER themeColorScheme CONSTANT)
    Q_PROPERTY(qreal manualDpiScaleOverride MEMBER manualDpiScaleOverride CONSTANT)
    // Animations off so geometry settles synchronously, as with reduced motion.
    Q_PROPERTY(bool uiAnimationsEnabled MEMBER uiAnimationsEnabled CONSTANT)
    Q_PROPERTY(QString username MEMBER username CONSTANT)

public:
    QString theme = QStringLiteral("Bloom");
    QString themeFlavor = QStringLiteral("dusk");
    QString themeColorScheme = QStringLiteral("lavender");
    qreal manualDpiScaleOverride = 1.0;
    bool uiAnimationsEnabled = false;
    QString username = QStringLiteral("tester");
};

class StubSidebarSettings : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool reduceMotion MEMBER reduceMotion CONSTANT)
    Q_PROPERTY(QStringList libraryOrder MEMBER libraryOrder NOTIFY libraryOrderChanged)

public:
    bool reduceMotion = true;
    QStringList libraryOrder;

signals:
    void libraryOrderChanged();
};

class StubLibraryViewModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList views MEMBER views NOTIFY viewsChanged)

public:
    QVariantList views;

signals:
    void viewsChanged();
};

class StubUpdateService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool updateAvailable MEMBER updateAvailable NOTIFY updateAvailableChanged)

public:
    bool updateAvailable = false;

signals:
    void updateAvailableChanged();
};

class StubInputModeManager : public QObject
{
    Q_OBJECT

public:
    Q_INVOKABLE void setNavigationMode(const QString &mode) { lastMode = mode; }
    Q_INVOKABLE void hideCursor(bool hide) { cursorHidden = hide; }

    QString lastMode;
    bool cursorHidden = false;
};

// Page stand-in: a FocusScope filling the window, like every page under the
// floating bar. Its focused child sends unhandled Up to the bar, as Main.qml
// does for Up at the top edge of a page.
constexpr auto kScene = R"QML(
import QtQuick
import BloomUI

Item {
    id: scene
    width: 1920
    height: 1080

    readonly property int barInsetTop: Theme.navBarInsetTop
    readonly property int barHeight: Theme.navBarHeight
    readonly property int itemHeight: Theme.navBarItemHeight
    readonly property int reservedHeight: Theme.navBarReservedHeight
    readonly property int pageMarginTop: Theme.pageMarginTop
    readonly property real layoutScale: Theme.layoutScale
    property string lastNavigation: ""
    property int spacePickerRequests: 0

    FocusScope {
        id: content
        objectName: "content"
        anchors.fill: parent
        focus: true

        property int restoreCount: 0
        function restoreFocusFromNavigation() {
            restoreCount++
            contentTarget.forceActiveFocus()
        }

        Item {
            id: contentTarget
            objectName: "contentTarget"
            anchors.fill: parent
            focus: true
            Keys.onUpPressed: bar.focusBar()
        }
    }

    NavigationBar {
        id: bar
        objectName: "bar"
        anchors.fill: parent
        mainContent: content
        onNavigationRequested: function(navigationId) { scene.lastNavigation = navigationId }
        onLibraryRequested: function(libraryId, libraryName) { scene.lastNavigation = "library:" + libraryId }
        onSpacePickerRequested: scene.spacePickerRequests++
    }
}
)QML";

} // namespace

class NavigationBarTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void barFloatsOverFullWidthContent();
    void upFromPageLandsOnCurrentEntry();
    void leftRightVisitEveryDestinationInOrder();
    void currentPageStaysDistinctFromFocus();
    void downAndEscapeRestorePageFocus();
    void activationNavigatesAndReturnsToCurrentEntry();
    void shiftEnterReordersLibraries();
    void recessedBarReturnsWhenFocused();
    void barMetricsFollowLayoutScale_data();
    void barMetricsFollowLayoutScale();

private:
    QQuickItem *item(const QString &name) const;
    QString focusedName() const;
    void press(Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier);
    void focusBarFromPage();
    QRectF sceneBounds(const QString &name) const;

    QTemporaryDir m_moduleRoot;
    StubLayout m_layout;
    StubConfig m_config;
    StubSidebarSettings m_sidebarSettings;
    StubLibraryViewModel m_libraryViewModel;
    StubUpdateService m_updateService;
    StubInputModeManager m_inputMode;
    std::unique_ptr<QQuickView> m_view;
};

void NavigationBarTest::initTestCase()
{
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QVERIFY(m_moduleRoot.isValid());

    // Stage the subset of BloomUI the bar needs as a local QML module.
    QDir root(m_moduleRoot.path());
    QVERIFY(root.mkdir(QStringLiteral("BloomUI")));
    const QDir module(root.filePath(QStringLiteral("BloomUI")));
    const QDir sourceDir(QStringLiteral(BLOOM_UI_SOURCE_DIR));
    for (const auto &file : {QStringLiteral("Theme.qml"), QStringLiteral("Icons.qml"),
                             QStringLiteral("FocusRing.qml"), QStringLiteral("NavigationBar.qml"),
                             QStringLiteral("SourceRouter.qml")}) {
        QVERIFY2(QFile::copy(sourceDir.filePath(file), module.filePath(file)), qPrintable(file));
    }
    QFile qmldir(module.filePath(QStringLiteral("qmldir")));
    QVERIFY(qmldir.open(QIODevice::WriteOnly | QIODevice::Text));
    qmldir.write("module BloomUI\n"
                 "singleton Theme 1.0 Theme.qml\n"
                 "singleton Icons 1.0 Icons.qml\n"
                 "FocusRing 1.0 FocusRing.qml\n"
                 "NavigationBar 1.0 NavigationBar.qml\n"
                 "SourceRouter 1.0 SourceRouter.qml\n");
    qmldir.close();

    QFile scene(root.filePath(QStringLiteral("Scene.qml")));
    QVERIFY(scene.open(QIODevice::WriteOnly | QIODevice::Text));
    scene.write(kScene);
    scene.close();

    m_libraryViewModel.views = {
        QVariantMap{{QStringLiteral("itemId"), QStringLiteral("lib-movies")},
                    {QStringLiteral("name"), QStringLiteral("Movies")},
                    {QStringLiteral("collectionType"), QStringLiteral("movies")}},
        QVariantMap{{QStringLiteral("itemId"), QStringLiteral("lib-shows")},
                    {QStringLiteral("name"), QStringLiteral("Shows")},
                    {QStringLiteral("collectionType"), QStringLiteral("tvshows")}},
    };
}

void NavigationBarTest::init()
{
    m_sidebarSettings.libraryOrder.clear();
    m_layout.apply(1920, 1080);

    m_view = std::make_unique<QQuickView>();
    QQmlEngine *engine = m_view->engine();
    engine->addImportPath(m_moduleRoot.path());
    QQmlContext *context = engine->rootContext();
    context->setContextProperty(QStringLiteral("ResponsiveLayoutManager"), &m_layout);
    context->setContextProperty(QStringLiteral("ConfigManager"), &m_config);
    context->setContextProperty(QStringLiteral("SidebarSettings"), &m_sidebarSettings);
    context->setContextProperty(QStringLiteral("LibraryViewModel"), &m_libraryViewModel);
    context->setContextProperty(QStringLiteral("UpdateService"), &m_updateService);
    context->setContextProperty(QStringLiteral("InputModeManager"), &m_inputMode);

    m_view->setSource(QUrl::fromLocalFile(QDir(m_moduleRoot.path()).filePath(QStringLiteral("Scene.qml"))));
    QVERIFY2(m_view->status() == QQuickView::Ready,
             qPrintable(m_view->errors().isEmpty() ? QString() : m_view->errors().first().toString()));
    m_view->show();
    m_view->requestActivate();
    QVERIFY(QTest::qWaitForWindowActive(m_view.get()));
    QTRY_COMPARE(focusedName(), QStringLiteral("contentTarget"));
}

void NavigationBarTest::cleanup()
{
    m_view.reset();
}

QQuickItem *NavigationBarTest::item(const QString &name) const
{
    // Repeater delegates have no QObject parent, so walk the visual tree.
    QList<QQuickItem *> pending{m_view->rootObject()};
    while (!pending.isEmpty()) {
        QQuickItem *candidate = pending.takeFirst();
        if (candidate->objectName() == name)
            return candidate;
        pending.append(candidate->childItems());
    }
    return nullptr;
}

QString NavigationBarTest::focusedName() const
{
    QQuickItem *focused = m_view->activeFocusItem();
    return focused ? focused->objectName() : QString();
}

void NavigationBarTest::press(Qt::Key key, Qt::KeyboardModifiers modifiers)
{
    QTest::keyClick(m_view.get(), key, modifiers);
}

void NavigationBarTest::focusBarFromPage()
{
    press(Qt::Key_Up);
    QVERIFY(item(QStringLiteral("bar"))->hasActiveFocus());
}

QRectF NavigationBarTest::sceneBounds(const QString &name) const
{
    QQuickItem *entry = item(name);
    if (!entry)
        return {};
    return entry->mapRectToScene(QRectF(0, 0, entry->width(), entry->height()));
}

void NavigationBarTest::barFloatsOverFullWidthContent()
{
    QQuickItem *scene = m_view->rootObject();
    QQuickItem *content = item(QStringLiteral("content"));
    QQuickItem *bar = item(QStringLiteral("navigationBar"));
    QVERIFY(content && bar);

    // Pages fill the window; the bar reserves no width.
    QCOMPARE(content->x(), 0.0);
    QCOMPARE(content->width(), scene->width());

    QCOMPARE(qRound(bar->y()), scene->property("barInsetTop").toInt());
    QCOMPARE(qRound(bar->height()), scene->property("barHeight").toInt());
    // Pages without a hero start below the bar.
    QCOMPARE(scene->property("pageMarginTop").toInt(), scene->property("reservedHeight").toInt());
    QVERIFY(scene->property("reservedHeight").toInt()
            > scene->property("barInsetTop").toInt() + scene->property("barHeight").toInt());
}

void NavigationBarTest::upFromPageLandsOnCurrentEntry()
{
    focusBarFromPage();
    QCOMPARE(focusedName(), QStringLiteral("navEntry_home"));
    QCOMPARE(item(QStringLiteral("bar"))->property("expanded").toBool(), true);
}

void NavigationBarTest::leftRightVisitEveryDestinationInOrder()
{
    focusBarFromPage();
    QCOMPARE(focusedName(), QStringLiteral("navEntry_home"));

    const QStringList expected{
        QStringLiteral("navLibrary_0"),
        QStringLiteral("navLibrary_1"),
        QStringLiteral("navEntry_search"),
        QStringLiteral("navEntry_settings"),
        QStringLiteral("navEntry_account"),
        QStringLiteral("navEntry_exit"),
    };
    m_inputMode.lastMode.clear();
    for (const QString &name : expected) {
        press(Qt::Key_Right);
        QCOMPARE(focusedName(), name);
    }
    QCOMPARE(m_inputMode.lastMode, QStringLiteral("keyboard"));
    QVERIFY(m_inputMode.cursorHidden);

    // Clamped at the ends rather than wrapping or leaking focus.
    press(Qt::Key_Right);
    QCOMPARE(focusedName(), QStringLiteral("navEntry_exit"));

    for (qsizetype i = expected.size() - 2; i >= 0; --i) {
        press(Qt::Key_Left);
        QCOMPARE(focusedName(), expected.at(i));
    }
    press(Qt::Key_Left);
    QCOMPARE(focusedName(), QStringLiteral("navEntry_home"));
    press(Qt::Key_Left);
    QCOMPARE(focusedName(), QStringLiteral("navEntry_home"));

    // Up inside the bar is consumed so focus never escapes off-screen.
    press(Qt::Key_Up);
    QCOMPARE(focusedName(), QStringLiteral("navEntry_home"));
}

void NavigationBarTest::currentPageStaysDistinctFromFocus()
{
    focusBarFromPage();
    press(Qt::Key_Right);

    QQuickItem *home = item(QStringLiteral("navEntry_home"));
    QQuickItem *movies = item(QStringLiteral("navLibrary_0"));
    QCOMPARE(home->property("selected").toBool(), true);
    QCOMPARE(home->hasActiveFocus(), false);
    QCOMPARE(movies->property("selected").toBool(), false);
    QCOMPARE(movies->hasActiveFocus(), true);
}

void NavigationBarTest::downAndEscapeRestorePageFocus()
{
    QQuickItem *bar = item(QStringLiteral("bar"));
    QQuickItem *content = item(QStringLiteral("content"));

    focusBarFromPage();
    press(Qt::Key_Right);
    press(Qt::Key_Down);
    QCOMPARE(focusedName(), QStringLiteral("contentTarget"));
    QCOMPARE(bar->property("expanded").toBool(), false);
    QCOMPARE(content->property("restoreCount").toInt(), 1);

    focusBarFromPage();
    press(Qt::Key_Escape);
    QTRY_COMPARE(focusedName(), QStringLiteral("contentTarget"));
    QCOMPARE(bar->property("expanded").toBool(), false);
    QCOMPARE(content->property("restoreCount").toInt(), 2);
}

void NavigationBarTest::activationNavigatesAndReturnsToCurrentEntry()
{
    QQuickItem *scene = m_view->rootObject();
    QQuickItem *bar = item(QStringLiteral("bar"));

    focusBarFromPage();
    press(Qt::Key_Right);
    press(Qt::Key_Right);
    press(Qt::Key_Right);
    QCOMPARE(focusedName(), QStringLiteral("navEntry_search"));
    press(Qt::Key_Return);
    QCOMPARE(scene->property("lastNavigation").toString(), QStringLiteral("search"));
    QCOMPARE(bar->property("currentNavigation").toString(), QStringLiteral("search"));
    QTRY_COMPARE(focusedName(), QStringLiteral("contentTarget"));

    // Re-entering lands on the current page's entry, not the first tab.
    focusBarFromPage();
    QCOMPARE(focusedName(), QStringLiteral("navEntry_search"));

    press(Qt::Key_Left);
    press(Qt::Key_Return);
    QCOMPARE(scene->property("lastNavigation").toString(), QStringLiteral("library:lib-shows"));
    QTRY_COMPARE(focusedName(), QStringLiteral("contentTarget"));
    focusBarFromPage();
    QCOMPARE(focusedName(), QStringLiteral("navLibrary_1"));
    QCOMPARE(item(QStringLiteral("navLibrary_1"))->property("selected").toBool(), true);
    QCOMPARE(item(QStringLiteral("navEntry_search"))->property("selected").toBool(), false);

    // Account access stays in the bar: the avatar opens the space picker,
    // which also offers sign-out.
    press(Qt::Key_Right);
    press(Qt::Key_Right);
    press(Qt::Key_Right);
    QCOMPARE(focusedName(), QStringLiteral("navEntry_account"));
    press(Qt::Key_Return);
    QCOMPARE(scene->property("spacePickerRequests").toInt(), 1);

    // A cancelled confirmation dialog hands focus back to the entry that
    // opened it, not to the current page's entry.
    item(QStringLiteral("contentTarget"))->forceActiveFocus();
    QVERIFY(QMetaObject::invokeMethod(bar, "focusDialogOrigin"));
    QCOMPARE(focusedName(), QStringLiteral("navEntry_account"));
}

void NavigationBarTest::shiftEnterReordersLibraries()
{
    QQuickItem *bar = item(QStringLiteral("bar"));

    focusBarFromPage();
    press(Qt::Key_Right);
    QCOMPARE(focusedName(), QStringLiteral("navLibrary_0"));

    press(Qt::Key_Return, Qt::ShiftModifier);
    QCOMPARE(bar->property("reorderingIndex").toInt(), 0);

    press(Qt::Key_Right);
    QCOMPARE(m_sidebarSettings.libraryOrder,
             QStringList({QStringLiteral("lib-shows"), QStringLiteral("lib-movies")}));
    QTRY_COMPARE(focusedName(), QStringLiteral("navLibrary_1"));
    QCOMPARE(bar->property("reorderingIndex").toInt(), 1);

    // Enter finishes reordering without navigating.
    press(Qt::Key_Return);
    QCOMPARE(bar->property("reorderingIndex").toInt(), -1);
    QCOMPARE(m_view->rootObject()->property("lastNavigation").toString(), QString());
    QCOMPARE(focusedName(), QStringLiteral("navLibrary_1"));
}

void NavigationBarTest::recessedBarReturnsWhenFocused()
{
    QQuickItem *bar = item(QStringLiteral("bar"));
    QQuickItem *panel = item(QStringLiteral("navigationBar"));

    QVERIFY(bar->setProperty("recessed", true));
    QCOMPARE(bar->property("shown").toBool(), false);
    QVERIFY(panel->y() + panel->height() <= 0);
    QCOMPARE(focusedName(), QStringLiteral("contentTarget"));

    // Up from the page brings the receded bar back with focus on it.
    focusBarFromPage();
    QCOMPARE(bar->property("shown").toBool(), true);
    QCOMPARE(qRound(panel->y()), m_view->rootObject()->property("barInsetTop").toInt());

    press(Qt::Key_Down);
    QCOMPARE(focusedName(), QStringLiteral("contentTarget"));
    QCOMPARE(bar->property("shown").toBool(), false);
}

void NavigationBarTest::barMetricsFollowLayoutScale_data()
{
    QTest::addColumn<int>("width");
    QTest::addColumn<int>("height");

    QTest::newRow("720p") << 1280 << 720;
    QTest::newRow("1080p") << 1920 << 1080;
    QTest::newRow("4k") << 3840 << 2160;
    QTest::newRow("ultrawide") << 3440 << 1440;
    QTest::newRow("4:3") << 1024 << 768;
    QTest::newRow("narrow") << 800 << 900;
}

void NavigationBarTest::barMetricsFollowLayoutScale()
{
    QFETCH(int, width);
    QFETCH(int, height);

    m_view->resize(width, height);
    m_view->rootObject()->setSize(QSizeF(width, height));
    m_layout.apply(width, height);
    QVERIFY(QQuickTest::qWaitForPolish(m_view.get()));

    QQuickItem *scene = m_view->rootObject();
    const qreal scale = scene->property("layoutScale").toReal();
    QVERIFY(scale > 0);

    // Bar tokens are pure design units: no breakpoints or pixel clamps.
    QCOMPARE(scene->property("barHeight").toInt(), qRound(76 * scale));
    QCOMPARE(scene->property("itemHeight").toInt(), qRound(64 * scale));
    QCOMPARE(scene->property("barInsetTop").toInt(), qRound(34 * scale));

    // The reserved band stays a small fraction of the window at every shape.
    QVERIFY(scene->property("reservedHeight").toReal() <= height * 0.16);

    // The utility cluster and the first tab are always on-screen.
    for (const auto &name : {QStringLiteral("navEntry_home"), QStringLiteral("navEntry_settings"),
                             QStringLiteral("navEntry_account"), QStringLiteral("navEntry_exit")}) {
        const QRectF bounds = sceneBounds(name);
        QVERIFY2(bounds.left() >= 0 && bounds.right() <= width && bounds.top() >= 0 && bounds.bottom() <= height,
                 qPrintable(QStringLiteral("%1 outside window: %2,%3 %4x%5 of %6x%7")
                                .arg(name).arg(bounds.x()).arg(bounds.y())
                                .arg(bounds.width()).arg(bounds.height()).arg(width).arg(height)));
    }

    // Tabs that overflow scroll into view when focused.
    focusBarFromPage();
    press(Qt::Key_Right);
    press(Qt::Key_Right);
    press(Qt::Key_Right);
    QCOMPARE(focusedName(), QStringLiteral("navEntry_search"));
    QTRY_VERIFY2(sceneBounds(QStringLiteral("navEntry_search")).right()
                     <= sceneBounds(QStringLiteral("navEntry_settings")).left(),
                 "focused tab hidden under the utility cluster");
    QVERIFY(sceneBounds(QStringLiteral("navEntry_search")).left() >= 0);
}

QTEST_MAIN(NavigationBarTest)
#include "NavigationBarTest.moc"
