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
- Default backend is platform-aware via `PlayerBackendFactory` (Wayland defaults to external mpv IPC; non-Wayland Linux prefers embedded when runtime-supported; Windows forces `win-libmpv`; other platforms default external).
- Active backend is logged at startup from `ApplicationInitializer`.
- Optional environment override for backend selection: `BLOOM_PLAYER_BACKEND`.
- Optional config backend preference: `settings.playback.player_backend` in `app.json` (`external-mpv-ipc`, `linux-libmpv-opengl`, `win-libmpv`, or unset for platform default).
- Selection precedence is now:
  - Windows: forced `win-libmpv` (env/config backend override values are ignored).
  - Non-Windows: `BLOOM_PLAYER_BACKEND` env override -> config `player_backend` -> platform default.
- Unknown backend names safely resolve to `external-mpv-ipc`.

Playback policy boundary
- `PlayerController` remains the stable QML-facing playback facade and owns
  state-machine coordination, service calls, backend commands, and reporting.
- Deterministic decisions that do not require controller state mutation live in
  `Bloom::PlaybackPolicy` and are built through the reusable
  `Bloom::PlayerPolicy` target. This includes common language normalization,
  stream scoring and initial track selection, HDR/Dolby Vision classification
  and output policy, alternate-version affinity, multipart primary-source
  filtering, completion thresholds, the screen that follows playback
  (`postPlaybackScreen`: Up Next, the after-movie screen, or none), and
  next-episode prefetch validation.
- Keep this policy layer Qt-Core-only and free of `QObject`, timers, networking,
  configuration reads, backend calls, and other I/O. Callers pass settings and
  playback snapshots explicitly, which makes policy behavior directly testable
  without a media server, mpv, display hardware, or a QML engine.
- This extraction is behavior-preserving: track precedence, Dolby Vision
  fallback, version affinity, completion thresholds, autoplay/Up Next, and
  multipart behavior retain the contracts documented below.

Windows embedded backend details
- Added `WindowsMpvBackend` scaffold under `src/player/backend/`.
- Selector token: `win-libmpv`.
- Windows backend now resolves target `winId` from `MpvVideoItem`, creates a dedicated child host window, and keeps host geometry synced to `VideoSurface` viewport updates for embedded playback.
- `VideoSurface` reports the viewport in Qt logical pixels; the backend maps it to the container's physical client pixels with `Bloom::ResponsiveLayoutPolicy::toNativePixelRect` and the window's `devicePixelRatio` before `SetWindowPos`. Skipping that mapping shrinks the video to 1/DPR of each axis (one third of a 4K screen at 300%). `WM_DPICHANGED` also triggers a resync.
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
- Windows display restoration after playback stop is deferred and non-blocking. HDR-off settle and refresh-rate restore are scheduled after the UI returns to `Idle`, allowing the main scene and detached playback overlay window to repaint/hide promptly. When post-playback Up Next or the after-movie screen is shown, Bloom parks that restore until the user either leaves the screen or starts another title, avoiding a restore-then-switch cycle between consecutive titles.
- Natural playback end, explicit stop, and error-triggered shutdown now share one coordinated terminal-transition path so reporting/autoplay work runs once per playback attempt.
- Only the `loadfile` and `stop` command replies are fatal on `win-libmpv`; both carry known reply userdata. Any other failed command (for example `sub-add` of an external subtitle that returns 404) is logged and playback continues, as on the Linux and external backends.
- Windows direct-libmpv event handling now suppresses playback reactivation events (`START_FILE`/`FILE_LOADED`/`PLAYBACK_RESTART`/`SEEK`) while a stop command is pending, preventing stale wakeup events from re-showing the embedded host window as a black frame during teardown.
- Refresh-rate matching starts mpv with explicit display-sync options for exact matches (`--video-sync=display-resample` plus `--display-fps=<content fps>`), except when the user has chosen to keep an already compatible higher multiple such as 120Hz for 23.976fps content. This avoids pacing fractional Windows modes as integer 23/29/59Hz after Bloom switches the display.
- Windows direct-libmpv diagnostics request mpv log messages by default at `warn` level and include detailed terminal event/teardown breadcrumbs. Set `BLOOM_WINDOWS_LIBMPV_MPV_LOG=info`, `debug`, or `trace` before launching Bloom to collect more mpv-side detail for playback termination, renderer, HDR, and shader diagnosis; use `no`/`off` to disable mpv log forwarding.
- Windows embedded playback always initializes mpv with `vo=gpu-next`. Each MPV profile can opt into a Windows render API with `windows_render_api`: `auto` leaves `gpu-api`/`gpu-context` unset, `d3d11` sets `gpu-api=d3d11` and `gpu-context=d3d11`, and `vulkan` sets `gpu-api=vulkan` and `gpu-context=winvk`. Low/Medium/High built-ins stay on `auto`; shader-heavy ArtCNN/nnedi3 built-ins default to Vulkan/winvk because ArtCNN is known to behave poorly on D3D11.
- Windows D3D11 profiles can opt into `windows_10bit_output`, which applies `d3d11-output-format=rgb10_a2` only when `windows_render_api=d3d11`. Vulkan and auto render paths do not receive the D3D11 output format.
- Windows playback-start logs include the selected profile, `windows_render_api`, effective `vo`, `gpu-api`, `gpu-context`, 10-bit D3D11 output state, HDR output args, and shader filenames. Use mpv stats/log overlays with `BLOOM_WINDOWS_LIBMPV_MPV_LOG=info` or `debug` to verify input/output colorspace, HDR signaling, renderer selection, and shader application during runtime validation.
- Embedded libmpv backends (`win-libmpv`, `linux-libmpv-opengl`) apply profile shaders after `mpv_initialize` using `change-list glsl-shaders clr/append` with absolute paths resolved from Bloom's mpv config directory (`~~/shaders/...` expands to `{config}/mpv/shaders/...`). CLI-style `--glsl-shaders-append` remains for external mpv only.

Display/HDR operation lifecycle
- `DisplayManager` is provided by the reusable `Bloom::Display` target. All HDR,
  refresh-rate change, and refresh restoration requests are asynchronous.
  `PlayerController` sequences HDR first, refresh matching second, and backend
  startup last; shutdown restores HDR before refresh without holding the GUI
  thread. The existing stabilization delay is applied only after a real mode
  switch.
- Linux refresh/HDR commands and Windows custom HDR commands run through an
  event-driven `QProcess`. `DisplayManagerOptions` configures the command
  deadline (5 seconds by default) and per-stream stdout/stderr cap (64 KiB by
  default). Their descendant processes are isolated as a Unix process group or
  Windows job so cancellation terminates the complete command tree. Command
  contents are not logged. Native Windows HDR and refresh APIs run off the GUI
  thread and have the same controller-visible deadline.
- A single operation generation invalidates stale command and native-operation
  completions. Windows native mutations are serialized; queued stale mutations
  check a process-lifetime generation before touching the display, and the
  newest physical request still runs last when its caller-visible deadline
  expires behind an already-running OS call. Starting another episode cancels
  in-flight restoration and then reconciles the actual desired HDR/refresh state
  before mpv starts. The earliest pre-playback refresh/HDR state remains the
  restore target, including the existing compatible-multiple skip and Up Next
  parking optimization.
- `displayOperationMeasured` and `diagnostics()` expose operation latency,
  timeout, cancellation, output-size, and success counters. Destruction never
  waits for an external command; if normal playback teardown did not finish,
  Bloom issues a best-effort non-blocking restore. Persistent automatic crash
  recovery was considered but intentionally not added because replaying stale
  display state after a reboot or user mode change would make restoration more
  intrusive.

