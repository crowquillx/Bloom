#include <QtTest/QtTest>
#include <QGuiApplication>
#include <QQuickWindow>
#include <QQuickItem>
#include <QImage>
#include <QDir>
#include <QQuickView>
#include <QQmlEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QElapsedTimer>

#include "core/ApplicationInitializer.h"
#include "core/ServiceLocator.h"
#include "ui/WindowManager.h"
#include "test/TestModeController.h"
#include "network/Types.h"
#include "utils/CacheMigrator.h"
#include "ui/FontLoader.h"
#include "player/backend/IPlayerBackend.h"

/**
 * @brief Visual regression test class for capturing and comparing screenshots.
 * 
 * This class captures screenshots of the application at different resolutions
 * and compares them against golden screenshots to detect visual regressions.
 * 
 * Test resolutions:
 * - 720p:  1280x720
 * - 1080p: 1920x1080
 * - 1440p: 2560x1440
 * - 4K:    3840x2160
 * 
 * Screens tested:
 * - HomeScreen
 * - LibraryScreen
 * - MovieDetailsView
 */
class VisualRegressionTest : public QObject
{
    Q_OBJECT 

private slots:
    void initTestCase();
    void cleanupTestCase();
    void testBackendServiceRegistration();

    // Home screen tests
    void testHomeScreen_720p();
    void testHomeScreen_1080p();
    void testHomeScreen_1440p();
    void testHomeScreen_4K();

    // Library screen tests
    void testLibraryScreen_720p();
    void testLibraryScreen_1080p();
    void testLibraryScreen_1440p();
    void testLibraryScreen_4K();

    // Details view tests
    void testMovieDetailsView_720p();
    void testMovieDetailsView_1080p();
    void testMovieDetailsView_1440p();
    void testMovieDetailsView_4K();

private:
    /**
     * @brief Resolution definition structure.
     */
    struct Resolution
    {
        int width;
        int height;
        QString name;
    };

    // Directories for test artifacts
    QDir m_goldenDir;      // tests/golden/
    QDir m_diffDir;        // tests/diffs/
    QDir m_captureDir;     // tests/captures/
    QDir m_fixtureDir;     // tests/fixtures/

    // Application components
    QGuiApplication* m_app = nullptr;
    ApplicationInitializer* m_appInitializer = nullptr;
    WindowManager* m_windowManager = nullptr;
    QQuickWindow* m_window = nullptr;

    /**
     * @brief Capture a screenshot of the current window.
     * 
     * @param window The window to capture.
     * @param screenName Name of the screen being captured.
     * @param res The resolution to capture at.
     * @return QImage The captured screenshot.
     */
    QImage captureScreen(QQuickWindow* window, const QString& screenName, const Resolution& res);

    /**
     * @brief Compare two images with a tolerance threshold.
     * 
     * @param actual The captured image.
     * @param golden The golden reference image.
     * @param tolerance The allowed difference (0.0-1.0, default 0.01 = 1%).
     * @return bool True if images match within tolerance.
     */
    bool compareImages(const QImage& actual, const QImage& golden, qreal tolerance = 0.01);

    /**
     * @brief Save a diff image showing differences between actual and golden.
     * 
     * @param actual The captured image.
     * @param golden The golden reference image.
     * @param name The base name for the diff file.
     * @return bool True if diff was saved successfully.
     */
    bool saveDiffImage(const QImage& actual, const QImage& golden, const QString& name);

    /**
     * @brief Navigate to a specific screen in the application.
     * 
     * @param screenName The name of the screen to navigate to.
     */
    void navigateToScreen(const QString& screenName);

    /**
     * @brief Set the window to a specific resolution.
     * 
     * @param window The window to resize.
     * @param res The target resolution.
     */
    void setWindowResolution(QQuickWindow* window, const Resolution& res);

    /**
     * @brief Wait for the window to finish rendering and be fully exposed.
     * 
     * @param window The window to wait for.
     * @param maxWaitMs Maximum time to wait in milliseconds.
     */
    void waitForExposureAndRendering(QQuickWindow* window, int maxWaitMs = 5000);

    /**
     * @brief Get the path to a golden image.
     * 
     * @param screen The screen name.
     * @param res The resolution.
     * @return QString The path to the golden image.
     */
    QString goldenPath(const QString& screen, const Resolution& res) const;

