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
    player/MpvVideoItem.h
    player/MpvVideoItem.cpp
    player/ThemeSongManager.cpp
    player/TrickplayProcessor.cpp
    player/TrickplayProcessor.h
    player/backend/IPlayerBackend.h
    player/backend/ExternalMpvBackend.h
    player/backend/ExternalMpvBackend.cpp
    player/backend/PlayerBackendFactory.h
    player/backend/PlayerBackendFactory.cpp
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
    ui/ResponsiveLayoutManager.h
    ui/ResponsiveLayoutManager.cpp
    security/ISecretStore.h
    security/SecretStoreFactory.h
    security/SecretStoreFactory.cpp
    security/SecretStoreLinux.h
    security/SecretStoreLinux.cpp
    security/SecretStoreWindows.h
    security/SecretStoreWindows.cpp
    test/TestModeController.h
    test/TestModeController.cpp
    test/MockAuthenticationService.h
    test/MockAuthenticationService.cpp
    test/MockLibraryService.h
    test/MockLibraryService.cpp
    resources/windows-icon.rc
)


set_source_files_properties(ui/Theme.qml PROPERTIES
    QT_QML_SINGLETON_TYPE TRUE
)

set_source_files_properties(ui/Icons.qml PROPERTIES
    QT_QML_SINGLETON_TYPE TRUE
)

if(COMMAND qt_policy)
    qt_policy(SET QTP0004 NEW)
endif()

set_source_files_properties(ui/TrackUtils.js PROPERTIES
    QT_QML_SKIP_QMLDIR_ENTRY TRUE
)

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
        ui/VideoSurface.qml
        ui/TrackSelector.qml
        ui/MediaInfoPanel.qml
        ui/ToastNotification.qml
        ui/RoundedImage.qml
        ui/ActiveSessionsScreen.qml
        ui/TrackUtils.js
)

if(UNIX AND NOT APPLE)
    target_sources(Bloom PRIVATE
        player/backend/LinuxMpvBackend.h
        player/backend/LinuxMpvBackend.cpp
    )

    if(COMMAND pkg_check_modules)
        pkg_check_modules(LIBMPV QUIET mpv)
    endif()

    if(LIBMPV_FOUND)
        target_include_directories(Bloom PRIVATE ${LIBMPV_INCLUDE_DIRS})
        target_link_libraries(Bloom PRIVATE ${LIBMPV_LIBRARIES})
        target_compile_definitions(Bloom PRIVATE BLOOM_HAS_LIBMPV=1)
    else()
        message(STATUS "libmpv development package not found; Linux embedded backend remains scaffold-only")
    endif()
endif()

if(WIN32)
    target_sources(Bloom PRIVATE
        player/backend/WindowsMpvBackend.h
        player/backend/WindowsMpvBackend.cpp
    )
endif()

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

target_include_directories(Bloom PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/player)

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

if(UNIX AND NOT APPLE)
    install(FILES resources/linux/com.github.bloom.desktop
        DESTINATION ${CMAKE_INSTALL_PREFIX}/share/applications
    )
    install(FILES resources/images/app/logo_trans.svg
        DESTINATION ${CMAKE_INSTALL_PREFIX}/share/icons/hicolor/scalable/apps
        RENAME com.github.bloom.svg
    )
endif()
