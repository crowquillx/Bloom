# Canonical media models

Bloom-owned canonical models separate UI/player code from provider wire formats. Their implementation starts in `src/models/MediaModels.*`; provider conversion belongs under the provider adapter, such as `src/providers/jellyfin/JellyfinModelMapper.*`.

## Rules

- Canonical map keys use `camelCase`.
- Durations, positions, chapter starts, and media segments use milliseconds.
- Provider time conversion occurs only in the provider adapter.
- `MediaRef` combines `connectionId` and `itemId`; a remote item ID is never globally unique by itself.
- Bloom profile `memberId` is request/source context only and must not be added to `MediaRef`, artwork cache keys, or library disk caches. See [profiles](profiles.md).
- `ArtworkRef` is token-free and identifies artwork by connection, item, kind, index, tag, and requested width.
- Artwork fetch URLs, headers, and expiring provider representations are transport details and must not become cache identities.
- `PlaybackDescriptor` owns the finalized stream request, normalized tracks, session identity, canonical timing, chapters, and reporting capabilities consumed by the player.
- New QML must not inspect provider DTO field names.

## Foundation types

`MediaModels` currently defines:

- `MediaRef`
- `ArtworkRef`
- `UserMediaState`
- `Person`
- `Chapter`
- `StreamRequest`
- `PlaybackTrack`
- `PlaybackReportingCapabilities`
- `PlaybackDescriptor`

The types expose temporary `QVariantMap` projections so existing QML-facing façades can migrate incrementally without making provider JSON the public contract.

## Jellyfin conversion

`JellyfinModelMapper` converts Jellyfin item, user-state, person, artwork, chapter, and tick fields into Bloom canonical values. Jellyfin ticks are converted to milliseconds there and must not be introduced into new model or QML APIs.

`LibraryService` asks the selected `IProviderAdapter` to map item, item-list, similar-item, series, next-episode, and chapter wire DTOs exactly once. Next-episode timeline resolution runs only on canonical episode maps and compares millisecond resume positions. Connection-aware canonical signals carry the `connectionId` captured when the request starts, so asynchronous mapping and consumers never substitute a later active connection.

Raw compatibility signals remain for unmigrated flows. Migrated consumers connect only to the parallel `canonical*` signals and must not fall back to PascalCase wire keys.

Existing list/detail/player flows are migrated in reviewable slices. During migration, compatibility façades may retain old methods, but canonical consumers must not fall back to PascalCase wire keys.

## Migrated surfaces

- **Movie Details**: `MovieDetailsViewModel` and `MovieDetailsView.qml` consume only canonical item, similar-item, and chapter payloads. Cached movie and similar JSON is canonical (`*_details_canonical.json` / `*_similar_items_canonical.json`); wire-shaped disk caches are rejected. Artwork resolves through `ArtworkRef` maps and `LibraryService::getCachedArtworkUrl`. Display, chapter, and playback request timing use milliseconds.
- **Root libraries**: `LibraryService` emits canonical views and query-correlated canonical item lists alongside the raw compatibility signals. `LibraryViewModel`, the root `LibraryScreen` grid, `Sidebar`, and per-library settings rows consume camelCase media projections only. Library disk caches reject provider wire-shaped rows, SWR comparisons use canonical `itemId`, and poster fallbacks resolve `ArtworkRef` maps. Query construction keeps provider sort/include tokens at the service boundary.
- **Series Details, seasons, and Up Next**: `SeriesDetailsViewModel`, `SeriesDetailsView.qml`, `SeriesSeasonEpisodeView.qml`, `PlayerController`, and `UpNextScreen.qml` consume canonical series, season, episode, next-episode, similar-item, focused-episode, and chapter projections. Season/episode roles expose `durationMs` / `positionMs`; specials retain provider placement semantics through canonical `airsBefore*` / `airsAfter*` fields. Post-playback prefetch, navigation, display metadata, resume, and artwork remain canonical through the player/QML boundary. Playback chapter navigation and the embedded overlay consume `name`, `startMs`, and token-free chapter `ArtworkRef` values; the former shared Jellyfin `ChapterInfo` tick DTO has been removed. Series, season, episode, and recommendation caches use `*_canonical.json` names and reject wire-shaped payloads. Artwork always resolves from connection-scoped `ArtworkRef` maps. Playback requests keep resume and chapter positions in milliseconds.
- **Home and hero**: `HomeScreen.qml`, `HeroBannerProvider.qml`, and `HeroBanner.qml` consume canonical libraries, Next Up, latest-media, rotating-backdrop, hero-library, and series-overview results. Continue Watching splitting uses `positionMs`; cards, hero metadata, and player requests use canonical camelCase and millisecond timing. Root, item, series, parent, thumb, logo, and backdrop images resolve through request-owned `ArtworkRef` values. Raw Home service signals remain temporarily for external compatibility but are not connected by the migrated Home surface.
- **Search and discovery**: Library search and random suggestions emit connection-scoped canonical media maps. Search terms are trimmed once and retained with the request identity so stale connection or term responses cannot replace current results or leave loading state active. `SearchResultCard.qml` reads only camelCase media fields and resolves library posters through `ArtworkRef`. Seerr discovery maps use the same camelCase presentation keys plus source-specific `seerr*` fields.
- **Screensaver and settings backdrops**: Screensaver candidates and Settings preview sources consume connection-scoped canonical media maps. Both surfaces resolve backdrop/logo `ArtworkRef` values when displayed; service payloads no longer contain pre-resolved artwork URLs or Jellyfin image-tag fields.

