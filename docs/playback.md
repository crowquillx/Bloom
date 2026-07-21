Playback — mpv & Jellyfin Integration

Overview
- Active production policy is platform-specific:
  - Windows always uses embedded libmpv (`win-libmpv`).
  - Linux keeps current fallback behavior; embedded libmpv remains experimental.
- Linux embedded libmpv backend (`linux-libmpv-opengl`) is currently experimental and less tested than Windows/external playback paths.
- Linux Wayland sessions currently default to `external-mpv-ipc` unless explicitly opted in (`BLOOM_ENABLE_WAYLAND_LIBMPV=1`) due to unresolved embedded render-path issues on some compositor/GPU combinations.
- Linux non-Wayland runtime selection still attempts embedded backend when runtime requirements are met, with automatic fallback to `external-mpv-ipc` when unsupported.
- Windows uses `win-libmpv`, which launches mpv with HWND embedding (`--wid`) against the attached `VideoSurface` target while preserving the external backend process path for non-Windows rollback/testing.
- Other non-Linux platforms keep `external-mpv-ipc` as default.
- `external-mpv-ipc` remains fully supported as an explicit backend override on non-Windows platforms via `BLOOM_PLAYER_BACKEND=external-mpv-ipc`.
- Flatpak bundles mpv and supports normal playback, but intentionally hides host
  power actions and arbitrary refresh-rate/HDR command settings because those
  operations are not safely available inside the sandbox.
- Bundled mpv Lua UI/trickplay scripts are retired; playback controls/overlay behavior are handled natively by Bloom UI.

Backend architecture
- Playback now routes through `IPlayerBackend` (`src/player/backend/IPlayerBackend.h`).
- `PlayerController` depends on the backend interface (not directly on `PlayerProcessManager`).
- Default backend is platform-aware via `PlayerBackendFactory` (Linux prefers embedded backend when runtime-supported; Windows defaults to `win-libmpv`; others default external).
- Active backend is logged at startup from `ApplicationInitializer`.
- Optional environment override for backend selection: `BLOOM_PLAYER_BACKEND`.
- Optional config backend preference: `settings.playback.player_backend` in `app.json` (`external-mpv-ipc`, `linux-libmpv-opengl`, `win-libmpv`, or unset for platform default).
- Selection precedence is now:
  - Windows: forced `win-libmpv` (env/config backend override values are ignored).
  - Non-Windows: `BLOOM_PLAYER_BACKEND` env override -> config `player_backend` -> platform default.
- Unknown backend names safely resolve to `external-mpv-ipc`.

Windows embedded backend details
- Added `WindowsMpvBackend` scaffold under `src/player/backend/`.
- Selector token: `win-libmpv`.
- Windows backend now resolves target `winId` from `MpvVideoItem`, creates a dedicated child host window, and keeps host geometry synced to `VideoSurface` viewport updates for embedded playback.
- Windows backend now attempts a direct libmpv control path first (`mpv_create`/`mpv_initialize`/`mpv_command_node_async`/`mpv_observe_property`/`mpv_wait_event`) while preserving the `PlayerController` contract.
- If direct libmpv initialization/load fails (or libmpv is unavailable at build time), `win-libmpv` reports an error; no implicit alternate backend is used.
- Playback controls are now exercised through the direct Windows backend command path in the same migration slice: play/pause/resume/seek/stop plus audio/subtitle property commands.
- Added `EmbeddedPlaybackOverlay.qml` as a backend-agnostic overlay host/state layer used by a transparent overlay window on Windows, intended for cross-platform overlay reuse.

