#include "ApplicationInitializer.h"
#include "ServiceLocator.h"
#include "utils/ConfigManager.h"
#include "utils/DisplayManager.h"
#include "ui/ResponsiveLayoutManager.h"
#include "utils/TrackPreferencesManager.h"
#include "player/backend/IPlayerBackend.h"
#include "player/backend/PlayerBackendFactory.h"
#include "player/PlayerController.h"
#include "player/ThemeSongManager.h"
#include "security/SecretStoreFactory.h"
#include "security/ISecretStore.h"
#include "network/AuthenticationService.h"
#include "network/LibraryService.h"
#include "network/PlaybackService.h"
#include "network/SeerrService.h"
#include "utils/InputModeManager.h"
#include "viewmodels/LibraryViewModel.h"
#include "viewmodels/SeriesDetailsViewModel.h"
#include "viewmodels/MovieDetailsViewModel.h"
#include "utils/SidebarSettings.h"
#include "ui/UiSoundController.h"
#include "utils/GpuMemoryTrimmer.h"
#include "utils/Logger.h"
#include "network/SessionManager.h"
#include "network/SessionService.h"
#include "test/TestModeController.h"
#include "test/MockAuthenticationService.h"
#include "test/MockLibraryService.h"

#include <QGuiApplication>
#include <QDebug>
#include <cstdio>

// Qt message handler to forward all Qt logs to custom Logger
// Uses shared thread_local guard (inQtMessageHandler from Logger.h) to prevent deadlock from recursive logging
static void qtMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    // Prevent recursive logging (e.g., if Logger or Qt internals emit debug output)
    // This avoids deadlock when the Logger mutex is already held on this thread
    // inQtMessageHandler is declared in Logger.h and is accessible from both this file and Logger.cpp
    if (inQtMessageHandler) {
        // Fallback to stderr for recursive calls to avoid deadlock
        fprintf(stderr, "[RECURSIVE] %s\n", qPrintable(msg));
        return;
    }
    inQtMessageHandler = true;

    // Build message with category prefix if present
    QString formattedMsg;
    if (context.category && qstrcmp(context.category, "default") != 0) {
        formattedMsg = QStringLiteral("[%1] %2").arg(context.category, msg);
    } else {
        formattedMsg = msg;
    }

    // Map QtMsgType to Logger::LogLevel
    switch (type) {
    case QtDebugMsg:
        Logger::instance().debug(formattedMsg);
        break;
    case QtInfoMsg:
        Logger::instance().info(formattedMsg);
        break;
    case QtWarningMsg:
        Logger::instance().warning(formattedMsg);
        break;
    case QtCriticalMsg:
    case QtFatalMsg:
        Logger::instance().error(formattedMsg);
        // For fatal messages, ensure we abort like the default handler
        if (type == QtFatalMsg) {
            inQtMessageHandler = false;
            abort();
        }
        break;
    }

    inQtMessageHandler = false;
}

ApplicationInitializer::ApplicationInitializer(QGuiApplication *app, QObject *parent)
    : QObject(parent)
    , m_app(app)
{
}

ApplicationInitializer::~ApplicationInitializer()
{
    // Clear ServiceLocator on destruction
    ServiceLocator::clear();
}

