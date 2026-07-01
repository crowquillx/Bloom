configure_file(config/version.h.in ${CMAKE_CURRENT_BINARY_DIR}/config/version.h)

include(GNUInstallDirs)

qt_add_executable(Bloom
    core/ApplicationInitializer.cpp
    ui/WindowManager.cpp
    ui/FontLoader.cpp
    utils/CacheMigrator.cpp
    utils/DetailViewCache.cpp
    main.cpp
    core/ServiceLocator.h
    core/ServiceLocator.cpp
    resources/fonts.qrc
    resources/sounds.qrc
    resources/images.qrc
    resources/mpv-shaders.qrc
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
    player/backend/MpvEmbeddedShaderUtils.h
    player/backend/MpvEmbeddedShaderUtils.cpp
    network/AuthenticationService.h
    network/AuthenticationService.cpp
    network/LibraryService.h
    network/LibraryService.cpp
    network/NextEpisodeResolver.h
    network/NextEpisodeResolver.cpp
    network/PlaybackService.h
    network/PlaybackService.cpp
    network/MediaSegmentProviderService.h
    network/MediaSegmentProviderService.cpp
    network/SeerrService.h
    network/SeerrService.cpp
    network/Types.h
    network/Types.cpp
    network/SessionManager.h
    network/SessionManager.cpp
    network/SessionService.h
    network/SessionService.cpp
    updates/UpdateTypes.h
    updates/IUpdateProvider.h
    updates/IUpdateProvider.cpp
    updates/GitHubReleaseUpdateProvider.h
    updates/GitHubReleaseUpdateProvider.cpp
    updates/IUpdateApplier.h
    updates/IUpdateApplier.cpp
    updates/WindowsNsisUpdateApplier.h
    updates/WindowsNsisUpdateApplier.cpp
    updates/UpdateService.h
    updates/UpdateService.cpp
    utils/ConfigManager.cpp
    utils/ConfigManager.h
    utils/AudioOutputRouter.cpp
    utils/AudioOutputRouter.h
    utils/MpvArgFilter.cpp
    utils/MpvArgFilter.h
    utils/DetailViewCache.h
    utils/TrackPreferencesManager.cpp
    utils/TrackPreferencesManager.h
    utils/LibraryCacheStore.cpp
    utils/LibraryCacheStore.h
    utils/InputModeManager.cpp
    utils/InputModeManager.h
    utils/InputBindingManager.cpp
    utils/InputBindingManager.h
    utils/DisplayManager.cpp
    utils/DisplayManager.h
    utils/SidebarSettings.h
    utils/SidebarSettings.cpp
    utils/SystemPowerController.h
    utils/SystemPowerController.cpp
    utils/GpuMemoryTrimmer.h
    utils/GpuMemoryTrimmer.cpp
    utils/Logger.h
    utils/Logger.cpp
    utils/LoggingConfig.h
    utils/LoggingConfig.cpp
    utils/BloomLogging.h
    utils/BloomLogging.cpp
    viewmodels/BaseViewModel.h
    viewmodels/BaseViewModel.cpp
    viewmodels/LibraryViewModel.h
    viewmodels/LibraryViewModel.cpp
    viewmodels/SeriesDetailsViewModel.h
    viewmodels/SeriesDetailsViewModel.cpp
    viewmodels/MovieDetailsViewModel.h
    viewmodels/MovieDetailsViewModel.cpp
    viewmodels/UpNextRecommendationsViewModel.h
    viewmodels/UpNextRecommendationsViewModel.cpp
    ui/ImageCacheProvider.h
    ui/ImageCacheProvider.cpp
    ui/UiSoundController.h
    ui/UiSoundController.cpp
    ui/ScreensaverController.h
    ui/ScreensaverController.cpp
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

if(WIN32)
    set_target_properties(Bloom PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

set_source_files_properties(ui/Theme.qml PROPERTIES
    QT_QML_SINGLETON_TYPE TRUE
)

set_source_files_properties(ui/Icons.qml PROPERTIES
    QT_QML_SINGLETON_TYPE TRUE
)

set_source_files_properties(ui/AccessibilityHelpers.qml PROPERTIES
    QT_QML_SINGLETON_TYPE TRUE
)

if(COMMAND qt_policy)
    qt_policy(SET QTP0004 NEW)
endif()

set_source_files_properties(ui/EpisodeSelection.js PROPERTIES
    QT_QML_SKIP_QMLDIR_ENTRY TRUE
)

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
        ui/AccessibilityHelpers.qml
        ui/A11yButton.qml
        ui/MetadataChip.qml
        ui/RatingMetadataChip.qml
        ui/PersonCard.qml
        ui/ScrollingCardLabel.qml
        ui/SecondaryActionButton.qml
        ui/SeriesDetailsView.qml
        ui/SeasonView.qml
        ui/SeriesSeasonEpisodeView.qml
        ui/MovieDetailsView.qml
        ui/HomeScreen.qml
        ui/HeroBanner.qml
        ui/HeroBannerLayoutHost.qml
        ui/HeroBannerProvider.qml
        ui/SettingsScreen.qml
        ui/settings/SettingsRail.qml
        ui/settings/SettingsToggleRow.qml
        ui/settings/SettingsSliderRow.qml
        ui/settings/SettingsTextInputRow.qml
        ui/settings/SettingsSpinBoxRow.qml
        ui/settings/SettingsComboBox.qml
        ui/settings/SettingsInfoRow.qml
        ui/settings/SettingsGroupDivider.qml
        ui/settings/PlaybackSettings.qml
        ui/settings/DisplaySettings.qml
        ui/settings/HomeSettings.qml
        ui/settings/VideoSettings.qml
        ui/settings/MpvSettings.qml
        ui/settings/IntegrationsSettings.qml
        ui/settings/InputSettings.qml
        ui/settings/AboutAccountSettings.qml
        ui/settings/NewProfileDialog.qml
        ui/settings/DeleteProfileDialog.qml
        ui/settings/RenameProfileDialog.qml
        ui/settings/ImportMpvConfigDialog.qml
        ui/SearchScreen.qml
        ui/MediaSourceSelectionDialog.qml
        ui/SeerrRequestDialog.qml
        ui/SeerrComboBox.qml
        ui/SearchResultCard.qml
        ui/UnwatchedBadge.qml
        ui/RetryButton.qml
        ui/Theme.qml
        ui/Icons.qml
        ui/Sidebar.qml
        ui/MediaProgressBar.qml
        ui/MpvProfileEditor.qml
        ui/VideoSurface.qml
        ui/EmbeddedPlaybackOverlay.qml
        ui/ScreensaverOverlay.qml
        ui/TrackSelector.qml
        ui/WheelStepScroller.qml
        ui/MediaInfoPanel.qml
        ui/ToastNotification.qml
        ui/UpNextScreen.qml
        ui/RoundedImage.qml
        ui/ActiveSessionsScreen.qml
        ui/EpisodeSelection.js
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

        find_library(BLOOM_LINUX_MPV_RUNTIME
            NAMES mpv libmpv.so.2 libmpv.so
            HINTS ${LIBMPV_LIBRARY_DIRS}
        )

        if(BLOOM_LINUX_MPV_RUNTIME AND BLOOM_BUNDLE_LIBMPV)
            add_custom_command(TARGET Bloom POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${BLOOM_LINUX_MPV_RUNTIME}
                    $<TARGET_FILE_DIR:Bloom>
                VERBATIM
            )

            install(FILES ${BLOOM_LINUX_MPV_RUNTIME}
                DESTINATION ${CMAKE_INSTALL_LIBDIR}/bloom
            )

            # Ensure bundled runtime libs are discoverable from bin/Bloom.
            set_property(TARGET Bloom APPEND PROPERTY BUILD_RPATH "$ORIGIN")
            set_property(TARGET Bloom APPEND PROPERTY INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}/bloom")
        elseif(BLOOM_BUNDLE_LIBMPV)
            message(WARNING "libmpv runtime library path not found; Linux embedded backend may fail on systems without system libmpv.")
        endif()
    else()
        message(STATUS "libmpv development package not found; Linux embedded backend remains scaffold-only")
    endif()

    if(SDL2_FOUND)
        target_include_directories(Bloom PRIVATE ${SDL2_INCLUDE_DIRS})
        target_link_libraries(Bloom PRIVATE ${SDL2_LIBRARIES})
        target_compile_definitions(Bloom PRIVATE BLOOM_HAS_SDL=1)
    else()
        message(STATUS "SDL2 development package not found; gamepad input backend disabled")
    endif()
endif()

if(WIN32)
    target_sources(Bloom PRIVATE
        player/backend/WindowsMpvBackend.h
        player/backend/WindowsMpvBackend.cpp
    )

    if(SDL2_FOUND)
        if(TARGET SDL2::SDL2)
            target_link_libraries(Bloom PRIVATE SDL2::SDL2)
            set(_bloom_sdl2_imported_configs ${CMAKE_BUILD_TYPE})
            get_target_property(_bloom_sdl2_target_imported_configs SDL2::SDL2 IMPORTED_CONFIGURATIONS)
            if(_bloom_sdl2_target_imported_configs)
                list(APPEND _bloom_sdl2_imported_configs ${_bloom_sdl2_target_imported_configs})
            endif()
            if(CMAKE_CONFIGURATION_TYPES)
                list(APPEND _bloom_sdl2_imported_configs ${CMAKE_CONFIGURATION_TYPES})
            endif()
            list(APPEND _bloom_sdl2_imported_configs DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
            list(REMOVE_DUPLICATES _bloom_sdl2_imported_configs)
            get_target_property(BLOOM_SDL2_RUNTIME_DLL SDL2::SDL2 IMPORTED_LOCATION)
            if(NOT BLOOM_SDL2_RUNTIME_DLL)
                foreach(_bloom_sdl2_config IN LISTS _bloom_sdl2_imported_configs)
                    if(BLOOM_SDL2_RUNTIME_DLL OR NOT _bloom_sdl2_config)
                        continue()
                    endif()
                    string(TOUPPER "${_bloom_sdl2_config}" _bloom_sdl2_config_upper)
                    get_target_property(BLOOM_SDL2_RUNTIME_DLL SDL2::SDL2 IMPORTED_LOCATION_${_bloom_sdl2_config_upper})
                endforeach()
            endif()
            get_target_property(_bloom_sdl2_import_lib SDL2::SDL2 IMPORTED_IMPLIB)
            if(NOT _bloom_sdl2_import_lib)
                foreach(_bloom_sdl2_config IN LISTS _bloom_sdl2_imported_configs)
                    if(_bloom_sdl2_import_lib OR NOT _bloom_sdl2_config)
                        continue()
                    endif()
                    string(TOUPPER "${_bloom_sdl2_config}" _bloom_sdl2_config_upper)
                    get_target_property(_bloom_sdl2_import_lib SDL2::SDL2 IMPORTED_IMPLIB_${_bloom_sdl2_config_upper})
                endforeach()
            endif()
        elseif(TARGET SDL2::SDL2-static)
            target_link_libraries(Bloom PRIVATE SDL2::SDL2-static)
        else()
            target_include_directories(Bloom PRIVATE ${SDL2_INCLUDE_DIRS})
            target_link_libraries(Bloom PRIVATE ${SDL2_LIBRARIES})
        endif()
            target_compile_definitions(Bloom PRIVATE BLOOM_HAS_SDL=1)

        set(_bloom_sdl2_library_hint ${SDL2_LIBRARY})
        if(NOT _bloom_sdl2_library_hint)
            set(_bloom_sdl2_library_hint ${_bloom_sdl2_import_lib})
        endif()

        if(NOT BLOOM_SDL2_RUNTIME_DLL AND _bloom_sdl2_library_hint)
            get_filename_component(_bloom_sdl2_lib_dir ${_bloom_sdl2_library_hint} DIRECTORY)
            get_filename_component(_bloom_sdl2_root ${_bloom_sdl2_lib_dir} DIRECTORY)
            find_file(BLOOM_SDL2_RUNTIME_DLL
                NAMES SDL2.dll
                HINTS
                    ${_bloom_sdl2_root}
                    $ENV{SDL2_ROOT}
                    $ENV{SDL2_DIR}
                PATH_SUFFIXES
                    bin
                    lib
                    lib/x64
                    lib/x86
            )
        endif()

        if(BLOOM_SDL2_RUNTIME_DLL AND BLOOM_SDL2_RUNTIME_DLL MATCHES "\\.dll$")
            add_custom_command(TARGET Bloom POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${BLOOM_SDL2_RUNTIME_DLL}
                    $<TARGET_FILE_DIR:Bloom>
                VERBATIM
            )
            install(FILES ${BLOOM_SDL2_RUNTIME_DLL} DESTINATION ${CMAKE_INSTALL_BINDIR})
        elseif(NOT TARGET SDL2::SDL2-static)
            message(WARNING "SDL2 runtime DLL not found; gamepad input may fail at runtime.")
        endif()
    else()
        message(STATUS "SDL2 development package not found; gamepad input backend disabled")
    endif()

    find_path(BLOOM_MPV_INCLUDE_DIR
        NAMES mpv/client.h
        HINTS
            $ENV{MPV_ROOT}
            $ENV{MPV_DIR}
            $ENV{LIBMPV_ROOT}
        PATH_SUFFIXES
            include
    )

    find_library(BLOOM_MPV_LIBRARY
        NAMES mpv mpv-2 libmpv libmpv-2
        HINTS
            $ENV{MPV_ROOT}
            $ENV{MPV_DIR}
            $ENV{LIBMPV_ROOT}
        PATH_SUFFIXES
            lib
            lib64
            x86_64
            libs
            bin
    )

    if(BLOOM_MPV_INCLUDE_DIR AND BLOOM_MPV_LIBRARY)
        target_include_directories(Bloom PRIVATE ${BLOOM_MPV_INCLUDE_DIR})
        target_link_libraries(Bloom PRIVATE ${BLOOM_MPV_LIBRARY})
        target_compile_definitions(Bloom PRIVATE BLOOM_HAS_LIBMPV=1)

        get_filename_component(_bloom_mpv_lib_dir ${BLOOM_MPV_LIBRARY} DIRECTORY)
        get_filename_component(_bloom_mpv_root ${_bloom_mpv_lib_dir} DIRECTORY)

        find_file(BLOOM_MPV_RUNTIME_DLL
            NAMES mpv-2.dll libmpv-2.dll mpv.dll
            HINTS
                ${_bloom_mpv_root}
                $ENV{MPV_ROOT}
                $ENV{MPV_DIR}
                $ENV{LIBMPV_ROOT}
            PATH_SUFFIXES
                bin
                lib
        )

        if(BLOOM_MPV_RUNTIME_DLL)
            add_custom_command(TARGET Bloom POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${BLOOM_MPV_RUNTIME_DLL}
                    $<TARGET_FILE_DIR:Bloom>
                VERBATIM
            )
            install(FILES ${BLOOM_MPV_RUNTIME_DLL} DESTINATION ${CMAKE_INSTALL_BINDIR})
        else()
            message(WARNING "libmpv runtime DLL not found; direct embedded backend may fail at runtime.")
        endif()
    else()
        message(WARNING "Windows libmpv SDK not found (headers/libs). Set MPV_ROOT (or MPV_DIR/LIBMPV_ROOT) to enable direct embedded backend.")
    endif()
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
target_include_directories(Bloom PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

qt6_add_translations(Bloom
    TS_FILES resources/translations/Bloom_en.ts
    QM_FILES_OUTPUT_VARIABLE qm_files
)
install(FILES ${qm_files} DESTINATION ${CMAKE_INSTALL_DATADIR}/bloom/translations)

if (TARGET Qt6::MultimediaQuick)
    target_link_libraries(Bloom PRIVATE Qt6::MultimediaQuick)
endif()

# Platform-specific secure storage libraries
if(UNIX AND NOT APPLE)
    # Linux: link libsecret
    target_include_directories(Bloom PRIVATE ${LIBSECRET_INCLUDE_DIRS})
    target_link_libraries(Bloom PRIVATE ${LIBSECRET_LIBRARIES})
elseif(WIN32)
    # Windows: link advapi32 for Credential Manager API and shell32 for elevated updater launch.
    target_link_libraries(Bloom PRIVATE advapi32 shell32)
endif()


# Copy config file to build directory for easy access during development
file(COPY ${CMAKE_SOURCE_DIR}/config/app.example.json DESTINATION ${CMAKE_BINARY_DIR}/config)

install(TARGETS Bloom
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    BUNDLE DESTINATION .
)

if(UNIX AND NOT APPLE)
    install(FILES resources/linux/com.github.crowquillx.Bloom.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
    )
    install(FILES resources/images/app/logo_trans.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
        RENAME com.github.crowquillx.Bloom.svg
    )
    install(FILES resources/linux/com.github.crowquillx.Bloom.metainfo.xml
        DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo
    )
endif()
