# Player Backend Refactor Plan (External mpv JSON IPC → Embedded libmpv)

## Implementation status (Milestone A + Milestone B kickoff)

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
   - unknown backend names fall back to `external-mpv-ipc` with warning log.
- Added regression tests for backend factory behavior in `tests/PlayerBackendFactoryTest.cpp`.
- Added integration-level assertion in `tests/VisualRegressionTest.cpp` that `ApplicationInitializer` registers `IPlayerBackend` in `ServiceLocator`.
- Updated platform default selection behavior:
   - Linux now defaults to `linux-libmpv-opengl` when runtime requirements are met.
   - Linux auto-falls back to `external-mpv-ipc` when embedded runtime requirements are not met.
   - Non-Linux platforms continue to default to `external-mpv-ipc`.
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
- Added Linux backend selection path in `PlayerBackendFactory` (`linux-libmpv-opengl`) with OpenGL runtime guard + fallback.
- Added Qt Quick surface primitives:
   - `MpvVideoItem` (QML-exposed C++ item)
   - `VideoSurface.qml`
- Added minimal `PlayerController` API for embedded target attach/detach, viewport forwarding, and internal/manual shrink mode property.
- Added Linux-conditional build wiring for new backend sources and optional `libmpv` discovery/linking.

Still pending after Milestone B closeout (moved to start of Milestone D):
- Final Linux target runtime validation for `mpv_render_context` reliability on representative hardware/compositors.
- Linux runtime parity validation (controls, reporting, stability, no CPU readback).

Validation sequencing note (current):
- Linux on-device/runtime validation is intentionally deferred to the beginning of Milestone D (D0) when Linux test infrastructure/hardware is available.
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

Selection/fallback behavior:
- ✅ Default backend is platform-aware (`linux-libmpv-opengl` on Linux when supported; `external-mpv-ipc` otherwise).
- ✅ `BLOOM_PLAYER_BACKEND` env override supported.
- ✅ Unknown backend names fall back safely to external backend with warning log.
- ⏳ Config-file backend selector key (deferred to later milestone).

Validation coverage:
- ✅ Build passes via project build script.
- ✅ `PlayerBackendFactoryTest` validates default selection, explicit selection, initial stopped state, and unknown-name fallback.
- ✅ `VisualRegressionTest` asserts backend service registration in startup wiring.
- ✅ `PlayerBackendFactoryTest` coverage now includes Linux backend-name selection behavior.
- ⏳ Linux embedded backend runtime validation pending on Linux target environment.

Commands used during current validation:
- `./scripts/build.ps1`
- `set BLOOM_PLAYER_BACKEND=external-mpv-ipc` (optional env selection smoke)
- `build-windows/tests/Release/PlayerBackendFactoryTest.exe`

## Milestone breakdown & status board

Status legend:
- ✅ done
- 🟨 in progress
- ⬜ not started
- ⚠️ blocked / decision needed

Overall milestone status:
- **Milestone A — Backend abstraction + external fallback:** ✅ done
- **Milestone B — Embedded integration + parity hardening (non-Linux runtime validation):** ✅ done
- **Milestone C — Windows embedded backend:** 🟨 in progress
- **Milestone D — Linux runtime validation kickoff + soft deprecation/default switch:** 🟨 partially landed (Linux default switch completed; Linux runtime validation + deprecation policy pending)

### Milestone A — Breakdown (completed)
- ✅ Backend interface (`IPlayerBackend`) created and wired.
- ✅ External adapter (`ExternalMpvBackend`) implemented.
- ✅ Factory (`PlayerBackendFactory`) implemented.
- ✅ `PlayerController` refactored to backend interface.
- ✅ Startup wiring updated (`ApplicationInitializer` + `ServiceLocator`).
- ✅ Logging + fallback behavior added.
- ✅ Regression tests added and passing.

### Milestone B — Breakdown (closed)

#### B1. Backend and rendering primitives
- ✅ Create `LinuxMpvBackend` with `mpv_handle` + `mpv_render_context` ownership. (implementation complete; Linux runtime validation moved to D0)
- ✅ Create `MpvVideoItem` (or equivalent C++ video item) for Qt Quick render integration.
- ✅ Define minimal render callback contract between backend and item.
- ✅ Add safe startup/shutdown lifecycle for libmpv context and render context. (`mpv_handle` + `mpv_render_context` startup/shutdown hooks added; target-environment runtime validation moved to D0)

#### B2. Controller/factory wiring
- ✅ Extend `PlayerBackendFactory` to instantiate Linux backend by name.
- ✅ Platform-aware default selection implemented (Linux embedded default with external fallback; non-Linux external default).
- ✅ Ensure `PlayerController` behavior/signals remain unchanged across backend swap. (event/property parity improvements landed; runtime verification on Linux targets moved to D0)

#### B3. QML surface integration
- ✅ Add `VideoSurface.qml` and integrate it into main playback UI path.
- ✅ Ensure overlays remain above video surface.
- ✅ Preserve focus + keyboard/gamepad navigation behavior in embedded path integration scope. (Linux on-device runtime verification moved to D0)

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
- ✅ Validate regressions do not appear on external fallback path.
- ✅ Add focused controller parity regressions for next-up/autoplay context handling.
- ➡️ Linux target runtime validation items moved to Milestone D kickoff (D0).

### Milestone C — Breakdown (planned)
- ⬜ Implement `WindowsLibmpvHwndBackend` (container HWND + parenting).
- ⬜ Implement native event filter + geometry sync/debounce.
- ⬜ Implement transition flicker mitigation path.
- ⬜ Add HDR diagnostics and validation path.

### Milestone D — Breakdown (kickoff + planned)
#### D0. Linux runtime validation closeout (moved from Milestone B)
- ⏳ Validate embedded playback on Linux target environment.
- ⏳ Validate resize/reposition reliability under real usage.
- ⏳ Validate no CPU readback path is used.
- ⏳ Validate Linux runtime parity (controls/reporting/stability) on representative compositor/hardware matrix.

- ⬜ Add config rollback toggle to keep `ExternalMpvBackend` available.
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
- Windows contingency notes: render-API fallback may be documented, not primary.
- Linux primary implementation: **libmpv render API + OpenGL** into Qt Quick item.
- Keep `ExternalMpvBackend` as rollback path, config-gated, **default OFF**.
- Keep `ExternalMpvBackend` as rollback path and explicit env override on all platforms.
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
- `WindowsLibmpvHwndBackend`

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

## Milestone A — Backend abstraction + external fallback

Deliverables:
- `IPlayerBackend` introduced.
- `ExternalMpvBackend` wraps current process manager behavior.
- `PlayerController` refactored to backend interface.
- Existing settings/profile UI remains functional.

Exit criteria:
- No behavior regressions on fallback path.
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
- `WindowsLibmpvHwndBackend` with container+video HWND parenting.
- Native event filter and geometry sync.
- Transition flicker mitigation.
- HDR diagnostics/logging path.

Exit criteria:
- Seamless fullscreen transitions and stable embedding.
- HDR output functional on target validation setup.
- Overlay experience visually seamless.

## Milestone D — Soft deprecation (optional)

Deliverables:
- Keep `ExternalMpvBackend` behind config rollback toggle.
- Default embedded backend path enabled when parity is met.

Exit criteria:
- Clear rollback/disable switch documented.
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

---

## 9) Planned file touchpoints (implementation phase)

### New
- `src/player/backend/IPlayerBackend.h`
- `src/player/backend/ExternalMpvBackend.*`
- `src/player/backend/LinuxMpvBackend.*`
- `src/player/backend/WindowsLibmpvHwndBackend.*`
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
