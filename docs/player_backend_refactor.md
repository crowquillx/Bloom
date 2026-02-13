# Player Backend Refactor Plan (External mpv JSON IPC → Embedded libmpv)

## Implementation status (Milestone A + Milestone B/C kickoff)

Implemented now:
- Added backend seam under `src/player/backend/`:
   - `IPlayerBackend`
   - `ExternalMpvBackend`
   - `PlayerBackendFactory`
- Refactored `PlayerController` to depend on `IPlayerBackend` instead of `PlayerProcessManager`.
- Wired backend creation via `PlayerBackendFactory` in `ApplicationInitializer`.
- Registered `IPlayerBackend` in `ServiceLocator` and log active backend at startup.
- Added factory/runtime guardrails:
   - `PlayerBackendFactory::createByName(...)`
   - env override support via `BLOOM_PLAYER_BACKEND`
   - unknown backend names resolve to `external-mpv-ipc` with warning log.
- Added regression tests for backend factory behavior in `tests/PlayerBackendFactoryTest.cpp`.
- Added integration-level assertion in `tests/VisualRegressionTest.cpp` that `ApplicationInitializer` registers `IPlayerBackend` in `ServiceLocator`.
- Updated platform default selection behavior:
   - Linux now defaults to `linux-libmpv-opengl` when runtime requirements are met.
   - Linux auto-selects `external-mpv-ipc` when embedded runtime requirements are not met.
   - Windows now defaults to `win-libmpv`.
   - Other non-Linux platforms continue to default to `external-mpv-ipc`.
- Hardened Linux embedded runtime path:
   - safer render/update callback lifecycle,
   - coalesced render update scheduling to avoid callback storms during teardown/re-init,
   - stronger viewport/FBO render state handling,
   - scenegraph re-init handling,
   - `client-message` forwarding parity,
   - `aid`/`sid` normalization parity with external backend semantics (including node-typed values like `no`/`auto`).

Not yet implemented in Milestone A:
- Config-file backend selector key (currently env-only override + platform default selection).
- Embedded Linux/Windows backends (Milestones B/C).

Milestone B kickoff implemented now:
- Extended `IPlayerBackend` with embedded video hooks (`supportsEmbeddedVideo`, target attach/detach, viewport updates).
- Added Linux backend implementation entry point: `LinuxMpvBackend`.
- Added Linux backend selection path in `PlayerBackendFactory` (`linux-libmpv-opengl`) with OpenGL runtime guard + external backend selection when requirements are missing.
- Added Qt Quick surface primitives:
   - `MpvVideoItem` (QML-exposed C++ item)
   - `VideoSurface.qml`
- Added minimal `PlayerController` API for embedded target attach/detach, viewport forwarding, and internal/manual shrink mode property.
- Added Linux-conditional build wiring for new backend sources and optional `libmpv` discovery/linking.

Milestone C kickoff implemented now:
- Added Windows backend scaffold: `WindowsMpvBackend`.
- Added Windows backend selection path in `PlayerBackendFactory` via selector token `win-libmpv`.
- Added Windows-conditional build wiring for app/test targets using factory wiring.
- Preserved explicit backend selection behavior: `external-mpv-ipc` remains selectable via override, and unknown backend names resolve safely.
- Added focused regression coverage for Windows backend selection/wiring behavior in `PlayerBackendFactoryTest`.
- Added transparent QML overlay-window path for embedded playback controls on Windows so controls render above video without resizing or clipping the video viewport.

Still pending after Milestone B closeout (moved to start of Milestone E):
- Final Linux target runtime validation for `mpv_render_context` reliability on representative hardware/compositors.
- Linux runtime parity validation (controls, reporting, stability, no CPU readback).
- Linux packaging/runtime bundling of `libmpv` with Linux build artifacts.

Validation sequencing note (current):
- Linux on-device/runtime validation is intentionally deferred to the beginning of Milestone E (E0) when Linux test infrastructure/hardware is available.
- Milestone B closes with backend parity/hardening plus cross-platform-safe regressions (build + non-Linux tests).

## Milestone A parity checklist (current)

Status legend:
- ✅ complete and validated
- ⏳ pending