    /**
     * @brief Run a visual regression test for a specific screen and resolution.
     * 
     * @param screenName The name of the screen to test.
     * @param res The resolution to test at.
     */
    void runVisualTest(const QString& screenName, const Resolution& res);
};

void VisualRegressionTest::initTestCase()
{
    // Set up directories relative to test binary location
    QString testDir = QFINDTESTDATA(".");
    
    m_goldenDir = QDir(testDir + "/golden");
    m_diffDir = QDir(testDir + "/diffs");
    m_captureDir = QDir(testDir + "/captures");
    m_fixtureDir = QDir(testDir + "/fixtures");

    // Create directories if they don't exist
    if (!m_goldenDir.exists()) {
        QVERIFY(m_goldenDir.mkpath("."));
    }
    if (!m_diffDir.exists()) {
        QVERIFY(m_diffDir.mkpath("."));
    }
    if (!m_captureDir.exists()) {
        QVERIFY(m_captureDir.mkpath("."));
    }

    // Verify fixture exists
    QString fixturePath = m_fixtureDir.filePath("test_library.json");
    if (!QFile::exists(fixturePath)) {
        QSKIP("Test fixture not found: tests/fixtures/test_library.json");
        return;
    }

    // Initialize test mode with fixture path and default resolution
    TestModeController::instance()->initialize(fixturePath, QSize(1920, 1080));
    qDebug() << "Test mode initialized with fixture:" << fixturePath;

    // Create QGuiApplication (required for QML)
    // Note: We use a static argc/argv since QGuiApplication requires references
    static int argc = 1;
    static char* argv[1] = {const_cast<char*>("VisualRegressionTest")};
    
    // Set application metadata
    QCoreApplication::setOrganizationName("Bloom");
    QCoreApplication::setOrganizationDomain("com.github.bloom");
    QCoreApplication::setApplicationName("Bloom");
    
    // Set Qt Quick Controls style
    QQuickStyle::setStyle("Basic");
    
    m_app = new QGuiApplication(argc, argv);
    QVERIFY(m_app != nullptr);

    // Register shared network metatypes
    registerNetworkMetaTypes();
    
    // Migrate cache
    CacheMigrator migrator;
    migrator.migrate();

    // Load fonts
    FontLoader fontLoader;
    fontLoader.load();
    
    // Initialize Application Services
    m_appInitializer = new ApplicationInitializer(m_app);
    QVERIFY(m_appInitializer != nullptr);
    
    m_appInitializer->registerServices();
    m_appInitializer->initializeServices();
    
    // Setup Window and UI
    m_windowManager = new WindowManager(m_app);
    QVERIFY(m_windowManager != nullptr);
    
    m_windowManager->setup(m_appInitializer->configManager());
    m_windowManager->exposeContextProperties(*m_appInitializer);
    m_windowManager->load();

    // Get the window from the QML engine
    auto rootObjects = m_windowManager->engine().rootObjects();
    if (rootObjects.isEmpty()) {
        QSKIP("Failed to load Main.qml - no root objects");
        return;
    }

    m_window = qobject_cast<QQuickWindow*>(rootObjects.first());
    if (!m_window) {
        QSKIP("Failed to cast root object to QQuickWindow");
        return;
    }

    qDebug() << "Bloom UI loaded successfully";
    qDebug() << "Window size:" << m_window->width() << "x" << m_window->height();

    // Wait for initial rendering and exposure
    waitForExposureAndRendering(m_window);
}

void VisualRegressionTest::cleanupTestCase()
{
    // Clean up in reverse order of creation
    delete m_windowManager;
    m_windowManager = nullptr;
    
    delete m_appInitializer;
    m_appInitializer = nullptr;
    
    delete m_app;
    m_app = nullptr;
}

void VisualRegressionTest::testBackendServiceRegistration()
{
    IPlayerBackend *backend = ServiceLocator::tryGet<IPlayerBackend>();
    QVERIFY2(backend != nullptr, "IPlayerBackend should be registered by ApplicationInitializer::registerServices");
#if defined(Q_OS_LINUX)
    QVERIFY2(backend->backendName() == QStringLiteral("linux-libmpv-opengl")
                 || backend->backendName() == QStringLiteral("external-mpv-ipc"),
             "Linux should select embedded backend when supported, otherwise fall back to external backend");
#elif defined(Q_OS_WIN)
    QCOMPARE(backend->backendName(), QStringLiteral("win-libmpv"));
#else
    QCOMPARE(backend->backendName(), QStringLiteral("external-mpv-ipc"));
#endif
}

