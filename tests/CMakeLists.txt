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