Core deliverables:
- ✅ `IPlayerBackend` introduced and compiled in app + tests.
- ✅ `ExternalMpvBackend` wraps current external mpv IPC/process behavior.
- ✅ `PlayerController` refactored to consume backend interface only.
- ✅ `ApplicationInitializer` constructs backend via `PlayerBackendFactory`.
- ✅ `IPlayerBackend` is registered in `ServiceLocator` during startup.
- ✅ Active backend is logged at startup.

Selection behavior:
- ✅ Default backend is platform-aware (`linux-libmpv-opengl` on Linux when supported; `win-libmpv` on Windows; `external-mpv-ipc` otherwise).
- ✅ `BLOOM_PLAYER_BACKEND` env override supported.
- ✅ Unknown backend names resolve safely to external backend with warning log.
- ⏳ Config-file backend selector key (deferred to later milestone).

Validation coverage:
- ✅ Build passes via project build script.
- ✅ `PlayerBackendFactoryTest` validates default selection, explicit selection, initial stopped state, and unknown-name external selection.
- ✅ `VisualRegressionTest` asserts backend service registration in startup wiring.
- ✅ `PlayerBackendFactoryTest` coverage now includes Linux backend-name selection behavior.
- ⏳ Linux embedded backend runtime validation pending on Linux target environment.

Commands used during current validation:
- `./scripts/build.ps1`
- `set BLOOM_PLAYER_BACKEND=external-mpv-ipc` (optional env selection smoke)
- `./scripts/run-windows-tests.ps1 -Config Release -OutputOnFailure -Regex "(PlayerBackendFactoryTest|PlayerControllerAutoplayContextTest)"`

## Milestone breakdown & status board

Status legend:
- ✅ done
- 🟨 in progress
- ⬜ not started
- ⚠️ blocked / decision needed

Overall milestone status:
- **Milestone A — Backend abstraction + external backend support:** ✅ done
- **Milestone B — Embedded integration + parity hardening (non-Linux runtime validation):** ✅ done
- **Milestone C — Windows embedded backend:** ✅ completed
- **Milestone D — Track selection logic and parity hardening:** ✅ completed
- **Milestone E — Linux runtime validation kickoff + soft deprecation/default switch:** 🟨 partially landed (Linux default switch completed; Linux runtime validation, Linux `libmpv` bundling, and deprecation policy pending)

### Milestone A — Breakdown (completed)
- ✅ Backend interface (`IPlayerBackend`) created and wired.
- ✅ External adapter (`ExternalMpvBackend`) implemented.
- ✅ Factory (`PlayerBackendFactory`) implemented.
- ✅ `PlayerController` refactored to backend interface.
- ✅ Startup wiring updated (`ApplicationInitializer` + `ServiceLocator`).
- ✅ Logging + backend-selection behavior added.
- ✅ Regression tests added and passing.

### Milestone B — Breakdown (closed)

#### B1. Backend and rendering primitives
- ✅ Create `LinuxMpvBackend` with `mpv_handle` + `mpv_render_context` ownership. (implementation complete; Linux runtime validation moved to E0)
- ✅ Create `MpvVideoItem` (or equivalent C++ video item) for Qt Quick render integration.
- ✅ Define minimal render callback contract between backend and item.
- ✅ Add safe startup/shutdown lifecycle for libmpv context and render context. (`mpv_handle` + `mpv_render_context` startup/shutdown hooks added; target-environment runtime validation moved to E0)

#### B2. Controller/factory wiring
- ✅ Extend `PlayerBackendFactory` to instantiate Linux backend by name.
- ✅ Platform-aware default selection implemented (Linux embedded default with external backend selection when unsupported; non-Linux external default).
- ✅ Ensure `PlayerController` behavior/signals remain unchanged across backend swap. (event/property parity improvements landed; runtime verification on Linux targets moved to E0)

#### B3. QML surface integration
- ✅ Add `VideoSurface.qml` and integrate it into main playback UI path.
- ✅ Ensure overlays remain above video surface.
- ✅ Preserve focus + keyboard/gamepad navigation behavior in embedded path integration scope. (Linux on-device runtime verification moved to E0)

#### B4. Runtime behavior parity
- ✅ Playback controls parity: command dispatch now supports typed variant command payloads.
- ✅ Track control parity: `aid`/`sid` update semantics normalized to external backend contract.
- ✅ Reporting parity: start/progress/pause/resume/stop paths preserved in backend abstraction.
- ✅ Next-up/autoplay/threshold behavior: fixed pending autoplay context handling across Idle transition so next-episode flow no longer loses series/item/track context before async callbacks.
- ✅ Added unit regression coverage for pending autoplay context flow, including mismatched-series guard behavior in `PlayerControllerAutoplayContextTest`.

