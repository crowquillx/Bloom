enable_testing()

add_executable(BaseViewModelTest
    BaseViewModelTest.cpp
    ${CMAKE_SOURCE_DIR}/src/viewmodels/BaseViewModel.cpp
)

target_include_directories(BaseViewModelTest PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(BaseViewModelTest
    PRIVATE
        Qt6::Core
        Qt6::Concurrent
        Qt6::Test
)

add_test(NAME BaseViewModelTest COMMAND BaseViewModelTest)

# Series/Season cache tests
add_executable(SeriesDetailsCacheTest
    SeriesDetailsCacheTest.cpp
    ${CMAKE_SOURCE_DIR}/src/viewmodels/SeriesDetailsViewModel.cpp
    ${CMAKE_SOURCE_DIR}/src/viewmodels/BaseViewModel.cpp
    ${CMAKE_SOURCE_DIR}/src/network/LibraryService.cpp
    ${CMAKE_SOURCE_DIR}/src/network/AuthenticationService.cpp
    ${CMAKE_SOURCE_DIR}/src/network/Types.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/ConfigManager.cpp
)

target_include_directories(SeriesDetailsCacheTest PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(SeriesDetailsCacheTest
    PRIVATE
        Qt6::Core
        Qt6::Network
        Qt6::Concurrent
        Qt6::Test
)

add_test(NAME SeriesDetailsCacheTest COMMAND SeriesDetailsCacheTest)

# Library cache store tests
add_executable(LibraryCacheStoreTest
    LibraryCacheStoreTest.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/LibraryCacheStore.cpp
)

target_include_directories(LibraryCacheStoreTest PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(LibraryCacheStoreTest
    PRIVATE
        Qt6::Core
        Qt6::Sql
        Qt6::Test
)

add_test(NAME LibraryCacheStoreTest COMMAND LibraryCacheStoreTest)

# ============================================================================
# Visual Regression Test Fixtures
# ============================================================================
# Copy test fixtures to build directory for runtime access
# These files are used by visual regression tests for deterministic test data

set(TEST_FIXTURES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
set(TEST_FIXTURES_DEST "${CMAKE_CURRENT_BINARY_DIR}/fixtures")

# Copy JSON fixture
configure_file(
    "${TEST_FIXTURES_DIR}/test_library.json"
    "${TEST_FIXTURES_DEST}/test_library.json"
    COPYONLY
)

# Copy placeholder image
configure_file(
    "${TEST_FIXTURES_DIR}/test_images/placeholder.svg"
    "${TEST_FIXTURES_DEST}/test_images/placeholder.svg"
    COPYONLY
)

# Create a custom target to copy all fixtures (useful for IDEs)
add_custom_target(copy_test_fixtures
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${TEST_FIXTURES_DIR}"
        "${TEST_FIXTURES_DEST}"
    COMMENT "Copying test fixtures to build directory"
)

# Define a path variable for tests to reference fixtures
set(TEST_FIXTURES_PATH "${TEST_FIXTURES_DEST}" CACHE PATH 
    "Path to test fixtures directory")

# ============================================================================
# Visual Regression Test
# ============================================================================

qt_add_executable(VisualRegressionTest
    VisualRegressionTest.cpp
    # Core sources
    ${CMAKE_SOURCE_DIR}/src/core/ApplicationInitializer.cpp
    ${CMAKE_SOURCE_DIR}/src/core/ServiceLocator.cpp
    # Utils sources
    ${CMAKE_SOURCE_DIR}/src/utils/ConfigManager.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/DisplayManager.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/InputModeManager.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/TrackPreferencesManager.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/LibraryCacheStore.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/SidebarSettings.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/GpuMemoryTrimmer.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/CacheMigrator.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/Logger.cpp
    # Network sources
    ${CMAKE_SOURCE_DIR}/src/network/AuthenticationService.cpp
    ${CMAKE_SOURCE_DIR}/src/network/LibraryService.cpp
    ${CMAKE_SOURCE_DIR}/src/network/PlaybackService.cpp
    ${CMAKE_SOURCE_DIR}/src/network/SessionManager.cpp
    ${CMAKE_SOURCE_DIR}/src/network/SessionService.cpp
    ${CMAKE_SOURCE_DIR}/src/network/Types.cpp
    # Player sources
    ${CMAKE_SOURCE_DIR}/src/player/PlayerController.cpp
    ${CMAKE_SOURCE_DIR}/src/player/PlayerProcessManager.cpp
    ${CMAKE_SOURCE_DIR}/src/player/ThemeSongManager.cpp
    ${CMAKE_SOURCE_DIR}/src/player/TrickplayProcessor.cpp
    # UI sources
    ${CMAKE_SOURCE_DIR}/src/ui/WindowManager.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ImageCacheProvider.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ResponsiveLayoutManager.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/FontLoader.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/UiSoundController.cpp
    # ViewModels
    ${CMAKE_SOURCE_DIR}/src/viewmodels/LibraryViewModel.cpp
    ${CMAKE_SOURCE_DIR}/src/viewmodels/SeriesDetailsViewModel.cpp
    ${CMAKE_SOURCE_DIR}/src/viewmodels/MovieDetailsViewModel.cpp
    ${CMAKE_SOURCE_DIR}/src/viewmodels/BaseViewModel.cpp
    # Test mode sources
    ${CMAKE_SOURCE_DIR}/src/test/TestModeController.cpp
    ${CMAKE_SOURCE_DIR}/src/test/MockAuthenticationService.cpp
    ${CMAKE_SOURCE_DIR}/src/test/MockLibraryService.cpp
    # Security sources
    ${CMAKE_SOURCE_DIR}/src/security/SecretStoreFactory.cpp
    # Resources
    ${CMAKE_SOURCE_DIR}/src/resources/fonts.qrc
    ${CMAKE_SOURCE_DIR}/src/resources/scripts.qrc
    ${CMAKE_SOURCE_DIR}/src/resources/sounds.qrc
    ${CMAKE_SOURCE_DIR}/src/resources/images.qrc
)

# Set QML singleton properties
set_source_files_properties(${CMAKE_SOURCE_DIR}/src/ui/Theme.qml PROPERTIES
    QT_QML_SINGLETON_TYPE TRUE
)
set_source_files_properties(${CMAKE_SOURCE_DIR}/src/ui/Icons.qml PROPERTIES
    QT_QML_SINGLETON_TYPE TRUE
)
set_source_files_properties(${CMAKE_SOURCE_DIR}/src/ui/TrackUtils.js PROPERTIES
    QT_QML_SKIP_QMLDIR_ENTRY TRUE
)

if(COMMAND qt_policy)
    qt_policy(SET QTP0004 NEW)
endif()

# Define QML sources with absolute paths
set(QML_SOURCES
    ${CMAKE_SOURCE_DIR}/src/ui/Main.qml
    ${CMAKE_SOURCE_DIR}/src/ui/LoginScreen.qml
    ${CMAKE_SOURCE_DIR}/src/ui/LibraryScreen.qml
    ${CMAKE_SOURCE_DIR}/src/ui/SeriesDetailsView.qml
    ${CMAKE_SOURCE_DIR}/src/ui/SeasonView.qml
    ${CMAKE_SOURCE_DIR}/src/ui/SeriesSeasonEpisodeView.qml
    ${CMAKE_SOURCE_DIR}/src/ui/MovieDetailsView.qml
    ${CMAKE_SOURCE_DIR}/src/ui/HomeScreen.qml
    ${CMAKE_SOURCE_DIR}/src/ui/SettingsScreen.qml
    ${CMAKE_SOURCE_DIR}/src/ui/SearchScreen.qml
    ${CMAKE_SOURCE_DIR}/src/ui/SearchResultCard.qml
    ${CMAKE_SOURCE_DIR}/src/ui/UnwatchedBadge.qml
    ${CMAKE_SOURCE_DIR}/src/ui/RetryButton.qml
    ${CMAKE_SOURCE_DIR}/src/ui/Theme.qml
    ${CMAKE_SOURCE_DIR}/src/ui/Icons.qml
    ${CMAKE_SOURCE_DIR}/src/ui/Sidebar.qml
    ${CMAKE_SOURCE_DIR}/src/ui/MediaProgressBar.qml
    ${CMAKE_SOURCE_DIR}/src/ui/MpvProfileEditor.qml
    ${CMAKE_SOURCE_DIR}/src/ui/TrackSelector.qml
    ${CMAKE_SOURCE_DIR}/src/ui/MediaInfoPanel.qml
    ${CMAKE_SOURCE_DIR}/src/ui/ToastNotification.qml
    ${CMAKE_SOURCE_DIR}/src/ui/RoundedImage.qml
    ${CMAKE_SOURCE_DIR}/src/ui/ActiveSessionsScreen.qml
    ${CMAKE_SOURCE_DIR}/src/ui/TrackUtils.js
)

# Set resource aliases for QML files to avoid absolute path issues on Windows
foreach(qml_file ${QML_SOURCES})
    get_filename_component(filename ${qml_file} NAME)
    set_source_files_properties(${qml_file} PROPERTIES
        QT_RESOURCE_ALIAS ui/${filename}
    )
endforeach()

# Add QML module
qt_add_qml_module(VisualRegressionTest
    URI BloomUI
    VERSION 1.0
    RESOURCE_PREFIX /
    NO_CACHEGEN
    QML_FILES
        ${QML_SOURCES}
)



# Add shaders (only if Qt6 ShaderTools is available)
find_package(Qt6 QUIET COMPONENTS ShaderTools)
if(Qt6ShaderTools_FOUND)
    qt6_add_shaders(VisualRegressionTest "bloom_shaders"
        PREFIX /shaders
        OUTPUTS
            rounded_image.frag.qsb
        FILES
            ${CMAKE_SOURCE_DIR}/src/resources/shaders/rounded_image.frag
    )
else()
    message(STATUS "Qt6 ShaderTools not found - skipping shader generation for tests")
endif()

target_include_directories(VisualRegressionTest PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(VisualRegressionTest PRIVATE
    Qt6::Core
    Qt6::Test
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Gui
    Qt6::Qml
    Qt6::Network
    Qt6::Concurrent
    Qt6::Sql
    Qt6::Multimedia
)

# Platform-specific secure storage libraries
if(UNIX AND NOT APPLE)
    # Linux: link libsecret
    target_sources(VisualRegressionTest PRIVATE ${CMAKE_SOURCE_DIR}/src/security/SecretStoreLinux.cpp)
    target_include_directories(VisualRegressionTest PRIVATE ${LIBSECRET_INCLUDE_DIRS})
    target_link_libraries(VisualRegressionTest PRIVATE ${LIBSECRET_LIBRARIES})
elseif(WIN32)
    # Windows: link advapi32 for Credential Manager API
    target_sources(VisualRegressionTest PRIVATE ${CMAKE_SOURCE_DIR}/src/security/SecretStoreWindows.cpp)
    target_link_libraries(VisualRegressionTest PRIVATE advapi32)
endif()

# Define test fixtures path
target_compile_definitions(VisualRegressionTest PRIVATE
    TEST_FIXTURES_PATH="${TEST_FIXTURES_PATH}"
)

add_test(NAME VisualRegressionTest COMMAND VisualRegressionTest)

# Copy golden directory to build directory for runtime access
set(GOLDEN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/golden")
set(GOLDEN_DEST "${CMAKE_CURRENT_BINARY_DIR}/golden")

# Create golden directory in build if it doesn't exist
if(NOT EXISTS "${GOLDEN_DEST}")
    file(MAKE_DIRECTORY "${GOLDEN_DEST}")
endif()

# Copy any existing golden images
if(EXISTS "${GOLDEN_DIR}")
    file(COPY "${GOLDEN_DIR}/" DESTINATION "${GOLDEN_DEST}")
endif()

# Create diffs and captures directories in build
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/diffs")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/captures")