## Artwork identity

`ArtworkRef::cacheKey()` serializes only token-free canonical fields. Token rotation or a refreshed provider representation does not change the cache key. `ImageCacheProvider` delegates cache misses to `IArtworkProvider`; `JellyfinArtworkProvider` resolves the current authenticated request with an authorization header and no query token. The resolved request URL is never persisted or logged.

## Playback boundary

A `PlaybackDescriptor` is valid when it has a valid `MediaRef` and finalized `StreamRequest`. `PlaybackService` obtains the selected adapter's `IPlaybackProvider`; `JellyfinPlaybackProvider` resolves relative PlaybackInfo URLs, adds current request authentication and track hints, converts timing, and identifies the canonical playback method. `PlayerController` consumes only the finalized descriptor and may map canonical tracks to mpv runtime track IDs. Playback positions and multipart durations remain milliseconds through the controller and service façade. The provider serializes report endpoints and payloads, including Jellyfin's final millisecond-to-tick conversion. Provider endpoints, query authentication, provider time units, and reporting DTOs stay outside the controller.

## Tests

`PlayerController` accepts `startPositionMs` for explicit, autoplay, retry, and recovery requests. Provider wire resume values are normalized through the active adapter before entering player state; the controller converts only between milliseconds and mpv seconds.

`CanonicalModelsTest` covers:

- Jellyfin tick/millisecond conversion
- PascalCase Jellyfin item mapping to canonical camelCase values used by Movie Details, Series Details, seasons/episodes, root libraries, and Home/hero (identity, sort/display values, timing, ratings, genres, people, provider IDs, special placement, and item/parent/series artwork refs, including independently owned series and parent thumbs)
- canonical chapter mapping with millisecond starts and token-free chapter artwork references
- token-free, round-trippable artwork cache keys
- provider-neutral playback descriptor projections
- Jellyfin stream finalization, canonical timing/tracks, and current credential injection at the playback-provider boundary
- provider-owned playback report endpoint selection and millisecond-to-Jellyfin-tick serialization

`SimilarItemsRetryTest` asserts Movie and Series detail shelves keep canonical request ownership and item/chapter shapes. `SeriesDetailsCacheTest` covers canonical series/list cache persistence, freshness, and wire-cache rejection. `NextEpisodeResolverTest` covers canonical timeline ordering, watched/resume state in milliseconds, special placement, and preferred-payload merging without provider DTO fields. `EpisodeSelectionScriptTest` exercises canonical episode identity, season ownership, watched state, and millisecond resume selection. `LibraryViewModelCanonicalTest` covers canonical root-library roles, empty-container filtering, wire-cache rejection, and SWR identity/order checks.
