#include "ApplicationInitializer.h"
#include "ServiceLocator.h"
#include "utils/ConfigManager.h"
#include "profiles/BloomProfileRepository.h"
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
#include "network/HttpTransport.h"
#include "network/LibraryService.h"
#include "network/PlaybackService.h"
#include "network/MediaSegmentProviderService.h"
#include "network/SeerrService.h"
#include "utils/InputModeManager.h"
#include "utils/InputBindingManager.h"
#include "viewmodels/LibraryViewModel.h"
#include "viewmodels/SeriesDetailsViewModel.h"
#include "viewmodels/MovieDetailsViewModel.h"
#include "viewmodels/UpNextRecommendationsViewModel.h"
#include "utils/SidebarSettings.h"
#include "utils/SystemPowerController.h"
#include "ui/UiSoundController.h"
#include "ui/ScreensaverController.h"
#include "utils/GpuMemoryTrimmer.h"
#include "utils/Logger.h"
#include "utils/LoggingConfig.h"
#include "utils/BloomLogging.h"
#include "network/SessionManager.h"
#include "network/SessionService.h"
#include "providers/IProviderAdapter.h"
#include "providers/jellyfin/JellyfinProviderAdapter.h"
#include "updates/UpdateService.h"
#include "test/TestModeController.h"
#include "test/MockAuthenticationService.h"
#include "test/MockLibraryService.h"

#include <QGuiApplication>
#include <QDebug>
#include <cstdio>

/**
 * @brief Qt message handler that forwards all Qt diagnostic output to the custom Logger.
 *
 * Installed via qInstallMessageHandler() during registerServices().  A thread-local
 * re-entrancy guard (inQtMessageHandler) prevents deadlock when Logger itself emits
 * Qt diagnostics; recursive calls fall back directly to stderr.
 *
 * @param type    Severity level from Qt's messaging system.
 * @param context Source location and category metadata provided by Qt.
 * @param msg     The formatted message string.
 */
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