#### B5. Credits-shrink hook (internal)
- ✅ Add backend/controller hook for runtime viewport resize.
- ✅ Wire a minimal internal test path for shrink/restore behavior. (controller property signal/state behavior covered by `PlayerControllerAutoplayContextTest`)

#### B6. Build and packaging
- ✅ Update CMake for Linux-only backend sources and libmpv linkage.
- ✅ Keep non-Linux builds unaffected.
- ✅ Document Linux dependency/link requirements in docs (build/runtime details tracked in playback/build documentation).

#### B7. Validation & exit criteria
- ✅ Validate regressions do not appear on external backend path.
- ✅ Add focused controller parity regressions for next-up/autoplay context handling.
- ➡️ Linux target runtime validation items moved to Milestone E kickoff (E0).

### Milestone C — Breakdown (completed)
- ✅ Implement `WindowsMpvBackend` target-handle plumbing with embedded launch argument injection (`--wid=<HWND>`) for Windows app-window embedding.
- ✅ Implement native event filter + geometry sync/debounce (Windows `WM_SIZE`/`WM_MOVE`/`WM_WINDOWPOSCHANGED` hook with debounced sync scheduling in scaffold backend).
- ✅ Implement initial transition flicker mitigation path (state-aware deferred geometry sync during move/resize/window-state transitions).
- ✅ Add initial HDR diagnostics and validation path (startup logging of HDR-relevant mpv option set and output-path hints in Windows backend scaffold).
- ✅ Expose native target handle to backend via `MpvVideoItem.winId` property to keep embedding hookup backend-agnostic from QML.
- ✅ Replace Windows IPC-delegated control/event path with a direct libmpv backend path when available (`mpv_create`/`mpv_initialize`, `mpv_command_node_async`, `mpv_observe_property`, `mpv_wait_event`) while preserving `PlayerController` signal/property contract.
- ✅ Keep direct-only behavior in migration path: `win-libmpv` now reports initialization/load failure, and explicit `external-mpv-ipc` selection remains available via backend override.
- ✅ Implement playback control routing in the same migration slice for direct Windows path (play/pause/resume/seek/stop + audio/subtitle command/property handling).
- ✅ Add Windows embedded overlay rendering foundation by introducing a dedicated embedded host window synced to viewport geometry plus a reusable backend-agnostic overlay host (`EmbeddedPlaybackOverlay.qml`) above `VideoSurface`.
- ✅ Harden direct libmpv event parity by mapping lifecycle events (`START_FILE`/`FILE_LOADED`/`PLAYBACK_RESTART`/`IDLE`/`END_FILE`) to backend running-state transitions and forwarding `COMMAND_REPLY`/end-file errors via backend error signals.
- ✅ Add embedded playback control bindings in the same slice: backend-agnostic overlay control bar wiring (`play/pause`, `seek ±10s`, `stop`) plus global keyboard shortcuts active during playback (`Space/K`, `Left/Right`, `J/L`, `S`).
- ✅ Ensure overlay visibility on Windows embedded playback with a transparent QML overlay window above video (no viewport reserve band, no clip, no video reposition); add dedicated `Esc` → stop playback behavior.
- ✅ Validate Windows-side non-runtime regressions with focused tests (`PlayerBackendFactoryTest`, `PlayerControllerAutoplayContextTest`, `VisualRegressionTest`) on current Windows build artifacts.
- ✅ Validate direct-libmpv path on representative Windows runtime packaging where libmpv is present in production deployment.

Milestone C/D/E Plezy parity checklist (review gate)
- [x] Control-path parity checked against Plezy patterns (async command dispatch + observed-property/event forwarding model).
- [x] Window-transition behavior parity checked against Plezy-style handling for move/resize/minimize/maximize/fullscreen.
- [x] Bloom-specific adaptation verified (Qt/C++ backend seam preserved; no Flutter/plugin coupling introduced).
- [x] Direct-only failure behavior validated for `win-libmpv` at code/test level (no implicit alternate backend path).
- [x] Playback controls parity verified as part of command-path migration (no temporary duplicate control implementations).

