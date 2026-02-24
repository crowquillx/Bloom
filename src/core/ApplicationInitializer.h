#pragma once

#include <QObject>
#include <memory>

class QGuiApplication;
class ConfigManager;
class DisplayManager;
class ResponsiveLayoutManager;
class TrackPreferencesManager;
class IPlayerBackend;
class SecretStore;
class AuthenticationService;
class LibraryService;
class PlaybackService;
class SeerrService;
class PlayerController;
class ThemeSongManager;
class InputModeManager;
class LibraryViewModel;
class SeriesDetailsViewModel;
class MovieDetailsViewModel;
class ISecretStore;
class SidebarSettings;
class UiSoundController;
class SessionManager;
class SessionService;
class MockAuthenticationService;
class MockLibraryService;

/**
 * @brief Owns and wires all application-level services during startup.
 *
 * ApplicationInitializer is constructed once in main() and drives the two-phase
 * startup sequence:
 *
 *  1. registerServices() — creates every service, registers it with ServiceLocator,
 *     and performs any required service-to-service wiring that can happen before the
 *     QML engine exists.  In test mode, real network services are replaced with mocks.
 *
 *  2. initializeServices() — connects cross-service signals (session persistence,
 *     playback-stopped checks, GPU trimming) and kicks off session restoration.
 *
 * All services are owned via std::unique_ptr and are destroyed when the initializer
 * goes out of scope, which also clears the ServiceLocator registry.
 */
class ApplicationInitializer : public QObject
{
    Q_OBJECT

public:
    /** @brief Constructs the initializer; does not create any services yet. */
    explicit ApplicationInitializer(QGuiApplication *app, QObject *parent = nullptr);

    /** @brief Destroys all owned services and clears the ServiceLocator. */
    ~ApplicationInitializer();

    /**
     * @brief Phase 1: instantiates every service and registers it with ServiceLocator.
     *
     * Services are created in dependency order (ConfigManager first, then auth,
     * network services, view-models, etc.).  In test mode the real AuthenticationService
     * and LibraryService are replaced by mock implementations loaded from fixture JSON.
     */
    void registerServices();

    /**
     * @brief Phase 2: connects cross-service signals and starts session restoration.
     *
     * Should be called after registerServices() and after WindowManager::setup() so
     * that all QML context properties are available when session restoration triggers
     * UI transitions.
     */
    void initializeServices();
    
    /**
     * @brief Returns the ConfigManager instance.
     *
     * Provided as a direct accessor for WindowManager which needs it before the
     * ServiceLocator is fully populated.  Prefer ServiceLocator::get<ConfigManager>()
     * elsewhere.
     */
    ConfigManager* configManager() { return m_configManager.get(); }

private:
    QGuiApplication *m_app;
    
    // Service ownership
    std::unique_ptr<ConfigManager> m_configManager;
    std::unique_ptr<DisplayManager> m_displayManager;
    std::unique_ptr<TrackPreferencesManager> m_trackPreferencesManager;
    std::unique_ptr<IPlayerBackend> m_playerBackend;
    // ISecretStore — owned here, raw pointer passed to AuthenticationService
    std::unique_ptr<ISecretStore> m_secretStore;
    
    std::unique_ptr<AuthenticationService> m_authService;
    std::unique_ptr<LibraryService> m_libraryService;
    std::unique_ptr<PlaybackService> m_playbackService;
    std::unique_ptr<SeerrService> m_seerrService;
    std::unique_ptr<PlayerController> m_playerController;
    std::unique_ptr<ThemeSongManager> m_themeSongManager;
    std::unique_ptr<InputModeManager> m_inputModeManager;
    std::unique_ptr<LibraryViewModel> m_libraryViewModel;
    std::unique_ptr<SeriesDetailsViewModel> m_seriesDetailsViewModel;
    std::unique_ptr<MovieDetailsViewModel> m_movieDetailsViewModel;
    std::unique_ptr<SidebarSettings> m_sidebarSettings;
    std::unique_ptr<UiSoundController> m_uiSoundController;
    std::unique_ptr<SessionManager> m_sessionManager;
    std::unique_ptr<SessionService> m_sessionService;
    std::unique_ptr<ResponsiveLayoutManager> m_responsiveLayoutManager;
    
    // Test mode mock services
    std::unique_ptr<MockAuthenticationService> m_mockAuthService;
    std::unique_ptr<MockLibraryService> m_mockLibraryService;
};
