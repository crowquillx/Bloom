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

// Minimal stand-ins for the context objects Sidebar.qml and Theme.qml read.

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

// Page stand-in: a FocusScope at the rail's reserved offset whose focused
// child sends Left to the rail, like the real screens do at their left edge.
constexpr auto kScene = R"QML(
import QtQuick
import BloomUI

Item {
    id: scene
    width: 1920
    height: 1080

    readonly property int reservedWidth: Theme.navRailReservedWidth
    readonly property int collapsedWidth: Theme.navRailCollapsedWidth
    readonly property int expandedWidth: Theme.navRailExpandedWidth
    readonly property int railInset: Theme.navRailInset
    readonly property int itemHeight: Theme.navRailItemHeight
    readonly property real layoutScale: Theme.layoutScale
    property string lastNavigation: ""
    property int spacePickerRequests: 0

    FocusScope {
        id: content
        objectName: "content"
        x: rail.sidebarWidth
        width: parent.width - x
        height: parent.height
        focus: true

        property int restoreCount: 0
        function restoreFocusFromSidebar() {
            restoreCount++
            contentTarget.forceActiveFocus()
        }

        Item {
            id: contentTarget
            objectName: "contentTarget"
            anchors.fill: parent
            focus: true
            Keys.onLeftPressed: rail.focusRail()
        }
    }

    Sidebar {
        id: rail
        objectName: "rail"
        anchors.fill: parent
        mainContent: content
        onNavigationRequested: function(navigationId) { scene.lastNavigation = navigationId }
        onLibraryRequested: function(libraryId, libraryName) { scene.lastNavigation = "library:" + libraryId }
        onSpacePickerRequested: scene.spacePickerRequests++
    }
}
)QML";

} // namespace

class NavigationRailTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void collapsedRailReservesItsFootprint();
    void keyboardFocusExpandsWithoutMovingContent();
    void upDownVisitEveryDestinationInOrder();
    void currentPageStaysDistinctFromFocus();
    void rightAndEscapeRestorePageFocus();
    void activationNavigatesAndReturnsToCurrentEntry();
    void shiftEnterReordersLibraries();
    void pointerPinExpandsWithoutTakingFocus();
    void railMetricsFollowLayoutScale_data();
    void railMetricsFollowLayoutScale();

private:
    QQuickItem *item(const QString &name) const;
    QString focusedName() const;
    void press(Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier);
    void focusRailFromPage();

    QTemporaryDir m_moduleRoot;
    StubLayout m_layout;
    StubConfig m_config;
    StubSidebarSettings m_sidebarSettings;
    StubLibraryViewModel m_libraryViewModel;
    StubUpdateService m_updateService;
    StubInputModeManager m_inputMode;
    std::unique_ptr<QQuickView> m_view;
};