Milestone C Windows manual validation script (runtime)
- Preconditions:
   - Build: `./scripts/build.ps1 -Config Release` completes.
   - App launch uses packaged runtime: `./install-windows/bin/Bloom.exe`.
   - `libmpv-2.dll` is present in the runtime search path.
- Startup checks:
   - Start playback for a known-good item.
   - Confirm log contains `Using direct libmpv control path`.
   - Confirm log does **not** contain `Direct libmpv unavailable; embedded-first mode requires direct libmpv and disables external IPC.`
- Controls/overlay checks (Plezy parity intent: controls over video, no layout disruption):
   - Move mouse during playback: controls appear over video.
   - Wait auto-hide timeout: controls disappear without shifting or clipping video.
   - Pause: controls remain visible; resume: controls can auto-hide again.
   - Keyboard: `Space/K` toggle pause, `Left/Right` and `J/L` seek, `S` and `Esc` stop.
- Window-transition checks (Plezy parity intent: stable embedding through transitions):
   - Resize window repeatedly while playing.
   - Minimize and restore during playback.
   - Maximize, restore, and toggle fullscreen (`F11` / `Alt+Enter`).
   - Move window across monitors (if available) and repeat fullscreen toggle.
   - Expected: no white/black stuck frames, no persistent misalignment, no controls/video desync.
- Command/event parity checks:
   - Perform rapid pause/seek/seek/seek sequences.
   - Confirm playback state, position updates, and end-of-file behavior remain correct.
   - Expected: no repeated command failure warnings, no stuck paused/running state.
- Track/reporting checks:
   - Change audio/subtitle tracks during playback.
   - Stop and resume item; verify expected track state/position behavior.
   - Verify Jellyfin session updates continue (start/progress/stop) without regression.
- Milestone C closure status (February 13, 2026):
   - ✅ Manual runtime validation completed on packaged Windows runtime artifact (`./install-windows/bin/Bloom.exe`) produced by `./scripts/build.ps1`.
   - ✅ Direct libmpv control path confirmed in logs.
   - ✅ Overlay/control/window-transition/manual parity checks completed.

### Milestone D — Breakdown (completed)
#### D1. Track-selection model unification (server + mpv)
- ✅ Define authoritative track identity model (Jellyfin stream index vs mpv runtime track ID) and enforce one mapping contract across controller, backend, and QML.
- ✅ Add explicit mapping layer and diagnostics for audio/subtitle selection, including `none/auto/default` handling.
- ✅ Ensure startup behavior preserves explicit user selection over server defaults when requested.

#### D2. Runtime track-switch parity
- ✅ Ensure in-playback audio/subtitle changes apply reliably in direct `win-libmpv` path.
- ✅ Ensure `None` subtitle selection consistently disables subtitle rendering.
- ✅ Validate behavior parity between direct and external backends for track selection semantics.

#### D3. Validation and regression coverage
- ✅ Add focused automated regressions around track-selection command mapping and lifecycle timing.
- ✅ Add manual runtime validation checklist for startup + in-playback track switching on representative media.
- ✅ Close known track-selection gaps before Milestone E runtime/deprecation work.

### Milestone E — Breakdown (kickoff + planned)
#### E0. Linux runtime validation closeout (moved from Milestone B)
- ⏳ Validate embedded playback on Linux target environment.
- ⏳ Validate resize/reposition reliability under real usage.
- ⏳ Validate no CPU readback path is used.
- ⏳ Validate Linux runtime parity (controls/reporting/stability) on representative compositor/hardware matrix.
- ⏳ Bundle `libmpv` with Linux build/package artifacts and verify runtime library resolution on target systems.

- ⬜ Add config toggle to keep `ExternalMpvBackend` available as an explicit backend selection.
- ⬜ Enable embedded path by default only when parity criteria are met.
- ⬜ Mark legacy external path deprecated (not removed).

## 1) Scope and locked decisions

### In scope
- Replace external mpv process + JSON IPC as the primary playback path.
- Introduce backend abstraction with separate platform implementations.
- Preserve mpv config/profile flexibility (config-dir, mpv.conf/input.conf, script/script-opts support, custom options).
- Keep business logic (reporting/session/next-up) in controller/service layer.

