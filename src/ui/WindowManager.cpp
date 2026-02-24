#include "WindowManager.h"
#include "core/ApplicationInitializer.h"
#include "utils/ConfigManager.h"
#include "ui/ImageCacheProvider.h"
#include "utils/GpuMemoryTrimmer.h"
#include "utils/DisplayManager.h"
#include "ui/ResponsiveLayoutManager.h"
#include "utils/SidebarSettings.h"
#include "utils/InputModeManager.h"
#include "ui/UiSoundController.h"
#include "player/PlayerController.h"
#include "player/ThemeSongManager.h"
#include "viewmodels/LibraryViewModel.h"
#include "viewmodels/SeriesDetailsViewModel.h"
#include "viewmodels/MovieDetailsViewModel.h"
#include "network/AuthenticationService.h"
#include "network/LibraryService.h"
#include "network/PlaybackService.h"
#include "network/SeerrService.h"
#include "core/ServiceLocator.h"

#include <QQmlContext>
#include <QQuickStyle>
#include <QLoggingCategory>

using namespace Qt::StringLiterals;

Q_LOGGING_CATEGORY(lcUiSceneGraph, "bloom.ui.scenegraph")

WindowManager::WindowManager(QGuiApplication* app, QObject *parent)
    : QObject(parent)
    , m_app(app)
{
    // Qt Quick Controls style is set to "Basic" in main() before QGuiApplication is created.

    // Add the qrc root so QML can resolve "qrc:/" imports directly.
    m_engine.addImportPath("qrc:/");
}

WindowManager::~WindowManager()
{
    // Services owned by ApplicationInitializer; nothing to release here.
}

/**
 * @brief Initialises subsystems that depend on a live QQmlApplicationEngine.
 *
 * Must be called after ApplicationInitializer::registerServices() and before
 * exposeContextProperties() / load().  Responsibilities:
 *
 *  - Creates and configures the ImageCacheProvider (image cache size, rounded-image
 *    pre-processing) and registers it as a QML image provider under the "cached" scheme.
 *  - Creates GpuMemoryTrimmer and wires it to ConfigManager and PlayerController so
 *    VRAM trimming responds to performance-mode and playback-state changes.
 *  - Connects objectCreated to forward the root QQuickWindow to GpuMemoryTrimmer and
 *    ResponsiveLayoutManager, and to hook up scene-graph error logging.
 *
 * @param configManager The already-loaded ConfigManager instance.
 */
void WindowManager::setup(ConfigManager* configManager)
{
    // ImageCacheProvider
    m_imageCacheProvider = new ImageCacheProvider(configManager->getImageCacheSizeMB());
    const QString roundedMode = configManager->getRoundedImageMode();
    const bool roundedPreprocessEnabled = configManager->getRoundedImagePreprocessEnabled()
        && roundedMode != "shader";
    m_imageCacheProvider->setRoundedPreprocessEnabled(roundedPreprocessEnabled);

    connect(configManager, &ConfigManager::roundedImagePreprocessEnabledChanged,
            m_imageCacheProvider, [configManager, this]() {
        const bool enabled = configManager->getRoundedImagePreprocessEnabled()
            && configManager->getRoundedImageMode() != "shader";
        m_imageCacheProvider->setRoundedPreprocessEnabled(enabled);
    });
    
    connect(configManager, &ConfigManager::roundedImageModeChanged,
            m_imageCacheProvider, [configManager, this]() {
        const bool enabled = configManager->getRoundedImagePreprocessEnabled()
            && configManager->getRoundedImageMode() != "shader";
        m_imageCacheProvider->setRoundedPreprocessEnabled(enabled);
    });

    m_engine.addImageProvider("cached", m_imageCacheProvider);
    m_engine.rootContext()->setContextProperty("ImageCacheProvider", m_imageCacheProvider);

    // GpuMemoryTrimmer
    m_gpuMemoryTrimmer = new GpuMemoryTrimmer(configManager, m_imageCacheProvider);
    ServiceLocator::registerService<GpuMemoryTrimmer>(m_gpuMemoryTrimmer);
    m_gpuMemoryTrimmer->setPerformanceModeEnabled(configManager->getPerformanceModeEnabled());
    
    connect(configManager, &ConfigManager::performanceModeEnabledChanged,
            [this, configManager]() {
        m_gpuMemoryTrimmer->setPerformanceModeEnabled(configManager->getPerformanceModeEnabled());
    });
    
    // Connect objectCreated to setup window in trimmer
    connect(&m_engine, &QQmlApplicationEngine::objectCreated,
            m_app, [this](QObject *obj, const QUrl &) {
        if (auto *window = qobject_cast<QQuickWindow *>(obj)) {
            connect(window, &QQuickWindow::sceneGraphError,
                    this, [window](QQuickWindow::SceneGraphError error, const QString &message) {
                qCCritical(lcUiSceneGraph)
                    << "Scene graph error"
                    << "window=" << window
                    << "error=" << static_cast<int>(error)
                    << "message=" << message;
            }, Qt::UniqueConnection);

            m_gpuMemoryTrimmer->setWindow(window);
            // Also set window on ResponsiveLayoutManager for viewport tracking
            auto* responsiveLayoutManager = ServiceLocator::tryGet<ResponsiveLayoutManager>();
            if (responsiveLayoutManager) {
                responsiveLayoutManager->setWindow(window);
            }
        }
    }, Qt::QueuedConnection);

    // Connect PlayerController to GpuMemoryTrimmer
    auto* playerController = ServiceLocator::get<PlayerController>();
    connect(playerController, &PlayerController::isPlaybackActiveChanged,
            [this, configManager, playerController]() {
        m_gpuMemoryTrimmer->setPerformanceModeEnabled(configManager->getPerformanceModeEnabled());
        m_gpuMemoryTrimmer->onPlaybackActiveChanged(playerController->isPlaybackActive());
    });
}