QImage VisualRegressionTest::captureScreen(QQuickWindow* window, const QString& screenName, const Resolution& res)
{
    if (!window) {
        qWarning() << "captureScreen: Window is null";
        return QImage();
    }

    // Set window size
    setWindowResolution(window, res);

    // Wait for layout to settle and rendering to complete
    waitForExposureAndRendering(window);

    // Capture screenshot
    QImage screenshot = window->grabWindow();

    // Save to captures directory for debugging
    if (!screenshot.isNull() && m_captureDir.exists()) {
        QString filename = QString("%1_%2.png").arg(screenName, res.name);
        QString savePath = m_captureDir.filePath(filename);
        if (!screenshot.save(savePath)) {
            qWarning() << "Failed to save captured screenshot to:" << savePath;
        } else {
            qDebug() << "Saved captured screenshot to:" << savePath;
        }
    } else if (screenshot.isNull()) {
        qWarning() << "grabWindow() returned null image";
    }

    return screenshot;
}

bool VisualRegressionTest::compareImages(const QImage& actual, const QImage& golden, qreal tolerance)
{
    // Check dimensions match
    if (actual.size() != golden.size()) {
        qDebug() << "Size mismatch: actual" << actual.size() << "vs golden" << golden.size();
        return false;
    }

    if (actual.format() != golden.format()) {
        // Convert to same format for comparison
        return compareImages(actual.convertToFormat(QImage::Format_RGB32), 
                            golden.convertToFormat(QImage::Format_RGB32), 
                            tolerance);
    }

    qint64 totalDiff = 0;
    const qint64 totalPixels = static_cast<qint64>(actual.width()) * static_cast<qint64>(actual.height());

    // Compare pixel by pixel
    for (int y = 0; y < actual.height(); ++y) {
        for (int x = 0; x < actual.width(); ++x) {
            QRgb a = actual.pixel(x, y);
            QRgb g = golden.pixel(x, y);

            // Calculate per-channel difference
            totalDiff += qAbs(qRed(a) - qRed(g));
            totalDiff += qAbs(qGreen(a) - qGreen(g));
            totalDiff += qAbs(qBlue(a) - qBlue(g));
        }
    }

    // Normalize to 0-1 range (max diff per pixel = 255*3)
    qreal avgDiff = static_cast<qreal>(totalDiff) / (totalPixels * 255.0 * 3.0);

    qDebug() << "Average difference:" << avgDiff << "tolerance:" << tolerance;

    return avgDiff <= tolerance;
}

bool VisualRegressionTest::saveDiffImage(const QImage& actual, const QImage& golden, const QString& name)
{
    if (actual.size() != golden.size()) {
        qWarning() << "Cannot create diff image: size mismatch";
        return false;
    }

    // Create diff image highlighting differences
    QImage diff(actual.width(), actual.height(), QImage::Format_RGB32);

    for (int y = 0; y < actual.height(); ++y) {
        for (int x = 0; x < actual.width(); ++x) {
            QRgb a = actual.pixel(x, y);
            QRgb g = golden.pixel(x, y);

            // Calculate difference and highlight in red
            int rDiff = qAbs(qRed(a) - qRed(g));
            int gDiff = qAbs(qGreen(a) - qGreen(g));
            int bDiff = qAbs(qBlue(a) - qBlue(g));

            // Use red channel to show differences, scale for visibility
            int diffIntensity = (rDiff + gDiff + bDiff) / 3;
            
            // Amplify small differences for visibility
            diffIntensity = qMin(255, diffIntensity * 4);

            diff.setPixel(x, y, qRgb(diffIntensity, 0, 0));
        }
    }

    QString diffPath = m_diffDir.filePath(name + "_diff.png");
    bool saved = diff.save(diffPath);
    if (saved) {
        qDebug() << "Saved diff image to:" << diffPath;
    }
    return saved;
}

