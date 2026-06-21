ConfigManager & Runtime Settings

Overview
- `ConfigManager` provides app configuration management and exposes Q_PROPERTY bindings for QML.
- The main config file is `app.json` stored in platform-specific locations:
  - Linux: `~/.config/Bloom/app.json`
  - Windows: `%APPDATA%/Bloom/app.json`
- On Windows, Bloom automatically migrates legacy nested config folders (for example `%APPDATA%/Bloom/Bloom`) into `%APPDATA%/Bloom` when needed.

Responsibilities
- Persist session information: server URL, access tokens (obfuscated or protected as feasible).
- Persist app-specific settings: `playbackCompletionThreshold`, `autoplayNextEpisode`, `backdropRotationInterval`, mpv extra flags.
- Emit change signals to allow QML to react to setting updates.

Best practices
- Add Q_PROPERTY for each new setting to enable reactive QML bindings.
- Read settings at point-of-use in C++ to avoid stale cache; if you must cache, connect to change signals to refresh the cache.
- For QML, prefer binding directly to `ConfigManager.someProperty` instead of copying values into local properties (so they update automatically when configurations change).

mpv config directory
- `~/.config/Bloom/mpv/` is optional and can be used to store `mpv.conf`, `input.conf`, and scripts.
- Pass `--config-dir` and any extra mpv flags through `PlayerProcessManager` which `ConfigManager` can configure at runtime.

Session management
- `setJellyfinSession()` to persist credentials; `clearJellyfinSession()` to clear them.
- `getJellyfinSession()` is used at app startup to attempt session restoration.
- Handle 401 errors by emitting `sessionExpired()` and invoking logout/clear sessions.

Config API sample (high level)
- `getMpvConfigArgs()` returns an array of command line args to pass to mpv.
- `playbackCompletionThreshold` is a Q_PROPERTY (default 90) and `autoplayNextEpisode` default true.
- `skipButtonAutoHideSeconds` is a Q_PROPERTY (default 6; range 0-120) for skip intro/credits popup visibility timing.
- `autoSkipIntro` and `autoSkipOutro` are Q_PROPERTY booleans (both default false) for one-time-per-playback intro/credits auto-skip.
- `defaultAudioTrackSelection` and `defaultSubtitleTrackSelection` are Q_PROPERTY strings stored at `settings.playback.default_audio_track_selection` and `settings.playback.default_subtitle_track_selection`. Defaults are `jellyfin-default`. Valid values are `jellyfin-default`, `file-default`, supported language codes such as `eng`/`jpn`/`spa`, and subtitle-only `off` or `forced`.
- `playbackVolume` and `playbackMuted` persist the last playback volume/mute state across app restarts (`settings.playback.playback_volume`, `settings.playback.playback_muted`).
- `audioOutputDevice` is a Q_PROPERTY string stored at `settings.playback.audio_output_device` (default `auto`). `auto` is canonicalized case-insensitively (so empty, `AUTO`, and `Auto` are treated as `auto`) and follows the system default output device; a specific value is an mpv audio-device id (e.g. `wasapi/{...}`) passed to mpv via `--audio-device`. See [docs/playback.md](playback.md) for the hotplug/reconnect behavior.
- `autoplayCountdownSeconds` drives the countdown shown before autoplay launches the next episode; the default is 10 seconds and disabling autoplay simply skips the timer.
- `mergeContinueWatchingWithNextUp` controls Home row grouping (default false): when false, in-progress episodes render in a separate **Continue Watching** row above **Next Up**; when true they are merged back into **Next Up**.
- `uiSoundsEnabled` / `uiSoundsVolume` keep the remote/keyboard navigation sounds available (default on at level 3) or let you mute them without touching the media volume slider.
- `performanceModeEnabled` allows the player backend to trim VRAM more aggressively when true (default false).
- `seerrBaseUrl` and `seerrApiKey` persist Seerr/Jellyseerr connection details (`settings.seerr.base_url`, `settings.seerr.api_key`).
- `externalSegmentProvidersEnabled` and `mediaSegmentProviderOrder` persist external intro/recap/credits provider settings under `settings.media_segments`. Defaults: enabled, provider order `["theintrodb", "introdb"]`. External provider reads are anonymous.

