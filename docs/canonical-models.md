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

`LibraryService` asks the selected `IProviderAdapter` to map item, item-list, similar-item, series, next-episode, and chapter wire DTOs exactly once. Connection-aware canonical signals carry the `connectionId` captured when the request starts, so asynchronous mapping and consumers never substitute a later active connection.

Raw compatibility signals remain for unmigrated flows. Migrated consumers connect only to the parallel `canonical*` signals and must not fall back to PascalCase wire keys.

Existing list/detail/player flows are migrated in reviewable slices. During migration, compatibility façades may retain old methods, but canonical consumers must not fall back to PascalCase wire keys.

## Migrated surfaces

- **Movie Details**: `MovieDetailsViewModel` and `MovieDetailsView.qml` consume only canonical item/similar-item payloads. Cached movie and similar JSON is canonical (`*_details_canonical.json` / `*_similar_items_canonical.json`); wire-shaped disk caches are rejected. Artwork resolves through `ArtworkRef` maps and `LibraryService::getCachedArtworkUrl`. Display timing uses `durationMs` / `positionMs`. Playback request payloads still convert resume position to ticks at the QML→player compatibility boundary until the playback request contract is migrated.
- **Root libraries**: `LibraryService` emits canonical views and query-correlated canonical item lists alongside the raw compatibility signals. `LibraryViewModel`, the root `LibraryScreen` grid, `Sidebar`, and per-library settings rows consume camelCase media projections only. Library disk caches reject provider wire-shaped rows, SWR comparisons use canonical `itemId`, and poster fallbacks resolve `ArtworkRef` maps. Query construction keeps provider sort/include tokens at the service boundary.
- **Series Details and seasons**: `SeriesDetailsViewModel`, `SeriesDetailsView.qml`, and `SeriesSeasonEpisodeView.qml` consume canonical series, season, episode, next-episode, similar-item, focused-episode, and chapter projections. Season/episode roles expose `durationMs` / `positionMs`; specials retain provider placement semantics through canonical `airsBefore*` / `airsAfter*` fields. Series, season, episode, and recommendation caches use `*_canonical.json` names and reject wire-shaped payloads. Artwork always resolves from connection-scoped `ArtworkRef` maps. Playback requests convert milliseconds to ticks only at the existing QML→player compatibility edge.

## Artwork identity

`ArtworkRef::cacheKey()` serializes only token-free canonical fields. Token rotation or a refreshed provider representation does not change the cache key. `ImageCacheProvider` delegates cache misses to `IArtworkProvider`; `JellyfinArtworkProvider` resolves the current authenticated request with an authorization header and no query token. The resolved request URL is never persisted or logged.

## Playback boundary

A `PlaybackDescriptor` is valid when it has a valid `MediaRef` and finalized `StreamRequest`. `PlaybackService` obtains the selected adapter's `IPlaybackProvider`; `JellyfinPlaybackProvider` resolves relative PlaybackInfo URLs, adds current request authentication and track hints, converts timing, and identifies the canonical playback method. `PlayerController` consumes only the finalized descriptor and may map canonical tracks to mpv runtime track IDs. Provider endpoints, query authentication, provider time units, and reporting DTOs stay outside the controller.

## Tests

`CanonicalModelsTest` covers:

- Jellyfin tick/millisecond conversion
- PascalCase Jellyfin item mapping to canonical camelCase values used by Movie Details, Series Details, seasons/episodes, and root libraries (identity, sort/display values, timing, ratings, genres, people, provider IDs, special placement, and item/parent/series artwork refs)
- canonical chapter mapping with millisecond starts and token-free chapter artwork references
- token-free, round-trippable artwork cache keys
- provider-neutral playback descriptor projections
- Jellyfin stream finalization, canonical timing/tracks, and current credential injection at the playback-provider boundary

`SimilarItemsRetryTest` asserts Movie and Series detail shelves keep canonical request ownership and item/chapter shapes. `SeriesDetailsCacheTest` covers canonical series/list cache persistence, freshness, and wire-cache rejection. `EpisodeSelectionScriptTest` exercises canonical episode identity, season ownership, watched state, and millisecond resume selection. `LibraryViewModelCanonicalTest` covers canonical root-library roles, empty-container filtering, wire-cache rejection, and SWR identity/order checks.