Audio output device & hotplug handling
- Goal: audio should follow the device the user actually wants, including devices that connect *after* Bloom (or playback) has already started — e.g. powering on a Bluetooth headset mid-session — without restarting the app.
- Setting: `ConfigManager.audioOutputDevice`, stored per platform with its label (see [docs/config.md](config.md)), default `auto`. `auto`/empty follows the system default output device; a specific value is an mpv audio-device id such as `wasapi/{...}`. Pickers live in **Settings > Playback > Audio Output Device** and in the playback audio panel (**Output** row below the track list). Both use `PlayerController.audioOutputOptions`: System Default plus the active driver's endpoints, and the saved choice while it is disconnected.
- Startup: `ConfigManager::getMpvConfigArgs()` always passes `--audio-fallback-to-null=yes` so playback keeps running (instead of erroring) when no usable device exists at a given moment. The audio plan then appends `--audio-device=<resolved id>`, which is `auto` while the saved device is missing.
- Idle enumeration: `IPlayerBackend::refreshAudioDevices()` lists devices without playback (external `mpv --audio-device=help`, or a temporary libmpv core). Bloom runs it at startup, when an output picker opens and on OS output changes.
- Hotplug detection: every backend observes mpv's `audio-device-list` property and emits `IPlayerBackend::audioDeviceListChanged(devices)` (each entry is `{name, description}`). `PlayerProcessManager` parses the IPC JSON; embedded libmpv backends share `src/player/backend/MpvNodeUtils.h` to parse the `MPV_FORMAT_NODE` array.
- Reaction: `PlayerController` caches the list as `availableAudioDevices`. After the first snapshot, device changes refresh capabilities and debounce audio reload for explicit and automatic output. See [Audio output](audio.md) for the shared plan, runtime checks, null output recovery and resume behavior.
- Changing the device during playback applies immediately through the same `applyAudioOutputDevice()` path. It reopens only the audio output; position, pause state and tracks are kept. See [Audio output](audio.md#output-device-selection) for resolution, the fallback ladder and WASAPI exclusive mode.
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
  - an event loop that stops draining mpv events as soon as a signal handler destroys or replaces the mpv handle (for example `PlayerController` calling `stopMpv()` from its `playbackEnded` handler).
- Remaining work: Linux target runtime validation matrix and any follow-up fixes from on-device testing.
- Current Linux support status: embedded path is not yet considered fully supported across compositor/driver combinations; treat `external-mpv-ipc` as the stable production path while Linux embedded validation continues.
- Controller parity hardening now preserves next-up/autoplay context across playback teardown, so next-episode prefetch/lookup flows keep the expected series/item/track state.
- Unit regression coverage now includes the mismatched-series guard for async next-episode callbacks to prevent stale-series autoplay context from being consumed.

End of file
- Every backend classifies mpv's `end-file` through `Bloom::PlaybackPolicy::classifyEndFile` and reports `IPlayerBackend::playbackEnded(reason, detail)` with the end reason (`eof`, `stop`, `quit`, `error`, `redirect`, `unknown`). A redirect or an entry followed by more playlist entries is not terminal.
- `PlayerController` treats `reason=error` as a playback error: a 404 or a failed transcode while Loading shows the error at once instead of waiting for the loading timeout, and an error mid-file never marks the item watched or opens Up Next. Recoverable stream failures still go through automatic recovery.
- Natural end is accepted from Loading, Buffering, Playing, and Paused. If terminal finalization finds no state transition, the controller still enters Idle (or Error) so an active state never outlives a stopped backend.

Key components
- IPlayerBackend: playback backend contract used by `PlayerController`.
- ExternalMpvBackend: adapter that delegates to `PlayerProcessManager`.
- LinuxMpvBackend: Linux embedded backend entry point.
- LinuxMpvBackend now includes basic `mpv_handle` lifecycle, property/event observation, and a Qt Quick `beforeRendering`-driven `mpv_render_context` render path (Linux runtime validation still pending).
- MpvVideoItem / VideoSurface: minimal viewport plumbing for embedded backend integration.
- PlayerProcessManager: manages external mpv process lifetime, sockets/pipes, scripts and config dir through the reusable `Bloom::PlayerProcess` production target. Observes `time-pos`, `duration`, `pause`, `aid`, `sid`, `paused-for-cache`, `volume`, `mute`, `playlist-pos`, `playlist-count`, `demuxer-cache-time`, and `audio-device-list`.
- PlayerController: provider-neutral state machine that handles play/pause/resume, listens for backend updates, manages track selection, and sends canonical playback state through `PlaybackService`.
- `IPlaybackProvider` / `JellyfinPlaybackProvider` / `SiloPlaybackProvider`: finalize provider PlaybackInfo into Bloom `PlaybackDescriptor` values and serialize canonical playback reports into provider endpoints and wire payloads. Silo uses the pinned native API v2 and protocol 3; Jellyfin retains its synchronous descriptor path.
- `PlaybackService`: stable application façade for playback preparation and reporting transport; its public timing contract uses milliseconds.
- TrackPreferencesManager: persists explicit audio/subtitle user choices to a separate JSON file using a versioned schema.
- SubtitleSearchViewModel: online subtitle search for the playing file (see "Searching for subtitles").

### External mpv lifecycle and IPC

`PlayerProcessManager` never waits synchronously for a child process or local
socket. Starting replacement playback records the newest request, asks the
active mpv to quit over IPC, waits for its process-finished event, and launches
the replacement on the next event-loop turn. Explicit stop cancels any queued
replacement. Shutdown progresses through bounded stages: graceful IPC `quit`
(750 ms by default), `QProcess::terminate()` (another 750 ms), and finally
`QProcess::kill()`. An intentional terminate/kill result is not reported as a
new playback failure.

Each manager and launch uses a collision-resistant endpoint containing a
non-reversible user token, the Bloom PID, a random instance token, and a launch
sequence. Unix sockets live under `QStandardPaths::RuntimeLocation` in an
owner-only directory, with an owner-only cache/runtime or unique temporary
directory fallback when required; overlong Unix socket paths are rejected.
Windows uses an equivalently unique named pipe. Bloom intentionally supports
multiple application instances and therefore does not enforce an app-wide
single-instance lock; independent endpoints prevent their external mpv
processes from colliding.

When Linux embedded rendering fails, `PlayerController` keeps that backend
alive until its asynchronous stopped signal arrives, then replaces it with the
external-mpv backend on the next event-loop turn. Repeated render errors during
that shutdown do not start overlapping fallback or terminal transitions.

IPC connection and reconnection are asynchronous. The default connection
window is 5 seconds with 100 ms per-attempt retry pacing and at most 50
attempts. A disconnect queues reconnection after the socket notification has
unwound. Pending commands are capped at 256 (oldest dropped), while outgoing
socket buffering and each incoming/outgoing JSON message are capped at 1 MiB.
Incoming documents must be JSON objects with a valid event shape; property
values and client-message arguments are type-checked before signals are emitted. Logs
record command names and lifecycle timing but never command payloads, media
URLs, or signed credentials.

Startup and IPC connection latency are logged and exposed through
`startupLatencyMeasured`, `ipcConnectionLatencyMeasured`, and `diagnostics()`.
`PlayerProcessManagerTest` runs a local fake-mpv child to cover graceful and
forced shutdown, replacement ordering, private endpoints, startup/IPC failure,
reconnection, bounded queues, and malformed JSON without playback hardware.

### Dolby Vision processing and FEL

Bloom appends a labelled `format=dolbyvision=yes:enhancement-layer=yes` filter
for DV sources when the runtime accepts it. Embedded playback applies filter
list operations in order through mpv's list commands. Existing filters,
shaders, HDR output settings, and decoder policy remain in effect.
The packaged stack and runtime checks are described in
[Dolby Vision processing and Profile 7 FEL](dolby-vision.md).
The experimental Linux render API cannot apply FEL; external gpu-next and
Windows gpu-next can use it with a compatible stack. Successful reconstruction
requires playback evidence and cannot be inferred from HDR display state.

HDR and Dolby Vision policy
- Bloom keeps `enableHDR` as the master switch. With `enableHDR=false`, HDR and Dolby Vision sources are direct-played where possible and locally tone-mapped to SDR by mpv/libplacebo.
- With `enableHDR=true`, the default `hdr_output_mode=match-content` keeps SDR content in SDR and switches HDR-capable content to HDR output when the active backend/display path supports it.
- `hdr_output_mode=tone-map-to-sdr` forces local SDR output for HDR/Dolby Vision sources. `hdr_output_mode=force-hdr-experimental` is intended only for validation/debugging.
- Dolby Vision detection uses Jellyfin stream metadata including `VideoRangeType`, codec tags/IDs, and Dolby Vision profile/level fields. Profile 7/8 content is treated as HDR-compatible by default; unsupported Dolby Vision profiles are locally tone-mapped to SDR unless `dolby_vision_fallback_mode=experimental-direct-play` is selected.
- Bloom does not request server transcoding for HDR/Dolby Vision fallback. Unsupported or unavailable HDR output should prefer local tone-mapping over full transcode.
- Windows match-content playback toggles OS HDR only for HDR output playback and restores the previous state afterward. SDR playback does not enable Windows HDR just because the setting is on.
- Windows embedded libmpv keeps the HDR output policy but lets each profile select the Windows render API through `windows_render_api` and HDR metadata behavior through `hdr_metadata_mode`. `target` is the safe default for `--target-colorspace-hint-mode`; `source-dynamic` is experimental and applies only during HDR output, not SDR playback or HDR-to-SDR tone mapping. Unsafe raw render-context overrides such as `gpu-api`, `gpu-context`, `vulkan-*`, and `d3d11-*` are filtered, and unsupported mpv HDR options are logged with the option name and mpv error.
- Embedded libmpv classifies an early terminal `END_FILE` as a recoverable interruption when mpv/ffmpeg logs show stream failures such as premature HTTP EOF, reconnect attempts, HTTP 5xx, or I/O errors, or mpv's `[curl]` stream reports a retry or failed transfer. `BloomBackend::StreamFailureTracker` (`MpvLibmpvRuntime`) decides whether a failure explains the EOF: the EOF arrives within 30 s of the failure, or at or before where the demuxer cache ended when the stream failed (the demuxer can play minutes of cache after the stream dies). An EOF within 2 s of the known duration is always a genuine end. This prevents hosted/sparse sources from closing the player when mpv reports EOF after a transient stream interruption; Linux also treats `END_FILE reason=unknown` with recent stream-failure logs as recoverable because some libmpv builds report premature remote EOF that way.
- Linux production HDR should continue to use the external mpv IPC backend unless embedded HDR output has been explicitly selected for validation.

Audio/Subtitle Track Selection
- Call `JellyfinClient::getPlaybackInfo(itemId)` to fetch `PlaybackInfoResponse` containing `MediaSources` with all available streams.
- Each `MediaSourceInfo` contains `mediaStreams` array with `MediaStreamInfo` objects describing video, audio, and subtitle tracks.
- If PlaybackInfo provides `DirectStreamUrl` or `TranscodingUrl`, `JellyfinPlaybackProvider` finalizes that provider-selected URL and appends missing auth/media-source/track query parameters. The provider's constructed `/Videos/{itemId}/stream` endpoint remains its fallback when PlaybackInfo does not include a playback URL. `PlayerController` never constructs or parses these provider endpoints.
- The server provides `defaultAudioStreamIndex` and `defaultSubtitleStreamIndex` which reflect the user's preferences set on the Jellyfin server. The global `Server Default` setting uses them.
- Use `PlayerController::setSelectedAudioTrack(index)` and `setSelectedSubtitleTrack(index)` to change tracks during playback via mpv IPC (`aid`, `sid` properties).
- Use `PlayerController::setSubtitleDelayMs(delayMs)`, `adjustSubtitleDelayMs(deltaMs)`, or `resetSubtitleDelay()` to retime primary subtitles during playback. Bloom stores delay in milliseconds, applies it to mpv's `sub-delay` property as fractional seconds, and accepts negative or positive offsets.
- Initial selection resolution order (`Bloom::PlaybackPolicy::resolveTrackSelection`, per kind, first match wins):
  - request-time override from the playback request/autoplay context, if valid
  - explicit local choice for the current season/movie scope (`TrackPreferencesManager`). **A track the user picked in Bloom always beats the server**: a pick on one episode is saved for its season, so every later episode of that season keeps it whatever the server says. The pick is saved with what the track is (`preferredLanguage`, `forcedOnly`, `hearingImpaired`, `title`, `codec`, `channels`), because stream indexes differ between files (Jellyfin numbers every stream of a file in one sequence, Silo's subtitle indexes are FFmpeg stream indexes that attachments shift). `PlaybackPolicy::explicitStreamIndex` takes the saved index only while that track is still the picked one, else the track that matches the pick: language, the forced flag, (subtitles) signs/songs versus full dialogue and (audio) a commentary versus the programme's sound must match; then title, the exact language tag (so `pt-BR` stays `pt-BR` over `pt-PT`), hearing impaired, codec and channels rank the rest. Only a file with no matching track falls through to the server, then the global defaults, then the file. "Subtitles off" is a pick of its own. A pick saved by an older Bloom carries only its index and resolves by index alone.
  - the server's preferences (see "Server track preferences" below), unless **Settings > Playback > Use My Server's Language Preferences** is off
  - global app fallback from Settings > Playback (`Server Default`, `File Default`, common language, or subtitle `Off`/`Forced`)
  - built-in safety fallback
- Built-in audio fallback uses Jellyfin `defaultAudioStreamIndex`, then file-level `isDefault`, then the first audio stream.
- Built-in subtitle fallback uses Jellyfin `defaultSubtitleStreamIndex`, then file-level `isDefault`, then forced subtitles, then subtitles off.
- Global subtitle `Forced` chooses the first forced subtitle track; when no forced track exists, subtitles stay off (`global-forced-off`) rather than falling through to Jellyfin/file defaults (which could enable full dialogue subtitles).
- Global language fallbacks match normalized common language aliases (for example `en`/`eng`, `fr`/`fre`/`fra`, `zh`/`chi`/`zho`). Audio prefers matching default streams, then stream order. Subtitles prefer matching default streams, then regular subtitles, then forced, then SDH/hearing-impaired, then embedded over external, then stream order. Every subtitle rule, including the forced and file-default fallbacks, considers embedded tracks before external files; mpv's embedded sid map skips external streams, which get their sid from `sub-add`.
- Runtime external subtitles can be injected with `PlayerController::addExternalSubtitleTrack(subtitleUrl, displayTitle, language[, jellyfinStreamIndexHint])`, which issues mpv `sub-add` and appends/selects a subtitle option for state sync. The `jellyfinStreamIndexHint` parameter is optional; when omitted or passed as `-1`, Bloom generates a temporary synthetic negative index for in-session selection only. If the subtitle is known to Jellyfin (for example a freshly downloaded subtitle now exposed by `PlaybackInfo`), pass its real stream index via `jellyfinStreamIndexHint` so Bloom tracks it with the canonical Jellyfin index.
- Jellyfin sidecar subtitles (`.srt`/`.ass`/`.vtt` files beside the video, or ones the server downloaded) reach mpv through `sub-add`. The PlaybackInfo `DeviceProfile` lists `srt`, `subrip`, `ass`, `ssa`, `vtt` and `webvtt` with `Method: External` next to the Embed profiles. Jellyfin matches embedded streams against the Embed profiles first, so embedded tracks stay in the stream and are never extracted; graphical formats stay Embed-only. For each external stream the server then answers `DeliveryMethod: External` with a `DeliveryUrl`. `JellyfinModelMapper` keeps that URL as the canonical stream's `externalUrl` with its credential query items removed, so the canonical model never holds a token. When the stream sets `IsExternalUrl` (for example a `.strm` item, where Jellyfin sends the sidecar's server disk path), only an absolute http(s) URL is kept. `JellyfinPlaybackProvider` resolves the URL against the server base path. On the server's origin it replaces any `ApiKey` or legacy alias (`api_key`, `X-Emby-Token`, `X-MediaBrowser-Token`) with the session's current `ApiKey`, as on the stream URL, and keeps every other query item byte for byte; a URL on another origin gets no token. When a text sidecar has no usable URL (none sent, a disk path, or a non-HTTP scheme), the provider uses `GET /Videos/{itemId}/{mediaSourceId}/Subtitles/{index}/Stream.{codec}` (`GetSubtitle`), which serves the file unchanged in its own format. A graphical sidecar without a URL stays unselectable. Each descriptor track carries its source index, so the sidecar keeps its menu entry and index, and `PlayerController` loads only the selected sidecar, with `select`, passing title and language. The other sidecars (Jellyfin and Silo) are registered in the menu and loaded with `select` the first time they are chosen: mpv reports a sid only for the track it selects, so a sidecar added with `auto` would stay unmapped and be fetched twice. `cycle sub` therefore cycles the embedded tracks and the sidecars already loaded. A selected sidecar is not sent as `SubtitleStreamIndex`, so a transcode never burns it in on top of mpv's copy. mpv fetches the URL itself; logged URLs go through `redactNetworkLogText`.
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

Server track preferences
- Each canonical media source may carry `serverTrackPreferences` (`Bloom::ServerTrackPreferences`, [canonical models](canonical-models.md#playback-boundary)). An empty member means the server has no preference for it, and Bloom's global defaults decide.
- Silo: `SiloModelMapper` reads them from `GET /api/v2/watch/{id}`. Audio is the version's `effective_audio_track_index`, an ordinal into `audio_tracks` and therefore Bloom's Silo audio stream index. Silo resolves that index even when the profile has no preference (its fallback is the first decodable default-flagged track, else the first) and does not say which. A different track is the server's choice (a profile-wide language). When the index equals the fallback, `PlaybackService` asks `GET /api/v2/audio-prefs/{series_id}` (`getAudioPreference`, with the series of an episode or the movie's own content ID) before delivering the playback info and hands the reply body to the provider. A stored row (200) makes the track the profile's choice only when the row names that track: its `track_signature` matches it exactly, or its `audio_language` is the track's language. So a pick that happens to be the file's default still follows to the next season and to other devices, while a file without the picked track (Silo resolved past the row to its default) stays no preference and Bloom's global defaults decide. 404 leaves it as no preference; any other failure keeps that reading. The read is skipped when nothing depends on it or the setting is off, and is capped at two attempts because playback waits for it. Subtitles come from `effective_subtitle_mode`, `effective_subtitle_language`, `effective_show_forced_subtitles` and `effective_subtitle_track_signature`, which Silo sends only when the profile stored them; a language without a mode means Silo's default mode, `auto`.
- Jellyfin: `JellyfinAuthenticator` maps `UserDto.Configuration` from sign-in and from the `GET /Users/{userId}` session validation (only then: a change made on the server shows after the next sign-in or session validation) into the account's defaults (`AuthenticationService::accountPlaybackDefaults`), and `PlaybackService` attaches them to every source that has no preferences of its own. `AudioLanguagePreference` and `PlayDefaultAudioTrack` drive audio. `SubtitleMode` maps `Always`→`always`, `Smart`→`auto`, `OnlyForced`→`forced`, `None`→`off` without forced tracks, and `Default`→no preference (the file's flags decide). `RememberAudioSelections`/`RememberSubtitleSelections` act only inside Jellyfin's own `PlaybackInfo` defaults, which the `Server Default` setting already uses. They are language-based: Jellyfin never names a track for the account. Only the live session's account has them, so a pooled member of a merged space has none.
- The server tier (`serverAudioChoice`, `serverSubtitleChoice`):
  - Audio: the server's track if this file has it; else, with a language, the file's default track when "play the default audio track" is on, then the closest language match (same tag first, then the same language with another region), preferring the default flag, then stream order.
  - Subtitles, given the language of the audio that plays: `off` shows the forced track in the audio's language when forced subtitles show, else none. `forced` (Jellyfin's `OnlyForced`) shows a forced track in the subtitle language, then one that names no language, then one in the audio's language, then any, or none. `always` uses the remembered track signature, else the closest full-dialogue track in the language. `auto` shows only the audio language's forced track when the audio is already in the subtitle language (or no language is set), else as `always`.
  - The language lookup of `always` and `auto` takes full dialogue only: a forced track, or one titled as signs, songs or forced, does not translate the dialogue, so a file whose only track in that language is one of those cannot meet the preference.
  - A preference the file cannot meet (no track in that language) says nothing, so Bloom's global defaults decide for that file.