### Locked decisions
- Windows primary implementation: **HWND embedding strategy** (Plezy-style).
- Windows contingency notes: render-API alternatives may be documented, not primary.
- Linux primary implementation: **libmpv render API + OpenGL** into Qt Quick item.
- Keep `ExternalMpvBackend` available only as an explicit backend selection (`BLOOM_PLAYER_BACKEND=external-mpv-ipc`) when needed.
- No requirement to preserve current Lua script UX; architecture must support future native controls/trickplay.

---

## 2) Current architecture summary

### Core integration points
- Playback architecture doc: `docs/playback.md`
- Process/IPC manager: `src/player/PlayerProcessManager.*`
- Playback orchestration/state/reporting: `src/player/PlayerController.*`
- Fullscreen/UI entry points: `src/ui/Main.qml`
- mpv settings/profile resolution: `src/utils/ConfigManager.*`
- mpv profile UI: `src/ui/MpvProfileEditor.qml`
- Service wiring/bootstrap: `src/core/ApplicationInitializer.cpp`

### Current flow
QML action → `PlayerController` builds playback context/options → `PlayerProcessManager` launches external `mpv` with IPC socket → IPC events mapped back into `PlayerController` state/signals → Jellyfin reporting via playback service.

---

## 3) Target architecture

## 3.1 Backend abstraction
Create `IPlayerBackend` (QObject-based interface) under `src/player/backend/`:

- Lifecycle: `initialize()`, `shutdown()`
- Media: `load(...)`, `stop()`
- Controls: `pause()`, `resume()`, `seekAbsolute()`, `seekRelative()`
- Track control: set/select audio/subtitle, observe active tracks
- Observed properties/events: position, duration, paused, cache/buffering, end-file, errors
- Video target integration hooks (for embedded rendering/embedding)

Implementations:
- `ExternalMpvBackend` (adapter over current `PlayerProcessManager` behavior)
- `LinuxMpvBackend`
- `WindowsMpvBackend`

`PlayerController` depends on `IPlayerBackend` only.

## 3.2 Windows strategy (HWND embedding)

Primary goals: seamless fullscreen-first UX, HDR support, robust transitions.

Design:
- Host/container HWND tracked to Qt window lifecycle.
- mpv video HWND parented into container HWND.
- Native event filter handles:
  - `WM_SIZE`
  - `WM_MOVE`
  - `WM_WINDOWPOSCHANGED`
  - minimize/maximize/fullscreen transitions
- Geometry sync and z-order updates batched/debounced to avoid flicker/composition glitches.
- Transition mitigation: deferred restore/timer path for known transient resize/move flicker windows.
- Detailed playback/window diagnostics via `QLoggingCategory` (`bloom.playback`).

HDR:
- Ensure HDR-capable mpv options remain available and are applied in embedded path.
- Add code-level diagnostics/logging to verify HDR-relevant configuration and output path state.

## 3.3 Linux strategy (libmpv render API + OpenGL)

Design:
- `LinuxMpvBackend` owns `mpv_handle` + `mpv_render_context`.
- `MpvVideoItem` (Qt Quick item) binds to backend render callbacks.
- Use `MPV_RENDER_API_TYPE_OPENGL` for GPU path.
- No CPU readback, no Qt Multimedia video path.
- Modular backend boundary to support future Wayland/HDR iterations without touching `PlayerController`/QML orchestration.

---

## 4) QML integration and UX behavior

### VideoSurface component
- Add a dedicated `VideoSurface` (QML + C++ item as needed).
- Keep overlays in QML above video layer.
- Ensure keyboard/gamepad navigation and focus behavior remain consistent with existing conventions.

### Credits-shrink behavior hook
- Add backend/controller API to update video viewport/geometry at runtime.
- Expose minimal controller property/signal for “shrink during credits / next-up” mode.
- Initial implementation can be internal/test hook; UX polish can iterate later.

---

## 5) mpv config/profile preservation strategy

Preserve support for:
- config dir (`--config-dir` semantics)
- `mpv.conf`, `input.conf`
- scripts and script-opts loading capability
- profile/custom options from Bloom UI and config

Define deterministic option precedence (low → high):
1. Backend safe defaults
2. Config-dir/base options
3. Resolved profile options (`series > library > default`)
4. Per-item runtime overrides
5. Explicit user custom extra options
6. Platform safety overrides (minimal, documented)

Notes:
- Keep existing config keys where possible.
- If migration keys are needed, document in `docs/config.md`.