Windows embedded overlay layering model
- Video is rendered by libmpv into a native child host window attached to the playback target (`--wid`).
- Playback controls are rendered in a separate transparent QML overlay window that tracks the main window geometry and sits above video.
- UX contract: showing/hiding controls must not resize, shift, or clip the video viewport.
- Credits/next-up shrink mode remains a separate feature path and must continue to work independently of normal full-frame overlay behavior.
- Terminal playback transitions are backend-first: Bloom waits for direct libmpv teardown before changing `PlayerController` into `Idle`/`Error`, and `WindowsMpvBackend` defers embedded host-window destruction while libmpv still owns the `--wid` target.
- Replacement playback requested during an in-flight terminal transition is queued until that transition finalizes, so a new item cannot start against stale embedded playback teardown state.
- Windows display restoration after playback stop is deferred and non-blocking. HDR-off settle and refresh-rate restore are scheduled after the UI returns to `Idle`, allowing the main scene and detached playback overlay window to repaint/hide promptly. When post-playback Up Next is shown, Bloom parks that restore until the user either leaves Up Next or starts another episode, avoiding a restore-then-switch cycle between consecutive episodes.
- Natural playback end, explicit stop, and error-triggered shutdown now share one coordinated terminal-transition path so reporting/autoplay work runs once per playback attempt.
- Windows direct-libmpv event handling now suppresses playback reactivation events (`START_FILE`/`FILE_LOADED`/`PLAYBACK_RESTART`/`SEEK`) while a stop command is pending, preventing stale wakeup events from re-showing the embedded host window as a black frame during teardown.
- Refresh-rate matching starts mpv with explicit display-sync options for exact matches (`--video-sync=display-resample` plus `--display-fps=<content fps>`), except when the user has chosen to keep an already compatible higher multiple such as 120Hz for 23.976fps content. This avoids pacing fractional Windows modes as integer 23/29/59Hz after Bloom switches the display.
- Windows direct-libmpv diagnostics request mpv log messages by default at `warn` level and include detailed terminal event/teardown breadcrumbs. Set `BLOOM_WINDOWS_LIBMPV_MPV_LOG=info`, `debug`, or `trace` before launching Bloom to collect more mpv-side detail for playback termination, renderer, HDR, and shader diagnosis; use `no`/`off` to disable mpv log forwarding.
- Windows embedded playback always initializes mpv with `vo=gpu-next`. Each MPV profile can opt into a Windows render API with `windows_render_api`: `auto` leaves `gpu-api`/`gpu-context` unset, `d3d11` sets `gpu-api=d3d11` and `gpu-context=d3d11`, and `vulkan` sets `gpu-api=vulkan` and `gpu-context=winvk`. Low/Medium/High built-ins stay on `auto`; shader-heavy ArtCNN/nnedi3 built-ins default to Vulkan/winvk because ArtCNN is known to behave poorly on D3D11.
- Windows D3D11 profiles can opt into `windows_10bit_output`, which applies `d3d11-output-format=rgb10_a2` only when `windows_render_api=d3d11`. Vulkan and auto render paths do not receive the D3D11 output format.
- Windows playback-start logs include the selected profile, `windows_render_api`, effective `vo`, `gpu-api`, `gpu-context`, 10-bit D3D11 output state, HDR output args, and shader filenames. Use mpv stats/log overlays with `BLOOM_WINDOWS_LIBMPV_MPV_LOG=info` or `debug` to verify input/output colorspace, HDR signaling, renderer selection, and shader application during runtime validation.
- Embedded libmpv backends (`win-libmpv`, `linux-libmpv-opengl`) apply profile shaders after `mpv_initialize` using `change-list glsl-shaders clr/append` with absolute paths resolved from Bloom's mpv config directory (`~~/shaders/...` expands to `{config}/mpv/shaders/...`). CLI-style `--glsl-shaders-append` remains for external mpv only.

Audio output device & hotplug handling
- Goal: audio should follow the device the user actually wants, including devices that connect *after* Bloom (or playback) has already started — e.g. powering on a Bluetooth headset mid-session — without restarting the app.
- Setting: `ConfigManager.audioOutputDevice` (`settings.playback.audio_output_device`, default `auto`). `auto`/empty follows the system default output device; a specific value is an mpv audio-device id such as `wasapi/{...}`. A picker lives in **Settings > Playback > Audio Output Device**, populated from the live device list plus the saved selection.
- Startup: `ConfigManager::getMpvConfigArgs()` always passes `--audio-fallback-to-null=yes` so playback keeps running (instead of erroring) when no usable device exists at a given moment, and passes `--audio-device=<id>` when a non-`auto` device is selected.
- Hotplug detection: every backend observes mpv's `audio-device-list` property and emits `IPlayerBackend::audioDeviceListChanged(devices)` (each entry is `{name, description}`). `PlayerProcessManager` parses the IPC JSON; embedded libmpv backends share `src/player/backend/MpvNodeUtils.h` to parse the `MPV_FORMAT_NODE` array.
- Reaction: `PlayerController` caches the list (exposed to QML as `availableAudioDevices`). The first snapshot after an mpv start is treated as the baseline and does not trigger a reload (the device was already applied via startup args). Subsequent changes are ignored while `auto` is selected so mpv can continue tracking the system default without an audible reload gap. For an explicit device, changes are debounced (~500ms) and then re-applied via `set_property audio-device <desired>` followed by `ao-reload`, which forces mpv to reinitialize and reopen that endpoint when it returns.
- Changing the device in Settings during playback applies immediately through the same `applyAudioOutputDevice()` path.
- Interface audio (navigation/UI sounds via `UiSoundController`, series theme songs via `ThemeSongManager`) uses QtMultimedia rather than mpv. `src/utils/AudioOutputRouter` keeps those `QAudioOutput`s pointed at the right device: it watches `QMediaDevices` (hotplug + default-device changes) and `ConfigManager.audioOutputDevice`, then reassigns the output device. `auto` follows the current system default; an explicit selection is best-effort matched from the saved mpv audio-device id to a Qt audio device, falling back to the system default when no match exists. This means UI sounds and theme songs also start working when a headset connects after launch, without restarting.

Reference implementation notes (Plezy)
- External reference: https://github.com/edde746/plezy
- Bloom should use Plezy as a design reference for mpv integration choices (embedded window lifecycle, async command/event flow, observed-property mapping, and transition/flicker mitigation patterns), not as a direct code drop.
- Because Plezy is Flutter-based and Bloom is Qt/C++, adopt the architecture decisions and sequencing, then map them onto Bloom backends (`IPlayerBackend`, `PlayerController`, Qt window/focus lifecycle, and existing services/tests).
- For Windows work, prefer Plezy’s approach as the sanity baseline for: direct libmpv control path, explicit event loop handling, and window transition handling around minimize/maximize/fullscreen.

