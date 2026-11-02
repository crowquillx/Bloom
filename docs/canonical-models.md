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
- `UserMediaState` (`watched`, `favorite`, `inWatchlist` where the provider has a watchlist, `notInterested` where the provider has that mark, `positionMs`, `unplayedItemCount`, `lastPlayedAt`)
- `Person`
- `Chapter`
- `StreamRequest`
- `PlaybackTrack`
- `PlaybackReportingCapabilities`
- `PlaybackDescriptor`

The types expose temporary `QVariantMap` projections so existing QML-facing façades can migrate incrementally without making provider JSON the public contract.

## People

A `Person` projection carries `connectionId`, `personId`, `media` (its `MediaRef`), `name`, `role` and `kind` (for a credit), and a token-free `artwork` reference of kind `person`. Person reads and people search add, when the provider has them, `overview` (the biography), `birthDate` and `deathDate` (calendar dates, `YYYY-MM-DD`), `birthplace`, and `providerIds`. Jellyfin maps a `Person` item's `Overview`, `PremiereDate`, `EndDate`, first `ProductionLocations` entry, `ProviderIds` and `ImageTags.Primary` (`JellyfinModelMapper::person`); Silo maps `bio`, `birth_date`, `death_date`, `birthplace`, provider IDs and `photo_url` (`SiloModelMapper::person`, artwork owned by the person so an expired photo refreshes through `getPerson`). Adapters expose them through `IProviderAdapter::mapPerson` and `mapPeople`; a person without an ID or name is dropped.

## Jellyfin conversion

Canonical `episodeOrder` preserves Jellyfin's `DisplayOrder` for external media
segment requests. Series metadata supplies the order for episode lookups. An
empty value means the provider did not supply an order. Silo's pinned v2
contract does not expose one. See [Media segments](media-segments.md).

`JellyfinModelMapper` converts Jellyfin item, user-state, person, artwork, chapter, playback-source, stream, trickplay, core media-segment, remote-session, ancestor, filter-facet, item-list envelope, and tick fields into Bloom canonical values. Jellyfin `PlaybackInfo` DTO parsing and tick conversion occur there; shared playback-source projections expose `durationMs` and must not carry provider time units. Trickplay duration correction, core segment tick-to-millisecond conversion, remote-session PascalCase mapping, and list/facet envelope extraction also remain inside this provider boundary. Production requests use only routes and parameters present and non-obsolete in the pinned Jellyfin 12 OpenAPI document.

Episode items from both providers can carry `seriesPrimaryArtwork` (the series poster, owned by the series) and `parentPrimaryArtwork` (the season poster), and their `backdropArtwork` is the parent's backdrop when the episode has none of its own. `SiloModelMapper` fills them from Silo's optional episode parent artwork; it adds the series backdrop ahead of a `backdrop_url` that is the episode still (`backdrop_is_episode_still`), so on Silo the series backdrop also replaces an episode-still backdrop. Series-owned references need the card's `series_id`; an episode card without one keeps only its own artwork. See [provider compatibility](provider-compatibility.md#catalog-and-conditional-requests).

`LibraryService` asks the selected `IProviderAdapter` to map item, item-list, similar-item, series, next-episode, and chapter wire DTOs exactly once. Next-episode timeline resolution runs only on canonical episode maps and compares millisecond resume positions. Connection-aware canonical signals carry the `connectionId` captured when the request starts, so asynchronous mapping and consumers never substitute a later active connection.

`LibraryService` no longer republishes provider-shaped item, list, Home, hero,
series, or similar-item DTO signals. Provider responses remain private to the
service/adapter boundary and only canonical projections cross into migrated
view models, QML, and the player. New consumers must use the connection-scoped
canonical signals and must not add PascalCase wire-key fallbacks.

Existing list/detail/player flows are migrated in reviewable slices. During migration, compatibility façades may retain old methods, but canonical consumers must not fall back to PascalCase wire keys.

## Poster badge inputs