MPV profile management
- `settings.mpv_profiles` contains the base set of profiles (`Low Quality`, `Medium Quality`, `High Quality`, `ArtCNN`, `ArtCNN-Deband`, `nnedi3`, `nnedi3-deband`, plus any user-created profiles) and their structured options (hwdec, interpolation, Windows render API, extra args).
- `settings.default_profile` names the profile to use when no library/series override is present.
- `settings.library_profiles` and `settings.series_profiles` hold dictionaries keyed by Jellyfin library/series IDs when an override is required at a higher level.
- Playback requests may omit `libraryId` in direct-navigation contexts such as Home > Continue Watching / Next Up. QML should pass any known context but must not block playback waiting for a library id; `PlayerController` recovers episodic library context by resolving series ancestors to the top-level Jellyfin library (`CollectionFolder`) before starting mpv, then falls back to the default profile if no mapping is available.
- Config v21 renames the old built-in `Default` profile to `Medium Quality`, rewrites default/library/series assignments from `Default` to `Medium Quality`, removes `Default` as a built-in, and preserves user-modified old built-ins as custom backup profiles before installing canonical built-ins.
- Config v22 adds the protected `ArtCNN`, `ArtCNN-Deband`, `nnedi3`, and `nnedi3-deband` built-ins without changing existing default, library, or series assignments.
- Config v23 adds profile-level `hdr_metadata_mode` and `windows_10bit_output` fields. Missing fields default to `target` and `false`; canonical shader-heavy anime built-ins move to `windows_render_api=vulkan` unless the stored profile was customized.
- Config v24 adds startup buffering mode settings. `settings.playback.startup_buffering_mode` defaults to `normal`; `settings.library_startup_buffering_modes` stores optional per-library overrides keyed by Jellyfin library ID. Valid values are `normal` and `remote-mount`; missing library entries use the global value.
- Built-in profile behavior: `Low Quality` uses mpv `--profile=fast`; `Medium Quality` uses `--profile=high-quality`; `High Quality` uses `--profile=high-quality` plus bundled FSRCNNX, KrigBilateral, and SSimDownscaler GLSL shaders loaded from `~~/shaders/...`. The ArtCNN/nnedi3 profiles use `--profile=high-quality`, `--hwdec=no`, a single selected bundled shader, Gandhi Sans Bold subtitle styling from `~~/fonts`, and optional mpv deband tuning for the `-Deband` variants.
- Settings > MPV > Edit Profiles > Import Config creates a new profile from an existing `mpv.conf`. v1 imports only global top-level options before the first `[profile]` section and stores them as normalized `extra_args` entries (`--option=value` or `--option`) in `settings.mpv_profiles`; it never overwrites an existing profile.
- MPV profile import/save/load normalizes one surrounding quote pair from `--option=value` and stores common shader aliases (`--glsl-shader`, `--glsl-shader-append`, `--glsl-shaders`, `--glsl-shaders-append`) as ordered `--glsl-shaders-append=...` entries. On playback, Bloom emits a single `--glsl-shaders-clr` before the profile shader appends so multiple shaders load in profile order while replacing any inherited shader list.
- MPV profiles support `windows_render_api` for Windows embedded playback. Values are `auto`, `d3d11`, or `vulkan`; missing or unknown values load as `auto`. `auto` keeps mpv's render backend choice, `d3d11` forces `gpu-api=d3d11`/`gpu-context=d3d11`, and `vulkan` forces `gpu-api=vulkan`/`gpu-context=winvk`.
- MPV profiles support `hdr_metadata_mode` for HDR output metadata hints. Values are `target` and `source-dynamic`; missing or unknown values load as `target`. Bloom still applies `--target-colorspace-hint=auto` for HDR output, and only changes `--target-colorspace-hint-mode` from the profile when HDR output is active.
- MPV profiles support `windows_10bit_output` for Windows embedded D3D11 playback. When true and `windows_render_api=d3d11`, Bloom applies `d3d11-output-format=rgb10_a2`; Vulkan and auto render paths ignore the toggle.
- Custom MPV profiles can be renamed from Settings > MPV > Edit Profiles. Renaming preserves default, library, and series assignments; built-in profiles cannot be renamed or deleted.
- Import accepts `option=value`, `--option=value`, `option`, and `--option`. Blank lines and full-line `#` comments are ignored. Unsupported lines are skipped and profile sections are ignored in v1.
- Import and manual extra-arg saving filter options Bloom manages during playback so profiles cannot override backend/render plumbing. Filtered names include `config-dir`, `config`, `input-conf`, `include`, `script`, `script-opts`, `scripts`, `osc`, `no-osc`, arbitrary `profile` values other than safe built-ins (`fast`, `high-quality`), `fullscreen`, `wid`, `input-ipc-server`, `idle`, `vo`, `hwdec`, `gpu-context`, `gpu-api`, and `vulkan-*`, `d3d11-*`, `opengl-*`, `wayland-*`, `x11-*`.

