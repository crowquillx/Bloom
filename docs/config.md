ConfigManager & Runtime Settings

Overview
- `ConfigManager` provides app configuration management and exposes Q_PROPERTY bindings for QML.
- The main config file is `app.json` stored in platform-specific locations:
  - Linux: `~/.config/Bloom/app.json`
  - Windows: `%APPDATA%/Bloom/app.json`

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

When adding settings
- Update `ConfigManager.h` (Q_PROPERTY & signals).
- Implement getters/setters in `ConfigManager.cpp` that persist to `app.json` and emit signals on change.
- Update docs (this file) and add UI controls in `SettingsScreen.qml` where appropriate.

Cache settings
- `settings.cache.image_cache_size_mb` controls the disk image cache size in megabytes. Default 500; minimum enforced at 50MB; config-only (no UI).

UI settings
- `settings.ui.launch_in_fullscreen` (Q_PROPERTY `launchInFullscreen`): When true, the application starts in fullscreen mode. Default false; configurable via Settings > Display > Launch in Fullscreen.
- `settings.ui.backdrop_rotation_interval`: Backdrop rotation interval in milliseconds. Default 30000 (30 seconds).

Display settings
- `settings.manualDpiScaleOverride` (Q_PROPERTY `manualDpiScaleOverride`): Manual DPI scale multiplier applied to automatic DPI detection. Range: 0.5 to 2.0. Default: 1.0 (automatic, no override).
  - Values below 1.0 decrease content size (e.g., 0.8 = 20% smaller)
  - Values above 1.0 increase content size (e.g., 1.2 = 20% larger)
  - Useful for fine-tuning content size on high-DPI displays (4K@300% scaling), unusual display configurations, or accessibility needs
  - Configurable via Settings > Display > Content Scale Override slider
  - The final DPI scale is calculated as: `baseScale * manualDpiScaleOverride`, where baseScale is determined by screen resolution and device pixel ratio