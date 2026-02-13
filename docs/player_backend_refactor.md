# Player Backend Refactor Plan (External mpv JSON IPC → Embedded libmpv)

## Implementation status (Milestone A)

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

Not yet implemented in Milestone A:
- Config-file backend selector key (currently env-only override + default external).
- Embedded Linux/Windows backends (Milestones B/C).

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
- ✅ Default backend remains `external-mpv-ipc`.
- ✅ `BLOOM_PLAYER_BACKEND` env override supported.
- ✅ Unknown backend names fall back safely to external backend with warning log.
- ⏳ Config-file backend selector key (deferred to later milestone).

Validation coverage:
- ✅ Build passes via project build script.
- ✅ `PlayerBackendFactoryTest` validates default selection, explicit selection, initial stopped state, and unknown-name fallback.
- ✅ `VisualRegressionTest` asserts backend service registration in startup wiring.

Commands used during Milestone A validation:
- `./scripts/build.ps1`
- `set BLOOM_PLAYER_BACKEND=external-mpv-ipc` (optional env selection smoke)
- `build-windows/tests/Release/PlayerBackendFactoryTest.exe -txt`

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
- `LinuxLibmpvOpenGLBackend`
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
- `LinuxLibmpvOpenGLBackend` owns `mpv_handle` + `mpv_render_context`.
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
- `LinuxLibmpvOpenGLBackend` + `MpvVideoItem` integrated.
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
- `src/player/backend/LinuxLibmpvOpenGLBackend.*`
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
