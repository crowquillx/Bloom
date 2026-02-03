# Hybrid rounded images

Hybrid mode prefers cached pre-rounded PNGs, falls back to the shader path, and only clips in software if both are unavailable.

## Config and modes
- `ConfigManager.roundedImageMode`: `auto` (default hybrid), `prerender` (prefer cached PNGs), `shader` (skip preprocessing, shader only).
- `ConfigManager.roundedImagePreprocessEnabled`: master toggle for generating cached rounded assets (ignored when mode=`shader`).
- Env overrides: `BLOOM_ROUNDED_IMAGE_MODE`, `BLOOM_ROUNDED_PREPROCESS` / `BLOOM_ROUNDED_IMAGE_PREPROCESS` (`0/1` or `true/false`).

## Pre-rounded path (ImageCacheProvider)
- Rounded variants are stored beside originals in the image cache, keyed by URL + radius + size.
- Generation triggers after cache hits/misses when preprocessing is enabled; emits `roundedImageReady(url, fileUrl)` once written.
- Defaults: radius `Theme.imageRadius`, size `640x960` (poster). Callers may pass custom radius/size to `requestRoundedImage(url, radius, w, h)`.
- Invalidation: radius/size are part of the cache key; `ConfigManager.clearCache()` removes originals and rounded variants together.

## Shader fallback
- GLSL source: `src/resources/shaders/rounded_image.frag`; compiled via `qt6_add_shaders` to `qrc:/shaders/rounded_image.frag.qsb`.
- `RoundedImage.qml` references the `.qsb` asset and uses identical radius semantics to the preprocessing path.
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
- Regression: focus/hover overlays still align with rounded corners; other imagery (backdrops/letterbox) remains unchanged.