- Plezy-aligned decisions already adopted in Bloom:
  - direct libmpv command/property/event model on Windows,
  - explicit window lifecycle handling for resize/move/minimize/maximize/fullscreen,
  - Qt/C++ adaptation boundaries preserved (no Flutter/plugin coupling),
  - direct-only `win-libmpv` behavior remains intentional (no implicit alternate backend path).

Linux embedded backend details (experimental)
- `IPlayerBackend` now includes embedded-video capability hooks:
  - `supportsEmbeddedVideo()`
  - `attachVideoTarget(...)` / `detachVideoTarget(...)`
  - `setVideoViewport(...)`
- Linux backend implementation entry point: `LinuxMpvBackend`.
- `MpvVideoItem` + `VideoSurface.qml` added for minimal embedded surface plumbing.
- `PlayerController` exposes minimal embedded-video passthrough and internal/manual shrink toggle API.
- Linux backend now includes:
  - typed `sendVariantCommand(...)` dispatch through libmpv command nodes,
  - `client-message`/`scriptMessage` forwarding parity,
  - `aid`/`sid` normalization parity with external backend contract (including node-typed mpv values like `no`/`auto`),
  - render hardening for viewport bounds/FBO-state restoration/update-callback lifecycle, including coalesced render-update scheduling during teardown/re-init,
  - stricter embedded arg filtering for render-critical options (`gpu-context`, `gpu-api`, and context-specific backend flags),
  - debug instrumentation behind `BLOOM_LINUX_LIBMPV_DEBUG=1` for runtime graphics API/FBO diagnostics,
  - automatic software-render fallback (`MPV_RENDER_API_TYPE_SW`) when OpenGL embedded rendering repeatedly reports an invalid framebuffer target on Linux runtime stacks.
- Remaining work: Linux target runtime validation matrix and any follow-up fixes from on-device testing.
- Current Linux support status: embedded path is not yet considered fully supported across compositor/driver combinations; treat `external-mpv-ipc` as the stable production path while Linux embedded validation continues.
- Controller parity hardening now preserves next-up/autoplay context across playback teardown, so next-episode prefetch/lookup flows keep the expected series/item/track state.
- Unit regression coverage now includes the mismatched-series guard for async next-episode callbacks to prevent stale-series autoplay context from being consumed.

Key components
- IPlayerBackend: playback backend contract used by `PlayerController`.
- ExternalMpvBackend: adapter that delegates to `PlayerProcessManager`.
- LinuxMpvBackend: Linux embedded backend entry point.
- LinuxMpvBackend now includes basic `mpv_handle` lifecycle, property/event observation, and a Qt Quick `beforeRendering`-driven `mpv_render_context` render path (Linux runtime validation still pending).
- MpvVideoItem / VideoSurface: minimal viewport plumbing for embedded backend integration.
- PlayerProcessManager: manages external mpv process lifetime, sockets/pipes, scripts and config dir. Observes `time-pos`, `duration`, `pause`, `aid`, and `sid` properties.
- PlayerController: provider-neutral state machine that handles play/pause/resume, listens for backend updates, manages track selection, and sends canonical playback state through `PlaybackService`.
- `IPlaybackProvider` / `JellyfinPlaybackProvider`: finalize provider PlaybackInfo into Bloom `PlaybackDescriptor` values, including the authenticated stream request, canonical timing/tracks, playback method, and session identity.
- PlaybackService: stable application façade for playback preparation and provider-specific reporting transport.
- TrackPreferencesManager: persists explicit audio/subtitle user choices to a separate JSON file using a versioned schema.

HDR and Dolby Vision policy
- Bloom keeps `enableHDR` as the master switch. With `enableHDR=false`, HDR and Dolby Vision sources are direct-played where possible and locally tone-mapped to SDR by mpv/libplacebo.
- With `enableHDR=true`, the default `hdr_output_mode=match-content` keeps SDR content in SDR and switches HDR-capable content to HDR output when the active backend/display path supports it.
- `hdr_output_mode=tone-map-to-sdr` forces local SDR output for HDR/Dolby Vision sources. `hdr_output_mode=force-hdr-experimental` is intended only for validation/debugging.
- Dolby Vision detection uses Jellyfin stream metadata including `VideoRangeType`, codec tags/IDs, and Dolby Vision profile/level fields. Profile 7/8 content is treated as HDR-compatible by default; unsupported Dolby Vision profiles are locally tone-mapped to SDR unless `dolby_vision_fallback_mode=experimental-direct-play` is selected.
- Bloom does not request server transcoding for HDR/Dolby Vision fallback. Unsupported or unavailable HDR output should prefer local tone-mapping over full transcode.
- Windows match-content playback toggles OS HDR only for HDR output playback and restores the previous state afterward. SDR playback does not enable Windows HDR just because the setting is on.
- Windows embedded libmpv keeps the HDR output policy but lets each profile select the Windows render API through `windows_render_api` and HDR metadata behavior through `hdr_metadata_mode`. `target` is the safe default for `--target-colorspace-hint-mode`; `source-dynamic` is experimental and applies only during HDR output, not SDR playback or HDR-to-SDR tone mapping. Unsafe raw render-context overrides such as `gpu-api`, `gpu-context`, `vulkan-*`, and `d3d11-*` are filtered, and unsupported mpv HDR options are logged with the option name and mpv error.
- Embedded libmpv classifies an early terminal `END_FILE` as a recoverable interruption when recent mpv/ffmpeg logs show stream failures such as premature HTTP EOF, reconnect attempts, HTTP 5xx, or I/O errors. This prevents hosted/sparse sources from closing the player when mpv reports EOF after a transient stream interruption; Linux also treats `END_FILE reason=unknown` with recent stream-failure logs as recoverable because some libmpv builds report premature remote EOF that way.
- Linux production HDR should continue to use the external mpv IPC backend unless embedded HDR output has been explicitly selected for validation.

