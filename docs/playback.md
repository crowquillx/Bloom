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
- PlayerController: state machine that handles play/pause/resume, listens for backend updates, manages track selection, and reports playback state to the Jellyfin server.
- JellyfinClient: handles API communication for reporting playback events, track selections, and sessions.
- TrackPreferencesManager: persists explicit audio/subtitle user choices to a separate JSON file using a versioned schema.

Audio/Subtitle Track Selection
- Call `JellyfinClient::getPlaybackInfo(itemId)` to fetch `PlaybackInfoResponse` containing `MediaSources` with all available streams.
- Each `MediaSourceInfo` contains `mediaStreams` array with `MediaStreamInfo` objects describing video, audio, and subtitle tracks.
- The server provides `defaultAudioStreamIndex` and `defaultSubtitleStreamIndex` which reflect the user's preferences set on the Jellyfin server.
- Use `PlayerController::setSelectedAudioTrack(index)` and `setSelectedSubtitleTrack(index)` to change tracks during playback via mpv IPC (`aid`, `sid` properties).
- Initial selection resolution order:
  - explicit saved preference for the current season/movie scope, if still valid
  - Jellyfin `defaultAudioStreamIndex` / `defaultSubtitleStreamIndex`
  - file-level defaults from stream metadata (`isDefault`, then forced subtitles when applicable)
  - fallback to first audio track / subtitles off
- Canonical track mapping contract:
  - UI and reporting state use Jellyfin `MediaStream.index`.
  - Runtime mpv switching uses mapped mpv track IDs (1-based per media type order).
  - Backends emit raw mpv track IDs; `PlayerController` reverse-maps them back to Jellyfin indices.
  - Subtitle `None` is Jellyfin `-1` and is applied as `sid=no`.
  - Startup applies resolved mapped selection deterministically; URL stream indices are treated as request hints/fallback.
- Track selections are persisted per-season for TV shows and per-movie for films (see Track Preference Persistence below).
- All playback reporting methods include `mediaSourceId`, `audioStreamIndex`, `subtitleStreamIndex`, and `playSessionId` for proper server sync.

Track Preference Persistence
- Track preferences are stored separately from the main config in `~/.config/Bloom/track_preferences.json`.
- The file stores only explicit user intent. Unset preferences fall back to Jellyfin/file defaults and are not written.
- Preferences are loaded at startup and saved with a 1-second delay to batch multiple changes.
- Schema is versioned. Legacy/unversioned files are intentionally discarded and replaced on the next save.

### TV Episodes (Per-Season)
- Preferences are stored by season ID, not series ID, because:
  - Different seasons may have different audio tracks available (e.g., added dubs in later seasons)
  - Subtitle indexing can vary between seasons
- When the user changes audio/subtitle track in `SeriesSeasonEpisodeView`, the preference is immediately saved.
- When navigating to a new episode in the same season, explicit preferences are restored. If they are missing or invalid for the new source, Bloom falls back through the standard resolution order.
- Use `PlayerController.getLastAudioTrackForSeason(seasonId)` and `getLastSubtitleTrackForSeason(seasonId)` to retrieve.
- Use `PlayerController.setExplicitSeasonAudioPreference(seasonId, index)` and `setExplicitSeasonSubtitlePreference(seasonId, index)` to save.

### Movies (Per-Movie)
- Preferences are stored by movie ID for rewatches.
- When the user changes audio/subtitle track in `MovieDetailsView`, the preference is immediately saved.
- When returning to the same movie, explicit preferences are restored instead of server defaults. If none were saved, Bloom uses Jellyfin/file defaults.
- Use `PlayerController.getLastAudioTrackForMovie(movieId)` and `getLastSubtitleTrackForMovie(movieId)` to retrieve.
- Use `PlayerController.setExplicitMovieAudioPreference(movieId, index)` and `setExplicitMovieSubtitlePreference(movieId, index)` to save.