When adding settings
- Update `ConfigManager.h` (Q_PROPERTY & signals).
- Implement getters/setters in `ConfigManager.cpp` that persist to `app.json` and emit signals on change.
- Update docs (this file) and add UI controls in `SettingsScreen.qml` where appropriate.

Logging settings
- `settings.logging.level` controls file/console verbosity. Valid values: `info` (default), `debug`, `quiet`. Default `info` suppresses uncategorized `qDebug()` spam (for example view-model cache traces) and disables debug output for noisy Qt categories such as `bloom.imagecache` and `bloom.library`.
- Default `info` level silences routine **image** (`bloom.imagecache`), **library cache** (`bloom.viewmodels`, `bloom.librarycache`, `bloom.cache`), and **playback trace** noise while keeping **all warnings and errors** (including image load failures).
- `settings.logging.qt_rules` optional Qt logging category rules (newline-separated) appended to Bloom defaults. Use for targeted diagnostics, for example `bloom.imagecache.debug=true`. The `QT_LOGGING_RULES` environment variable is still honored by Qt before startup.
- Pass `--verbose` / `-v` on the command line to force full debug logging for one session (overrides `settings.logging.level`).
- **Settings UI:** Settings → About & Account → **Log Level** (same section as updates). Changes apply immediately without restart.


### Logging categories (for `qt_rules`)

At the default `info` level, Bloom applies filter rules in `src/utils/LoggingConfig.cpp` (`LoggingConfig::defaultQtRules()`). The table below lists every category used in the codebase. **Warnings and errors are never filtered** for any category.

| Category | Purpose | Routine output hidden at `info` |
|----------|---------|----------------------------------|
| `bloom.imagecache` | Poster/backdrop disk + memory image cache | debug, info |
| `bloom.viewmodels` | Library/series/movie view-model cache & SWR | debug, info |
| `bloom.librarycache` | SQLite library list cache | debug, info |
| `bloom.cache` | Detail-view JSON cache files | debug, info |
| `bloom.library` | Jellyfin library API requests | debug, info |
| `bloom.auth` | Login, session, keychain | debug |
| `bloom.config` | Config load/save | debug |
| `bloom.app` | Application startup / service wiring | debug |
| `bloom.ui` | Fonts, responsive layout | debug |
| `bloom.ui.scenegraph` | Qt Quick scene graph diagnostics | debug |
| `bloom.playback` | Playback controller | debug |
| `bloom.playback.ipc` | mpv JSON IPC traffic | debug |
| `bloom.playback.trickplay` | Trickplay tile processing | debug |
| `bloom.playback.trace` | Playback state-machine trace | debug, info |
| `bloom.playback.displaytrace` | Display refresh rate / HDR switching | debug |
| `bloom.playback.backend.linux.libmpv` | Embedded libmpv on Linux | debug |
| `bloom.playback.backend.windows.libmpv` | Embedded libmpv on Windows | debug |
| `bloom.playback.backend.factory` | Player backend selection | debug |
| `bloom.playback.backend.*` | Wildcard for all backend categories | debug |
| `bloom.gpu.trim` | GPU memory trimming | debug |
| `bloom.mediaSegments` | Intro/credits segment providers | debug |
| `jellyfin.network` | Shared Jellyfin network types/helpers | debug |
| `bloom.test` | Test mode / mock services | debug |

Example — debug image cache only:

```json
"qt_rules": "bloom.imagecache.debug=true\nbloom.imagecache.info=true"
```

Cache settings
- `settings.cache.image_cache_size_mb` controls the disk image cache size in megabytes. Default 500; minimum enforced at 50MB; config-only (no UI).
- `settings.cache.rounded_image_mode` controls how rounded thumbnails are generated and cached. Defaults to `auto` (platform decides); you can force `always` (preprocess every image) or `never`.
- `settings.cache.rounded_preprocess_enabled` toggles whether rounded thumbnails are created ahead of time; keep it true for smooth UI animations or turn it off if caching takes too much work on low-end devices.

UI settings
- `settings.ui.theme` (Q_PROPERTY `theme`): Base visual theme. Built-ins are `Jellyfin`, `Rosé Pine`, and `Catppuccin`; default `Jellyfin`.
- `settings.ui.theme_flavor` (Q_PROPERTY `themeFlavor`): Optional lowercase flavor ID for themes that expose a Flavor control. Catppuccin defaults to `mocha`; Rosé Pine defaults to `main`; themes without flavors use an empty string.
- `settings.ui.theme_color_scheme` (Q_PROPERTY `themeColorScheme`): Optional lowercase accent/color-scheme ID for themes that expose a Color Scheme control. Existing Jellyfin installs migrate to `blue`; Catppuccin defaults to `mauve`.
- `settings.ui.launch_in_fullscreen` (Q_PROPERTY `launchInFullscreen`): When true, the application starts in fullscreen mode. Default true; configurable via Settings > Display > Launch in Fullscreen.
- `settings.ui.backdrop_rotation_interval`: Backdrop rotation interval in milliseconds. Default 30000 (30 seconds).
- `settings.ui.ui_animations_enabled` (Q_PROPERTY `uiAnimationsEnabled`): Toggle the QML transition/animation flourishes for keyboard/remote navigation; defaults to true but can be toggled to reduce GPU load.

Update settings
- `settings.updates.channel` (Q_PROPERTY `updateChannel`): Update track to query. Valid values are `stable` and `dev`. Default `stable`.
- `settings.updates.auto_check_enabled` (Q_PROPERTY `autoUpdateCheckEnabled`): Whether Bloom checks for updates automatically at startup. Default false; manual checks remain available from Settings > Updates.
- `settings.updates.last_check_at` (Q_PROPERTY `lastUpdateCheckAt`): UTC ISO-8601 timestamp of the last successful manifest check. Empty means Bloom has never checked.
- `settings.updates.skipped_update_version` (Q_PROPERTY `skippedUpdateVersion`): Stores the last dismissed startup-popup marker so the same update is not re-prompted on every launch.

Display settings
- `settings.manualDpiScaleOverride` (Q_PROPERTY `manualDpiScaleOverride`): Manual DPI scale multiplier applied to automatic DPI detection. Range: 0.5 to 2.0. Default: 1.0 (automatic, no override).
  - Values below 1.0 decrease content size (e.g., 0.8 = 20% smaller)
  - Values above 1.0 increase content size (e.g., 1.2 = 20% larger)
  - Useful for fine-tuning content size on high-DPI displays (4K@300% scaling), unusual display configurations, or accessibility needs
  - Configurable via Settings > Display > Content Scale Override slider
  - The final DPI scale is calculated as: `baseScale * manualDpiScaleOverride`, where baseScale is determined by screen resolution and device pixel ratio