Audio/Subtitle Track Selection
- Call `JellyfinClient::getPlaybackInfo(itemId)` to fetch `PlaybackInfoResponse` containing `MediaSources` with all available streams.
- Each `MediaSourceInfo` contains `mediaStreams` array with `MediaStreamInfo` objects describing video, audio, and subtitle tracks.
- If PlaybackInfo provides `DirectStreamUrl` or `TranscodingUrl`, `JellyfinPlaybackProvider` finalizes that provider-selected URL and appends missing auth/media-source/track query parameters. The provider's constructed `/Videos/{itemId}/stream` endpoint remains its fallback when PlaybackInfo does not include a playback URL. `PlayerController` never constructs or parses these provider endpoints.
- The server provides `defaultAudioStreamIndex` and `defaultSubtitleStreamIndex` which reflect the user's preferences set on the Jellyfin server.
- Use `PlayerController::setSelectedAudioTrack(index)` and `setSelectedSubtitleTrack(index)` to change tracks during playback via mpv IPC (`aid`, `sid` properties).
- Use `PlayerController::setSubtitleDelayMs(delayMs)`, `adjustSubtitleDelayMs(deltaMs)`, or `resetSubtitleDelay()` to retime primary subtitles during playback. Bloom stores delay in milliseconds, applies it to mpv's `sub-delay` property as fractional seconds, and accepts negative or positive offsets.
- Initial selection resolution order:
  - request-time override from the playback request/autoplay context, if valid
  - explicit saved preference for the current season/movie scope, if still valid
  - global app fallback from Settings > Playback (`Jellyfin Default`, `File Default`, common language, or subtitle `Off`/`Forced`)
  - built-in safety fallback
- Built-in audio fallback uses Jellyfin `defaultAudioStreamIndex`, then file-level `isDefault`, then the first audio stream.
- Built-in subtitle fallback uses Jellyfin `defaultSubtitleStreamIndex`, then file-level `isDefault`, then forced subtitles, then subtitles off.
- Global subtitle `Forced` chooses the first forced subtitle track; when no forced track exists, subtitles stay off (`global-forced-off`) rather than falling through to Jellyfin/file defaults (which could enable full dialogue subtitles).
- Global language fallbacks match normalized common language aliases (for example `en`/`eng`, `fr`/`fre`/`fra`, `zh`/`chi`/`zho`). Audio prefers matching default streams, then stream order. Subtitles prefer matching default streams, then regular subtitles, then forced, then SDH/hearing-impaired, then stream order.
- Runtime external subtitles can be injected with `PlayerController::addExternalSubtitleTrack(subtitleUrl, displayTitle, language[, jellyfinStreamIndexHint])`, which issues mpv `sub-add` and appends/selects a subtitle option for state sync. The `jellyfinStreamIndexHint` parameter is optional; when omitted or passed as `-1`, Bloom generates a temporary synthetic negative index for in-session selection only. If the subtitle is known to Jellyfin (for example a freshly downloaded subtitle now exposed by `PlaybackInfo`), pass its real stream index via `jellyfinStreamIndexHint` so Bloom tracks it with the canonical Jellyfin index.
- External subtitle addition is only permitted during active playback (`Playing` or `Paused`); calls during `Loading` or `Buffering` are rejected to avoid desync with the initial-track-application window.
- If `sub-add` fails, a retry call with the same `jellyfinStreamIndexHint` will resend the mpv command to re-attempt the load, provided the previous mapping has not yet been resolved.
- Canonical track mapping contract:
  - UI and reporting state use Jellyfin `MediaStream.index`.
  - Runtime mpv switching uses mapped mpv track IDs (1-based per media type order).
  - Backends emit raw mpv track IDs; `PlayerController` reverse-maps them back to Jellyfin indices.
  - Subtitle `None` is Jellyfin `-1` and is applied as `sid=no`.
  - Startup applies resolved mapped selection deterministically; URL stream indices are treated as request hints/fallback.
- Track selections are persisted per-season for TV shows and per-movie for films (see Track Preference Persistence below).
- Subtitle delay is persisted in the same season/movie scope as subtitle selection.
- All playback reporting methods include `mediaSourceId`, `audioStreamIndex`, `subtitleStreamIndex`, and `playSessionId` for proper server sync.

