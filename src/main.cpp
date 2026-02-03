#include <QGuiApplication>
#include <QCoreApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>

#include "utils/CacheMigrator.h"
#include "ui/FontLoader.h"
#include "core/ApplicationInitializer.h"
#include "ui/WindowManager.h"
#include "network/Types.h"

int main(int argc, char *argv[])
{
    // Set application metadata
    QCoreApplication::setOrganizationName("Bloom");
    QCoreApplication::setOrganizationDomain("com.github.bloom");
    QCoreApplication::setApplicationName("Bloom");
    
    // Set Qt Quick Controls style
    QQuickStyle::setStyle("Basic");
    
    QGuiApplication app(argc, argv);

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

