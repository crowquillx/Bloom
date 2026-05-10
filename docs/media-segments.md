Media Segment Providers

Overview
- `PlaybackService::getMediaSegments()` is the only playback-facing entry point.
- Jellyfin server segments are loaded first from Intro Skipper when available.
- `MediaSegmentProviderService` can then query external providers and merge only missing segment types.

Precedence
- Jellyfin server segments always win for their type.
- Provider order defaults to TheIntroDB, then IntroDB.
- If Jellyfin provides an intro and TheIntroDB provides intro plus credits, Bloom keeps Jellyfin's intro and adds TheIntroDB credits.

Provider parsing
- TheIntroDB v2 uses `GET https://api.theintrodb.org/v2/media?tmdb_id=...`; TV requests also include `season` and `episode`. Reads are anonymous.
- TheIntroDB segment arrays map `intro` to Intro, `recap` to Recap, `credits` to Outro, and `preview` to Preview. `start_ms: null` means zero; `end_ms: null` uses the item duration if known or the segment is dropped.
- IntroDB uses `GET https://api.introdb.app/segments?imdb_id=...&season=...&episode=...`. Reads are anonymous. It maps `intro`, `recap`, and `outro` to Bloom segment types.
- Invalid external segments are dropped when the end is missing, the end is not after the start, or a timestamp is negative.

Adding a provider
- Add request and parser code to `MediaSegmentProviderService`.
- Normalize all times to `MediaSegmentInfo` ticks.
- Set `source` to a stable lowercase provider id.
- Add the provider id to `ConfigManager::getMediaSegmentProviderOrder()` defaults only when it is suitable as a default.
- Add parser and merge tests in `MediaSegmentProviderServiceTest`.