Track Preference Persistence
- Track preferences are stored separately from the main config in `~/.config/Bloom/track_preferences.json` and grouped by `connectionId` so identical season/movie IDs on different servers cannot collide.
- The file stores only explicit user intent. Unset preferences fall back to Jellyfin/file defaults and are not written.
- Subtitle delay is written as `subtitleDelayMs` only when non-zero.
- Global app-level audio/subtitle fallback defaults are stored in the main config, not in `track_preferences.json`.
- Preferences are loaded at startup and saved with a 1-second delay to batch multiple changes.
- Schema is versioned. Version 4 stores `scopes.<connectionId>.episodes` and `.movies`; version 2/3 files migrate into the active connection (or `_local` when signed out). Older unversioned files are intentionally discarded.

### TV Episodes (Per-Season)
- Preferences are stored by season ID, not series ID, because:
  - Different seasons may have different audio tracks available (e.g., added dubs in later seasons)
  - Subtitle indexing can vary between seasons
- When the user changes audio/subtitle track in `SeriesSeasonEpisodeView`, the preference is immediately saved.
- When navigating to a new episode in the same season, explicit preferences and subtitle delay are restored. If track preferences are missing or invalid for the new source, Bloom falls back through the standard resolution order.
- Use `PlayerController.getLastAudioTrackForSeason(seasonId)` and `getLastSubtitleTrackForSeason(seasonId)` to retrieve.
- Use `PlayerController.setExplicitSeasonAudioPreference(seasonId, index)` and `setExplicitSeasonSubtitlePreference(seasonId, index)` to save.
- `SeriesSeasonEpisodeView` preloads Jellyfin chapter metadata for the highlighted episode and keeps a `Chapters` rail between `Episodes` and `Cast & Crew`; missing chapters resolve to a quiet reserved empty state.
- Activating a chapter card reuses the normal episode playback request, including track preference/version-prompt handling, but sets `startPositionTicks` to that chapter’s start tick so playback begins at the selected chapter.

### Movies (Per-Movie)
- Preferences are stored by movie ID for rewatches.
- When the user changes audio/subtitle track in `MovieDetailsView`, the preference is immediately saved.
- `MovieDetailsView` exposes the same chapter-card playback rail above `Cast & Crew`; activating a chapter starts the movie at that chapter's start tick.
- Detail-page primary `Play` / `Resume` controls remain pressable while playback info is preparing, show a spinner in place of the normal glyph, and preserve the existing "still preparing" toast on press.
- When returning to the same movie, explicit preferences and subtitle delay are restored instead of server defaults. If none were saved, Bloom uses Jellyfin/file defaults.
- Use `PlayerController.getLastAudioTrackForMovie(movieId)` and `getLastSubtitleTrackForMovie(movieId)` to retrieve.
- Use `PlayerController.setExplicitMovieAudioPreference(movieId, index)` and `setExplicitMovieSubtitlePreference(movieId, index)` to save.

### JSON Format
```json
{
  "version": 3,
  "episodes": {
    "seasonId1": {
      "audio": { "mode": "explicit", "streamIndex": 1 },
      "subtitle": { "mode": "explicit", "streamIndex": 2 },
      "subtitleDelayMs": -125
    },
    "seasonId2": {
      "audio": { "mode": "unset" },
      "subtitle": { "mode": "off" }
    }
  },
  "movies": {
    "movieId1": {
      "audio": { "mode": "explicit", "streamIndex": 1 },
      "subtitle": { "mode": "explicit", "streamIndex": 0 }
    }
  }
}
```
- `mode: "unset"` means "use Jellyfin/file defaults".
- `mode: "off"` is distinct from unset and is primarily used for subtitles.
- The schema leaves room for future rule fields such as preferred language, forced-only, or hearing-impaired preferences.

### Navigation Path Considerations
- **HomeScreen "Next Up"**: Episode data includes `ParentId` (seasonId). `SeriesSeasonEpisodeView.qml` uses `initialSeasonId` to load the correct season.
- **Library browsing**: `currentSeasonId` is set when the user selects a season.
- **Search → Series → Season → Episode**: Season ID is captured when entering the season view.
- **Post-playback next episode**: `showEpisodeDetails` extracts seasonId from episode data if not already set.
- **Explicit episode navigation**: when `initialEpisodeId` is supplied (for example from Home -> Next Up), the season view must preserve that exact episode selection and must not silently fall back to the first unwatched item if the wrong season/model loads first.

### Next Episode Resolution
- `LibraryService::getNextUnplayedEpisode()` now resolves the best next episode locally from the full recursive episode list for the series instead of taking the first item from Jellyfin `/Shows/NextUp`.
- Canonical episode order is built from regular season/episode numbering plus special-placement fields:
  - `AirsBeforeSeasonNumber`
  - `AirsAfterSeasonNumber`
  - `AirsBeforeEpisodeNumber`
- Post-playback Up Next and next-episode prefetch use this same canonical resolver, so specials placed after a season or before a specific episode must win over later regular-season episodes.
- Missing episodes (`LocationType == "Virtual"`) are excluded from the canonical timeline.
- Resolution order:
  - If `excludeItemId` is set, treat it as the explicit anchor and return the first later not-fully-played episode.
  - Otherwise, prefer the most recently active in-progress episode (`PlaybackPositionTicks > 0`).
  - Otherwise, anchor on the most recently played episode using `UserData.LastPlayedDate` when available.
  - If no anchor exists, fall back to the first unplayed regular episode, then to the first unplayed special.