void VisualRegressionTest::navigateToScreen(const QString& screenName)
{
    if (!m_window) {
        qWarning() << "navigateToScreen: Window is null";
        return;
    }

    // Find the StackView by traversing the object hierarchy
    QQuickItem* contentItem = m_window->contentItem();
    QQuickItem* stackView = contentItem->findChild<QQuickItem*>("stackView");
    
    if (!stackView) {
        qWarning() << "navigateToScreen: Could not find StackView";
        return;
    }
    
    // Navigate based on screen name
    if (screenName == "HomeScreen") {
        // Pop all screens to get back to home (or replace login with home)
        // In test mode, mock auth should already be authenticated
        // The StackView should show HomeScreen after auth success
        int maxAttempts = 100;
        int attempts = 0;
        while (stackView->property("depth").toInt() > 1) {
            QMetaObject::invokeMethod(stackView, "pop", Qt::DirectConnection);
            QCoreApplication::processEvents();
            if (++attempts >= maxAttempts) {
                qWarning() << "navigateToScreen: exceeded max pop attempts (" << maxAttempts << ")";
                break;
            }
        }
    } else if (screenName == "LibraryScreen") {
        // Push LibraryScreen
        QVariantMap props;
        props["currentParentId"] = "library-movies";
        props["currentLibraryId"] = "library-movies";
        props["currentLibraryName"] = "Movies";
        
        int initialDepth = stackView->property("depth").toInt();
        
        bool invokeSuccess = QMetaObject::invokeMethod(stackView, "push", 
                                  Qt::DirectConnection,
                                  Q_ARG(QString, "LibraryScreen.qml"),
                                  Q_ARG(QVariant, QVariant(props)));
                                  
        if (!invokeSuccess) {
            qWarning() << "navigateToScreen: Failed to invoke push for LibraryScreen";
        }
        
        int finalDepth = stackView->property("depth").toInt();
        if (finalDepth <= initialDepth) {
            qWarning() << "navigateToScreen: Validation failed - Stack depth did not increase for LibraryScreen (Depth:" << initialDepth << "->" << finalDepth << ")";
        }
        QVERIFY(finalDepth > initialDepth);
        
    } else if (screenName == "MovieDetailsView") {
        // Navigate to a movie details view
        QVariantMap props;
        props["currentParentId"] = "library-movies";
        props["currentLibraryId"] = "library-movies";
        props["currentLibraryName"] = "Movies";
        props["itemId"] = "movie-001";
        
        int initialDepth = stackView->property("depth").toInt();
        
        bool invokeSuccess = QMetaObject::invokeMethod(stackView, "push", 
                                  Qt::DirectConnection,
                                  Q_ARG(QString, "MovieDetailsView.qml"),
                                  Q_ARG(QVariant, QVariant(props)));
                                  
        if (!invokeSuccess) {
            qWarning() << "navigateToScreen: Failed to invoke push for MovieDetailsView";
        }
        
        int finalDepth = stackView->property("depth").toInt();
        if (finalDepth <= initialDepth) {
            qWarning() << "navigateToScreen: Validation failed - Stack depth did not increase for MovieDetailsView (Depth:" << initialDepth << "->" << finalDepth << ")";
        }
        QVERIFY(finalDepth > initialDepth);
    }
    
    // Wait for navigation and rendering
    waitForExposureAndRendering(m_window);
    
    qDebug() << "Navigated to screen:" << screenName;
}

void VisualRegressionTest::setWindowResolution(QQuickWindow* window, const Resolution& res)
{
    if (window) {
        // Ensure window is visible and not minimized
        window->show();
        window->requestActivate();
        
        // Set the geometry
        window->setGeometry(0, 0, res.width, res.height);
        
        qDebug() << "Set window resolution to:" << res.width << "x" << res.height;
    }
}

void VisualRegressionTest::waitForExposureAndRendering(QQuickWindow* window, int maxWaitMs)
{
    if (!window) {
        return;
    }

    QElapsedTimer timer;
    timer.start();

    // First, wait for the window to be exposed
    while (!window->isExposed() && timer.elapsed() < maxWaitMs) {
        QCoreApplication::processEvents();
        QThread::msleep(10);
    }

    if (!window->isExposed()) {
        qWarning() << "Window not exposed after" << maxWaitMs << "ms";
        return;
    }

    // Wait for initial frame to be rendered
    // Process events and request updates for a fixed number of frames to ensure rendering has started
    int framesToWait = 3;
    while (timer.elapsed() < maxWaitMs && framesToWait > 0) {
        window->requestUpdate();
        QCoreApplication::processEvents();
        
        // Small delay to allow rendering
        QThread::msleep(16); // ~60fps frame time
        
        framesToWait--;
    }

    // Additional settling time for animations and layout
    QThread::msleep(100);
    QCoreApplication::processEvents();
    
    qDebug() << "Window exposed and rendered after" << timer.elapsed() << "ms";
}

QString VisualRegressionTest::goldenPath(const QString& screen, const Resolution& res) const
{
    return QString("%1_%2.png").arg(screen, res.name);
}