ApplicationInitializer::ApplicationInitializer(QGuiApplication *app,
                                               bool consoleOutputEnabled,
                                               bool verboseLogging,
                                               QObject *parent)
    : QObject(parent)
    , m_app(app)
    , m_consoleOutputEnabled(consoleOutputEnabled)
    , m_verboseLogging(verboseLogging)
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
        qCWarning(lcApp) << "Failed to initialize Logger, falling back to console output";
    }

    // 1. ConfigManager - No dependencies, must be first to load settings
    m_configManager = std::make_unique<ConfigManager>();
    ServiceLocator::registerService<ConfigManager>(m_configManager.get());

    // Load configuration early so logging and downstream services can read settings
    m_configManager->load();

    // 1.1 BloomProfileRepository - Depends on ConfigManager (loads after config)
    m_bloomProfileRepository = std::make_unique<BloomProfileRepository>(m_configManager.get());
    ServiceLocator::registerService<BloomProfileRepository>(m_bloomProfileRepository.get());

    LoggingConfig::apply(
        LoggingConfig::levelFromString(m_configManager->getLogLevel()),
        m_configManager->getQtLoggingRules(),
        m_verboseLogging);

    Logger::instance().setConsoleOutputEnabled(m_consoleOutputEnabled);
    // Install Qt message handler to route all qDebug/qWarning/etc through our Logger
    qInstallMessageHandler(qtMessageHandler);

    // 1.5 DisplayManager - Depends on ConfigManager
    m_displayManager = std::make_unique<DisplayManager>(m_configManager.get());
    ServiceLocator::registerService<DisplayManager>(m_displayManager.get());
    
    // 1.6 ResponsiveLayoutManager - No dependencies (uses QGuiApplication::primaryScreen)
    m_responsiveLayoutManager = std::make_unique<ResponsiveLayoutManager>();
    ServiceLocator::registerService<ResponsiveLayoutManager>(m_responsiveLayoutManager.get());
    
    // 1.7 TrackPreferencesManager - connection-scoped through ConfigManager
    m_trackPreferencesManager = std::make_unique<TrackPreferencesManager>(m_configManager.get());
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
        m_mediaSegmentProviderService = std::make_unique<MediaSegmentProviderService>(m_mockAuthService.get(), m_configManager.get());
        ServiceLocator::registerService<MediaSegmentProviderService>(m_mediaSegmentProviderService.get());
        m_playbackService = std::make_unique<PlaybackService>(m_mockAuthService.get(),
                                                              m_configManager.get(),
                                                              m_mediaSegmentProviderService.get());
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
        
        // 3. Provider-neutral transport and Jellyfin wire adapters
        m_httpTransport = std::make_unique<HttpTransport>();
        m_providerAdapter = std::make_unique<JellyfinProviderAdapter>();

        // 3.1 AuthenticationService - stable façade over provider boundaries
        m_authService = std::make_unique<AuthenticationService>(
            m_secretStore.get(),
            m_httpTransport.get(),
            m_providerAdapter.get());
        ServiceLocator::registerService<AuthenticationService>(m_authService.get());
        
        // 3.2 LibraryService - Depends on AuthenticationService
        m_libraryService = std::make_unique<LibraryService>(m_authService.get());
        ServiceLocator::registerService<LibraryService>(m_libraryService.get());
        
        // 3.3 PlaybackService - Depends on AuthenticationService
        m_mediaSegmentProviderService = std::make_unique<MediaSegmentProviderService>(m_authService.get(), m_configManager.get());
        ServiceLocator::registerService<MediaSegmentProviderService>(m_mediaSegmentProviderService.get());
        m_playbackService = std::make_unique<PlaybackService>(m_authService.get(),
                                                              m_configManager.get(),
                                                              m_mediaSegmentProviderService.get());
        ServiceLocator::registerService<PlaybackService>(m_playbackService.get());
        
        // 3.4 SeerrService - Depends on AuthenticationService + ConfigManager
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

    // 5.1 InputBindingManager - Depends on QGuiApplication and ConfigManager
    m_inputBindingManager = std::make_unique<InputBindingManager>(m_app, m_configManager.get());
    ServiceLocator::registerService<InputBindingManager>(m_inputBindingManager.get());

    // 5.5 ScreensaverController - Depends on app input, config, playback, and auth
    if (isTestMode) {
        m_screensaverController = std::make_unique<ScreensaverController>(
            m_app,
            m_configManager.get(),
            m_playerController.get(),
            m_mockAuthService.get());
    } else {
        m_screensaverController = std::make_unique<ScreensaverController>(
            m_app,
            m_configManager.get(),
            m_playerController.get(),
            m_authService.get());
    }
    ServiceLocator::registerService<ScreensaverController>(m_screensaverController.get());
    
    // 6. LibraryViewModel
    m_libraryViewModel = std::make_unique<LibraryViewModel>();
    ServiceLocator::registerService<LibraryViewModel>(m_libraryViewModel.get());

    // 7. SeriesDetailsViewModel
    m_seriesDetailsViewModel = std::make_unique<SeriesDetailsViewModel>();
    ServiceLocator::registerService<SeriesDetailsViewModel>(m_seriesDetailsViewModel.get());

    // 7.5 MovieDetailsViewModel
    m_movieDetailsViewModel = std::make_unique<MovieDetailsViewModel>();
    ServiceLocator::registerService<MovieDetailsViewModel>(m_movieDetailsViewModel.get());

    // 7.6 UpNextRecommendationsViewModel
    m_upNextRecommendationsViewModel = std::make_unique<UpNextRecommendationsViewModel>();
    ServiceLocator::registerService<UpNextRecommendationsViewModel>(m_upNextRecommendationsViewModel.get());

    // 8. SidebarSettings
    m_sidebarSettings = std::make_unique<SidebarSettings>();
    ServiceLocator::registerService<SidebarSettings>(m_sidebarSettings.get());

    // 9. UI Sound Controller
    m_uiSoundController = std::make_unique<UiSoundController>(m_configManager.get());
    ServiceLocator::registerService<UiSoundController>(m_uiSoundController.get());

    // 9.5 System power/controller actions exposed to the home power menu
    m_systemPowerController = std::make_unique<SystemPowerController>(m_configManager.get());
    ServiceLocator::registerService<SystemPowerController>(m_systemPowerController.get());

    // 10. SessionManager - Depends on ConfigManager and SecretStore
    m_sessionManager = std::make_unique<SessionManager>(m_configManager.get(), m_secretStore.get());
    ServiceLocator::registerService<SessionManager>(m_sessionManager.get());

    // 10.5 UpdateService - Depends on ConfigManager and PlayerController
    m_updateService = std::make_unique<UpdateService>(m_configManager.get(), m_playerController.get());
    ServiceLocator::registerService<UpdateService>(m_updateService.get());

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
    
    // AuthenticationService persists successful production logins. Keep session
    // clearing centralized here so real and mock services share logout behavior.
    connect(auth, &AuthenticationService::loggedOut,
        [config]() {
            config->clearJellyfinSession();
    });
    connect(auth, &AuthenticationService::loggedOut, this, [this]() {
        if (m_seriesDetailsViewModel) {
            m_seriesDetailsViewModel->clear();
        }
        if (m_movieDetailsViewModel) {
            m_movieDetailsViewModel->clear();
        }
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
    
    // Session restoration and legacy credential migration must finish before
    // SessionManager can rotate the device ID or touch the same secret store.
    if (isTestMode) {
        m_mockAuthService->initialize(m_configManager.get());
        m_sessionManager->initialize();
    } else {
        m_authService->initialize(m_configManager.get());
        if (m_authService->isRestoringSession()) {
            connect(m_authService.get(), &AuthenticationService::isRestoringSessionChanged,
                    this, [this]() {
                if (!m_authService->isRestoringSession()) {
                    m_sessionManager->initialize();
                }
            });
        } else {
            m_sessionManager->initialize();
        }
    }
}