- Tie-breakers when play dates are missing or equal prefer later canonical position, then larger `PlaybackPositionTicks`.
- `excludeItemId` is used by autoplay/prefetch so the player can advance from the current item even before Jellyfin has updated watch state on the server.
- If no next episode is available, post-playback navigation still opens `UpNextScreen.qml` in an empty state. The screen must keep keyboard focus and offer actions back to Home or the series.
- The no-next empty state may show up to six TV-series recommendations via `UpNextRecommendationsViewModel`. Jellyfin similar series are listed first; Seerr TV recommendations are appended when the source series has a TMDB id and Seerr is configured. Recommendation provider failures are silent on this screen.
- Leaving the Up Next interstitial keeps the exact resolved episode context, including season-0 specials, by carrying the resolved episode id plus its `ParentId`/`SeasonId` directly into `SeriesSeasonEpisodeView`. When playback started from an existing library/episode-list screen, Up Next restores that screen in place; fallback direct navigation is used only when no reusable context exists. Exiting Up Next without starting playback releases any parked display restore.

UI Components for Track Selection
- `TrackSelector.qml`: Reusable dropdown component for selecting audio/subtitle tracks with keyboard navigation.
- `MediaInfoPanel.qml`: Displays video info (resolution, codec, HDR) and contains audio/subtitle `TrackSelector` components.
- `SeriesSeasonEpisodeView.qml` and `MovieDetailsView.qml`: Integrate `MediaInfoPanel` to show track selection before playback.
- Explicit user play actions now route through `PlayerController::requestPlayback(...)` instead of assembling stream URLs in QML. This keeps version selection, multipart resolution, and track-hint handling consistent across detail pages, quick play, and Up Next manual play.
- `MediaSourceSelectionDialog.qml` is the global version picker for entries with multiple Jellyfin media sources (for example 1080p vs 4K). It is opened from `Main.qml`, uses the app theme/focus patterns, supports keyboard-only selection, and restores focus with `Qt.callLater(...)` on close.
- `EmbeddedPlaybackOverlay.qml`: Native 10-foot playback overlay (top metadata bar + bottom transport row) rendered in the dedicated transparent overlay window for Windows embedded playback.
  - Active playback chrome uses theme-derived top and bottom regional shadows so metadata, transport controls, and chapter cards stay readable over video in both dark and light theme variants.
  - Left group: audio/subtitle icon buttons (runtime track cycling via `PlayerController`).
  - The subtitle panel includes subtitle delay controls. Plus/minus adjust the saved delay by 1 ms and reset returns the current season/movie scope to zero delay.
  - Center group: skip back 10s, previous chapter, play/pause, next chapter, skip forward 10s.
  - Right group: volume icon button opens a native volume panel (slider + muted state) with left/right keyboard/gamepad adjustment and Enter/Space mute toggle.
  - Direct playback shortcuts are resolved through `InputBindingManager` in the `playback` runtime context so keyboard and controller bindings are configurable in Settings > Input. Defaults include `+`/`=` or media volume-up for volume up, `-` or media volume-down for volume down, `V` for the volume panel, `A` for audio tracks, and `S`/`T`/`C` for subtitles.
  - Default controller playback bindings use D-pad/left stick for playback overlay navigation or hidden-overlay seek behavior, A for confirm/Skip Intro/Credits, B for Escape-style dismiss then stop, Start for play/pause, LB/RB for seek, LT/RT for chapters, Y/X for audio/subtitle selectors, Back/View for volume, right stick vertical for volume, L3 for ASS subtitle override, and R3 for deband.
  - Progress row: clickable seek track, current/total time labels, and keyboard seek via left/right.
  - Escape and controller B first dismiss visible playback chrome (full controls, seek preview, skip-intro popup, or open track/volume panels); a second press stops playback when the overlay is already hidden. The header back control exits playback immediately while controls are visible.
  - Trickplay preview bubble: renders processed Jellyfin trickplay thumbnails from `PlayerController` and is hidden entirely when trickplay images are unavailable.
  - Chapter mode: `Down` enters a Jellyfin-backed horizontal chapter rail when the active item exposes `Chapters`; `Up` returns to transport controls and `Escape`/`Back` hides the overlay. `Left`/`Right` browse chapter cards, while `Enter`/`Space` seeks to the focused chapter and keeps the rail open until normal inactivity dismissal.
  - Chapter cards use the item chapter thumbnail when Jellyfin provides usable chapter image metadata. Missing chapter artwork renders a neutral themed placeholder tile instead of reusing episode/poster artwork.
  - Chapter thumbnail diagnostics are logged during playback chapter normalization: loaded chapter metadata, generated Jellyfin chapter-image URLs, and image-cache transport/decode failures. These logs are intended for server/client compatibility debugging when the rail falls back to placeholders.
  - Intro/outro skip UX: transient "Skip Intro"/"Skip Credits" pop-up button auto-focuses when a segment window starts, then a compact persistent skip button remains available until that segment ends.
    - Popup timing is controlled by `ConfigManager.skipButtonAutoHideSeconds` (`settings.playback.skip_button_auto_hide_seconds`, range 0-120; 0 disables popup only). The popup is still dismissed as soon as the active intro/outro segment ends.
    - Optional automatic skip is controlled by `ConfigManager.autoSkipIntro` and `ConfigManager.autoSkipOutro`; each auto-skip applies at most once per playback item even if the user seeks back.
    - Segment source precedence is Jellyfin server data first, then configured external providers. Server segments win per segment type; external providers may fill missing types such as recap, intro, credits, or preview.
    - External provider order defaults to TheIntroDB then IntroDB. Reads are anonymous. TheIntroDB requires TMDB metadata; IntroDB requires series IMDb metadata plus season/episode numbers.

