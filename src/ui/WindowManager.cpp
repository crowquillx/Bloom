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
    // Set Qt Quick Controls style to "Basic" is done in main() before QGuiApplication
    
    // Add qrc root to import path
    m_engine.addImportPath("qrc:/");
}

WindowManager::~WindowManager()
{
    // Stack allocated in main or parented to this, so auto cleanup
}

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
