#pragma once

#include <QObject>
#include <memory>

class QGuiApplication;
class ConfigManager;
class DisplayManager;
class TrackPreferencesManager;
class PlayerProcessManager;
class SecretStore; // interface?
class AuthenticationService;
class LibraryService;
class PlaybackService;
class PlayerController;
class ThemeSongManager;
class InputModeManager;
class LibraryViewModel;
class SeriesDetailsViewModel;
class ISecretStore;
class SidebarSettings;
class UiSoundController;

class ApplicationInitializer : public QObject
{
    Q_OBJECT

public:
    explicit ApplicationInitializer(QGuiApplication *app, QObject *parent = nullptr);
    ~ApplicationInitializer();

    void registerServices();
    void initializeServices();
    
    // Accessors for services that might be needed by WindowManager directly (though ServiceLocator is preferred)
    ConfigManager* configManager() { return m_configManager.get(); }

private:
    QGuiApplication *m_app;
    
    // Service ownership
    std::unique_ptr<ConfigManager> m_configManager;
    std::unique_ptr<DisplayManager> m_displayManager;
    std::unique_ptr<TrackPreferencesManager> m_trackPreferencesManager;
    std::unique_ptr<PlayerProcessManager> m_playerProcessManager;
    // SecretStoreFactory returns a unique_ptr, wait, original code: auto secretStore = SecretStoreFactory::create();
    // AuthenticationService takes a raw pointer to it.
    // I need to store the secret store. Assuming ISecretStore interface or similar.
    // Check main.cpp again, it just says auto secretStore.
    // I'll use a forward decl for the interface if possible, or include headers if I must.
    std::unique_ptr<ISecretStore> m_secretStore;
    
    std::unique_ptr<AuthenticationService> m_authService;
    std::unique_ptr<LibraryService> m_libraryService;
    std::unique_ptr<PlaybackService> m_playbackService;
    std::unique_ptr<PlayerController> m_playerController;
    std::unique_ptr<ThemeSongManager> m_themeSongManager;
    std::unique_ptr<InputModeManager> m_inputModeManager;
    std::unique_ptr<LibraryViewModel> m_libraryViewModel;
    std::unique_ptr<SeriesDetailsViewModel> m_seriesDetailsViewModel;
    std::unique_ptr<SidebarSettings> m_sidebarSettings;
    std::unique_ptr<UiSoundController> m_uiSoundController;
};