Playback overlay metadata
- `PlayerController` exposes `overlayTitle`, `overlaySubtitle`, `overlayBackdropUrl`, and `overlayLogoUrl` for the native overlay (header, buffering card, and backdrop).
- Optional `overlayLogoUrl` is a Jellyfin logo image URL (typically the `image://cached/...` form from `LibraryService::getCachedImageUrlWithWidth`). `EmbeddedPlaybackOverlay.qml` shows the logo when the URL is non-empty and the image reaches `Image.Ready`; on load failure (`Image.Error`) or when the URL is empty, the overlay falls back to `overlayTitle` text.
- When a logo is present, the overlay metadata block keeps `overlaySubtitle` visually centered beneath it in both the buffering card and the active playback header; text fallbacks use a heavier weight for readability.
- `PlayerController` also exposes normalized playback chapters, the currently playing chapter index derived from live playback position, and a seek-by-chapter action. Focused rail selection remains local UI state so playback progress never steals navigation focus while browsing.
- Detail views set metadata before playback starts:
  - Movies: title + production year.
  - Episodes: series title + `Sxx Exx - Episode Name`.
- Fallback behavior still exists (`currentItemId`) when explicit metadata is not provided.

Alternate versions and multipart playback
- Jellyfin alternate media sources are surfaced from `PlaybackInfoResponse.mediaSources`. When there is more than one source and playback is user-initiated, Bloom now prompts before playback starts instead of silently taking the first source.
- Automatic next-episode playback does not re-prompt. `PlayerController` keeps a runtime version affinity from the chosen source and reuses it for the next episode in this order:
  - same parent folder path
  - same media-source name
  - same normalized video/container/bitrate signature
  - first available source
- Jellyfin multipart items are resolved via `/Videos/{itemId}/AdditionalParts`. Bloom fetches `PlaybackInfo` for each part, matches each later part against the chosen root version with the same affinity order, and falls back to that part’s first source when no match exists.
- Multipart playback is queued into the backend playlist with `appendUrlsToPlaylist(...)` so part 2 starts seamlessly after part 1 without returning to idle.
- User-initiated playback uses request-scoped `PlaybackInfo` and `AdditionalParts` responses so prefetch responses for the same item cannot satisfy a newer play request. If the primary `PlaybackInfo` request fails or returns no media sources, Bloom surfaces a playback error instead of falling back to a plain stream URL.

Multipart reporting model
- Bloom keeps two playback identities for multipart items:
  - logical item: the original movie/episode shown in the overlay and used for completion/autoplay decisions
  - reporting segment: the currently active physical part used for Jellyfin start/progress/pause/stop reporting
- Progress shown in the client is aggregate across all parts. The controller tracks the prior-segment tick offset and combines it with the current segment position for timeline and completion-threshold calculations.
- On playlist position changes, Bloom swaps the active segment metadata, refreshes per-part media segments/trickplay data, reports the segment handoff to Jellyfin, and suppresses terminal playback-end handling until the final playlist entry finishes.

Playback Reporting
- Report sequence: Start -> Periodic Progress -> Pause/Resume -> Stop.
- Use `POST /Sessions/Playing` when playback begins (includes track selection).
- Periodic progress: `POST /Sessions/Playing/Progress` (throttled, e.g., every 10s) to update resume position and track state.
- Stop: `POST /Sessions/Playing/Stopped` when playback ends.
- Explicit stops send a final unthrottled progress report, then one stopped report, before playback context cleanup or backend teardown can clear reporting metadata.
- Completion threshold (default 90%) is used for client-side autoplay/next-up decisions only; Bloom no longer auto-calls the watch/mark endpoint at threshold.

Important design notes
- Avoid heavy UI work during playback or IPC handling on main thread.
- Use PlayerController as a single source for playback state; it should be responsible for reporting state and keeping user-level playback logic (resume, next episode/autoplay, track selection).
- PlayerProcessManager should accept an mpv config dir and script directory (e.g., `~/.config/Bloom/mpv`) and allow adding `extra_flags` via `app.json`.

Playback caching and network resilience
- Bloom injects global mpv cache and reconnect flags automatically via `ConfigManager::getMpvConfigArgs()`:
  - `--cache=yes`
  - `--demuxer-max-bytes=<playbackCacheSizeMB * 1024 * 1024>`
  - `--stream-lavf-o=reconnect=1,reconnect_streamed=1,reconnect_delay_max=30`