- Write-back: when the user picks an audio or subtitle track (the in-playback menus, `cycle` bindings, the season page's track menu via `chooseSeasonAudioTrack`/`chooseSeasonSubtitleTrack`, or the movie page's via `chooseMovieAudioTrack`/`chooseMovieSubtitleTrack`), `PlayerController` saves it for the season or movie first and then, after a 1.5 s pause (so browsing through tracks sends only the last one), asks `PlaybackService::writeTrackPreference` to remember it for the series of an episode or for the movie itself (Silo keys a movie's preference by its content ID). Silo answers with `PUT /api/v2/audio-prefs/{series_id}` (`audio_track_index` ordinal, `audio_language`, and a `track_signature` of language, title, embedded title, codec, layout and channels, sent exactly as Silo reported them for that track: a title Bloom filled in from the embedded title for display goes back empty) or `PUT /api/v2/subtitle-prefs/{series_id}` (`subtitle_track_index` as Silo's combined ordinal or `-1` for off, `subtitle_mode` `always`/`off`, `subtitle_language`, and a signature of source, language, codec, label, forced and hearing-impaired; `show_forced_subtitles` is left out so the series keeps its own). Both are `natural_idempotent`, so they retry as idempotent. A failure is only logged: it never blocks, reverts, or reports anything about the local choice.
- Waiting writes are kept per kind and per series or movie, so a pick for one never cancels another's. Each keeps the server, account and profile it was picked under (`PlaybackService::trackPreferenceWriteIdentity`); it is dropped when that changes (`trackPreferenceIdentityChanged` on sign-out, session expiry, server, account or profile switches) or when the setting turns off, and is checked again when it is sent. Writes still waiting at app shutdown are dropped; the pick is saved locally already. In `PlaybackService`, writes for one series or movie and kind go one at a time and only the newest waiting write is kept.
- A pick that needs a new stream (a pinned audio track or a subtitle the stream cannot show) is held until the server delivers that stream and no other change is pending. The held pick remembers what it replaced: the track that played, the choice saved for it then (a pick, or nothing when the server or Bloom's defaults chose it), and that track's write if it was still waiting, including the write of an earlier held pick. A refused change, a queued change the provider refuses at once, and a change the server answers with another track than asked for all undo the pick the same way (`restoreHeldTrackPick`): the undone pick's write is dropped and never sent. If the track that plays again is one the pick replaced, its saved choice comes back (so Bloom's or the server's choice is not turned into a local pick) and its waiting write is sent; for example, pick B, then C while B's change runs, B plays and C is refused: B is saved and written. Any other delivered track is saved as the choice. If the server already holds an undone pick, the restored track is written in its place. A change that never answers (playback ends, the session changes) keeps the pick locally but does not write it.
- Nothing is written for Bloom's own automatic selection (startup, autoplay, a request override), for extras, for Jellyfin (`POST /Users/Configuration` replaces the whole configuration), or when the setting is off. Silo's `library-playback-prefs` holds library-wide languages, not a pick, and is not written.

### Subtitle appearance
- `ConfigManager.subtitleAppearance` (`Bloom::SubtitleAppearance`, see [config](config.md)) is how text subtitles look: size, font, text colour, background box with its colour and opacity, outline and outline colour, shadow, how far subtitles are raised, and whether styled (ASS/SSA) subtitles are restyled too. Settings › Playback › Subtitle Appearance edits it with a live preview; the playback pop-out changes the size. The preview (`SubtitleAppearancePreview.qml`) draws the style `PlayerController.subtitlePreviewStyle` resolves through `SubtitleAppearancePolicy::previewProperties` under the default MPV profile, so a field left at Default shows that profile's font, font size, colours, outline and shadow; the outline is drawn as wide as `sub-border-size`. It approximates libass, and a library or series with another profile can look different.
- `Bloom::SubtitleAppearancePolicy` (Qt Core, in `Bloom::PlayerPolicy`) maps it to mpv. A field left at its default sets nothing, so the MPV profile's `sub-*` arguments (Gandhi Sans on the ArtCNN/nnedi3 profiles) and mpv's defaults stay in charge of it. The other fields map to:

  | Field | mpv |
  | --- | --- |
  | Size (75–200 %) | `sub-scale` (size / 100) |
  | Font | `sub-font`: Gandhi Sans / Gandhi Sans Bold (bundled in `~~/fonts`), sans-serif, serif, monospace, Arial, Verdana |
  | Text colour (Default, or an explicit colour such as White) | `sub-color` |
  | Outline (off, thin, medium, thick) and colour (Default or explicit) | `sub-border-size` 0 / 1.2 / 2.4 / 3.6, `sub-border-color` |
  | Shadow (off, soft, medium, strong) | `sub-shadow-offset` 0 / 0.75 / 1.5 / 2.5, `sub-shadow-color=#C0000000` |
  | Background box, colour, opacity (25–100 %) | `sub-border-style=opaque-box`, `sub-border-color=#AARRGGBB`, `sub-border-size=4` (the box margin), `sub-shadow-offset=0`; outline and shadow are unused then |
  | Raise (0–40 % of the screen height) | `sub-pos` (100 − raise) |
  | Apply to styled subtitles | `sub-ass-override=force` |

  The border and shadow names are the aliases every mpv release accepts (`sub-outline-*` and `sub-back-color` in mpv 0.41). Apply to styled subtitles is off by default: `force` also restyles signs and typesetting, which breaks most anime subtitles. Off leaves `sub-ass-override` as the profile has it (`no` on the shader profiles, mpv's `scale` otherwise), and the `K` toggle still cycles it during playback.
- Colours have a Default of their own, so an explicit White or Black applies even under a profile with other colours.
- Image subtitles (PGS, VobSub): mpv 0.41 (`sub/sd_lavc.c`) applies `sub-scale` and `sub-pos` to them only while `sub-ass-override` is not `no`. So Size and Raise move and scale image subtitles under profiles that keep mpv's `scale` (or with Apply to Styled Subtitles on), but not under the ArtCNN/nnedi3 profiles, which set `sub-ass-override=no`. Font, colours, outline, shadow and box never change image subtitles.
- `PlayerController::initiateMpvStart` appends `mpvArguments` right after the profile arguments, and keeps the arguments before them as the base. While mpv runs, each `subtitleAppearanceChanged` sets only the properties whose value changed (`set_property`, through `PlayerProcessManager`'s IPC under external mpv and the libmpv command path on embedded backends), so a change applies live on every backend. A field back at its default gets the base's value, or mpv 0.41's default when the profile does not set it; a value from the user's own `mpv.conf` comes back with the next playback. The diff is between Bloom's previous and new appearance, never against mpv's live state: a property that mpv's own keys or the `K` style override toggle changed is sent again only when the Bloom field behind it changes (a size step sends `sub-scale` alone, for example).
- External mpv has the focus during its playback, so there the size follows `script-message bloom-subtitle-size` (`+` or no argument, `-`, `reset`) from mpv's `input.conf`, shown on mpv's OSD, which also answers a step past 75 % or 200 %; see [input bindings](input-bindings.md) for an `input.conf` that routes mpv's own size keys through Bloom.
- On a Silo connection, `SubtitleAppearanceSync` reads the profile's appearance on this device when the connection or profile becomes live and adopts it while the user has not changed the appearance in Bloom (`subtitleAppearanceUserModified`); a profile with no appearance gives the defaults, so one profile's look does not carry over to another. The appearance and the user-change mark are one per device: once the user changes it, no profile's appearance is adopted until Reset. A change the user makes in Bloom is written, 1.5 s after the last one, as the live profile's device override with the whole current appearance; Reset removes that override and adopts the profile's appearance again. Requests go one at a time and a write follows a read of the same profile. A write or removal that does not reach the server stays marked for that connection and profile across restarts and goes first the next time it is live; a debounced change is also sent on quit. Writes are best effort and never block or revert the local change. Jellyfin keeps the appearance local. The wire mapping is in [provider compatibility](provider-compatibility.md#subtitle-appearance).


### Syncing subtitles to the audio
Silo can re-time an external subtitle file or a downloaded subtitle to the file's audio. `SubtitleSyncViewModel` (QML `SubtitleSync`, see [view models](viewmodels.md#subtitlesyncviewmodel)) drives it for the selected subtitle; the requests are in [provider compatibility](provider-compatibility.md#playback-themes-and-seek-previews).

- **Which subtitles**: a subtitle track's `syncKey` is the inventory's `sync_key`, which Silo puts on external and downloaded SRT, WebVTT, ASS and SSA tracks. Embedded tracks never carry one, and Jellyfin has no sync. The rows appear only when `getSubtitleSyncStatus` says `available` and `allowed`; a file beside the media (an `external-…` key) also needs its `external`. The status is read at each playback start and again after an account, profile or session change, so a server that turned the sync on is picked up at the next playback. A subtitle whose first read is a `404` is not offered for the rest of the playback. A subtitle just downloaded through the subtitle search carries `stored-<id>` from the download's answer (`Bloom::DownloadedSubtitle::syncKey`), so it offers the sync, and Silo's automatic sync of a new download is picked up as below.
- **Sync to Audio** starts a job (or joins the subtitle's active one) and polls it every 3 seconds while it is pending or running, showing its phase and progress. After 5 minutes Bloom stops following it and says so; selecting the row again picks the job up. There is no realtime socket; Silo's `subtitle_sync_updated` event is not used.
- **Automatic sync**: when the status says `auto_sync`, Silo starts a job by itself the first time a player is served a subtitle it has not synced. A subtitle whose first read shows its own timing and no job is read once more 4 seconds after mpv loads it; a job found then is followed like one the user started (its progress shows in the row, and its end is not announced), and a changed timing is reloaded.
- **When the timing changes**, the subtitle's URL already serves the corrected cues, so `PlayerController::reloadSelectedExternalSubtitle()` adds the same URL again with `select` (after the same cross-origin check as any sidecar). The old copy stays mapped and selected until mpv reports the new copy's sid, and is removed only then; a `sid=no` or the old sid reported meanwhile is ignored. If no new sid is reported within 10 seconds (a failed fetch, or a `sub-add` mpv refused), the old copy stays selected and the outcome says the new timing applies the next time the subtitle loads; a copy that arrives later is removed. While playback is buffering or loading, the reload waits and runs once it plays again. A download saved over a selected subtitle mpv already holds ("Searching for subtitles") is reloaded the same way, so it too never passes through off. A selected sidecar that mpv has not loaded yet, or that a new stream (a seek re-anchor or replan) is about to add again from its URL, is not reloaded: it fetches the new timing when it loads, and counts as loaded. `sub-delay` keeps the HLS timeline offset: the server's correction is on the source clock, like the cues.
- **Subtitle delay**: once a sync's new timing is loaded (the job is `synced`), that subtitle plays with its own delay override of 0 instead of the season or movie delay, since a delay set to make up for the same error would apply it twice. The override is keyed by the subtitle's sync key, stored with the season/movie preferences of the connection (`subtitleDelayOverrides`, at most 64 per scope), and replaces only the delay: the timeline offset is still subtracted once. Nudging the delay while that subtitle is selected edits its override; other subtitles and other episodes keep the season value. Reset Timing, or a read that finds the server's timing back to the original, removes the override, so the season delay returns. A job that finds the subtitle already in sync, does not match the audio (`no_match`, most likely another release), or fails leaves the timing and the delay alone and says why. Whether a sync changed anything is judged from the timing mpv had when the job was joined, not from the job's status.
- **Reset Timing** reads the subtitle again for its validator and sends `setSubtitleTiming` with the identity; the reloaded subtitle then plays with its own timing and the season delay.
- **Lifetime**: stopping playback, another item, a new session or another subtitle cancels the request in flight and the polling. The job carries on on the server. A session or profile change can drop a request without an answer: with the same account and profile Bloom sends it again (each may be replayed; a reset is settled by a read), and with another one it stops following the job and says so.
- **External mpv**: unavailable. The sync is part of the Bloom playback overlay, which external mpv does not show; use Silo's web player or select the subtitle in Bloom's embedded player.

Track Preference Persistence
- Track preferences are stored separately from the main config in `~/.config/Bloom/track_preferences.json` and grouped by `connectionId` so identical season/movie IDs on different servers cannot collide.
- The file stores only explicit user intent. Unset preferences fall back to the server's preferences, then Bloom's global defaults and the file's defaults, and are not written.
- Subtitle delay is written as `subtitleDelayMs` only when non-zero.
- A re-timed external subtitle's own delay (see "Syncing subtitles to the audio") is written in the same scope as `subtitleDelayOverrides`, a list of `{syncKey, delayMs}` holding at most 64 entries, the oldest dropped first.
- Global app-level audio/subtitle fallback defaults are stored in the main config, not in `track_preferences.json`.
- Preferences are loaded at startup and saved with a 1-second delay to batch multiple changes.
- Schema is versioned. Version 4 stores `scopes.<connectionId>.episodes` and `.movies`; version 2/3 files migrate into the active connection (or `_local` when signed out). Older unversioned files are intentionally discarded.

### TV Episodes (Per-Season)
- Preferences are stored by season ID, not series ID, because:
  - Different seasons may have different audio tracks available (e.g., added dubs in later seasons)
  - Subtitle indexing can vary between seasons
- When the user changes audio/subtitle track in `SeriesSeasonEpisodeView`, the preference is immediately saved.
- When navigating to a new episode in the same season, explicit preferences and subtitle delay are restored, ahead of any server preference, and follow the picked track when the new file numbers it differently. If the new file has no matching track, Bloom falls back through the standard resolution order. A new season starts without a local choice: played straight on from the previous season, that season's picks carry into it (below); otherwise, and on other devices, the server's series preference (which Bloom wrote back on Silo) carries the pick into it.
- Moving into another season, the finished season's explicit picks carry into it for each kind (audio, subtitles) the next season has no pick of its own. They carry as the saved pick, matched by what the track is like any saved pick, never as the previous file's stream index (`seasonTrackPreferencesWithCarry`). Autoplay resolves them directly; Up Next's play and the episode list it leads to send `carryTrackPreferencesFromSeasonId` with the request (`navigateToNextEpisode` passes the season). That includes Continue on the "Still watching?" prompt and a countdown start, which also send `startedByAutoplay` for the episode count. Once the next season's episode reaches Playing, the carried picks are saved for that season (`saveCarriedTrackPreferences`), so its later episodes keep them rather than follow the server. Within a season the saved pick resolves by itself, and a next season's own pick is never overridden.
- The season and movie pages send no track index with Play or Resume (`preferredAudioIndex`/`preferredSubtitleIndex` `-2`): the indexes they resolved for display may predate a pick made in the player, or belong to another version. The controller resolves the saved picks against the version that plays. A pick made on the page is saved at once (`chooseSeason*`/`chooseMovie*`).
- Use `PlayerController.getLastAudioTrackForSeason(seasonId)` and `getLastSubtitleTrackForSeason(seasonId)` to retrieve.
- Use `PlayerController.setExplicitSeasonAudioPreference(seasonId, index)` and `setExplicitSeasonSubtitlePreference(seasonId, index)` to save locally only. A user's pick on the season page goes through `chooseSeasonAudioTrack(seasonId, seriesId, mediaSource, index)` / `chooseSeasonSubtitleTrack(...)`, which also writes it back to the server.
- `SeriesSeasonEpisodeView` preloads provider-neutral chapter metadata for the highlighted episode and keeps a `Chapters` card rail between `Episodes` and `Cast & Crew`. When no chapter has a thumbnail, the rail is replaced by a compact `ChapterTimeline` strip after `Cast & Crew`; an episode without chapters shows neither.
- Activating a chapter card or a timeline chapter reuses the normal episode playback request, including track preference/version-prompt handling, and sets canonical `startPositionMs` so playback begins at the selected chapter.

### Movies (Per-Movie)
- Preferences are stored by movie ID for rewatches.
- When the user changes audio/subtitle track in `MovieDetailsView`, the preference is immediately saved.
- `MovieDetailsView` exposes the same chapter-card playback rail above `Cast & Crew`, or the `ChapterTimeline` strip after `Recommended From Your Library` when no chapter has a thumbnail; activating a chapter starts the movie at that chapter's start tick.
- Detail-page primary `Play` / `Resume` controls remain pressable while playback info is preparing, show a spinner in place of the normal glyph, and preserve the existing "still preparing" toast on press.
- When returning to the same movie, explicit preferences and subtitle delay are restored instead of server preferences. If none were saved, Bloom uses the standard resolution order.
- Use `PlayerController.getLastAudioTrackForMovie(movieId)` and `getLastSubtitleTrackForMovie(movieId)` to retrieve.
- Use `PlayerController.setExplicitMovieAudioPreference(movieId, index)` and `setExplicitMovieSubtitlePreference(movieId, index)` to save locally only (index only). The movie page's track menu uses `chooseMovieAudioTrack(movieId, mediaSource, index)` / `chooseMovieSubtitleTrack(...)`, which save what the track is and write it back to the server for the movie.

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
- If the end-of-playback lookup fails (`LibraryService::nextUnplayedEpisodeFailed` with the matching `player:resolve:` request context), `PlayerController` drops the pending autoplay context, which ends the Up Next handoff surface, and starts the display/HDR restore it had parked for Up Next. A failed prefetch is ignored; playback end resolves the next episode again.
- `UpNextScreen.qml` shows the series backdrop and logo with the queued episode's `S# E# • Name`, overview, an autoplay countdown ring, a primary "Play S# E#" hero button, and an icon-only Close (back to the episode list, same as Escape/Back). It follows the hero archetype in `docs/style-guide.md`: `StageBackdrop`, eyebrow, display-type title fallback, and a landscape `MediaCard` shelf. Its bottom row lists the queued episode and the rest of its season from `UpNextRecommendationsViewModel::upcomingEpisodes` (same season number, missing episodes skipped). Focusing a row card previews that episode in the hero and cancels the countdown; activating one plays it through `PlayerController::playNextEpisode`.
- The post-playback flow is one continuous surface. While the next episode resolves, `Main.qml` covers the window with an opaque `Theme.backgroundPrimary` handoff surface (spinner after 600 ms) so the page underneath never flashes. After a play request, Up Next stays on screen in its `starting` state until playback covers the window (the embedded video surface becomes active, or playback reaches Playing/Paused), then pops. An error, a return to Idle, or a 45 s fallback also ends the handoff.
- If no next episode is available, post-playback navigation still opens `UpNextScreen.qml` in an empty state. The screen must keep keyboard focus and offer actions back to Home or the series.
- The no-next empty state may show up to six TV-series recommendations via `UpNextRecommendationsViewModel`. Jellyfin similar series are listed first; Seerr TV recommendations are appended when the source series has a TMDB id and Seerr is configured. Recommendation provider failures are silent on this screen.
- Leaving the Up Next interstitial keeps the exact resolved episode context, including season-0 specials, by carrying the resolved episode id plus its `ParentId`/`SeasonId` directly into `SeriesSeasonEpisodeView`. When playback started from an existing library/episode-list screen, Up Next restores that screen in place; fallback direct navigation is used only when no reusable context exists. Exiting Up Next without starting playback releases any parked display restore.

Still watching? and the sleep timer
- The rules are pure functions in `Bloom::PlaybackPolicy` (`shouldAskStillWatching`, `autoplayChainCountAfterStart`, `sleepTimerSurvivesPlaybackEnd`, `sleepTimerFiresAtPlaybackEnd`, `sleepTimerChoiceForNewPlayback`); `PlayerController` keeps the count, the timer, and the input observation.
- "Still watching?" counts the episodes the Up Next countdown started in a row (`PlayerController::autoplayChainCount()`). The countdown running out calls `playNextEpisode(episode, seriesId, true)` and adds one; every other start (a page's Play, Up Next's Play button or a card, Continue, the overlay's episode controls, Home) restarts the count at zero. A retry, recovery or version switch (`continuesSession`) leaves it alone.
- When the count reaches `ConfigManager.stillWatchingEpisodeThreshold` (default 3, `0` turns it off) and autoplay is on, the next Up Next opens with `autoplay=false` and `PlayerController.stillWatchingPromptActive`: no countdown and no season row, "Still watching?" as the eyebrow, Continue (primary, focused; plays the queued episode as a manual start) and Stop (returns to the series like Close). Back/Escape is Stop. With the default, the episode that finishes after three autoplayed ones in a row asks. The prompt is part of the post-playback Up Next screen, so it renders in the main window with every backend (mpv has already stopped).
- What counts as input, any of which restarts the count:
  - key presses (including keys that match a `Main.qml` `Shortcut`, which Qt delivers as `ShortcutOverride`/`Shortcut` events instead of a key press, and keys the controller backend synthesizes for navigation), mouse button presses, wheel, touch and tablet presses, and pointer travel of at least 8 logical pixels from a reference point, seen by an app-level event filter on `PlayerController` (installed by `ApplicationInitializer`). The pointer entering a window or a window activating resets the reference without counting, so jitter, stale positions and hover events resent at a resting pointer do not count. This covers Bloom's main window, the Windows overlay window, Up Next and every other Bloom screen.
  - controller playback actions (`InputBindingManager::actionTriggeredWithContext`), which do not travel as key events.
  - with `external-mpv-ipc`, Bloom cannot see keys or clicks in mpv's own window. It sees mpv's `pause` property, and because Bloom never pauses or resumes on its own while playing (except for the sleep timer, whose pause is excluded), a pause or resume while playing counts. Bloom's own `bloom-*` script messages (`bloom-skip-intro`, `bloom-skip-outro`, `bloom-sleep-timer`), which come from bindings in mpv's `input.conf`, count too; other scripts' messages do not, because scripts send them on their own. Seeks, volume and track changes made with mpv's own bindings do not count, because they cannot be told apart from Bloom's commands.
- Sleep timer: the playback settings pop-out has a Sleep Timer page with Off, 15, 30, 45, 60, 90 minutes, and "End of this episode" (`sleepTimerEndOfItemLabel`: "End of this movie" without a series, "End of this video" for an extra). The root row shows the time left. `PlayerController.setSleepTimer(choice)` arms it (0 off, -1 end of item, otherwise minutes); the optional `playback.sleepTimer` binding opens the page. External mpv has the focus during its playback, so there the timer is driven from mpv's `input.conf` with `script-message bloom-sleep-timer` (no argument steps through the choices with `cycleSleepTimer()`; `off`, `end` or a listed number of minutes sets one), and each change shows on mpv's OSD; see `docs/input-bindings.md`.
  - A duration counts playing time only: pauses, buffering and the Up Next screen do not use it up, and its 1 Hz tick runs only while playback is active or a grace countdown runs. When it runs out, `PlayerController` pauses playback and starts a 30 s grace countdown (`sleepTimerGraceActive`, `sleepTimerGraceRemainingSeconds`). `EmbeddedPlaybackOverlay` shows it as a centred `DialogSurface` card, in the detached overlay window on Windows and inline for Linux embedded playback; with external mpv it appears on mpv's OSD (`show-text`). Any input above cancels the countdown (with external mpv, resume playback in mpv's window or send a `bloom-*` script message); the timer is then off and playback stays paused unless the input resumed it. When the grace ends, playback stops through the normal stop path: progress and stop are reported as usual, and Up Next does not open even past the completion threshold. Playback that ends any other way while the grace countdown runs (the item reaching its end before the pause applied, mpv closing) also counts as the timer ending it: the timer is off and Up Next does not open.
  - "End of this episode" lets the item play to its end, then skips Up Next (and so autoplay) and the after-movie screen; Bloom returns to the page playback started from, as after a stop, and the display restore runs at once.
  - Stopping playback cancels the timer. A duration survives the episode ending only when Up Next opens (`m_shouldAutoplay`), then the trip through Up Next (countdown or Play), with the time it had left; an end that opens no Up Next (a movie, an extra, quitting in mpv's window before the completion threshold), a failed next-episode lookup, or leaving Up Next without starting playback (`releaseDeferredPostPlaybackDisplayRestore`) ends it. The overlay's previous/next episode controls (`continuesWatching: "episodeSkip"`) keep a duration or "end of this episode", which then ends after the episode skipped to; Up Next (`continuesWatching: "upNext"`) never carries "end of this episode" over, since it already fired. Any other new playback starts with `ConfigManager.sleepTimerDefault` (Off by default). Recovery after an error keeps it.
  - Silo's `player.sleep_timer_default_minutes` setting is not read: Bloom reads Silo effective settings only for poster badges (`PosterBadgeService`), with no general reader for player settings, so the local setting is the only default.

### After-movie screen
- `PlayerPolicy::postPlaybackScreen` (Qt Core only) decides what follows a session: a feature that reached the completion threshold (`playbackCompletionThreshold`, the same rule as episodes) leads to Up Next when it has a series, and to the after-movie screen when it has none and its playback request marked it `isMovie` (carried through retries, recovery and version switches; an item without a series that is not a movie, such as a home video, leads nowhere). A session stopped before the threshold, one that ended with an error (`endedWithError`, including a terminal reason upgraded to an error before finalization), one the sleep timer ended (`endedBySleepTimer`: "end of this movie/episode", or its grace countdown stopping playback), and an extra (`PlaybackContentRules::allowsUpNext` is false) lead nowhere, so the display restore runs at once. Stopping past the threshold counts as finishing, as for episodes.
- `prepareTerminalTransition` records the finished movie (`itemId`, `connectionId`, the overlay's `name`, `backdropUrl` and `logoUrl`, and the playback `attemptId`). Entering Idle with it pending parks the display/HDR restore as for Up Next, owned by that attempt (`m_afterMovieRestoreAttemptId`). After terminal finalization `PlayerController::navigateToAfterMovie(movieData)` is emitted; a queued replacement playback or a cancelled transition drops it instead. There is no next-episode lookup, autoplay, or handoff surface for movies.
- Only `releaseAfterMovieDisplayRestore(attemptId)` with that attempt's id releases the parked restore; a stale screen (another attempt) is ignored, and Up Next's `releaseDeferredPostPlaybackDisplayRestore()` leaves a restore parked for an after-movie screen alone. The after-movie release never touches the sleep timer (a movie's timer already ended with it), so a buried screen cannot clear Up Next's parked restore or a duration carried into the next episode. The restore is also released after `kAfterMovieRestoreTimeoutMs` (5 minutes) with the screen still up, when the screensaver starts over it, when a new playback request fails before it starts (Up Next's Play or any other start while a restore is parked), and when a replacement playback never reaches `startPlayback`; a new playback that starts takes the display over and reconciles it itself.
- `Main.qml` pushes `AfterMovieScreen.qml` on top of the page playback started from. It shows the movie's backdrop and logo (fresh details from `UpNextRecommendationsViewModel::loadForMovie`, playback's metadata until they arrive), the year, runtime and age rating, a `UserRatingControl` where the provider has ratings, one Done button, and a "Because You Watched …" shelf of movies and series (Silo `listBecauseWatched`, Jellyfin similar items). There is no countdown.
- Done, Escape and Back pop only the screen (`StackView.popCurrentItem`), returning to that page. A recommendation replaces just the screen with the title's details page, so Back from it returns there too. Leaving the screen any way releases the parked restore: popped, removed by a space switch, or covered by a page the navigation bar pushes. A covered screen stays on the stack; Back to it reloads its rows and rating. Its reads and rating writes go to the movie's `connectionId` through `SourceRouter`.
- Ratings change at once and roll back with a toast when the server refuses (see [view models](viewmodels.md#user-ratings)). Recommendation and details failures are silent; the screen keeps its title and Done.

Searching for subtitles
- When the file has no suitable subtitles, the user can search online subtitle providers through the server and load one without leaving playback. The rules live in C++: `PlaybackService` runs the requests through the playing connection's `IPlaybackProvider` (`supportsSubtitleSearch`, `createSubtitleSearchStatusRequest`, `createSubtitleSearchRequest`, `createSubtitleDownloadRequest` and their parsers), and `SubtitleSearchViewModel` (`SubtitleSearch` in QML, see [view models](viewmodels.md#subtitlesearchviewmodel)) holds the page's state. Wire DTOs stay in the providers; the rest of Bloom sees `Bloom::SubtitleSearchResult` (`src/models/SubtitleSearch.h`): `id`, `provider`, `language`, `title` (release name), `format`, `downloads`, `score`, `hearingImpaired`, `forced`, `machineTranslated`, `aiTranslated`, `hashMatch` (the provider matched this exact file) and `uploadedAt`.
- Gating: Silo offers the search when `getSubtitleProviderStatus` says `state: available` with `enabled` and `allowed`; the document is read once per connection and profile (`PlaybackService::subtitleSearchScope`) while it plays, read again after a profile, account or rights change (`subtitleSearchScopeChanged`), and a failed read hides the entry until the next item asks again. Jellyfin has no such document: the entry is offered when the user's policy has `EnableSubtitleManagement` or `IsAdministrator` (read with the administrator flag at sign-in and session validation, `AuthenticationService::accountCanManageSubtitles`). When the policy says neither way it is offered and the server's 403 shows as an error.
- The page lists a language row first. It starts in the Settings › Playback subtitle language (`defaultSubtitleTrackSelection` when it is a language), else the audio language, else English, and Left/Right (or its arrows) move through `ConfigManager.supportedTrackLanguages`; the page shows the new language loading and searches once it has stayed for 500 ms (`SubtitleSearchViewModel::kLanguageSearchDelayMs`), so stepping through several languages sends one search. Results are sorted best first: those that match the file, then by score, then by downloads; ties keep the server's order. Each row shows the release name, provider, format, score, downloads and SDH/Forced/Machine-translated flags, and "Matches this file" when it does. Loading, empty and error states are `StateMessage` rows; selecting the error row searches again. Silo's `warnings` (providers that failed while others answered) show as a "providers failed" row above any results, so a failing provider does not read as "No subtitles found"; selecting it searches again.
- Choosing a result downloads it through the server; `PlayerController::addDownloadedSubtitleTrack` adds it to the Subtitles menu (and to the active segment's tracks) and selects it through `setSelectedSubtitleTrack`, like a pick from the menu. A burned-in subtitle is therefore replaced through a new stream first (the download is loaded beside it once it plays), and a replan in flight, a held pick and the subtitle delay are handled as for any pick.
  - The pick is the user's (see "Track preferences"): it is saved for the season or movie (extras save nothing) by what the track is, but only once mpv has loaded and selected it (its sid is mapped, `downloadedSubtitleLoaded`). Until then the result row says it is downloading. If mpv never confirms it (15 s after `sub-add`, 45 s when a new stream comes first) or the server cannot switch streams, nothing is saved, the menu goes back to what plays, and the notice says the subtitle could not be loaded (`downloadedSubtitleLoadFailed`).
  - Jellyfin: the download is a stream of the media source, so it is added to the playing source's `mediaStreams` with its language, title, codec and forced/SDH flags, and the saved pick carries that identity and matches the same kind of track in later episodes. A new stream keeps it in the menu.
  - Silo: the download is no stream of the source (sessions list downloaded tracks only by inventory position), so it gets a menu index of its own (from `kDownloadedSubtitleIndexBase`, -2000, down) and the saved pick is `ExplicitStream` with its language and SDH flag and no index; later episodes pick their own track in that language (an embedded one included), and `getLastSubtitleTrackFor…` answers -2 for it. It replaces a held pick and cancels a waiting write of an earlier pick, and is not written back to Silo, which stores track preferences by combined ordinal of a file. When a seek re-anchor or track change brings a new inventory that lists the download (`PlaybackTrack.storedSubtitleId`, from the sidecar URL's `downloaded_subtitle_id`), that entry takes the download's menu index and fresh URL; when the inventory does not list it, the download is added back. A download the viewer asked for stays selected unless the new stream burns another subtitle in. The next session's inventory lists it as a downloaded track.
  - A subtitle the viewer picks while the server downloads wins: the download is then only listed (`subtitleListed`), not selected or saved.
  - A notice at the top of the overlay says which subtitle was added and the menu closes; a failure shows the reason there and leaves the page open. When the stream changes under the same item while a download is in flight (a version switch, a new session), the download is dropped and the notice says to choose it again.
  - Silo: `downloadSubtitle` answers the stored subtitle. Bloom loads it through the session's sidecar route, `/api/v2/stream/{session_id}/subtitles/0.{vtt|ass}?file_id=…&downloaded_subtitle_id=…` with the account `token`, which serves a stored subtitle of the session's file by its identity (the path ordinal is display only). ASS/SSA keep their styling; other text formats arrive as WebVTT. Without a session there is no URL, and the download waits for the next session.
  - Jellyfin: `DownloadRemoteSubtitles` (204) saves the file beside the media and queues an item refresh. Bloom then reads PlaybackInfo for the item again, up to six times two seconds apart (`PlaybackService::kDownloadedSubtitleReadAttempts`/`kDownloadedSubtitleReadIntervalMs`), until the playing media source lists an external subtitle stream in the downloaded language that it did not list before. Streams are compared by what they are (language, forced, SDH, codec, title, and path when the provider gives one), counted, since indexes can shift when the item refreshes; streams in other languages are never taken, and with several new ones the highest index wins. Its URL is built as for any sidecar (`DeliveryUrl` with the current `ApiKey`, or the `GetSubtitle` fallback), and its real index is passed to `sub-add` so the menu, reporting and the saved pick use it. If it never appears, the error says the subtitle will be listed next time; when a stream of that language was already there, it says the download may have replaced a subtitle file of the same name (Jellyfin then lists no new stream, and the existing one already serves the new file).
- Retry semantics: searches and status reads are reads (`Idempotent`); downloads are sent once (`Never`), because Silo marks `downloadSubtitle` `non_retryable` and a Jellyfin replay could fetch from the provider again. Each request carries the caller's request key and is dropped after a session, connection or profile change.
- Merged spaces route playback to the owning connection first, so the live connection that runs the search is the playing item's own.
- Stale answers: the view model drops results of an older search, another language or another item, cancels its requests when playback stops or moves to another item, and does not load a download whose answer arrives after playback moved on.
- External mpv: the search is part of the Bloom playback overlay, which external mpv does not show, so it is omitted there; mpv's own window has no Bloom menus. The detail pages' subtitle menus do not offer it either (a Silo download needs a playing session to be loaded, and Jellyfin's would only appear after the item refresh); download from an embedded backend, or from the server's web UI, and the subtitle is listed the next time the item plays.
- Windows: the page is part of `EmbeddedPlaybackOverlay`, so with `win-libmpv` it renders in the detached overlay window above video like the rest of the settings pop-out.
- Not in this change: Silo's `listStoredSubtitles` and `deleteStoredSubtitle` (removing a download needs `If-Match` and is left to the server UI), AI translation and subtitle sync.
- Tests: `SubtitleSearchTest` covers both providers' requests and parsers, the gating (Silo status per profile, Jellyfin's subtitle management policy), sorting, Silo warnings, the Silo search and download over a fake network (with the download sent once), the Jellyfin PlaybackInfo re-read, its wait for the refresh and its match by identity and language, cancellation, and the view model's flow: search, download, sub-add with select, the pick saved only after mpv's sid report with the Jellyfin stream's identity, a Silo download saved by language and found in a later episode's embedded track, a failed load saving nothing, a manual pick during the download winning, a burned-in subtitle replaced through a new stream with the download kept across a track change, a seek re-anchor and an audio change, stale answers, cancellation when playback stops, the interrupted-download notice, the debounced language steps and the default language. `TrackPreferencesManagerTest` round-trips a pick with a language and no index.

### Shuffle play
- Series pages (the whole series), season pages (that season), movie or series library pages (the whole library) and collection pages (the collection's movies and episodes: `kind: "collection"` with the canonical collection's `collectionKind`, `boxSet`, `library` or `user`) have a Shuffle button. Silo sends a collection as `{scope: {kind: "library_collection" | "user_collection", id}}` to `createShuffle` and offers it only for the kinds its capability document lists in `scope_kinds`; Jellyfin reads random pages with the box set as `ParentId` and shuffles box sets only. The collection page starts its shuffle on the collection's own connection (`SourceRouter`, and `connectionId` in the scope). Pages call `PlayerController.startShuffle({kind, id, collectionKind, title})` with canonical `Bloom::ShuffleScope` names; `LibraryService.supportsShuffle(kind, collectionKind)` decides whether the button shows (see [provider compatibility](provider-compatibility.md#playback-themes-and-seek-previews)).
- `ShuffleSession` (`src/player/ShuffleSession.*`, owned by `PlayerController`) holds the running shuffle. Silo keeps it on the server (`ProviderCapability::ServerShuffle`): `createShuffle` names what plays now (`current`) and after it (`next`), `advanceShuffle` moves on from the item that finished, `skipShuffleItem` replaces the announced next item, `getShuffle` re-reads it, `deleteShuffle` stops it. For Jellyfin (`ProviderCapability::LocalShuffle`) Bloom keeps the queue from random pages under the scope (`getShuffleItems`, 50 at a time, another page when two or fewer are queued after the next one). `PlaybackPolicy::planLocalShuffleRefill` drops items already handed out this cycle, so nothing repeats until the whole scope has played; a page with nothing new is read again at most three times while the scope holds more, then a new cycle starts. What is already queued stays queued once, and the item playing now comes again only when nothing else can (a one-item scope repeats it, so Up Next always has a pick). A page that adds nothing to the queue (a scope smaller than the queue wants) stops the reads until the next advance. A failed refill is tried again on the next move, unless nothing is queued: one failing while the session is starting, moving on, or while Up Next waits on an empty queue (`refresh()` reads another page then) ends the shuffle, which closes Up Next and releases the display restore. A skipped item returns to the pool, as on Silo. Neither provider puts extras in a scope.
- Each pick plays from its beginning (`startPositionMs: 0`, whatever its resume point) as an ordinary `requestPlayback` with `"shuffle": true`. Its resume point survives: until playback passes the point the pick had (its canonical `positionMs`), progress, pause and stop reports carry that point instead of the earlier position (`PlayerPolicy::shuffleReportPositionMs`), so neither provider moves it back and Jellyfin's 5 % minimum never clears it. Once playback has passed it, reports follow playback again, and a completed pick still counts as played. Retries, recovery and version switches carry the flag (`continuesSession`, `RecoveryContext::shuffle`), so they stay in the shuffle. Any other playback start ends it.
- Track preferences: each episode resolves its own season's picks (`seasonId` is the episode's season, `preferredAudioIndex`/`preferredSubtitleIndex` are `-2`, and no `carryTrackPreferencesFromSeasonId` is sent), and movies their own; nothing carries from one pick to the next. Silo's shuffle cards name the series and season number but not the season, so `ShuffleSession` reads the series' seasons once (a private `getItems` key) before handing out its first episode; a season scope uses the scope itself. A failed listing plays the episode without a season scope and is not kept, so the series' next episode asks again; the listings are cleared with the shuffle.
- End of a pick (`PlayerPolicy::postPlaybackScreen` with `shuffle`, which answers `ShuffleUpNext`): as for an episode's Up Next and the after-movie screen, any end that is not an error past the completion threshold (playing to the end, Stop, or closing mpv's window there) opens Up Next in shuffle mode, never the series' Up Next, the after-movie screen (a shuffled movie included) or a next-episode prefetch; Close there ends the shuffle. Ending short of the threshold, an "End of this movie/episode" sleep timer, or the sleep timer's grace countdown ends the shuffle. A failure keeps it only while recovery retries the same pick; leaving the error (`clearError`) ends it. `finalizeTerminalTransition` opens the shuffle's Up Next before it would emit `navigateToAfterMovie`.
- Up Next in shuffle mode: `PlayerController.shuffleUpNextRequested(item, autoplay)` opens `UpNextScreen` with `shuffle: true` on top of the page the shuffle started from, after the opaque handoff surface. It shows the next pick (a movie by its own logo or title and year, an episode by its series and `S# E# • Name`), the "Up Next · Shuffle" eyebrow, the usual countdown (`autoplayNextEpisode`, `autoplayCountdownSeconds`), Play, "Shuffle Again" (`skipShuffleNext`, another pick replaces the shown one) and Close. No season row is shown. Play (or the countdown) calls `playShuffleNext(startedByAutoplay)`, which advances the shuffle and plays the pick it returns; Silo also re-reads the shuffle when Up Next opens, so a next item that can no longer play is replaced before it is offered. Close, Back and Escape (and Escape on the handoff surface) call `stopShuffle()` and return to the page underneath.
- Still watching: a countdown start counts like autoplay (`startedByAutoplay`), so after `stillWatchingEpisodeThreshold` picks in a row with no input the shuffle's Up Next asks "Still watching?" instead: Continue plays the next pick as a manual start, Stop ends the shuffle.
- Sleep timer: a duration runs on through the shuffle's Up Next (`continuesWatching: "upNext"`) and through Next (`"episodeSkip"`) with the time it had left; "end of this movie/episode" ends the shuffle at the end of the pick; stopping cancels the timer as usual.
- Next during playback skips the pick: the overlay's Next ("Next in shuffle", for movies too) and `script-message bloom-next-item` from mpv's `input.conf` advance the shuffle and replace the playing pick through the queued replacement path. Previous is hidden while a pick plays, and the season's episode neighbours are not read. Next is disabled while the shuffle moves on.
- The shuffle belongs to the connection it started on (merged spaces route detail pages to the owner first; a combined "All …" page has no Shuffle). Replies for another connection or an earlier shuffle are dropped. A Silo shuffle that answers `404` is gone (stopped elsewhere, or untouched for the server's 7-day retention); one whose scope holds nothing playable answers `409`. Either ends the shuffle with a toast (`shuffleEnded`), as does any failure to move on, which also deletes it when the server answered. `deleteShuffle` is sent only while the shuffle's connection is still the active one. A shuffle request whose connection, profile or provider changed before the answer is failed by `LibraryService` with status 0 (as Home card and rating writes are), so a starting or moving shuffle ends instead of waiting, without a `DELETE`; moving on after the active connection changed ends the shuffle at once.

UI Components for Playback
- Explicit user play actions now route through `PlayerController::requestPlayback(...)` instead of assembling stream URLs in QML. This keeps version selection, multipart resolution, and track-hint handling consistent across detail pages, quick play, and Up Next manual play.
- `MediaSourceSelectionDialog.qml` is the global version picker for entries with multiple media sources (for example 1080p vs 4K) when `playbackVersionPreference` is `ask`. `Main.qml` loads it lazily; the dialog parents itself to `Overlay.overlay` so it centers on the window. It uses the app theme/focus patterns, supports keyboard-only selection, and restores focus with `Qt.callLater(...)` on close.
- `EmbeddedPlaybackOverlay.qml`: Native 10-foot playback overlay rendered in the dedicated transparent overlay window for Windows embedded playback (and inline for Linux embedded validation). It shows a compact header and one unified glass control bar.
  - Header (top-left): back control (exits playback), the series/movie logo bounded to a compact wordmark, and the subtitle line beneath it (`S1 E6 - Name` renders as `S1 E6 • Name`). Light top and bottom scrims keep chrome legible without dimming the whole picture.
  - Control bar (bottom): an optional preview strip, the scrub row (elapsed time, progress with cache fill and provider chapter ticks, remaining time), and the transport tray. The tray holds, left to right: volume button plus inline volume slider (hidden when the bar is too narrow), previous episode, previous chapter, back 10 s, play/pause, forward 10 s, next chapter, next episode, and the playback settings control. All transport controls are icon-only with accessible names and pointer tooltips.
  - Previous/next episode controls appear only for episodes and are disabled until `PlayerController` resolves a neighbour (see "In-playback episode navigation"). Disabled and hidden controls are skipped by Left/Right navigation.
  - Playback settings pop-out: one menu combines Subtitles, Audio Track, Quality (only when the item has more than one version), Audio Output, and Sleep Timer (see "Still watching? and the sleep timer"). The root list shows each category's current value; Right/Enter opens a page, Left returns to the root list, and Back/Escape returns to the root list when the page was opened from it, otherwise closes the menu. `A` and `S`/`T`/`C` (controller Y/X) open the Audio and Subtitles pages directly.
  - On the Subtitles page, "Search subtitles…" (shown when the server offers it) opens the subtitle search page; see "Searching for subtitles".
  - The Subtitles page ends with the subtitle delay row: Left/Right adjust the saved delay by `subtitleDelayStepMs` (1 ms), Select resets it, and pointer users get `-`, `+`, and reset buttons. Delay is scoped to the current season/movie, except for a subtitle Silo re-timed, which keeps its own delay (see "Syncing subtitles to the audio").
  - Below it, the Subtitle Size row steps the text subtitle size by `subtitleSizeStepPercent` (5 %) with Left/Right or `-`/`+` and resets it to 100 % with Select or the reset button (`PlayerController.adjustSubtitleSize(steps)` / `resetSubtitleSize()`). The size is the global Settings › Playback › Subtitle Appearance › Size, so it is saved for every playback and applies at once (see "Subtitle appearance").
  - On Silo, when the selected subtitle is an external file or a download the server can re-time, the Subtitles page ends with a Sync to Audio row, and after a correction a Reset Timing row (see "Syncing subtitles to the audio"). Select starts the action; the menu stays open and the row's second line shows the progress and then the outcome, which also shows as a notice.
  - The Quality page lists `PlayerController.playbackVersions`; choosing another version calls `switchPlaybackVersion` (see "In-playback version switching").
  - Direct playback shortcuts are resolved through `InputBindingManager` in the `playback` runtime context so keyboard and controller bindings are configurable in Settings > Input. Defaults include `+`/`=` or media volume-up for volume up, `-` or media volume-down for volume down, `V` for the volume control, `A` for audio tracks, and `S`/`T`/`C` for subtitles.
  - Volume: activating the volume button (or `V`, controller Back/View) puts the inline slider into adjust mode, where Left/Right change volume by 5, Space toggles mute, and Enter/Back leave adjust mode. The slider fills at 100%; a readout shows boost above 100%. When the bar is too narrow for the slider, a floating volume pill hosts adjust mode instead.
  - Volume up/down bindings adjust volume without opening the full playback chrome. When no chrome is visible, the compact volume pill appears alone as a short-lived bottom-center OSD (~1.8s, refreshed on repeat). When any chrome is already visible (full controls, seek preview, or chapter rail), volume up/down only adjusts volume and refreshes the relevant auto-hide timer.
  - Default controller playback bindings use D-pad/left stick for playback overlay navigation or hidden-overlay seek behavior, A for confirm/Skip Intro/Credits, B for Escape-style dismiss then stop, Start for play/pause, LB/RB for seek, LT/RT for chapters, Y/X for the audio/subtitle pages of the settings pop-out, Back/View for volume, right stick vertical for volume, L3 for ASS subtitle override, and R3 for deband.
  - Navigation: Up moves transport → scrub row → back control; Down reverses it, and Down from the transport tray enters the chapter rail when chapters exist. Left/Right on the scrub row seek by 10 s. Left/Right in the tray move between controls and wrap.
  - Escape and controller B first dismiss visible playback chrome (settings page, settings menu, volume adjust mode, volume OSD, skip prompt, then the controls); a second press stops playback when the overlay is already hidden. The header back control exits playback immediately while controls are visible.
  - Auto-hide: the chrome hides after 2.5 s of inactivity, but never while paused, seeking, while a menu or volume adjust mode is open, or while the pointer is over the chrome. Pointer movement keeps visible chrome alive; moving into the top or bottom hot zone reveals it. The mouse cursor hides after 2.5 s without pointer movement (`cursorIdleHideMs`), paused or not, unless it rests on the chrome or is scrubbing; hiding switches `InputModeManager` to keyboard mode as a key press does. Synthetic hover events at an unchanged position do not count as movement.
  - Enter while no playback control can take it (chrome hidden, seek-only mode, or only the volume OSD showing) opens the full controls with play/pause focused, like controller A; with external mpv it is swallowed. `Main.qml` also disables the page tree behind embedded video, so a hidden page (for example a focused Play button) never receives keys during playback.
  - Seek-only mode: Left/Right while the chrome is hidden seek and show only the scrub row (no header or tray), which hides again after the seek preview hold.
  - Trickplay filmstrip: while seeking (keyboard, controller, or a transport seek) or scrubbing with the pointer, a strip of server trickplay or locally generated frames opens above the scrub row. The centred, highlighted frame is the previewed position and carries its timestamp; neighbours are one seek step apart (`max(trickplayIntervalMs, 10 s)`), and clicking one seeks there. Frames come from `PlayerController.trickplayPreviewUrlAt(seconds)`, which uses a small encoded-frame cache for server images and the session image cache for local previews. The strip is hidden during normal playback and entirely absent when trickplay is unavailable. Without trickplay, pointer hover shows a time bubble instead.
  - Chapter mode: `Down` from the tray (or while the chrome is hidden) replaces the preview strip with a provider-backed chapter rail inside the bar and hides the tray. `Up` returns to transport controls and `Escape`/`Back` hides the overlay. `Left`/`Right` browse chapter cards, while `Enter`/`Space` (or a click) seeks to the focused chapter and keeps the rail open until normal inactivity dismissal.
  - Chapter cards use the active provider's chapter thumbnail when usable chapter image metadata is available. Missing chapter artwork renders a neutral themed placeholder tile instead of reusing episode/poster artwork.
  - Chapter thumbnail diagnostics are logged during provider-neutral chapter normalization: loaded chapter metadata, generated provider image URLs, and image-cache transport/decode failures. These logs are intended for server/client compatibility debugging when the rail falls back to placeholders.
  - Intro/outro skip UX: a single "Skip Intro"/"Skip Credits" control appears only while the corresponding segment is active. With the chrome hidden it is an auto-focused prompt at the bottom right; with the chrome visible it sits above the bar's right edge and is the last stop in the tray's Left/Right order.
    - Popup timing is controlled by `ConfigManager.skipButtonAutoHideSeconds` (`settings.playback.skip_button_auto_hide_seconds`, range 0-120; 0 disables popup only). The popup is still dismissed as soon as the active intro/outro segment ends.
    - Optional automatic skip is controlled by `ConfigManager.autoSkipIntro` and `ConfigManager.autoSkipOutro`; each auto-skip applies at most once per playback item even if the user seeks back.
    - Segment source precedence is provider-neutral: native Silo marker responses win per segment type; when Jellyfin is active, the core `/MediaSegments/{id}` response is the server-marker source and wins per type. External providers are queried only after the active server response and may fill missing types such as recap, intro, credits, or preview.
    - External provider order defaults to TheIntroDB then IntroDB. Reads are anonymous. TheIntroDB v4 prefers TVDB metadata and accepts TMDB or IMDb metadata as a fallback. Episode requests use series IDs, season/episode coordinates, and the server's episode order when available. IntroDB requires series IMDb metadata plus season/episode. See `docs/media-segments.md`.

Playback overlay metadata
- `PlayerController` exposes `overlayTitle`, `overlaySubtitle`, `overlayBackdropUrl`, and `overlayLogoUrl` for the native overlay (header, buffering card, and backdrop).
- Optional `overlayLogoUrl` is a Jellyfin logo image URL (typically the `image://cached/...` form from `LibraryService::getCachedImageUrlWithWidth`). `EmbeddedPlaybackOverlay.qml` shows the logo when the URL is non-empty and the image reaches `Image.Ready`; on load failure (`Image.Error`) or when the URL is empty, the overlay falls back to `overlayTitle` text.
- When a logo is present, the buffering card keeps `overlaySubtitle` visually centered beneath it; the active playback header left-aligns the subtitle under the compact logo. Text fallbacks use a heavier weight for readability.
- `PlayerController` also exposes normalized playback chapters, the currently playing chapter index derived from live playback position, and a seek-by-chapter action. Focused rail selection remains local UI state so playback progress never steals navigation focus while browsing.
- Detail views set metadata before playback starts:
  - Movies: title + production year.
  - Episodes: series title + `Sxx Exx - Episode Name`.
- Fallback behavior still exists (`currentItemId`) when explicit metadata is not provided.

Playing extras
- Detail pages play a local extra (trailer, featurette, deleted scene, ...) with an ordinary `PlayerController::requestPlayback` whose request carries `contentKind: "extra"` and the extra's own `itemId`. Playback info, descriptors, version choice, and reporting go through the normal provider path. See [canonical models](canonical-models.md#local-extras).
- `PlayerPolicy::playbackContentRules(PlaybackContentKind::Extra)` (Qt Core only, no I/O) decides what an extra may touch. The controller applies it when the request arrives and keeps the kind for the playing item:
  - it starts at the beginning: the request's start position is dropped, so the parent's resume point is never used. A retry, recovery, or version switch of a playing extra carries `contentKind` (and `RecoveryContext` keeps it) plus `continuesSession: true`, so it stays an extra and continues where it was;
  - its series and season are dropped, so there is no season track scope, no previous/next episode controls, and no autoplay series;
  - it never prefetches or resolves a next episode and never opens Up Next or the after-movie screen or autoplays, even past the completion threshold;
  - audio and subtitle picks during it are not saved for any scope, so nothing is written back to the server's track preferences;
  - it starts with no subtitle delay, and subtitle timing changed during it is not saved for any scope. (Audio delay is a global setting, not per scope.)
- Reporting names only the extra. Start, progress, and stop carry the extra's own ID, and Bloom never calls the watched endpoint at a threshold, so the parent movie or series is never marked watched and its resume position and Continue Watching entry are untouched.
- What the server keeps for the extra itself:
  - Jellyfin extras are ordinary items with their own user data, so Jellyfin records their position and played state on the extra's ID. Bloom does not read it: extras always start from the beginning and the shelf shows no progress. Jellyfin's resume and Next Up queries list library items, not extras owned by a movie or series; this is expected but not live-validated.
  - Silo records protocol 3 progress against the extra's `content_id`. `ItemExtraInfo` carries no user state, so Bloom shows none. Silo's Continue Watching lists catalog items, and its extras live in a separate table; this is also not live-validated.
- In a merged space the detail page routes to the extra's `connectionId` (`SourceRouter`) before requesting playback. Leaving the extra returns to the detail page with focus on the card or Play Trailer button that started it.

In-playback version switching
- `launchResolvedPlaybackRequest` records the primary presentation sources of the launched item as `PlayerController.playbackVersions` (`{id, title, subtitle, selected}`, built by `PlayerPolicy::playbackVersionOptions`). The list is scoped to the item it was built for and reads as empty for any other current item. Multipart items publish no versions: a restart always begins at the first part, so switching mid-item would lose the position.
- `switchPlaybackVersion(mediaSourceId)` ignores unknown or already-selected versions. Otherwise it issues an ordinary `requestPlayback` for the current item at the current position (or the queued resume position while the item is still loading) with `mediaSourceId` set, which forces that source and skips the version prompt. Track indices differ between versions, so the new version resolves audio/subtitles from the saved scope preferences. Replacement goes through the normal queued replacement path, so the previous attempt reports stop exactly once.

In-playback episode navigation
- When an episode starts (series and season known), `PlayerController` requests the current season listing and the series season listing through `LibraryService::getItems` (typed `Episode` and `Season` so Silo uses its hierarchy routes) with private `player-adjacent-episodes` request keys, so view models ignore the responses. Responses are accepted only for the same item and connection.
- `PlayerPolicy::resolveAdjacentEpisodes` picks the neighbours in provider order. At a season boundary, `PlayerPolicy::adjacentSeasonId` finds the previous/next non-empty season (specials are skipped unless the current season is specials) and one more listing supplies its last/first episode.
- `hasEpisodeNavigation`, `hasPreviousEpisode`, and `hasNextEpisode` drive the overlay controls. `skipToPreviousEpisode()`/`skipToNextEpisode()` request the neighbour like Up Next manual play: series overlay title and logo, `S# E# - Name` subtitle, resume position, version affinity, the current audio index, and the subtitle choice only when the season scope holds an explicit selection or Off.


Silo API v2 playback migration
- Playback starts only when `/api/v2/playback/capabilities` allows protocol 3, `playback_plan_v3`, and `original_http`. Capability ETags support conditional reads. Build and contract identity fields never enable a feature.
- Start uses an opaque string file ID, the advertised installation ID, and one playback attempt ID per request. Bloom advertises declared mpv codec capabilities, original HTTP delivery, and the HLS class for server remux and transcode. The server keeps the original file whenever it can. It keeps provider transport claims independent from local passthrough. See [Audio output](audio.md).
- `PlaybackDecision.playback_plan` supplies the stream URL, selected tracks, source duration, and timeline. Header-authenticated media is not advertised on the URL-only mpv path. Non-empty required stream headers remain unavailable. Unknown outcomes and deliveries fail safely, and progressive remux is never requested. Bloom preserves the returned stream reference and appends the documented account-token fallback only on the configured API origin. It does not add credentials to other origins. For HLS on that origin mpv also sends the bearer header, because long server playlists carry the query through `EXT-X-DEFINE`, which FFmpeg does not expand.
- HLS plans map to `DirectStream` (remux) and `Transcode`. `Bloom::StreamRequest` carries the timeline offset, whether seeks must reanchor, and the tracks the stream carries. `PlayerController` keeps every position on the source clock: it adds the offset to mpv's position, subtracts it from seek targets and sidecar subtitle timing, and takes the duration from the plan. On a stream that cannot seek anywhere (a growing copy remux), seeks outside mpv's forward cache become `seek_reanchor` replans.
- Replans (`POST /playback/{session_id}/replan`, `track_change` or `seek_reanchor`) carry the active plan identity and a new replan request ID; the old audio PATCH route is not in the v2 contract. On HLS, audio changes and embedded-subtitle changes replan, and the embedded subtitle arrives as an HLS WebVTT rendition when negotiated, otherwise as a sidecar or burned in. A replan answer reloads mpv in place (`loadfile replace` with `start=`) and re-adds sidecars after `fileLoaded`; the replaced file's end is ignored. Only one replan is in flight; later requests merge into the next one, and a refused replan restores the previous track.
- Progress sends monotonically increasing sequences, installation identity, and position in seconds. Stop sends a stable stop ID and the last position. Provider session state remains in memory and is scoped to the account, profile, and Bloom media connection.

Silo theme audio
- Theme metadata comes from the detail response's `themes` set. Bloom checks `/api/v2/catalog/themes/capabilities` before requesting a grant for the first theme. Grants use the set's `owner_id`, which can differ from the detail item ID.
- The grant URL is an expiring credential. Bloom keeps it in memory, resolves it against the server origin, and passes it to `ThemeSongManager`. It does not build an audio URL from an item ID.
- Converted delivery is progressive AAC in audio-only MP4. Bloom disables media-player looping for Silo grants and requests a fresh grant at each loop. It fetches grant audio once without ranges into a buffer, with a 16 MiB limit and a 45-second deadline, before giving a seekable device to QtMultimedia. QtMultimedia cannot make additional network requests or replay a converted URL. Expired grants and unknown delivery values fail closed.

Silo seek previews
- Server trickplay is preferred. If metadata or images are unavailable, Bloom starts local generation for the full direct-play timeline. Generation does not wait for a seek. See [seek previews](seek-previews.md) for process ownership, limits, and validation.
- `/api/v2/watch/{id}` supplies consolidated versions, tracks, source duration, chapters, and per-version `trickplay_available` state.
- Bloom requests `/api/v2/watch/{id}/trickplay?file_id=...` only when the playback capability advertises `trickplay_v1` and the selected watch version has published previews.
- The adapter maps thumbnail size, sheet grid, interval in milliseconds, and thumbnail count. Sheet URLs remain opaque and expire at `expires_at`. A bounded in-memory cache keeps them separate by server, account, profile, and item. Missing or expired sheets produce no preview URL.
- PlayerController passes the current file ID on every item or multipart transition. Jellyfin preview requests retain their existing route and mapping.


Native multipart playback starts and reports one protocol 3 session per physical file. Content identity and the logical aggregate timeline remain separate from file identity. A native failure never falls back to a Jellyfin stream.

Jellyfin and native Silo playback share the provider-neutral descriptor/reporting boundary; only the provider owns endpoint construction and wire serialization.

Alternate versions and multipart playback
- Alternate media sources are surfaced from `PlaybackInfoResponse.mediaSources`. `playbackVersionPreference` (Settings > Playback > Preferred Version) decides which one a user-initiated request plays (`PlayerPolicy::preferredMediaSource`):
  - `highest-quality` (default): most pixels, then HDR/Dolby Vision over SDR, then bitrate, then file size.
  - `smallest`: smallest file size, then lowest bitrate, then fewest pixels. Sources missing a metric never win on it.
  - `ask`: opens `MediaSourceSelectionDialog` before playback starts. Requests that cannot prompt (autoplay, recovery) fall back to `highest-quality`.
  - Ties keep server order. An explicit `mediaSourceId` (the in-playback Quality page) always wins.
- Automatic next-episode playback does not re-prompt. `PlayerController` keeps a runtime version affinity from the chosen source and reuses it for the next episode in this order:
  - same parent folder path
  - same media-source name
  - same normalized video/container/bitrate signature
  - the version preference above
- Jellyfin multipart items are resolved via `/Videos/{itemId}/AdditionalParts`. Bloom fetches `PlaybackInfo` for each part, matches each later part against the chosen root version with the same affinity order, and falls back to the version preference when no match exists.
- Multipart playback is queued into the backend playlist with `appendUrlsToPlaylist(...)` so part 2 starts seamlessly after part 1 without returning to idle.
- User-initiated playback uses request-scoped `PlaybackInfo` and `AdditionalParts` responses so prefetch responses for the same item cannot satisfy a newer play request. If the primary `PlaybackInfo` request fails or returns no media sources, Bloom surfaces a playback error instead of falling back to a plain stream URL.

Multipart reporting model
- Bloom keeps two playback identities for multipart items:
  - logical item: the original movie/episode shown in the overlay and used for completion/autoplay decisions
  - reporting segment: the currently active physical part used for Jellyfin start/progress/pause/stop reporting
- Progress shown in the client is aggregate across all parts. The controller tracks the prior-segment millisecond offset and combines it with the current segment position for timeline and completion-threshold calculations.
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
- `win-libmpv` also passes `--curl-enabled=no` before profile args. The pinned Windows mpv build has mpv's libcurl stream backend, which retries a dropped transfer up to 5 times with no delay and then ends the stream; the reconnect options above only apply to FFmpeg's http protocol. With curl disabled, a server hiccup (for example a debrid proxy such as Decypharr) stalls mpv into `paused-for-cache` while FFmpeg reconnects with backoff. A profile can set `--curl-enabled=yes` to opt back in.
- `playbackCacheSizeMB` is exposed in Settings -> Playback as "Playback Cache Size" (range 50–2048 MB, default 500 MB).
- This allows mpv to buffer aggressively in RAM so brief server outages do not stall playback.
- Startup buffering mode can be set globally (`settings.playback.startup_buffering_mode`) and overridden per library under the active `settings.connection_state` scope. `normal` keeps existing startup behavior. `remote-mount` starts mpv paused and appends startup-prebuffer args after profile args (`cache-pause-initial`, `cache-pause-wait=60`, `demuxer-readahead-secs=60`, `cache-secs=120`, and larger demuxer/cache buffers). Bloom keeps the full-screen startup buffering UI visible and unpauses only after `demuxer-cache-time` reaches the initial cache target or the extended startup timeout is reached.
- Runtime rebuffering uses the compact native playback spinner over the video instead of the full-screen startup card. This keeps the current frame visible while `paused-for-cache` or recovery is active.
- For longer server outages, `PlayerController` supports provider-aware automatic recovery:
  - When playback hits `Error` because of a network/timeout failure, the controller stashes a `RecoveryContext` with the active connection/provider, canonical media identity, provider source/version, track selections, and last known position.
  - If the backend reports stopped before the recoverable mpv error arrives, `PlayerController` upgrades the pending terminal transition from `Stop` to `Error`, stashes the same recovery context, and keeps playback active so embedded video/overlay surfaces are not torn down during retry.
  - If `autoRecoverPlayback` is enabled (default `true`, stored in `settings.playback.auto_recover_playback`), the controller enters recovery mode and pings the selected provider every 5 seconds via `LibraryService::pingServer()`. The ping reads the provider's authenticated identity route (native Silo `GET /api/v2/account/me`, Jellyfin `GET /Users/{id}`) through `HttpTransport`: one attempt with a 10-second deadline and no replay, since the 5-second timer is the retry loop. The public health routes (`/health`, `/System/Info/Public`) never answer `401`, so they cannot tell a returned server from an expired session. A refused access token is renewed first (Silo refresh token); only a refused renewal reports the session expired, and the sign-out then waits until playback stops (`DeferSessionExpiry`). A renewal that cannot reach the server counts as unreachable, so recovery keeps pinging.
  - When the ping succeeds, recovery asks the selected `IPlaybackProvider` for fresh playback information: Jellyfin refreshes `/Items/{id}/PlaybackInfo`; native Silo refetches watch state and capabilities, then starts a protocol 3 session. Both resume at the last known position with prior track hints.
  - Recovery retries indefinitely until the provider returns or the user explicitly stops/retries/clears the error. A failed native start or expired signed URL is surfaced/refetched through Silo's owning catalog resource, never silently replaced with a Jellyfin stream.
  - Manual retry (`retry()` / `Event::Play`) follows the same provider-aware refresh path instead of replaying an old stream URL.
- `autoRecoverPlayback` is not exposed in the settings UI; it can be toggled by editing `app.json` directly.

mpv config hints
- Create `mpv.conf` and `input.conf` when you need custom behavior (e.g., enable vo, hardware settings) in `~/.config/Bloom/mpv/`.
- Always pass mpv arguments from `ConfigManager` so users can override behavior at runtime.
- Built-in MPV profiles are `Low Quality`, `Medium Quality`, `High Quality`, `ArtCNN`, `ArtCNN-Deband`, `nnedi3`, and `nnedi3-deband`. The shader profiles append Bloom-bundled GLSL/hook shaders from `~~/shaders/`, and `ConfigManager` keeps those shader files plus Gandhi Sans subtitle fonts refreshed under Bloom's mpv config directory.
- Settings -> MPV Profiles now edits `extra_args` as an ordered list of one argument per entry (add/remove per row) for keyboard-first 10-foot usability.
- Migration compatibility: `extra_args` accepts both array and legacy newline-delimited string formats and normalizes to array on save.
- Subtitle styling from Settings › Playback › Subtitle Appearance is appended after the profile arguments; fields left at their default keep the profile's `sub-*` values (see "Subtitle appearance").
- Embedded playback shortcuts include `K` to toggle `sub-ass-override` between `no` and `yes`, and `B` to toggle mpv debanding between `no` and `yes`. Controller mappings route through the same `InputBindingManager` actions and call `PlayerController.toggleSubtitleAssOverride()` / `PlayerController.toggleDeband()`.

Jellyfin integration
- Key endpoints used frequently:
  - `/Items?UserId={userId}`
  - `/Shows/NextUp`
  - `POST /Items/{itemId}/PlaybackInfo` with `PlaybackInfoDto.UserId` - Get media streams and track info
  - `/Sessions/Playing` - Start, progress, stop reporting with track selection
- Jellyfin requests follow the pinned 12.0 OpenAPI contract. Playback progress does not send the unlisted `EventName`, and stopped reports are limited to `PlaybackStopInfo` fields.
- The client persists non-secret session/connection metadata via `ConfigManager`; access, refresh, and profile credentials are restored through `CredentialStore`.
- On 401 responses, the selected provider's transport/authenticator performs bounded single-flight recovery and the refresh-once policy, then emits a sessionExpired event and triggers the provider-aware logout/restore flow if recovery fails or times out. Native Silo recovery never switches to a Jellyfin endpoint.

Security & privacy
- Access, refresh, and profile tokens issued after PIN verification are persisted only by `CredentialStore`; `app.json` stores provider-neutral connection metadata and an opaque credential reference.
- Avoid logging sensitive tokens or opaque signed media/artwork query values; prefer redacted logging when necessary.

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

Native mpv backend logs and trickplay download errors redact complete HTTP URLs and relative query credentials before logging or emitting error text. This covers media URLs (Jellyfin stream URLs carry `ApiKey=`), logged mpv commands and property values, and mpv's `end-file` error text. External mpv output remains drained without logging.