void NavigationRailTest::initTestCase()
{
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QVERIFY(m_moduleRoot.isValid());

    // Stage the subset of BloomUI the rail needs as a local QML module.
    QDir root(m_moduleRoot.path());
    QVERIFY(root.mkdir(QStringLiteral("BloomUI")));
    const QDir module(root.filePath(QStringLiteral("BloomUI")));
    const QDir sourceDir(QStringLiteral(BLOOM_UI_SOURCE_DIR));
    for (const auto &file : {QStringLiteral("Theme.qml"), QStringLiteral("Icons.qml"),
                             QStringLiteral("FocusRing.qml"), QStringLiteral("Sidebar.qml"),
                             QStringLiteral("SourceRouter.qml")}) {
        QVERIFY2(QFile::copy(sourceDir.filePath(file), module.filePath(file)), qPrintable(file));
    }
    QFile qmldir(module.filePath(QStringLiteral("qmldir")));
    QVERIFY(qmldir.open(QIODevice::WriteOnly | QIODevice::Text));
    qmldir.write("module BloomUI\n"
                 "singleton Theme 1.0 Theme.qml\n"
                 "singleton Icons 1.0 Icons.qml\n"
                 "FocusRing 1.0 FocusRing.qml\n"
                 "Sidebar 1.0 Sidebar.qml\n"
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

void NavigationRailTest::init()
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

void NavigationRailTest::cleanup()
{
    m_view.reset();
}

QQuickItem *NavigationRailTest::item(const QString &name) const
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

QString NavigationRailTest::focusedName() const
{
    QQuickItem *focused = m_view->activeFocusItem();
    return focused ? focused->objectName() : QString();
}

void NavigationRailTest::press(Qt::Key key, Qt::KeyboardModifiers modifiers)
{
    QTest::keyClick(m_view.get(), key, modifiers);
}

void NavigationRailTest::focusRailFromPage()
{
    press(Qt::Key_Left);
    QVERIFY(item(QStringLiteral("rail"))->hasActiveFocus());
}

void NavigationRailTest::collapsedRailReservesItsFootprint()
{
    QQuickItem *scene = m_view->rootObject();
    QQuickItem *rail = item(QStringLiteral("rail"));
    QQuickItem *panel = item(QStringLiteral("railPanel"));
    QVERIFY(rail && panel);

    QCOMPARE(rail->property("expanded").toBool(), false);
    QCOMPARE(qRound(panel->width()), scene->property("collapsedWidth").toInt());
    QCOMPARE(qRound(panel->x()), scene->property("railInset").toInt());
    QCOMPARE(qRound(item(QStringLiteral("content"))->x()), scene->property("reservedWidth").toInt());
    QCOMPARE(scene->property("reservedWidth").toInt(),
             scene->property("railInset").toInt() + scene->property("collapsedWidth").toInt());
}

void NavigationRailTest::keyboardFocusExpandsWithoutMovingContent()
{
    QQuickItem *scene = m_view->rootObject();
    QQuickItem *content = item(QStringLiteral("content"));
    const QRectF contentBefore(content->position(), content->size());

    focusRailFromPage();

    QCOMPARE(item(QStringLiteral("rail"))->property("expanded").toBool(), true);
    QCOMPARE(qRound(item(QStringLiteral("railPanel"))->width()), scene->property("expandedWidth").toInt());
    QCOMPARE(QRectF(content->position(), content->size()), contentBefore);
}

void NavigationRailTest::upDownVisitEveryDestinationInOrder()
{
    focusRailFromPage();
    QCOMPARE(focusedName(), QStringLiteral("railEntry_home"));

    const QStringList expected{
        QStringLiteral("railEntry_search"),
        QStringLiteral("railLibrary_0"),
        QStringLiteral("railLibrary_1"),
        QStringLiteral("railEntry_settings"),
        QStringLiteral("railEntry_account"),
        QStringLiteral("railEntry_exit"),
    };
    m_inputMode.lastMode.clear();
    for (const QString &name : expected) {
        press(Qt::Key_Down);
        QCOMPARE(focusedName(), name);
    }
    QCOMPARE(m_inputMode.lastMode, QStringLiteral("keyboard"));
    QVERIFY(m_inputMode.cursorHidden);

    // Clamped at the ends rather than wrapping or leaking focus.
    press(Qt::Key_Down);
    QCOMPARE(focusedName(), QStringLiteral("railEntry_exit"));

    for (qsizetype i = expected.size() - 2; i >= 0; --i) {
        press(Qt::Key_Up);
        QCOMPARE(focusedName(), expected.at(i));
    }
    press(Qt::Key_Up);
    QCOMPARE(focusedName(), QStringLiteral("railEntry_home"));
    press(Qt::Key_Up);
    QCOMPARE(focusedName(), QStringLiteral("railEntry_home"));

    // Left inside the rail is consumed so focus never escapes off-screen.
    press(Qt::Key_Left);
    QCOMPARE(focusedName(), QStringLiteral("railEntry_home"));
}

void NavigationRailTest::currentPageStaysDistinctFromFocus()
{
    focusRailFromPage();
    press(Qt::Key_Down);

    QQuickItem *home = item(QStringLiteral("railEntry_home"));
    QQuickItem *search = item(QStringLiteral("railEntry_search"));
    QCOMPARE(home->property("selected").toBool(), true);
    QCOMPARE(home->hasActiveFocus(), false);
    QCOMPARE(search->property("selected").toBool(), false);
    QCOMPARE(search->hasActiveFocus(), true);
}

void NavigationRailTest::rightAndEscapeRestorePageFocus()
{
    QQuickItem *rail = item(QStringLiteral("rail"));
    QQuickItem *content = item(QStringLiteral("content"));

    focusRailFromPage();
    press(Qt::Key_Down);
    press(Qt::Key_Right);
    QCOMPARE(focusedName(), QStringLiteral("contentTarget"));
    QCOMPARE(rail->property("expanded").toBool(), false);
    QCOMPARE(content->property("restoreCount").toInt(), 1);

    focusRailFromPage();
    press(Qt::Key_Escape);
    QTRY_COMPARE(focusedName(), QStringLiteral("contentTarget"));
    QCOMPARE(rail->property("expanded").toBool(), false);
    QCOMPARE(content->property("restoreCount").toInt(), 2);
}

void NavigationRailTest::activationNavigatesAndReturnsToCurrentEntry()
{
    QQuickItem *scene = m_view->rootObject();
    QQuickItem *rail = item(QStringLiteral("rail"));

    focusRailFromPage();
    press(Qt::Key_Down);
    press(Qt::Key_Return);
    QCOMPARE(scene->property("lastNavigation").toString(), QStringLiteral("search"));
    QCOMPARE(rail->property("currentNavigation").toString(), QStringLiteral("search"));
    QTRY_COMPARE(focusedName(), QStringLiteral("contentTarget"));

    // Re-entering lands on the current page's entry, not the top of the rail.
    focusRailFromPage();
    QCOMPARE(focusedName(), QStringLiteral("railEntry_search"));

    press(Qt::Key_Down);
    press(Qt::Key_Down);
    press(Qt::Key_Return);
    QCOMPARE(scene->property("lastNavigation").toString(), QStringLiteral("library:lib-shows"));
    QTRY_COMPARE(focusedName(), QStringLiteral("contentTarget"));
    focusRailFromPage();
    QCOMPARE(focusedName(), QStringLiteral("railLibrary_1"));
    QCOMPARE(item(QStringLiteral("railLibrary_1"))->property("selected").toBool(), true);
    QCOMPARE(item(QStringLiteral("railEntry_search"))->property("selected").toBool(), false);

    // Account access stays in the rail: the entry opens the space
    // picker, which also offers sign-out.
    press(Qt::Key_Down);
    press(Qt::Key_Down);
    QCOMPARE(focusedName(), QStringLiteral("railEntry_account"));
    press(Qt::Key_Return);
    QCOMPARE(scene->property("spacePickerRequests").toInt(), 1);

    // A cancelled confirmation dialog hands focus back to the entry that
    // opened it, not to the current page's entry.
    item(QStringLiteral("contentTarget"))->forceActiveFocus();
    QVERIFY(QMetaObject::invokeMethod(rail, "focusDialogOrigin"));
    QCOMPARE(focusedName(), QStringLiteral("railEntry_account"));
}

void NavigationRailTest::shiftEnterReordersLibraries()
{
    QQuickItem *rail = item(QStringLiteral("rail"));

    focusRailFromPage();
    press(Qt::Key_Down);
    press(Qt::Key_Down);
    QCOMPARE(focusedName(), QStringLiteral("railLibrary_0"));

    press(Qt::Key_Return, Qt::ShiftModifier);
    QCOMPARE(rail->property("reorderingIndex").toInt(), 0);

    press(Qt::Key_Down);
    QCOMPARE(m_sidebarSettings.libraryOrder,
             QStringList({QStringLiteral("lib-shows"), QStringLiteral("lib-movies")}));
    QTRY_COMPARE(focusedName(), QStringLiteral("railLibrary_1"));
    QCOMPARE(rail->property("reorderingIndex").toInt(), 1);

    // Enter finishes reordering without navigating.
    press(Qt::Key_Return);
    QCOMPARE(rail->property("reorderingIndex").toInt(), -1);
    QCOMPARE(m_view->rootObject()->property("lastNavigation").toString(), QString());
    QCOMPARE(focusedName(), QStringLiteral("railLibrary_1"));
}

void NavigationRailTest::pointerPinExpandsWithoutTakingFocus()
{
    QQuickItem *rail = item(QStringLiteral("rail"));
    QQuickItem *toggle = item(QStringLiteral("railToggle"));
    QVERIFY(toggle);

    const QPointF center = toggle->mapToScene(QPointF(toggle->width() / 2, toggle->height() / 2));
    QTest::mouseClick(m_view.get(), Qt::LeftButton, Qt::NoModifier, center.toPoint());

    QCOMPARE(rail->property("pinnedOpen").toBool(), true);
    QCOMPARE(rail->property("expanded").toBool(), true);
    QCOMPARE(focusedName(), QStringLiteral("contentTarget"));

    press(Qt::Key_Escape);
    QCOMPARE(rail->property("pinnedOpen").toBool(), false);
    QCOMPARE(rail->property("expanded").toBool(), false);
    QCOMPARE(focusedName(), QStringLiteral("contentTarget"));
}

void NavigationRailTest::railMetricsFollowLayoutScale_data()
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

void NavigationRailTest::railMetricsFollowLayoutScale()
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

    // Rail tokens are pure design units: no breakpoints or pixel clamps.
    QCOMPARE(scene->property("collapsedWidth").toInt(), qRound(96 * scale));
    QCOMPARE(scene->property("expandedWidth").toInt(), qRound(360 * scale));
    QCOMPARE(scene->property("itemHeight").toInt(), qRound(72 * scale));
    QCOMPARE(qRound(item(QStringLiteral("content"))->x()), scene->property("reservedWidth").toInt());

    // The reserved strip stays a small fraction of the window at every shape.
    QVERIFY(scene->property("reservedWidth").toReal() <= width * 0.16);

    // Every destination remains on-screen in the collapsed and expanded rail.
    QQuickItem *panel = item(QStringLiteral("railPanel"));
    for (bool expanded : {false, true}) {
        if (expanded) {
            focusRailFromPage();
            QVERIFY(QQuickTest::qWaitForPolish(m_view.get()));
        }
        QVERIFY(panel->x() + panel->width() <= width);
        for (const auto &name : {QStringLiteral("railEntry_home"), QStringLiteral("railEntry_settings"),
                                 QStringLiteral("railEntry_account"), QStringLiteral("railEntry_exit")}) {
            QQuickItem *entry = item(name);
            const QRectF bounds = entry->mapRectToScene(QRectF(0, 0, entry->width(), entry->height()));
            QVERIFY2(bounds.top() >= 0 && bounds.bottom() <= height,
                     qPrintable(QStringLiteral("%1 outside window: %2..%3 of %4")
                                    .arg(name).arg(bounds.top()).arg(bounds.bottom()).arg(height)));
            QVERIFY2(bounds.right() <= panel->x() + panel->width() + 1, qPrintable(name));
        }
    }
}

QTEST_MAIN(NavigationRailTest)
#include "NavigationRailTest.moc"
