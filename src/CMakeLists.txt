qt_add_executable(Bloom
    core/ApplicationInitializer.cpp
    ui/WindowManager.cpp
    ui/FontLoader.cpp
    utils/CacheMigrator.cpp
    main.cpp
    core/ServiceLocator.h
    core/ServiceLocator.cpp
    resources/fonts.qrc
    resources/scripts.qrc
    resources/sounds.qrc
    resources/images.qrc
    player/PlayerProcessManager.cpp
    player/PlayerController.cpp
    player/ThemeSongManager.cpp
    player/TrickplayProcessor.cpp
    player/TrickplayProcessor.h
    network/AuthenticationService.h
    network/AuthenticationService.cpp
    network/LibraryService.h
    network/LibraryService.cpp
    network/PlaybackService.h
    network/PlaybackService.cpp
    network/Types.h
    network/Types.cpp
    network/SessionManager.h
    network/SessionManager.cpp
    network/SessionService.h
    network/SessionService.cpp
    utils/ConfigManager.cpp
    utils/ConfigManager.h
    utils/TrackPreferencesManager.cpp
    utils/TrackPreferencesManager.h
    utils/LibraryCacheStore.cpp
    utils/LibraryCacheStore.h
    utils/InputModeManager.cpp
    utils/InputModeManager.h
    utils/DisplayManager.cpp
    utils/DisplayManager.h
    utils/SidebarSettings.h
    utils/SidebarSettings.cpp
    utils/GpuMemoryTrimmer.h
    utils/GpuMemoryTrimmer.cpp
    utils/Logger.h
    utils/Logger.cpp
    viewmodels/BaseViewModel.h
    viewmodels/BaseViewModel.cpp
    viewmodels/LibraryViewModel.h
    viewmodels/LibraryViewModel.cpp
    viewmodels/SeriesDetailsViewModel.h
    viewmodels/SeriesDetailsViewModel.cpp
    viewmodels/MovieDetailsViewModel.h
    viewmodels/MovieDetailsViewModel.cpp
    ui/ImageCacheProvider.h
    ui/ImageCacheProvider.cpp
    ui/UiSoundController.h
    ui/UiSoundController.cpp
    security/ISecretStore.h
    security/SecretStoreFactory.h
    security/SecretStoreFactory.cpp
    security/SecretStoreLinux.h
    security/SecretStoreLinux.cpp
    security/SecretStoreWindows.h
    security/SecretStoreWindows.cpp
)


set_source_files_properties(ui/Theme.qml PROPERTIES
    QT_QML_SINGLETON_TYPE TRUE
)

set_source_files_properties(ui/Icons.qml PROPERTIES
    QT_QML_SINGLETON_TYPE TRUE
)

qt_policy(SET QTP0004 NEW)

qt6_add_shaders(Bloom "bloom_shaders"
    PREFIX /shaders
    OUTPUTS
        rounded_image.frag.qsb
    FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/shaders/rounded_image.frag
)

qt_add_qml_module(Bloom
    URI BloomUI
    VERSION 1.0
    RESOURCE_PREFIX /
    NO_CACHEGEN
    QML_FILES
        ui/Main.qml
        ui/LoginScreen.qml
        ui/LibraryScreen.qml
        ui/SeriesDetailsView.qml
        ui/SeasonView.qml
        ui/SeriesSeasonEpisodeView.qml
        ui/MovieDetailsView.qml
        ui/HomeScreen.qml
        ui/SettingsScreen.qml
        ui/SearchScreen.qml
        ui/SearchResultCard.qml
        ui/UnwatchedBadge.qml
        ui/RetryButton.qml
        ui/Theme.qml
        ui/Icons.qml
        ui/Sidebar.qml
        ui/MediaProgressBar.qml
        ui/MpvProfileEditor.qml
        ui/TrackSelector.qml
        ui/MediaInfoPanel.qml
        ui/ToastNotification.qml
        ui/RoundedImage.qml
        ui/ActiveSessionsScreen.qml
)

target_link_libraries(Bloom
    PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Network
    Qt6::Sql
    Qt6::Concurrent
    Qt6::Multimedia

)

qt6_add_translations(Bloom
    TS_FILES resources/translations/Bloom_en.ts
    QM_FILES_OUTPUT_VARIABLE qm_files
)
install(FILES ${qm_files} DESTINATION ${CMAKE_BINARY_DIR}/translations)

if (TARGET Qt6::MultimediaQuick)
    target_link_libraries(Bloom PRIVATE Qt6::MultimediaQuick)
endif()

# Platform-specific secure storage libraries
if(UNIX AND NOT APPLE)
    # Linux: link libsecret
    target_include_directories(Bloom PRIVATE ${LIBSECRET_INCLUDE_DIRS})
    target_link_libraries(Bloom PRIVATE ${LIBSECRET_LIBRARIES})
elseif(WIN32)
    # Windows: link advapi32 for Credential Manager API
    target_link_libraries(Bloom PRIVATE advapi32)
endif()


# Copy config file to build directory for easy access during development
file(COPY ${CMAKE_SOURCE_DIR}/config/app.example.json DESTINATION ${CMAKE_BINARY_DIR}/config)

install(TARGETS Bloom
    RUNTIME DESTINATION bin
    BUNDLE DESTINATION .
)
