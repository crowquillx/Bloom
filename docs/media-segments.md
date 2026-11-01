Media Segment Providers

Overview
- `PlaybackService::getMediaSegments()` is the provider-neutral playback entry point.
- The active provider's server segments are loaded before any external lookup.
- Native Silo markers use `/api/v2/markers/files/{file_id}` for the selected playback version or multipart file, falling back to `/api/v2/markers/items/{content_id}` only when no file identity is available. They remain authoritative for every segment type Silo returns.
- On Jellyfin, Bloom uses the OpenAPI-listed core `/MediaSegments/{id}` route and maps tick ranges at the provider boundary. It does not probe excluded legacy plugin routes.
- Only after the server response is normalized does `MediaSegmentProviderService` query configured external providers to fill missing types.

Precedence
- Native Silo server segments win per segment type.
- Jellyfin core server segments win per segment type when the Jellyfin provider is active. An empty core response is valid and allows configured external providers to fill missing types.
- External provider order defaults to TheIntroDB, then IntroDB.
- `mergeSegmentsByType` keeps every server segment, including several of one type, and adds a later provider's segments only for types no earlier source supplied (all of that provider's ranges of such a type). For example, a server intro plus TheIntroDB intro/credits keeps the server intro and adds only credits.
- The player first joins overlapping or touching ranges of one type (`PlaybackPolicy::mergeSkipRanges`), so two intros back to back are one skip to the end of both. It then skips the merged range that holds the position (`PlaybackPolicy::skipRangeIndex`), or one starting at most 2 seconds ahead (an intro at the very start of the file); otherwise a skip request, including the `bloom-skip-intro` and `bloom-skip-outro` script messages, does nothing. A second intro never jumps back to the first one's end, nor does a skip from between ranges jump far ahead. Auto-skip applies once to each merged intro or credits range of an item, keyed by type and start, so a later separate intro is skipped on entry too, even when reached by seeking straight from another intro.
Provider parsing
- TheIntroDB v4 uses `GET https://api.theintrodb.org/v4/media`. Bloom sends one identity in this order of preference: `tvdb_id`, `tmdb_id`, then `imdb_id`. Reads are anonymous and do not send media-server credentials.
- Episode lookups load the parent series metadata and use its provider IDs. Episode-level provider IDs identify an episode, so Bloom does not use them as series IDs, including when the series lookup fails.
- TV requests require both `season` and `episode`; zero is valid for specials. Missing or out-of-range coordinates prevent the request. Movie requests omit TV coordinates and `mapping`; other media types are not queried.
- Jellyfin `DisplayOrder` maps to canonical `episodeOrder`. The series value takes precedence over an episode value, and Bloom sends it as `mapping`. The API accepts Jellyfin aliases such as `altdvd` and `alttwo`. When no order is available, including from the pinned Silo v2 contract, Bloom omits `mapping` and the API uses TVDB default order. Unknown or unavailable orders return an API error; Bloom then continues to the next configured provider.
- A known positive item duration is sent as `duration_ms` to select the closest release version. An unknown duration is omitted, so the API selects its most popular version.
- TheIntroDB segment arrays map `intro` to Intro, `recap` to Recap, `credits` to Outro, and `preview` to Preview. Each segment must contain integer millisecond timestamps or explicit null values. `start_ms: null` means zero; `end_ms: null` uses the item duration if known or the segment is dropped. Version-list and episode-count responses provide no skip segments.
- IntroDB uses `GET https://api.introdb.app/segments?imdb_id=...&season=...&episode=...`. Reads are anonymous. It maps `intro`, `recap`, and `outro` to Bloom segment types.
- Invalid external segments are dropped when the end is missing, the end is not after the start, or a timestamp is negative.

The v4 request and response contract was checked against the official
[OpenAPI document](https://theintrodb.org/openapi.yaml) and
[migration guide](https://theintrodb.org/docs/migration-v4) on 2026-10-18.
The v2 endpoint is no longer used. Existing provider settings need no migration.

Adding a provider
- Add request and parser code to `MediaSegmentProviderService`.
- Normalize all times to `MediaSegmentInfo` milliseconds (`startMs` / `endMs`).
- Set `source` to a stable lowercase provider id.
- Add the provider id to `ConfigManager::getMediaSegmentProviderOrder()` defaults only when it is suitable as a default.
- Add parser and merge tests in `MediaSegmentProviderServiceTest`.

`SiloCatalogServiceTest` checks v4 requests, identity fallback, specials, movie
parameters, and the series metadata flow for Jellyfin and Silo with a controlled
network manager. These tests do not prove live timestamp coverage or playback.

Silo v2 markers retain opaque string file IDs. A file's markers carry one singular `intro`, `credits`, `recap`, and `preview` range plus `marker_segments`, every effective occurrence in source-time order, so a kind can repeat (an intro after a cold open and another midway). When `marker_segments` is not empty, `SiloModelMapper::mediaSegments` maps each occurrence to its own segment (IDs `file:kind`, then `file:kind:1`, ...); an occurrence matching the singular range keeps that range's `source` and `confidence`. Otherwise the singular fields are used. The dedicated marker read remains authoritative for the selected file.
