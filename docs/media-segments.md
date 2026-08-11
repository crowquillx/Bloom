Media Segment Providers

Overview
- `PlaybackService::getMediaSegments()` is the provider-neutral playback entry point.
- The active provider's server segments are loaded before any external lookup.
- Native Silo markers use `/api/v1/markers/files/{file_id}` for the selected playback version or multipart file, falling back to `/api/v1/markers/items/{content_id}` only when no file identity is available. They remain authoritative for every segment type Silo returns.
- On Jellyfin, Bloom uses the OpenAPI-listed core `/MediaSegments/{id}` route and maps tick ranges at the provider boundary. It does not probe excluded legacy plugin routes.
- Only after the server response is normalized does `MediaSegmentProviderService` query configured external providers to fill missing types.

Precedence
- Native Silo server segments win per segment type.
- Jellyfin core server segments win per segment type when the Jellyfin provider is active. An empty core response is valid and allows configured external providers to fill missing types.
- External provider order defaults to TheIntroDB, then IntroDB.
- `mergeSegmentsByType` keeps the first server segment for each type and only adds a later provider's type when no server segment of that type exists. For example, a server intro plus TheIntroDB intro/credits keeps the server intro and adds only credits.
Provider parsing
- TheIntroDB v2 uses `GET https://api.theintrodb.org/v2/media?tmdb_id=...`; TV requests also include `season` and `episode`. Reads are anonymous.
- TheIntroDB segment arrays map `intro` to Intro, `recap` to Recap, `credits` to Outro, and `preview` to Preview. `start_ms: null` means zero; `end_ms: null` uses the item duration if known or the segment is dropped.
- IntroDB uses `GET https://api.introdb.app/segments?imdb_id=...&season=...&episode=...`. Reads are anonymous. It maps `intro`, `recap`, and `outro` to Bloom segment types.
- Invalid external segments are dropped when the end is missing, the end is not after the start, or a timestamp is negative.

Adding a provider
- Add request and parser code to `MediaSegmentProviderService`.
- Normalize all times to `MediaSegmentInfo` milliseconds (`startMs` / `endMs`).
- Set `source` to a stable lowercase provider id.
- Add the provider id to `ConfigManager::getMediaSegmentProviderOrder()` defaults only when it is suitable as a default.
- Add parser and merge tests in `MediaSegmentProviderServiceTest`.
