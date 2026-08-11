# Reusable production libraries shared by the application and unit tests.
# Keep dependency direction acyclic:
#   Models -> Config / Transport / ImageCache / PlayerProcess -> Providers -> Network
#                     Config -> Display
#                     Qt Core -> PlayerPolicy
#                     Monocypher -> Updater -> Config

add_library(BloomMonocypher STATIC
    ${CMAKE_SOURCE_DIR}/third_party/monocypher/src/monocypher.c
    ${CMAKE_SOURCE_DIR}/third_party/monocypher/src/monocypher.h
    ${CMAKE_SOURCE_DIR}/third_party/monocypher/src/optional/monocypher-ed25519.c
    ${CMAKE_SOURCE_DIR}/third_party/monocypher/src/optional/monocypher-ed25519.h
)
add_library(Bloom::Monocypher ALIAS BloomMonocypher)
target_include_directories(BloomMonocypher
    PUBLIC
        ${CMAKE_SOURCE_DIR}/third_party/monocypher/src
)

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

add_library(BloomDisplay STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/utils/DisplayManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/utils/DisplayManager.h
)
add_library(Bloom::Display ALIAS BloomDisplay)
target_include_directories(BloomDisplay
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}
)
target_link_libraries(BloomDisplay
    PUBLIC
        Bloom::Config
        Qt6::Concurrent
        Qt6::Core
        Qt6::Gui
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
if(BUILD_TESTING)
    target_compile_definitions(BloomImageCache PUBLIC BLOOM_TESTING=1)
endif()

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

add_library(BloomPlayerPolicy STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/player/PlaybackPolicy.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/player/PlaybackPolicy.h
)
add_library(Bloom::PlayerPolicy ALIAS BloomPlayerPolicy)
target_include_directories(BloomPlayerPolicy
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}
)
target_link_libraries(BloomPlayerPolicy
    PUBLIC
        Qt6::Core
)

add_library(BloomUpdater STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/updates/IUpdateProvider.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/updates/IUpdateProvider.h
    ${CMAKE_CURRENT_SOURCE_DIR}/updates/IUpdateApplier.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/updates/IUpdateApplier.h
    ${CMAKE_CURRENT_SOURCE_DIR}/updates/UpdateTypes.h
    ${CMAKE_CURRENT_SOURCE_DIR}/updates/UpdateManifestVerifier.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/updates/UpdateManifestVerifier.h
    ${CMAKE_CURRENT_SOURCE_DIR}/updates/UpdateNetworkPolicy.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/updates/UpdateNetworkPolicy.h
    ${CMAKE_CURRENT_SOURCE_DIR}/updates/GitHubReleaseUpdateProvider.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/updates/GitHubReleaseUpdateProvider.h
    ${CMAKE_CURRENT_SOURCE_DIR}/updates/WindowsNsisUpdateApplier.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/updates/WindowsNsisUpdateApplier.h
    ${CMAKE_CURRENT_SOURCE_DIR}/updates/UpdateService.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/updates/UpdateService.h
)
add_library(Bloom::Updater ALIAS BloomUpdater)
target_include_directories(BloomUpdater
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}
)
target_link_libraries(BloomUpdater
    PUBLIC
        Bloom::Config
        Bloom::Monocypher
        Qt6::Core
        Qt6::Gui
        Qt6::Network
)
target_compile_definitions(BloomUpdater
    PRIVATE
        BLOOM_UPDATE_AUTHENTICODE_PUBLISHER="${BLOOM_UPDATE_AUTHENTICODE_PUBLISHER}"
)
if(WIN32)
    target_link_libraries(BloomUpdater PUBLIC crypt32 shell32 wintrust)
endif()

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
    BloomMonocypher
    BloomModels
    BloomConfig
    BloomDisplay
    BloomTransport
    BloomImageCache
    BloomPlayerProcess
    BloomPlayerPolicy
    BloomUpdater
    BloomProviders
    BloomNetwork
    PROPERTIES FOLDER "Bloom/Libraries"
)