### JSON Format
```json
{
  "version": 2,
  "episodes": {
    "seasonId1": {
      "audio": { "mode": "explicit", "streamIndex": 1 },
      "subtitle": { "mode": "explicit", "streamIndex": 2 }
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
- Missing episodes (`LocationType == "Virtual"`) are excluded from the canonical timeline.
- Resolution order:
  - If `excludeItemId` is set, treat it as the explicit anchor and return the first later not-fully-played episode.
  - Otherwise, prefer the most recently active in-progress episode (`PlaybackPositionTicks > 0`).
  - Otherwise, anchor on the most recently played episode using `UserData.LastPlayedDate` when available.
  - If no anchor exists, fall back to the first unplayed regular episode, then to the first unplayed special.
- Tie-breakers when play dates are missing or equal prefer later canonical position, then larger `PlaybackPositionTicks`.
- `excludeItemId` is used by autoplay/prefetch so the player can advance from the current item even before Jellyfin has updated watch state on the server.

UI Components for Track Selection
- `TrackSelector.qml`: Reusable dropdown component for selecting audio/subtitle tracks with keyboard navigation.
- `MediaInfoPanel.qml`: Displays video info (resolution, codec, HDR) and contains audio/subtitle `TrackSelector` components.
- `SeriesSeasonEpisodeView.qml` and `MovieDetailsView.qml`: Integrate `MediaInfoPanel` to show track selection before playback.
- Explicit user play actions now route through `PlayerController::requestPlayback(...)` instead of assembling stream URLs in QML. This keeps version selection, multipart resolution, and track-hint handling consistent across detail pages, quick play, and Up Next manual play.
- `MediaSourceSelectionDialog.qml` is the global version picker for entries with multiple Jellyfin media sources (for example 1080p vs 4K). It is opened from `Main.qml`, uses the app theme/focus patterns, supports keyboard-only selection, and restores focus with `Qt.callLater(...)` on close.
- `EmbeddedPlaybackOverlay.qml`: Native 10-foot playback overlay (top metadata bar + bottom transport row) rendered in the dedicated transparent overlay window for Windows embedded playback.
  - Left group: audio/subtitle icon buttons (runtime track cycling via `PlayerController`).
  - Center group: skip back 10s, previous chapter, play/pause, next chapter, skip forward 10s.
  - Right group: volume icon button opens a native volume panel (slider + muted state) with left/right keyboard/gamepad adjustment and Enter/Space mute toggle.
  - Progress row: clickable seek track, current/total time labels, and keyboard seek via left/right.
  - Trickplay preview bubble: renders processed Jellyfin trickplay thumbnails from `PlayerController` and is hidden entirely when trickplay images are unavailable.
  - Intro/outro skip UX: transient "Skip Intro"/"Skip Credits" pop-up button auto-focuses when a segment window starts, then a compact persistent skip button remains available until that segment ends.
    - Popup timing is controlled by `ConfigManager.skipButtonAutoHideSeconds` (`settings.playback.skip_button_auto_hide_seconds`, range 0-15; 0 disables popup only).
    - Optional automatic skip is controlled by `ConfigManager.autoSkipIntro` and `ConfigManager.autoSkipOutro`; each auto-skip applies at most once per playback item even if the user seeks back.

Playback overlay metadata
- `PlayerController` now exposes `overlayTitle` and `overlaySubtitle` for native overlay header text.
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
- Completion threshold (default 90%) is used for client-side autoplay/next-up decisions only; Bloom no longer auto-calls the watch/mark endpoint at threshold.

Important design notes
- Avoid heavy UI work during playback or IPC handling on main thread.
- Use PlayerController as a single source for playback state; it should be responsible for reporting state and keeping user-level playback logic (resume, next episode/autoplay, track selection).
- PlayerProcessManager should accept an mpv config dir and script directory (e.g., `~/.config/Bloom/mpv`) and allow adding `extra_flags` via `app.json`.

mpv config hints
- Create `mpv.conf` and `input.conf` when you need custom behavior (e.g., enable vo, hardware settings) in `~/.config/Bloom/mpv/`.
- Always pass mpv arguments from `ConfigManager` so users can override behavior at runtime.
- Settings -> MPV Profiles now edits `extra_args` as an ordered list of one argument per entry (add/remove per row) for keyboard-first 10-foot usability.
- Migration compatibility: `extra_args` accepts both array and legacy newline-delimited string formats and normalizes to array on save.

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
- If track preferences aren't being restored, check that `track_preferences.json` is schema version 2 and that the relevant `episodes` or `movies` scope contains an explicit preference entry. Unset scopes intentionally fall back to Jellyfin/file defaults and do not get written.