Providers that know a file's technical summary put it in the item's `mediaSummary` map (`resolution`, `hdr`, `audio`, `audioChannels`, `videoCodec`, `container`, `aspectRatio`, `releaseType`, `edition`, `multiAudio`, `multiSub`), leaving out empty and false values. Items may also carry `advisoryAge`, `originalLanguage`, and `showStatus` (the series lifecycle, separate from `status`). Silo fills these from catalog cards; Jellyfin list responses do not. `Bloom::PosterBadgeLayout` and `Bloom::resolvePosterBadges` (`src/models/PosterBadges.*`) turn them into badges. See [poster badges](poster-badges.md).

## Home card progress stamp

Silo Continue Watching and Up Next cards carry `progressUpdatedAt`, the card's `progress_updated_at` string exactly as the server sent it (absent when the card has none). Hiding a Continue Watching card sends it back, and the server compares it byte for byte with its stored stamp, so it is never reparsed or reformatted. `lastPlayedAt` may hold the same instant but is a display and ordering value. Jellyfin cards have no such field. See [provider compatibility](provider-compatibility.md#catalog-and-conditional-requests).

## Local extras

A movie's or series' local extras (trailers, featurettes, deleted scenes, ...) are canonical maps described in `src/models/MediaExtras.h`: `media`, `connectionId`, `itemId` (the extra's own playable ID), `parentId`, `name`, `extraKind`, `durationMs`, and, when the provider has a still, `primaryArtwork`/`artwork` (16:9). `extraKind` is one of `trailer`, `teaser`, `featurette`, `clip`, `behindTheScenes`, `bloopers`, `deletedScene`, `interview`, `scene`, `short`, `other`. Only local files are extras; remote (YouTube) trailers are never mapped.

- **Silo**: the catalog item detail carries `extras[]`, so the canonical detail map (`SiloModelMapper::mediaDetail`, used for every single-item read) always has an `extras` list. Silo omits `extras` when an item has none, and the mapper then sets an empty list, so a fresh detail replaces extras cached from an earlier one. Cards never carry the key. The still is an `ArtworkRef` owned by the parent with `ArtworkOwnerKind::Extra`, because an extra has no catalog route to refresh from. Silo also returns `fileId`.
- **Jellyfin**: details carry no extras. `LibraryService::getItemExtras` reads `LocalTrailers` then `SpecialFeatures` through `IProviderAdapter::mapExtras` (`JellyfinModelMapper::extras`), drops duplicates, and emits `canonicalItemExtrasLoaded(connectionId, itemId, extras, complete)`, or `canonicalItemExtrasFailed` when both reads fail. When only one read fails, the answer carries what arrived with `complete == false`; the view models show it but do not cache it, so the next visit asks again. A cached shelf shows at once on a return visit and is refreshed in the background. A provider with extras in the detail answers an empty, complete list.
- `MediaExtras::preferredTrailer` picks the extra Play Trailer starts: the first `trailer`, else the first `teaser`.

See [provider compatibility](provider-compatibility.md#catalog-and-conditional-requests) for the wire rules and [playback](playback.md) for how extras play.

## Ratings and airings

Items may carry `serverRatings`: the ratings the media server shows on a title page, in its display order. Each entry has `source` (`imdb`, `tmdb`, `rottenTomatoesCritic`, `rottenTomatoesAudience`, or a plugin's own source name), `name` (the server's plain-text mark), `score` on a 0-100 scale, and `display` (the score as the source writes it, such as `8.3` or `83%`). Silo fills it from `ratings` on details, else from the fixed `rating_imdb`, `rating_tmdb`, and `rating_rt_*` fields; Jellyfin does not. The `ratings` map (`imdb`, `tmdb`, `rottenTomatoesCritic`, `rottenTomatoesAudience`) stays for poster badges. Detail pages show `serverRatings` first and add MDBList and AniList ratings only for sources the server does not show (`ExternalRatingsHelper::withServerRatings`). Every rating the view models publish carries `sourceKey` (`ExternalRatingsHelper::ratingSourceKey`, the one table of source spellings such as `myanimelist`/`mal` and `popcorn`/`rottenTomatoesAudience`), and server entries carry `fromServer`. `RatingMetadataChip` picks its logo by `sourceKey`, shows `display` when present (a score of 0 included) and falls back to `name` for a source without a logo.

An airing map describes the next episode to air: `type`, `airDate` (`YYYY-MM-DD`), `airAt` (a UTC instant, only when the network's time and zone are known), `seasonNumber` and `episodeNumber` (-1 when absent), `episodeTitle`, and `badges`. Items may carry one as `nextAiring` (Silo's card `upcoming_event`; its `airDate` is the source date and it has no instant). `LibraryService::getNextAiring` answers one from the provider's calendar (`supportsNextAiring`), where `airDate` is the viewer's local day. See [provider compatibility](provider-compatibility.md#catalog-and-conditional-requests).

## Subtitle appearance

`Bloom::SubtitleAppearance` (`src/models/SubtitleAppearance.*`) is the canonical subtitle look, with camelCase map keys (`sizePercent`, `font`, `textColor`, `outlineColor`, `outline`, `shadow`, `background`, `backgroundColor`, `backgroundOpacity`, `positionPercent`, `applyToStyled`). `withValues` validates every key, so a provider answer can only set fields it maps. Silo's `SubtitleAppearance` read answers a partial map in `capabilityMetadata.appearance` and keeps its wire document opaque (`document`), which a write hands back; see [provider compatibility](provider-compatibility.md#subtitle-appearance).

## Migrated surfaces

- **Movie Details**: `MovieDetailsViewModel` and `MovieDetailsView.qml` consume only canonical item, connection-scoped similar-item, and chapter payloads. Cached movie and similar JSON is canonical (`*_details_canonical.json` / `*_similar_items_canonical.json`); wire-shaped disk caches are rejected. Similar-item request state is owned by connection and item, so a stale completion settles only its old request and cannot leave loading stuck or invalidate a replacement. Artwork resolves through `ArtworkRef` maps and `LibraryService::getCachedArtworkUrl`. Display, chapter, and playback request timing use milliseconds.
- **Root libraries**: `LibraryService` emits connection-scoped canonical views and query-correlated canonical item lists. `LibraryViewModel`, the root `LibraryScreen` grid, `NavigationBar`, and per-library settings rows consume camelCase media projections only. Library disk caches reject provider wire-shaped rows; SWR compares full canonical items and publishes metadata-only changes as row updates when canonical identities/order remain stable. Poster fallbacks resolve `ArtworkRef` maps. Sort state uses Bloom keys such as `releaseDate` and `dateAdded`; Jellyfin and Silo translate them to native fields inside their catalog adapters. Items carry `dateAdded` (Jellyfin `DateCreated`, Silo `added_at`) so combined pages can merge libraries by it.
- **Library types**: Bloom supports only movie and series libraries. `LibraryKinds` (`Bloom::Models`) maps Jellyfin `movies`/`tvshows` and Silo `movies`/`series` to the kinds `movies` and `series`; every other type (music, photos, home videos, box sets, mixed, books) is dropped where views enter the app (`LibraryService` views and `MergedCatalogService` views and source listings), so no consumer ever sees it. Supported views gain `libraryKind`. Search treats unsupported libraries like hidden ones and searches the supported libraries one by one.
- **Series Details, seasons, and Up Next**: `SeriesDetailsViewModel`, `SeriesDetailsView.qml`, `SeriesSeasonEpisodeView.qml`, `PlayerController`, and `UpNextScreen.qml` consume canonical series, season, episode, next-episode, similar-item, focused-episode, and chapter projections. Season/episode roles expose `durationMs` / `positionMs`; specials retain provider placement semantics through canonical `airsBefore*` / `airsAfter*` fields. Post-playback prefetch, navigation, display metadata, resume, and artwork remain canonical through the player/QML boundary. Post-playback recommendations are connection-scoped, merge local-library and request discovery maps through camelCase keys, and reject stale responses. Playback chapter navigation and the embedded overlay consume `name`, `startMs`, and token-free chapter `ArtworkRef` values; the former shared Jellyfin `ChapterInfo` tick DTO has been removed. Series, season, episode, and recommendation caches use `*_canonical.json` names and reject wire-shaped payloads. Artwork always resolves from connection-scoped `ArtworkRef` maps. Playback requests keep resume and chapter positions in milliseconds.
- **Home and hero**: `HomeScreen.qml`, `HeroBannerProvider.qml`, and `HeroBanner.qml` consume connection-scoped canonical libraries, Next Up, latest-media, rotating-backdrop, hero-library, and series-overview results. Continue Watching splitting uses `positionMs`; cards, hero metadata, and player requests use canonical camelCase and millisecond timing. Root, item, series, parent, thumb, logo, and backdrop images resolve through request-owned `ArtworkRef` values. Obsolete raw Home and hero service signals have been removed.
- **Search and discovery**: Library search and random suggestions emit connection-scoped canonical media maps. Search terms are trimmed once and retained with the request identity so stale connection or term responses cannot replace current results or leave loading state active. Search and Up Next result cards (`MediaCard`) read only camelCase media fields and resolve library posters through `ArtworkRef`. Request discovery maps from Seerr and Silo share one shape: the same camelCase presentation keys plus `source: "request"`, `requestBackend`, `requestMediaType`, `requestTmdbId`, `requestStatus`, and Silo's `requestable` and `libraryItemId`; request list items add `requestId`, `requestState`, `requestedSeasons`, `createdAt`, and `canCancel` ([media requests](seerr.md)).
- **Screensaver and settings backdrops**: Screensaver candidates and Settings preview sources consume connection-scoped canonical media maps. Both surfaces resolve backdrop/logo `ArtworkRef` values when displayed; service payloads no longer contain pre-resolved artwork URLs or Jellyfin image-tag fields.
- **Person pages and people search**: `LibraryService::getPerson`, `getPersonItems` and `searchPeople` (and `MergedCatalog.searchPeople`) emit connection-scoped canonical person and item maps. `PersonViewModel`, `PersonScreen.qml` and Search's People shelf read only camelCase fields.
- **Shared QML components**: `LibraryScreen.qml`, `PersonCard.qml`, and `MediaProgressBar.qml` require canonical camelCase media/person fields, token-free artwork references, and millisecond progress values. Their transitional PascalCase, image-tag, and tick fallbacks have been removed, along with the unused wire-shaped `DetailMetadataHelper`.

## Artwork identity

`ArtworkRef::cacheKey()` serializes only token-free canonical fields. Token rotation or a refreshed provider representation does not change the cache key. `ImageCacheProvider` delegates cache misses to `IArtworkProvider`; `JellyfinArtworkProvider` resolves the current authenticated request with an authorization header and no query token. The resolved request URL is never persisted or logged.

## Playback boundary

A `PlaybackDescriptor` is valid when it has a valid `MediaRef` and finalized `StreamRequest`. `PlaybackService` parses the response and delegates `PlaybackInfo` mapping through `AuthenticationService::mapPlaybackInfo`; `AuthenticationService` forwards it to the selected `IProviderAdapter`. `PlaybackService` then obtains the adapter's `IPlaybackProvider` to finalize the stream. `JellyfinPlaybackProvider` resolves relative stream and trickplay-tile URLs, adds current request authentication and track hints, and identifies the canonical playback method. Additional-part item DTOs are mapped before `PlayerController` receives them, so multipart identity uses canonical `itemId`. Autoplay and compatibility fallbacks also request a provider-finalized descriptor instead of constructing or selecting authenticated Jellyfin URLs. Playback positions and multipart durations remain milliseconds through the controller and service façade. The provider serializes report endpoints and payloads, including Jellyfin's final millisecond-to-tick conversion. Provider endpoints, query authentication, provider time units, and reporting DTOs stay outside the controller.

Positions in a descriptor are source positions. A server-packaged stream may start its clock later: `StreamRequest::timelineOffsetMs` is added to a player position to get the source position, `seekRequiresReanchor` marks a stream that can seek only inside what the server has produced, and `carriesSourceTracks` is false when the stream holds only `deliveredAudioTracks` and `deliveredSubtitleTracks` (source indexes in mpv order). `subtitleBurnedIn` marks a pinned subtitle rendered into the video. A `PlaybackTrack` with `requiresReplan` cannot be selected on the current stream; choosing it asks the provider for a new one (`IPlaybackProvider::createReplanRequest`). A canonical subtitle stream's `externalUrl` is the provider-relative sidecar location the server advertised; only the descriptor's `PlaybackTrack::externalUrl`, which the provider resolved and authorized, is handed to mpv. `PlaybackTrack::sourceIndex` names the source stream a descriptor track delivers, so a sidecar replaces its source menu entry instead of adding a second one.

A media source may carry `serverTrackPreferences` (`Bloom::ServerTrackPreferences` in `models/ServerTrackPreferences.h`, header-only so `Bloom::PlayerPolicy` reads it without linking the models library). Keys: `audioStreamIndex` (the server's chosen audio track as a Bloom source index), `audioLanguage` with `playDefaultAudioTrack`, and, when `subtitleMode` (`off`, `always`, `auto` or `forced`) is set, `subtitleLanguage`, `showForcedSubtitles` and `subtitleSignature` (`source`, `language`, `codec`, `label`, `forced`, `hearingImpaired`). Absent keys mean the server has no preference. Silo fills it per version from `/watch`; for Jellyfin, `PlaybackService` copies the account's language defaults onto each source. Silo streams also carry `embeddedTitle`, the container's own title tag, which Silo's track signatures name. When Silo sends no title, `title` shows the embedded title and `titleFromEmbeddedTitle` is `true`, so a signature written back keeps Silo's empty title. See [playback](playback.md).

## Tests

`PlayerController` accepts `startPositionMs` for explicit, autoplay, retry, and recovery requests. Provider wire resume values are normalized through the active adapter before entering player state; the controller converts only between milliseconds and mpv seconds.

`CanonicalModelsTest` covers:

- Jellyfin tick/millisecond conversion
- PascalCase Jellyfin item mapping to canonical camelCase values used by Movie Details, Series Details, seasons/episodes, root libraries, and Home/hero (identity, sort/display values, timing, ratings, genres, people, provider IDs, special placement, and item/parent/series artwork refs, including independently owned series and parent thumbs)
- canonical chapter mapping with millisecond starts and token-free chapter artwork references
- token-free, round-trippable artwork cache keys
- provider-neutral playback descriptor projections
- Jellyfin `PlaybackInfo`, media-source, and stream mapping with millisecond durations and no shared tick fields
- Jellyfin stream finalization, canonical timing/tracks, and current credential injection at the playback-provider boundary
- Jellyfin external subtitle `DeliveryUrl` preservation, resolution, credential handling, the `GetSubtitle` fallback, and the External subtitle profiles
- provider-owned playback report endpoint selection and millisecond-to-Jellyfin-tick serialization
- Jellyfin trickplay, core media-segment, remote-session, ancestor, filter-facet, and item-list DTO mapping plus provider-owned authenticated trickplay tile URLs

`ProviderCatalogTest` covers extras mapping for both providers (kinds, dropped entries, missing title/runtime/still, empty lists, the Jellyfin routes against the pinned OpenAPI manifest, and Silo's parent-owned still), `SiloCatalogServiceTest` covers `LibraryService::getItemExtras` (trailers first, duplicates dropped, a partial failure flagged incomplete, both failing, and Silo's empty answer), and `HomeRowsAndWatchlistTest` covers the detail view models' `extras` and `trailer` (partial answers not cached, background refresh after a cache hit).

`SimilarItemsRetryTest` asserts Movie and Series detail shelves keep canonical request ownership and item/chapter shapes. `SeriesDetailsCacheTest` covers canonical series/list cache persistence, freshness, and wire-cache rejection. `NextEpisodeResolverTest` covers canonical timeline ordering, watched/resume state in milliseconds, special placement, and preferred-payload merging without provider DTO fields. `EpisodeSelectionScriptTest` exercises canonical episode identity, season ownership, watched state, and millisecond resume selection. `LibraryViewModelCanonicalTest` covers canonical root-library roles, empty-container filtering, wire-cache rejection, SWR identity/order checks, and metadata-only row refreshes for user state and visible artwork/text fields.

`CanonicalBoundaryContractTest` scans migrated QML/player sources for provider
wire-field access and provider-adapter includes. It also prevents removed raw
`LibraryService` DTO signals from being declared or emitted again and keeps
provider-native sort values out of library QML/view-model state.

## Audio planning

`PlaybackTrack.audio` preserves canonical audio source metadata.
`PlaybackDescriptor.audioOutputPlan` carries the selected local output plan.
`MediaSourceInfo.directPlayAvailable` preserves a provider's explicit permission
to play the original source. See [Audio output](audio.md) for the metadata fields
and the separation between decode support and encoded transport.