- `playbackCacheSizeMB` is exposed in Settings -> Playback as "Playback Cache Size" (range 50–2048 MB, default 500 MB).
- This allows mpv to buffer aggressively in RAM so brief server outages do not stall playback.
- Startup buffering mode can be set globally (`settings.playback.startup_buffering_mode`) and overridden per library under the active `settings.connection_state` scope. `normal` keeps existing startup behavior. `remote-mount` starts mpv paused and appends startup-prebuffer args after profile args (`cache-pause-initial`, `cache-pause-wait=60`, `demuxer-readahead-secs=60`, `cache-secs=120`, and larger demuxer/cache buffers). Bloom keeps the full-screen startup buffering UI visible and unpauses only after `demuxer-cache-time` reaches the initial cache target or the extended startup timeout is reached.
- Runtime rebuffering uses the compact native playback spinner over the video instead of the full-screen startup card. This keeps the current frame visible while `paused-for-cache` or recovery is active.
- For longer server outages, `PlayerController` supports automatic recovery:
  - When playback hits `Error` because of a network/timeout failure, the controller stashes a `RecoveryContext` with the current item, stream URL, track selections, and last known position.
  - If the backend reports stopped before the recoverable mpv error arrives, `PlayerController` upgrades the pending terminal transition from `Stop` to `Error`, stashes the same recovery context, and keeps playback active so embedded video/overlay surfaces are not torn down during retry.
  - If `autoRecoverPlayback` is enabled (default `true`, stored in `settings.playback.auto_recover_playback`), the controller enters recovery mode and pings the server every 5 seconds via `LibraryService::pingServer()`.
  - When the ping succeeds, playback refreshes Jellyfin `PlaybackInfo` and resumes at the last known position using a fresh media source plus the prior track preference hints.
  - Recovery retries indefinitely until the server returns or the user explicitly stops/retries/clears the error.
  - Manual retry (`retry()` / `Event::Play`) while recovering refreshes Jellyfin `PlaybackInfo` and resumes from the stashed position instead of replaying the old stream URL.
- `autoRecoverPlayback` is not exposed in the settings UI; it can be toggled by editing `app.json` directly.

mpv config hints
- Create `mpv.conf` and `input.conf` when you need custom behavior (e.g., enable vo, hardware settings) in `~/.config/Bloom/mpv/`.
- Always pass mpv arguments from `ConfigManager` so users can override behavior at runtime.
- Built-in MPV profiles are `Low Quality`, `Medium Quality`, `High Quality`, `ArtCNN`, `ArtCNN-Deband`, `nnedi3`, and `nnedi3-deband`. The shader profiles append Bloom-bundled GLSL/hook shaders from `~~/shaders/`, and `ConfigManager` keeps those shader files plus Gandhi Sans subtitle fonts refreshed under Bloom's mpv config directory.
- Settings -> MPV Profiles now edits `extra_args` as an ordered list of one argument per entry (add/remove per row) for keyboard-first 10-foot usability.
- Migration compatibility: `extra_args` accepts both array and legacy newline-delimited string formats and normalizes to array on save.
- Embedded playback shortcuts include `K` to toggle `sub-ass-override` between `no` and `yes`, and `B` to toggle mpv debanding between `no` and `yes`. Controller mappings route through the same `InputBindingManager` actions and call `PlayerController.toggleSubtitleAssOverride()` / `PlayerController.toggleDeband()`.

Jellyfin integration
- Key endpoints used frequently:
  - `/Users/{userId}/Items`
  - `/Shows/NextUp`
  - `/Items/{itemId}/PlaybackInfo` - Get media streams and track info
  - `/Sessions/Playing` - Start, progress, stop reporting with track selection
- The client must persist session information via `ConfigManager` and restore sessions during startup.
- On 401 responses, emit a sessionExpired event and trigger a logout/restore flow.

Security & privacy
- Session tokens should be persisted securely in `app.json` and cleared on logout.
- Avoid logging sensitive tokens; prefer obfuscated logging when necessary.

Troubleshooting
- Ensure proper owner and permissions for socket/pipes when using Unix sockets.
- On Wayland, avoid `--wid`; run mpv as a top-level window and use the compositor for overlays.
- For embedded Linux diagnostics, set `BLOOM_LINUX_LIBMPV_DEBUG=1` to log graphics API/FBO details.
- To disable automatic software fallback while debugging OpenGL-only behavior, set `BLOOM_LINUX_LIBMPV_SW_FALLBACK=0`.
- Embedded Linux guardrails:
  - Trickplay is disabled by default for `linux-libmpv-opengl` stability; set `BLOOM_LINUX_LIBMPV_ENABLE_TRICKPLAY=1` to opt in.
  - MPV stats hotkeys (`I`, `Shift+I`, `0-9`) are disabled by default on embedded Linux; set `BLOOM_LINUX_LIBMPV_ENABLE_STATS_HOTKEYS=1` to opt in.
- Test playback flows with typical server and client device combinations to ensure direct play vs transcode logic works.
- If track selection doesn't sync, verify that `playSessionId` is being passed correctly in all reporting calls.
- If track preferences aren't being restored, check that `track_preferences.json` is schema version 4 and that the active connection under `scopes` contains the relevant `episodes` or `movies` entry. Unset preferences intentionally fall back to provider/file defaults and are not written.
