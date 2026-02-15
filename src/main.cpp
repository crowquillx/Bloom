#include <QGuiApplication>
#include <QCoreApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QSize>
#include <QDebug>
#include <clocale>

#include "config/version.h"

#include "utils/CacheMigrator.h"
#include "ui/FontLoader.h"
#include "core/ApplicationInitializer.h"
#include "ui/WindowManager.h"
#include "network/Types.h"
#include "test/TestModeController.h"

int main(int argc, char *argv[])
{
    // libmpv requires C numeric locale for reliable option/property parsing.
    if (setlocale(LC_NUMERIC, "C") == nullptr) {
        qWarning() << "Failed to force LC_NUMERIC=C; libmpv initialization may fail on non-C locales";
    }

    // Set application metadata
    QCoreApplication::setOrganizationName("Bloom");
    QCoreApplication::setOrganizationDomain("com.github.bloom");
    QCoreApplication::setApplicationName("Bloom");
    QCoreApplication::setApplicationVersion(BLOOM_VERSION);
    
    // Set Qt Quick Controls style
    QQuickStyle::setStyle("Basic");
    
    QGuiApplication app(argc, argv);
    app.setWindowIcon(QIcon(":/images/logo.ico"));

    // Parse command-line arguments
    QCommandLineParser parser;
    parser.setApplicationDescription("Bloom - Jellyfin HTPC Client");
    parser.addHelpOption();
    parser.addVersionOption();
    
    QCommandLineOption testModeOption(
        QStringList() << "test-mode",
        "Run in visual regression test mode. Loads test fixtures instead of connecting to a server."
    );
    QCommandLineOption fixtureOption(
        QStringList() << "test-fixture",
        "Path to test fixture JSON file (default: tests/fixtures/test_library.json)",
        "path",
        "tests/fixtures/test_library.json"
    );
    QCommandLineOption resolutionOption(
        QStringList() << "test-resolution",
        "Viewport resolution for screenshots in WxH format (default: 1920x1080)",
        "resolution",
        "1920x1080"
    );
    
    parser.addOption(testModeOption);
    parser.addOption(fixtureOption);
    parser.addOption(resolutionOption);
    parser.process(app);
    
    // Initialize test mode if requested
    if (parser.isSet(testModeOption)) {
        QString fixturePath = parser.value(fixtureOption);
        QString resolutionStr = parser.value(resolutionOption);
        
        // Parse resolution string (WxH format)
        QSize resolution;
        QStringList parts = resolutionStr.split('x');
        if (parts.size() == 2) {
            bool okWidth = false, okHeight = false;
            int width = parts[0].toInt(&okWidth);
            int height = parts[1].toInt(&okHeight);
            if (okWidth && okHeight && width > 0 && height > 0) {
                resolution = QSize(width, height);
            }
        }
        
        if (resolution.isEmpty()) {
            qWarning() << "Invalid resolution format:" << resolutionStr << "- using default 1920x1080";
            resolution = QSize(1920, 1080);
        }
        
        TestModeController::instance()->initialize(fixturePath, resolution);
    }

    // Register shared network metatypes
    registerNetworkMetaTypes();
    
    // Migrate cache
    CacheMigrator migrator;
    migrator.migrate();

    // Load fonts
    FontLoader fontLoader;
    fontLoader.load();
    
    // Initialize Application Services
    ApplicationInitializer appInitializer(&app);
    appInitializer.registerServices();
    appInitializer.initializeServices();
    
    // Setup Window and UI
    WindowManager windowManager(&app);
    // Passing ConfigManager needed for WindowManager setup
    windowManager.setup(appInitializer.configManager());
    windowManager.exposeContextProperties(appInitializer);
    windowManager.load();

    return app.exec();
}