void ApplicationInitializer::registerServices()
{
    // 0. Logger - Initialize logging system first (before any services)
    if (!Logger::instance().initialize()) {
        qWarning() << "Failed to initialize Logger, falling back to console output";
    }
    // Enable debug-level logging and console output
    Logger::instance().setMinLogLevel(Logger::LogLevel::Debug);
    Logger::instance().setConsoleOutputEnabled(true);
    // Install Qt message handler to route all qDebug/qWarning/etc through our Logger
    qInstallMessageHandler(qtMessageHandler);
    

    // 1. ConfigManager - No dependencies, must be first to load settings
    m_configManager = std::make_unique<ConfigManager>();
    ServiceLocator::registerService<ConfigManager>(m_configManager.get());


    // Load configuration early so downstream services can read settings (e.g., cache size)
    m_configManager->load();
    
    // 1.5 DisplayManager - Depends on ConfigManager
    m_displayManager = std::make_unique<DisplayManager>(m_configManager.get());
    ServiceLocator::registerService<DisplayManager>(m_displayManager.get());
    
    // 1.6 ResponsiveLayoutManager - No dependencies (uses QGuiApplication::primaryScreen)
    m_responsiveLayoutManager = std::make_unique<ResponsiveLayoutManager>();
    ServiceLocator::registerService<ResponsiveLayoutManager>(m_responsiveLayoutManager.get());
    
    // 1.7 TrackPreferencesManager - No dependencies
    m_trackPreferencesManager = std::make_unique<TrackPreferencesManager>();
    ServiceLocator::registerService<TrackPreferencesManager>(m_trackPreferencesManager.get());
    
    // 2. Player backend - No dependencies
    m_playerBackend = PlayerBackendFactory::create(m_configManager->getPlayerBackend());
    ServiceLocator::registerService<IPlayerBackend>(m_playerBackend.get());
    qInfo() << "ApplicationInitializer: Active player backend:" << m_playerBackend->backendName();
    
    // Check if we're in test mode
    bool isTestMode = TestModeController::instance()->isTestMode();
    
    if (isTestMode) {
        qDebug() << "ApplicationInitializer: Running in test mode - registering mock services";
        
        // 2.5 SecretStore - Create platform-specific secure storage (still needed for mock services)
        m_secretStore = SecretStoreFactory::create();
        
        // 3. MockAuthenticationService - Pre-authenticated for testing
        m_mockAuthService = std::make_unique<MockAuthenticationService>(m_secretStore.get());
        ServiceLocator::registerService<AuthenticationService>(m_mockAuthService.get());
        
        // 3.1 MockLibraryService - Returns fixture data
        m_mockLibraryService = std::make_unique<MockLibraryService>();
        // Load fixture data
        QJsonObject fixture = TestModeController::instance()->loadFixture();
        m_mockLibraryService->loadFixture(fixture);
        ServiceLocator::registerService<LibraryService>(m_mockLibraryService.get());
        
        // 3.2 PlaybackService - Still use real service but with mock auth
        m_playbackService = std::make_unique<PlaybackService>(m_mockAuthService.get());
        ServiceLocator::registerService<PlaybackService>(m_playbackService.get());
        
        // 3.3 SeerrService - Third-party search/request integration
        m_seerrService = std::make_unique<SeerrService>(m_mockAuthService.get(), m_configManager.get());
        ServiceLocator::registerService<SeerrService>(m_seerrService.get());
        
        // 4. PlayerController
        m_playerController = std::make_unique<PlayerController>(
            m_playerBackend.get(),
            m_configManager.get(),
            m_trackPreferencesManager.get(),
            m_displayManager.get(),
            m_playbackService.get(),
            m_mockLibraryService.get(),
            m_mockAuthService.get()
        );
        ServiceLocator::registerService<PlayerController>(m_playerController.get());
        
        // 4.5 ThemeSongManager
        m_themeSongManager = std::make_unique<ThemeSongManager>(
            m_mockLibraryService.get(),
            m_configManager.get(),
            m_playerController.get()
        );
        ServiceLocator::registerService<ThemeSongManager>(m_themeSongManager.get());
    } else {
        // Normal (production) service registration
        
        // 2.5 SecretStore - Create platform-specific secure storage
        m_secretStore = SecretStoreFactory::create();
        
        // 3. AuthenticationService - Depends on SecretStore
        m_authService = std::make_unique<AuthenticationService>(m_secretStore.get());
        ServiceLocator::registerService<AuthenticationService>(m_authService.get());
        
        // 3.1 LibraryService - Depends on AuthenticationService
        m_libraryService = std::make_unique<LibraryService>(m_authService.get());
        ServiceLocator::registerService<LibraryService>(m_libraryService.get());
        
        // 3.2 PlaybackService - Depends on AuthenticationService
        m_playbackService = std::make_unique<PlaybackService>(m_authService.get());
        ServiceLocator::registerService<PlaybackService>(m_playbackService.get());
        
        // 3.3 SeerrService - Depends on AuthenticationService + ConfigManager
        m_seerrService = std::make_unique<SeerrService>(m_authService.get(), m_configManager.get());
        ServiceLocator::registerService<SeerrService>(m_seerrService.get());
        
        // 4. PlayerController
        m_playerController = std::make_unique<PlayerController>(
            m_playerBackend.get(),
            m_configManager.get(),
            m_trackPreferencesManager.get(),
            m_displayManager.get(),
            m_playbackService.get(),
            m_libraryService.get(),
            m_authService.get()
        );
        ServiceLocator::registerService<PlayerController>(m_playerController.get());
        
        // 4.5 ThemeSongManager
        m_themeSongManager = std::make_unique<ThemeSongManager>(
            m_libraryService.get(),
            m_configManager.get(),
            m_playerController.get()
        );
        ServiceLocator::registerService<ThemeSongManager>(m_themeSongManager.get());
    }
    
    // 5. InputModeManager - Depends on QGuiApplication
    m_inputModeManager = std::make_unique<InputModeManager>(m_app);
    ServiceLocator::registerService<InputModeManager>(m_inputModeManager.get());
    
    // 6. LibraryViewModel
    m_libraryViewModel = std::make_unique<LibraryViewModel>();
    ServiceLocator::registerService<LibraryViewModel>(m_libraryViewModel.get());

    // 7. SeriesDetailsViewModel
    m_seriesDetailsViewModel = std::make_unique<SeriesDetailsViewModel>();
    ServiceLocator::registerService<SeriesDetailsViewModel>(m_seriesDetailsViewModel.get());

    // 7.5 MovieDetailsViewModel
    m_movieDetailsViewModel = std::make_unique<MovieDetailsViewModel>();
    ServiceLocator::registerService<MovieDetailsViewModel>(m_movieDetailsViewModel.get());

    // 8. SidebarSettings
    m_sidebarSettings = std::make_unique<SidebarSettings>();
    ServiceLocator::registerService<SidebarSettings>(m_sidebarSettings.get());

    // 9. UI Sound Controller
    m_uiSoundController = std::make_unique<UiSoundController>(m_configManager.get());
    ServiceLocator::registerService<UiSoundController>(m_uiSoundController.get());

    // 10. SessionManager - Depends on ConfigManager and SecretStore
    m_sessionManager = std::make_unique<SessionManager>(m_configManager.get(), m_secretStore.get());
    ServiceLocator::registerService<SessionManager>(m_sessionManager.get());

    // 11. SessionService - Depends on AuthenticationService
    if (isTestMode) {
        m_sessionService = std::make_unique<SessionService>(m_mockAuthService.get());
    } else {
        m_sessionService = std::make_unique<SessionService>(m_authService.get());
    }
    ServiceLocator::registerService<SessionService>(m_sessionService.get());
}