---

## 6) Milestones and exit criteria

## Milestone A — Backend abstraction + external backend support

Deliverables:
- `IPlayerBackend` introduced.
- `ExternalMpvBackend` wraps current process manager behavior.
- `PlayerController` refactored to backend interface.
- Existing settings/profile UI remains functional.

Exit criteria:
- No behavior regressions on external backend path.
- App remains buildable.

## Milestone B — Linux embedded backend

Deliverables:
- `LinuxMpvBackend` + `MpvVideoItem` integrated.
- Embedded video in Qt Quick with overlays above video.
- Credits-shrink internal hook wired.

Exit criteria:
- Embedded playback works on Linux.
- Runtime resize/reposition works.
- No CPU readback path.

## Milestone C — Windows embedded backend

Deliverables:
- `WindowsMpvBackend` with container+video HWND parenting.
- Native event filter and geometry sync.
- Transition flicker mitigation.
- HDR diagnostics/logging path.
- Overlay rendering path for embedded Windows playback, with transparent QML controls layered above video and reusable overlay components shared across platforms.

Exit criteria:
- Seamless fullscreen transitions and stable embedding.
- HDR output functional on target validation setup.
- Overlay experience visually seamless, with controls shown above video without moving or clipping the video viewport.
- Overlay UI/state layer is backend-agnostic and reusable by non-Windows embedded paths.

## Milestone D — Track selection logic and parity hardening

Deliverables:
- Define and document the canonical track identity contract across Jellyfin indices and mpv runtime IDs.
- Ensure startup and runtime track selection behavior is deterministic and user-driven (including subtitle off/none).
- Add targeted automated + manual validation coverage for track selection semantics.

Progress update (Milestone D implementation):
- Canonical contract implemented:
  - `selectedAudioTrack` / `selectedSubtitleTrack` in QML and `PlayerController` are Jellyfin `MediaStream.index` values.
  - Runtime mpv commands use explicit mapped mpv track IDs (`aid`/`sid`, 1-based per media-type order), passed from QML as mapping tables.
  - Subtitle `None` is represented as Jellyfin `-1` and always applied as mpv `sid=no`.
- Startup precedence rules implemented:
  - Selected Jellyfin track values are resolved through the canonical map and applied explicitly as startup `aid`/`sid` when available.
  - URL `AudioStreamIndex` / `SubtitleStreamIndex` pins are treated as request hints/fallback, not authoritative runtime selection.
  - Subtitle `None` (`-1`) always applies startup `sid=no`.
- Direct `win-libmpv` command-shape parity hardened:
  - `WindowsMpvBackend` now handles `set_property` through libmpv property APIs (`mpv_set_property*`) for direct path parity.
  - Fixed direct-path regression where `set_property sid no` returned `invalid parameter`, which prevented reliable subtitle-off behavior.
- Focused regression coverage added:
  - `PlayerControllerAutoplayContextTest` now includes startup mapping, pinned-URL precedence, runtime mapped switching, and subtitle-None assertions.
  - Runtime mapped-switch test now also asserts season preference persistence on in-playback changes.
- Runtime validation update:
  - Manual Windows direct-path validation confirmed subtitle `None` and track switching apply after the direct `set_property` fix.
  - In-playback track controls are now exposed in `EmbeddedPlaybackOverlay.qml` via separate audio/subtitle selectors (including `None`) that call `PlayerController` runtime selection methods.

Dated validation note (February 13, 2026):
- Runtime log review + manual verification on Windows direct `win-libmpv` confirmed:
  - `set_property sid no` now applies successfully (no `invalid parameter` warning).
  - Subtitle `None` disables rendering at startup as expected.
  - In-playback audio/subtitle switching applies and matches selected tracks.

Manual runtime validation checklist (Windows direct `win-libmpv`):
1. Startup with default tracks:
   - Start playback without changing tracks in details view.
   - Expected: startup logs show deterministic mapped `aid`/`sid` selection (or `sid no` when subtitle is `None`).
2. Startup with user-selected non-default tracks:
   - Select non-default audio/subtitle before pressing play.
   - Expected: startup logs show mapped `aid`/`sid` applied; actual tracks match selection.
3. Subtitle None:
   - Select subtitle `None` before start and during playback.
   - Expected: logs show `sid no`; subtitles are disabled reliably.