Video settings
- `settings.video.enable_framerate_matching` (Q_PROPERTY `enableFramerateMatching`): When true, automatically switches the display refresh rate to match the video content framerate. Default: false. Configurable via Settings > Video > Enable Framerate Matching.
- `settings.video.skip_refresh_rate_on_compatible_multiple` (Q_PROPERTY `skipRefreshRateOnCompatibleMultiple`): When true, Bloom will skip the refresh rate switch if the current display refresh rate is already an integer multiple of the video framerate (for example 120Hz with 23.976/24fps content). When false, Bloom will attempt to switch to the exact video framerate, though DisplayManager::setRefreshRate still skips switching when the current rate already matches the target within a tight tolerance. On Windows, integer-reported fractional modes are matched by family (23Hz for 23.976, 29Hz for 29.97, 59Hz for 59.94) so true 24/30/60Hz modes are not mistaken for fractional targets. Default: false. Configurable via Settings > Video > Skip switch when refresh rate is already a multiple.
- `settings.video.framerate_match_delay` (Q_PROPERTY `framerateMatchDelay`): Seconds to wait after switching refresh rate before starting playback. Range: 0-5 seconds. Default: 1 second.
  - This delay allows the display and GPU to stabilize after the mode switch, preventing dropped frames that can occur if playback starts immediately
  - The delay is only applied when a real refresh-rate mode switch was performed
  - Especially important on Windows where display mode changes can be asynchronous
  - Set to 0 to disable the delay (not recommended if experiencing dropped frames)
  - Configurable via Settings > Video > Refresh Rate Switch Delay slider (always visible; disabled/grayed out when Framerate Matching is off, interactive only when enabled)
  - When Bloom performs or confirms an exact refresh-rate match, mpv is started with an explicit `--display-fps` and `--video-sync=display-resample` override so fractional modes reported by the OS as integer 23/29/59Hz are paced as 23.976/29.97/59.94Hz.
- `settings.video.enable_hdr` (Q_PROPERTY `enableHDR`): Master HDR switch. Default: false. Configurable via Settings > Video > Enable HDR.
  - When false, Bloom keeps output in SDR and locally tone-maps detected HDR/Dolby Vision content through mpv/libplacebo.
  - When true, Bloom defaults to match-content behavior: SDR items stay SDR, and HDR-capable items attempt HDR display output.
  - HDR-specific mpv arguments are applied only when the current item is detected as HDR content (not for SDR items).
  - For HDR content, Bloom waits briefly for the HDR mode switch to settle before applying framerate matching. This avoids some Windows/TV/GPU combinations reverting to 60Hz after HDR is enabled.
  - Bloom also snapshots the pre-HDR refresh rate and uses it as the restore target when playback ends.
  - This pre-HDR restore path is applied even when framerate matching is disabled.
- `settings.video.hdr_output_mode` (Q_PROPERTY `hdrOutputMode`): HDR output policy. Default: `match-content`. Configurable via Settings > Video > Advanced > HDR Output Mode.
  - `match-content`: SDR in SDR, HDR in HDR when supported.
  - `tone-map-to-sdr`: force local SDR output for HDR/Dolby Vision content.
  - `force-hdr-experimental`: force HDR output hints for validation/debugging.
  - Windows embedded libmpv preserves these output decisions while allowing per-profile `windows_render_api` selection; unsafe raw render-context overrides such as `gpu-api`, `gpu-context`, `vulkan-*`, and `d3d11-*` are filtered.
- `settings.video.dolby_vision_fallback_mode` (Q_PROPERTY `dolbyVisionFallbackMode`): Dolby Vision fallback policy. Default: `prefer-compatible-hdr`. Configurable via Settings > Video > Advanced > Dolby Vision Fallback.
  - `prefer-compatible-hdr`: use HDR-compatible Dolby Vision profile 7/8 paths as HDR; unsupported profiles locally tone-map to SDR.
  - `tone-map-unsupported`: locally tone-map unsupported Dolby Vision to SDR.
  - `experimental-direct-play`: allow uncertain Dolby Vision direct playback for validation.
- `settings.video.linux_refresh_rate_command`, `settings.video.linux_hdr_command`, and `settings.video.windows_custom_hdr_command` store the optional OS-specific commands that Bloom executes when switching refresh rate or HDR modes; leave them blank to use the bundled defaults or specify custom commands for your display hardware.
# Hero banner

Home hero settings are stored under `settings.ui.hero_banner`: `enabled`, `source`, `max_items`,
`auto_cycle_enabled`, `auto_cycle_interval`, `backdrop_sync_enabled`, `hidden_item_types`,
`library_unwatched_only`, `library_ids`, `logo_placement`, and `info_placement`. Item count is
clamped to 1–25 and the interval to 3,000–120,000 milliseconds. An empty `library_ids` list means
all libraries. Placement values are normalized camelCase strings (`bottomLeft`, `topRight`,
`center`, `centerLarge` for logo only); unknown values fall back to `bottomLeft`.
