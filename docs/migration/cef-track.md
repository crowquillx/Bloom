# CEF Track Migration Plan (Worker B)

_Last updated: 2026-05-25 (UTC)_

## Scope and ownership

This document is **CEF-track only** scaffolding for the migration kickoff. It is additive and interface-first, and assumes another worker is concurrently drafting Flutter-track specifics.

## 1) Architecture note (short + concrete)

### Target shape

Bloom migration uses a **shared native backend layer** (`Bloom Core`) implemented in C++ and reused by all frontends (current QML, CEF shell, Flutter shell). CEF is a UI host, not a second backend.

- **C++ owns**: auth/session, Jellyfin API access, models/viewmodels, playback state machine, reporting lifecycle, settings/config, track selection persistence hooks.
- **JS owns**: rendering, navigation state, focus visuals, command dispatch, view composition.
- **Command/event bridge** is typed and versioned so frontend code cannot bypass backend policy or duplicate Jellyfin logic.

### Process boundaries

- CEF Browser Process: window lifecycle, app-level menus/input routing, bridge registration.
- CEF Renderer Process: JS app execution and typed bridge client.
- Bloom Core host (C++): service locator + adapters around existing network/player/config services.

### Lifecycle model

1. App boot: initialize `ServiceLocator` + `BloomCoreFacade`.
2. Frontend handshake: JS requests bridge schema version and capabilities.
3. Session restore: C++ attempts persisted token/session restore; emits session status.
4. Library browse: JS requests library query; C++ returns paged results.
5. Playback: JS issues `play/start`, `play/pause`, `play/stop`; C++ controls player + emits state updates.
6. Reporting: C++ emits/handles start/progress/pause/stop lifecycle independent of UI details.

## 2) File-level implementation plan (ownership + milestones)

> Milestones are organized so CEF can deliver value quickly while preserving one shared backend contract.

### Milestone 0 — Contract freeze (Week 1)

**Owner: Shared Core + CEF (joint review)**

Planned files:
- `docs/migration/shared-core-contract.md` (shared contract spec; owned by cross-track group)
- `docs/migration/cef-track.md` (this file; CEF responsibilities and sequencing)

Deliverables:
- Finalize `Bloom Core` command/event surface and naming.
- Freeze MVP command set for vertical slice.
- Record versioning policy (`bridgeVersion`, backward compatibility window).

### Milestone 1 — Native bridge scaffolding (Week 1-2)

**Owner: CEF track**

Planned C++ scaffolding:
- `src/platform/cef/CefAppBootstrap.*`
- `src/platform/cef/CefBridgeRouter.*`
- `src/platform/cef/CefMessageCodec.*` (JSON envelope + typed payload mapping)
- `src/core/BloomCoreFacade.*` (thin orchestration over existing services)

Deliverables:
- Browser/renderer bridge registration.
- Envelope format:
  - Command: `{id, domain, action, payload, schemaVersion}`
  - Event: `{event, domain, payload, ts, schemaVersion}`
- Unified error shape `{code, message, retriable, details}`.

### Milestone 2 — Vertical slice MVP (Week 2-3)

**Owner: CEF track with Shared Core support**

Commands implemented:
- `session.restore`
- `auth.login` / `auth.logout`
- `library.list`
- `playback.start`
- `playback.pause`
- `playback.stop`

Events implemented:
- `session.stateChanged`
- `library.page`
- `playback.stateChanged`
- `reporting.lifecycle` (`start/progress/pause/stop`)

### Milestone 3 — HTPC interaction hardening (Week 3-5)

**Owner: CEF track**

Planned files:
- `src/platform/cef/CefInputModeAdapter.*`
- `src/platform/cef/CefFocusNavigator.*`
- `src/platform/cef/CefWindowOverlayCoordinator.*`

Deliverables:
- Keyboard/gamepad-first focus traversal model.
- Pointer-vs-keyboard mode detection parity with existing `InputModeManager` behavior.
- Overlay and window geometry synchronization policy for playback controls.

### Milestone 4 — Extended parity work (Week 5+)

**Owner: Shared Core + CEF + Flutter**

Deliverables:
- Search/filter/sort parity.
- Track selection mapping + persistence hooks.
- Settings/config full bridge coverage.
- Offline/cache semantics and richer error UX.

## 3) Minimal vertical slice status

## What runs now (kickoff baseline)

- No committed CEF runtime vertical slice in repo yet.
- Existing native services already exist in C++ (network/playback/config), which are the intended backend source for CEF via `Bloom Core` façade.

## What is stubbed for kickoff

- Stub bridge transport (`invoke(command)` / `subscribe(event)`) with static responses.
- Stub flow target:
  1. `session.restore` returns authenticated or signed-out state.
  2. `library.list` returns first page of media cards.
  3. `playback.start/pause/stop` mutates mock playback state and emits events.