void VisualRegressionTest::runVisualTest(const QString& screenName, const Resolution& res)
{
    // Verify window is available
    if (!m_window) {
        QSKIP("Window not initialized - cannot run visual test");
        return;
    }

    // Navigate to the screen
    navigateToScreen(screenName);

    // Capture screenshot
    QImage actual = captureScreen(m_window, screenName, res);

    // Generate screenshot even if null for debugging
    if (actual.isNull()) {
        // Create a placeholder image to indicate failure
        actual = QImage(res.width, res.height, QImage::Format_RGB32);
        actual.fill(Qt::red);
        
        QString failPath = m_captureDir.filePath(QString("%1_%2_FAILED.png").arg(screenName, res.name));
        actual.save(failPath);
        
        QFAIL(qPrintable(QString("Failed to capture screenshot for %1 at %2 - saved red placeholder to %3")
                        .arg(screenName, res.name, failPath)));
        return;
    }

    // Load golden image
    QString goldenFilePath = m_goldenDir.filePath(goldenPath(screenName, res));
    QImage golden(goldenFilePath);

    // If no golden exists, create one (for initial run)
    if (golden.isNull()) {
        QString newGoldenPath = m_goldenDir.filePath(goldenPath(screenName, res));
        QVERIFY2(actual.save(newGoldenPath), 
                 qPrintable(QString("Failed to save new golden image: %1").arg(newGoldenPath)));
        qDebug() << "Created new golden image:" << newGoldenPath;
        QSKIP(qPrintable(QString("No golden image found, created new one: %1").arg(newGoldenPath)));
        return;
    }

    // Compare images
    bool matches = compareImages(actual, golden, 0.01);

    if (!matches) {
        // Save diff image for debugging
        saveDiffImage(actual, golden, QString("%1_%2").arg(screenName, res.name));
    }

    QVERIFY2(matches, 
             qPrintable(QString("Image mismatch for %1 at %2 (see diffs/ directory)")
                       .arg(screenName, res.name)));
}

// ============================================================================
// Home Screen Tests
// ============================================================================

void VisualRegressionTest::testHomeScreen_720p()
{
    Resolution res = {1280, 720, "720p"};
    runVisualTest("HomeScreen", res);
}

void VisualRegressionTest::testHomeScreen_1080p()
{
    Resolution res = {1920, 1080, "1080p"};
    runVisualTest("HomeScreen", res);
}

void VisualRegressionTest::testHomeScreen_1440p()
{
    Resolution res = {2560, 1440, "1440p"};
    runVisualTest("HomeScreen", res);
}

void VisualRegressionTest::testHomeScreen_4K()
{
    Resolution res = {3840, 2160, "4K"};
    runVisualTest("HomeScreen", res);
}

// ============================================================================
// Library Screen Tests
// ============================================================================

void VisualRegressionTest::testLibraryScreen_720p()
{
    Resolution res = {1280, 720, "720p"};
    runVisualTest("LibraryScreen", res);
}

void VisualRegressionTest::testLibraryScreen_1080p()
{
    Resolution res = {1920, 1080, "1080p"};
    runVisualTest("LibraryScreen", res);
}

void VisualRegressionTest::testLibraryScreen_1440p()
{
    Resolution res = {2560, 1440, "1440p"};
    runVisualTest("LibraryScreen", res);
}

void VisualRegressionTest::testLibraryScreen_4K()
{
    Resolution res = {3840, 2160, "4K"};
    runVisualTest("LibraryScreen", res);
}

// ============================================================================
// Movie Details View Tests
// ============================================================================

void VisualRegressionTest::testMovieDetailsView_720p()
{
    Resolution res = {1280, 720, "720p"};
    runVisualTest("MovieDetailsView", res);
}

void VisualRegressionTest::testMovieDetailsView_1080p()
{
    Resolution res = {1920, 1080, "1080p"};
    runVisualTest("MovieDetailsView", res);
}

void VisualRegressionTest::testMovieDetailsView_1440p()
{
    Resolution res = {2560, 1440, "1440p"};
    runVisualTest("MovieDetailsView", res);
}

void VisualRegressionTest::testMovieDetailsView_4K()
{
    Resolution res = {3840, 2160, "4K"};
    runVisualTest("MovieDetailsView", res);
}

QTEST_APPLESS_MAIN(VisualRegressionTest)
#include "VisualRegressionTest.moc"
