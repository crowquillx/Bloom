# Canonical media models

Bloom-owned canonical models separate UI/player code from provider wire formats. Their implementation starts in `src/models/MediaModels.*`; provider conversion belongs under the provider adapter, such as `src/providers/jellyfin/JellyfinModelMapper.*`.

## Rules

- Canonical map keys use `camelCase`.
- Durations, positions, chapter starts, and media segments use milliseconds.
- Provider time conversion occurs only in the provider adapter.
- `MediaRef` combines `connectionId` and `itemId`; a remote item ID is never globally unique by itself.
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

Existing list/detail/player flows are migrated in reviewable slices. During migration, compatibility façades may retain old methods, but canonical consumers must not fall back to PascalCase wire keys.

## Artwork identity

`ArtworkRef::cacheKey()` serializes only token-free canonical fields. Token rotation or a refreshed provider representation does not change the cache key. `ImageCacheProvider` delegates cache misses to `IArtworkProvider`; `JellyfinArtworkProvider` resolves the current authenticated request with an authorization header and no query token. The resolved request URL is never persisted or logged.

## Playback boundary

A `PlaybackDescriptor` is valid when it has a valid `MediaRef` and finalized `StreamRequest`. `PlaybackService` obtains the selected adapter's `IPlaybackProvider`; `JellyfinPlaybackProvider` resolves relative PlaybackInfo URLs, adds current request authentication and track hints, converts timing, and identifies the canonical playback method. `PlayerController` consumes only the finalized descriptor and may map canonical tracks to mpv runtime track IDs. Provider endpoints, query authentication, provider time units, and reporting DTOs stay outside the controller.

## Tests

`CanonicalModelsTest` covers:

- Jellyfin tick/millisecond conversion
- PascalCase Jellyfin item mapping to canonical camelCase values
- token-free, round-trippable artwork cache keys
- provider-neutral playback descriptor projections
- Jellyfin stream finalization, canonical timing/tracks, and current credential injection at the playback-provider boundary