/**
 * @brief Registers all C++ services and objects as named QML context properties.
 *
 * Each service retrieved from ServiceLocator is set on the root QQmlContext so
 * that QML files can reference it by name (e.g. ConfigManager, SeerrService).
 * Also exposes application metadata: appVersion and qtVersion.
 *
 * Must be called after setup() and before load().
 *
 * @param appInit The ApplicationInitializer; currently unused directly but kept
 *                as a parameter for future per-initializer context needs.
 */
void WindowManager::exposeContextProperties(ApplicationInitializer& appInit)
{
    // Expose services to QML
    QQmlContext* context = m_engine.rootContext();
    
    context->setContextProperty("PlayerController", ServiceLocator::get<PlayerController>());
    context->setContextProperty("LibraryViewModel", ServiceLocator::get<LibraryViewModel>());
    context->setContextProperty("SeriesDetailsViewModel", ServiceLocator::get<SeriesDetailsViewModel>());
    context->setContextProperty("MovieDetailsViewModel", ServiceLocator::get<MovieDetailsViewModel>());
    context->setContextProperty("ThemeSongManager", ServiceLocator::get<ThemeSongManager>());
    context->setContextProperty("InputModeManager", ServiceLocator::get<InputModeManager>());
    context->setContextProperty("SidebarSettings", ServiceLocator::get<SidebarSettings>());
    context->setContextProperty("ConfigManager", ServiceLocator::get<ConfigManager>());
    context->setContextProperty("DisplayManager", ServiceLocator::get<DisplayManager>());
    context->setContextProperty("ResponsiveLayoutManager", ServiceLocator::get<ResponsiveLayoutManager>());
    
    context->setContextProperty("UiSoundController", ServiceLocator::get<UiSoundController>());

    context->setContextProperty("AuthenticationService", ServiceLocator::get<AuthenticationService>());
    context->setContextProperty("LibraryService", ServiceLocator::get<LibraryService>());
    context->setContextProperty("PlaybackService", ServiceLocator::get<PlaybackService>());
    context->setContextProperty("SeerrService", ServiceLocator::get<SeerrService>());

    // App metadata for QML
    context->setContextProperty("appVersion", QCoreApplication::applicationVersion());
    context->setContextProperty("qtVersion", QString(qVersion()));
}

/**
 * @brief Loads the root QML file and shows the application window.
 *
 * Loads qrc:/BloomUI/ui/Main.qml into the engine.  If the root object cannot be
 * created (e.g. syntax error in QML), the application exits with code -1.
 * After loading, the root QQuickWindow is forwarded to GpuMemoryTrimmer and
 * ResponsiveLayoutManager for viewport tracking.
 *
 * Must be called after exposeContextProperties().
 */
void WindowManager::load()
{
    const QUrl url(u"qrc:/BloomUI/ui/Main.qml"_s);
    
    connect(&m_engine, &QQmlApplicationEngine::objectCreated,
            m_app, [url](QObject *obj, const QUrl &objUrl) {
        if (!obj && url == objUrl)
            QCoreApplication::exit(-1);
    }, Qt::QueuedConnection);

    m_engine.load(url);
    
    if (!m_engine.rootObjects().isEmpty()) {
        if (auto *window = qobject_cast<QQuickWindow *>(m_engine.rootObjects().constFirst())) {
            m_gpuMemoryTrimmer->setWindow(window);
            auto* responsiveLayoutManager = ServiceLocator::tryGet<ResponsiveLayoutManager>();
            if (responsiveLayoutManager) {
                responsiveLayoutManager->setWindow(window);
            }
        }
    }
}