4. In-playback switching:
   - Switch audio/subtitle during playback.
   - Expected: mapped `aid`/`sid` commands appear and selected track changes without silent audio drop.

Exit criteria:
- Audio/subtitle selections apply correctly at startup and during playback.
- Subtitle `None` reliably disables subtitle rendering.
- Track-selection behavior is consistent across supported backends.

Milestone D closure status (February 13, 2026):
- ✅ Startup track selection determinism validated on Windows direct `win-libmpv`.
- ✅ Runtime audio/subtitle switching validated via playback overlay selectors.
- ✅ Subtitle `None` behavior validated at startup and in playback.
- ✅ Focused regression coverage passing (`PlayerBackendFactoryTest`, `PlayerControllerAutoplayContextTest`) on current build artifacts.

## Milestone E — Soft deprecation (optional)

Deliverables:
- Keep `ExternalMpvBackend` available via explicit backend selection for controlled use.
- Default embedded backend path enabled when parity is met.

Exit criteria:
- Clear backend-selection guidance documented.
- Legacy path marked deprecated (not removed until agreed).

---

## 7) Risk register and mitigations

1. **Windows flicker/composition regressions**
   - Mitigate with event-filter batching, geometry debounce, transition timer restore, aggressive logging.
2. **HDR regressions on Windows**
   - Keep HDR option pipeline explicit; add diagnostics for actual active output path.
3. **Linux compositor/driver variability**
   - Isolate backend implementation and keep rendering contract narrow.
4. **Config/profile drift across backends**
   - Single option resolver and shared precedence contract for all backends.
5. **Track/index mapping regressions**
   - Keep mapping logic centralized and backend-agnostic.
6. **Build/packaging complexity**
   - Add platform-conditional CMake and docs updates in same milestone.

---

## 8) Validation checklist

### Functional
- Play/pause/resume/seek/stop
- Audio/subtitle track selection and persistence
- Next episode flow and completion thresholds
- Jellyfin reporting (start/progress/pause/stop) unchanged

### UX
- QML overlays render above video
- Overlay show/hide does not move or clip the video viewport
- Credits-shrink viewport behavior works
- Keyboard/gamepad navigation unaffected

### Platform
- Linux: embedded render loop stable, resize/reposition reliable
- Windows: WM transition handling stable, no major flicker/black-frame issues
- Windows HDR: diagnostics confirm expected configuration/output mode behavior

### Reliability
- Repeated start/stop cycles stable
- Error handling returns to safe idle state
- Backend selection/active backend visible in logs
- Plezy parity checklist items for relevant Milestone C/D/E changes completed and noted in PR/review summary

---

## 9) Planned file touchpoints (implementation phase)

### New
- `src/player/backend/IPlayerBackend.h`
- `src/player/backend/ExternalMpvBackend.*`
- `src/player/backend/LinuxMpvBackend.*`
- `src/player/backend/WindowsMpvBackend.*`
- `src/player/backend/PlayerBackendFactory.*`
- `src/player/MpvVideoItem.*` (or `src/ui/` equivalent)
- `src/ui/VideoSurface.qml`

### Modified
- `src/player/PlayerController.*`
- `src/core/ApplicationInitializer.cpp`
- `src/ui/Main.qml`
- `src/utils/ConfigManager.*`
- `src/CMakeLists.txt` and relevant top-level CMake/docs/build files

---

## 10) Review notes

- Keep changes incremental and reviewable (milestone-sized).
- Avoid `#ifdef` leakage into UI logic; keep platform branching in backend layer.
- Preserve existing user-facing config semantics unless explicitly migrated and documented.

---

## 11) External reference baseline (Plezy)

- Primary external reference for this refactor: https://github.com/edde746/plezy
- Bloom should treat Plezy as a design/implementation-pattern reference for embedded mpv architecture, especially:
   - direct libmpv command/property/event control loop,
   - Windows embedded window lifecycle and transition handling,
   - async command completion and observed-property forwarding.
- Bloom should not copy Flutter/plugin-specific integration details directly; adapt the same design decisions to Bloom’s Qt/C++ backend seam (`IPlayerBackend`, `PlayerController`, ServiceLocator wiring, and QML surface model).
- For Milestone C and subsequent cleanup, use Plezy as a sanity check when choosing behavior under resize/move/minimize/fullscreen transitions and control-path migration away from IPC delegation.
