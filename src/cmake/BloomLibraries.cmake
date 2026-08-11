# Reusable production libraries shared by the application and unit tests.
# Keep dependency direction acyclic:
#   Models -> Config / Transport / ImageCache -> Providers -> Network

add_library(BloomModels STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/models/MediaModels.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/network/Types.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/utils/BloomLogging.cpp
)
add_library(Bloom::Models ALIAS BloomModels)
target_include_directories(BloomModels
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}
)
target_link_libraries(BloomModels
    PUBLIC
        Qt6::Core
        Qt6::Network
)

add_library(BloomConfig STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/providers/ServerConnection.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/profiles/BloomProfile.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/security/CredentialStore.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/utils/ConfigManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/utils/ConfigStorage.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/utils/MpvArgFilter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/utils/LoggingConfig.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/utils/Logger.cpp
)
add_library(Bloom::Config ALIAS BloomConfig)
target_include_directories(BloomConfig
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}
)
target_link_libraries(BloomConfig
    PUBLIC
        Bloom::Models
        Qt6::Core
        Qt6::Gui
        Qt6::Network
)

add_library(BloomTransport STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/network/HttpTransport.cpp
)
add_library(Bloom::Transport ALIAS BloomTransport)
target_include_directories(BloomTransport
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}
)
target_link_libraries(BloomTransport
    PUBLIC
        Bloom::Models
        Qt6::Core
        Qt6::Network
)

add_library(BloomImageCache STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/ImageCacheProvider.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/ImageCacheProvider.h
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/ImageCacheStore.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/ImageCacheStore.h
)
add_library(Bloom::ImageCache ALIAS BloomImageCache)
target_include_directories(BloomImageCache
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}
)
target_link_libraries(BloomImageCache
    PUBLIC
        Bloom::Models
        Qt6::Concurrent
        Qt6::Core
        Qt6::Gui
        Qt6::Network
        Qt6::Quick
        Qt6::Sql
)

add_library(BloomPlayerProcess STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/player/PlayerProcessManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/player/PlayerProcessManager.h
)
add_library(Bloom::PlayerProcess ALIAS BloomPlayerProcess)
target_include_directories(BloomPlayerProcess
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}
)
target_link_libraries(BloomPlayerProcess
    PUBLIC
        Bloom::Models
        Qt6::Core
        Qt6::Network
)

add_library(BloomProviders STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/providers/jellyfin/JellyfinAuthenticator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/providers/jellyfin/JellyfinModelMapper.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/providers/jellyfin/JellyfinPlaybackProvider.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/providers/jellyfin/JellyfinRequestFactory.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/providers/silo/SiloModelMapper.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/providers/silo/SiloPlaybackProvider.cpp
)
add_library(Bloom::Providers ALIAS BloomProviders)
target_include_directories(BloomProviders
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}
)
target_link_libraries(BloomProviders
    PUBLIC
        Bloom::Config
        Bloom::Transport
        Qt6::Core
        Qt6::Network
)

add_library(BloomNetwork STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/providers/ActiveArtworkProvider.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/providers/jellyfin/JellyfinArtworkProvider.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/providers/silo/SiloArtworkProvider.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/network/AuthenticationService.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/network/LibraryService.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/network/NextEpisodeResolver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/network/PlaybackService.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/network/MediaSegmentProviderService.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/network/SeerrService.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/network/SessionManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/network/SessionService.cpp
)
add_library(Bloom::Network ALIAS BloomNetwork)
target_include_directories(BloomNetwork
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}
)
target_link_libraries(BloomNetwork
    PUBLIC
        Bloom::Providers
        Qt6::Concurrent
        Qt6::Core
        Qt6::Network
)

set_target_properties(
    BloomModels
    BloomConfig
    BloomTransport
    BloomImageCache
    BloomPlayerProcess
    BloomProviders
    BloomNetwork
    PROPERTIES FOLDER "Bloom/Libraries"
)