void ApplicationInitializer::initializeServices()
{
    // Check if we're in test mode
    bool isTestMode = TestModeController::instance()->isTestMode();
    
    // Get the appropriate auth service (real or mock)
    AuthenticationService* auth = isTestMode 
        ? m_mockAuthService.get()
        : m_authService.get();
    auto* config = m_configManager.get();
    
    // Connect authentication signals to persist/clear session
    connect(auth, &AuthenticationService::loginSuccess,
        [config, auth](const QString &userId, const QString &accessToken, const QString &username) {
            // Only update config on fresh login (username present)
            // During session restoration, username is empty and we don't want to overwrite config
            if (!username.isEmpty()) {
                // Store session without token (token is in SecretStore)
                config->setJellyfinSession(
                    auth->getServerUrl(),
                    userId,
                    "",  // Token now stored in SecretStore
                    username
                );
            }
    });

    connect(auth, &AuthenticationService::loggedOut,
        [config]() {
            config->clearJellyfinSession();
    });
    
    connect(auth, &AuthenticationService::sessionExpired,
        [auth]() {
            qWarning() << "Session expired, triggering logout";
            auth->logout();
    });
    
    connect(auth, &AuthenticationService::sessionExpiredAfterPlayback,
        [auth]() {
            qWarning() << "Session expired (detected during playback), triggering logout";
            auth->logout();
    });
    
    // Connect playback stopped to check for pending session expiry
    connect(m_playerController.get(), &PlayerController::playbackStopped,
        [auth]() {
            qDebug() << "Playback stopped, checking for pending session expiry";
            auth->checkPendingSessionExpiry();
    });
    
    // GpuMemoryTrimmer is created and wired by WindowManager::setup(), which runs
    // after service registration and owns the ImageCacheProvider dependency.
    
    // Session Restoration & Migration (skip in test mode - already authenticated)
    if (isTestMode) {
        // Initialize mock auth service with pre-authenticated state
        m_mockAuthService->initialize(m_configManager.get());
    } else {
        m_authService->initialize(m_configManager.get());
    }

    // Initialize SessionManager for device ID rotation checks
    m_sessionManager->initialize();
}
