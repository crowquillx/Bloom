# Hybrid rounded images

Hybrid mode prefers cached pre-rounded PNGs, falls back to the shader path, and only clips in software if both are unavailable.

## Config and modes
- `ConfigManager.roundedImageMode`: `auto` (default hybrid), `prerender` (prefer cached PNGs), `shader` (skip preprocessing, shader only).
- `ConfigManager.roundedImagePreprocessEnabled`: master toggle for generating cached rounded assets (ignored when mode=`shader`).
- Env overrides: `BLOOM_ROUNDED_IMAGE_MODE`, `BLOOM_ROUNDED_PREPROCESS` / `BLOOM_ROUNDED_IMAGE_PREPROCESS` (`0/1` or `true/false`).

## Pre-rounded path (ImageCacheProvider)
- Provider artwork is identified by a token-free `ArtworkRef` cache key (`connectionId`, item, owner kind, kind/index/tag, and requested width). Authenticated URLs and headers are resolved only on a cache miss and are never persisted or logged. UI code passes the complete reference to `LibraryService.getCachedArtworkUrlFromRef(...)`; this preserves native Silo's in-memory opaque source while deriving width-specific cache identities. Transient sources remain available for the authenticated session regardless of catalog size and are cleared when account state resets.
- `ImageCacheStore` owns the SQLite index and cache files on one dedicated worker thread. That thread exclusively creates, uses, closes, and removes its named `QSQLITE` connection; image loading and rounded rendering continue on the image pool.
- Cache data uses atomic `QSaveFile` replacement. SQLite stores only SHA-256 identities and safe hash filenames, so direct or signed source URLs cannot be recovered from index pages or diagnostics.
- Startup reconciles missing files, stale rows, recorded-size drift, and orphan files. A corrupt index is discarded and rebuilt; an unavailable index disables disk caching without preventing network image loading.
- Replacement accounting subtracts the prior row's tracked size before adding the replacement. LRU eviction scans all candidates until it reaches 80% of the configured limit, continues after individual deletion failures, and reports actual removed bytes.
- Identical identity-and-size loads share one provider-owned job across disk lookup, network transfer, decode, and cache write. `QQuickImageResponse` objects are subscribers: cancelling or destroying one response does not cancel the work needed by another, and terminal jobs are removed before subscriber callbacks can re-enter the provider.
- Network attempts have a 15-second default deadline and a 20 MiB response limit. Decodes are scaled through `QImageReader` to the requested pixel size when supplied and are rejected above the 192 MiB decoded-image limit. Failed or cancelled jobs discard pending rounded requests; cache generations prevent work that predates `clearCache()` from repopulating disk state.
- `ImageCacheProvider.cacheStats()` exposes disk hits/misses, writes/replacements, evicted entries/bytes, deletion failures, recovery, image hits, network loads, coalescing, decode attempts/successes/latency, rounded generations, and current in-flight/pending gauges without exposing cache identities.
- Linux builds and portable artifacts ship Qt Image Formats so Silo's native S3 artwork and Jellyfin-compat image redirects can decode WebP responses. `ArtworkRefreshTest` guards the runtime decoder, and portable packaging rejects an artifact missing `libqwebp.so`.
- Rounded variants are stored beside originals and keyed by the `ArtworkRef` identity + radius + size. Identical generation requests share one generation reservation, including across concurrent cache lookups; generation-tagged cleanup cannot accidentally release a newer post-clear job.
- Generation triggers after cache hits/misses when preprocessing is enabled; emits `roundedImageReady(url, fileUrl)` once written.
- Defaults: radius `Theme.imageRadius`, size `640x960` (poster). Callers may pass custom radius/size to `requestRoundedImage(url, radius, w, h)`.
- Invalidation: artwork tag, radius, and size are part of the cache key; `ConfigManager.clearCache()` removes originals and rounded variants together.
- Migration: image-cache schema v3 replaces pre-hashed metadata by recreating the recoverable cache index and data files, ensuring caller-provided or signed URLs from older releases cannot remain in SQLite live, WAL, or freelist pages.

## Shader fallback
- GLSL source: `src/resources/shaders/rounded_image.frag`; compiled via `qt6_add_shaders` to `qrc:/shaders/rounded_image.frag.qsb`.
- `RoundedImage.qml` references the `.qsb` asset and uses identical radius semantics to the preprocessing path.
- One base `Image` now feeds both the shader and software fallback, eliminating the fallback's duplicate image-provider request. The pre-rounded `Image` is instantiated lazily only when that path is eligible and has a source.
- The Qt 6 RHI shader uses the required uniform-block/texture bindings, a signed-distance rounded-rectangle mask, and premultiplied masked output. GPU pixel coverage caught the former reversed bindings and transparent-corner color bleed.
- `ShaderEffectSource.live` is disabled. Source readiness, source/geometry/source-size changes, fill mode, filtering, visibility, and shader-path transitions explicitly schedule a refresh. On the final local OpenGL RHI measurement, 60 idle frames scheduled zero refreshes and a source replacement rendered correctly in 67 ms. `RoundedImagePreferredRenderPathTest` repeats the rounding, idle, and replacement checks whenever the runner exposes a shader-capable scene graph; the deterministic offscreen test separately covers software fallback, prerender pixels, and focus/hover overlays.
- Build regenerates `.qsb` automatically; keep the GLSL file for edits/regeneration.

## QML usage
- Use `RoundedImage` with config-driven mode:
  - `mode: ConfigManager.roundedImageMode`
  - `preferPreRounded: ConfigManager.roundedImagePreprocessEnabled`
  - `allowShader: ConfigManager.roundedImageMode !== "prerender"`
- Request cached rounded assets from `ImageCacheProvider.requestRoundedImage(...)` and set `preRoundedSource` when available. Example in `LibraryScreen.qml`.

## Smoke checklist
- Build: `.qsb` packaged under `:/shaders`; app loads without shader warnings.
- Functional: Library grid shows rounded posters; when rounded PNGs are missing, shader fallback renders correctly; software clip only when both paths unavailable.
- Cache: first load creates rounded variants; subsequent loads reuse them; clearing cache removes originals and rounded variants.
- Performance: on low-end hardware, compare GPU load/frame pacing between `auto`, `shader`, and `prerender` modes.
- Shader validation: on a machine with a working Qt RHI backend, run `RoundedImageTest -v1` without `BLOOM_ROUNDED_TEST_SOFTWARE`; confirm the rounded-corner checks pass, 60 idle frames schedule no refreshes, and source-replacement latency is reported. Headless software-only runners do not exercise this branch.
- Regression: focus/hover overlays still align with rounded corners; other imagery (backdrops/letterbox) remains unchanged.
