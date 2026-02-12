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

add_executable(VisualRegressionTest
    VisualRegressionTest.cpp
)

target_link_libraries(VisualRegressionTest PRIVATE
    Qt6::Test
    Qt6::Quick
    Qt6::Gui
    Qt6::Qml
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