- Reporting lifecycle events are emitted from one C++ playback adapter path (not JS timers/business logic).

## Exit criteria for "slice complete"

- Login/session restore displayed in CEF UI.
- Library list navigable by keyboard/gamepad.
- Start/pause/stop commands round-trip through typed bridge.
- State and reporting events observable in both C++ logs and JS dev console.

## 4) Risks and mitigations

1. **Risk: Backend logic leaks into JS over time.**
   - Mitigation: enforce command whitelist and prohibit direct Jellyfin HTTP in frontend packages; code review checklist item.

2. **Risk: Bridge contract churn blocks parallel tracks.**
   - Mitigation: schema versioning + additive fields only during MVP window; weekly contract review cadence.

3. **Risk: Focus/navigation regressions for HTPC remote workflows.**
   - Mitigation: explicit focus graph model, deterministic initial focus per route, CI smoke tests for D-pad navigation flows.

4. **Risk: Overlay/window layering issues during embedded playback.**
   - Mitigation: keep overlay coordination in native host; define geometry sync events for resize/maximize/fullscreen/minimize/restore transitions.

5. **Risk: Reporting lifecycle drift (start/progress/pause/stop) across frontends.**
   - Mitigation: lifecycle emitted by shared C++ playback/reporting service only, with frontend as passive observer.

## 5) Time estimates

Assumptions: 1-2 engineers CEF-focused, shared-core support available, no major mpv/CEF embedding blocker.

- **Feasibility prototype (typed bridge + vertical slice):** ~2-3 weeks.
- **First usable HTPC CEF build (keyboard/gamepad usable, basic library + playback controls):** ~5-7 weeks.
- **Feature parity with current QML client (core browse/playback/reporting/settings/search/track prefs):** ~12-16 weeks.

## Keyboard/remote focus model

CEF UI must remain fully keyboard/gamepad navigable:

- Frontend defines route-level **focus scopes** and directional neighbors (up/down/left/right).
- Native input adapter detects last input mode (pointer vs keyboard/gamepad) and emits `input.modeChanged`.
- Cursor is hidden in keyboard/gamepad mode and restored in pointer mode.
- On async data reload, frontend restores last stable focus target (equivalent behavior to `Qt.callLater` focus restoration pattern).

## Windowing and overlay implications

- Playback UI overlays (transport controls, OSD, progress) should be hosted in a layer that remains visible above embedded video surfaces.
- Geometry sync API between native window host and frontend overlay should include:
  - `window.boundsChanged`
  - `window.stateChanged` (normal/maximized/fullscreen/minimized/restored)
- Transition handling must avoid visible video jump/clip when toggling overlay visibility and window states.

## Concrete C++ <-> JS binding boundary

## Bridge API design

Single entrypoint in JS:

- `bridge.invoke<TResponse>(command: BridgeCommand): Promise<BridgeResult<TResponse>>`
- `bridge.subscribe<TEvent>(eventName: string, handler: (evt: TEventEnvelope<TEvent>) => void): Unsubscribe`

C++ dispatcher routes by `domain/action` to `BloomCoreFacade` methods.

### Command domains (MVP)

- `session`: restore, status
- `auth`: login, logout
- `library`: list, search (search can be stubbed in MVP)
- `playback`: start, pause, stop, state
- `reporting`: lifecycle query/debug (optional for tooling)
- `settings`: get/set (partial MVP)

### Typed payload principles

- Every command/event has explicit schema with required/optional fields.
- Unknown fields ignored (forward compatibility).
- Unknown command/action returns deterministic `UNSUPPORTED_COMMAND`.
- Correlation ID (`id`) echoed in success/error responses.

### Lifecycle hooks required by shared backend mandate

- `onPlaybackStarted(itemId, positionTicks)` -> reporting start.
- `onPlaybackProgress(itemId, positionTicks)` -> reporting progress.
- `onPlaybackPaused(itemId, positionTicks)` -> reporting pause.
- `onPlaybackStopped(itemId, positionTicks)` -> reporting stop + watched-threshold resolution.
- Track selection hooks:
  - `getPreferredTracks(profile, itemId)`
  - `persistTrackSelection(itemId, audioTrackId, subtitleTrackId)`

These hooks are implemented in C++ and surfaced to UI as state/events only.

## Open decisions to resolve in shared contract review

- Final event naming convention (`dot.case` vs `snake_case`) — recommend `dot.case` for domain grouping.
- Transport encoding choice (JSON-only MVP vs optional binary fast-path later).
- Capability negotiation format for incremental rollout across CEF/Flutter.
