# Flutter Track Migration Plan (Worker A)

Last updated: 2026-05-25
Owner: Worker A (Flutter track)
Scope: Flutter-track docs/scaffolding only

## 1) Architecture note (short + concrete)

Flutter should be a **thin UI shell** on Linux desktop/HTPC while all media/business logic stays in a shared native layer named **Bloom Core**.

- **UI layer (Flutter):** routes, widgets, focus graph, keyboard/gamepad mapping, visual theming, and user intent dispatch.
- **Bridge layer (Flutter plugin):** typed FFI/API wrapper exposing Bloom Core commands + event streams.
- **Core layer (native C++):** auth/session, Jellyfin API access, browse/search models, playback and mpv orchestration, playback reporting, config persistence, track-selection mapping persistence.

Principle: no backend duplication in Dart; Dart only sends commands and renders state snapshots/streams.

## 2) File-level implementation plan, ownership, milestones

### Proposed files (Flutter track ownership)

- `docs/migration/flutter-track.md` (this file)
  - migration plan, milestones, status, risks, estimates.
- `docs/migration/shared-core-contract.md` (new shared contract proposal)
  - Bloom Core command/event/data contract draft for cross-track alignment.
- `flutter/` (new additive scaffolding; not implemented yet)
  - `flutter/bloom_flutter_app/` main desktop app shell.
  - `flutter/plugins/bloom_core_bridge/` federated plugin exposing FFI boundary.
  - `flutter/packages/bloom_core_contract/` generated or hand-authored Dart models mirroring shared contract.

### Milestone breakdown

- **M0: Contract alignment (1 week)**
  - Freeze v0 Bloom Core contract for auth/session/library/playback/reporting/settings.
  - Define compatibility/versioning policy and error envelope.
  - Exit criteria: shared doc sign-off by Flutter + CEF + native owners.

- **M1: Bridge bootstrap + smoke integration (1–1.5 weeks)**
  - Create Flutter plugin with Linux desktop implementation.
  - Wire command channel/FFI + event stream subscription.
  - Implement minimal no-op mock backend for local UI iteration.
  - Exit criteria: demo app receives fake session + library + player events.

- **M2: Vertical slice on real core (2 weeks)**
  - Real login/session restore, library listing, start/pause/stop playback.
  - Reporting lifecycle events flow through core.
  - Exit criteria: end-to-end flow works against a Jellyfin server.

- **M3: HTPC interaction hardening (1.5–2 weeks)**
  - 10-foot focus graph, gamepad/keyboard nav, pointer-mode suppression.
  - Playback overlay controls + OSD state mirrored from core.
  - Exit criteria: couch-remote navigation without mouse.

- **M4: feature ramp (4–8 weeks parallelized)**
  - Search, details pages, episodes/next up, track selection UX, settings pages.
  - Robust error/reconnect, cache strategy, telemetry hooks.

## 3) Minimal vertical slice status

### Target slice
Login/session restore -> library listing -> playback start/pause/stop

### Current status (migration kickoff)

- **Runs now:**
  - Existing native Bloom app path already supports auth/library/playback/reporting in C++/QML stack (reference behavior).
- **Flutter track currently stubbed/planned:**
  - Flutter shell and plugin not yet created in-repo.
  - Contract defined at doc level (this phase).
  - Vertical slice implementation starts at M1/M2.

### Slice acceptance criteria (for M2)

- Login with server URL + token/credential exchange through Bloom Core API.
- Session restore on app relaunch using persisted session state from core/config.
- Library list renders first page of items with poster/title metadata.
- Selecting an item sends `playback.start` command; pause/stop commands operate reliably.
- Playback lifecycle reporting (`start/progress/pause/stop`) emitted by core and observable in Flutter logs/diagnostics.

## 4) Linux desktop 10-foot focus/input strategy

Flutter must mirror Bloom's keyboard/gamepad-first UX expectations:

- **Primary nav mode:** directional focus traversal, enter/back/media keys.
- **Input mode manager (Flutter side):**
  - Track latest input source (`keyboard_gamepad` vs `pointer`).
  - Hide cursor and highlight focus rails in keyboard/gamepad mode.
  - Re-enable cursor affordances in pointer mode.
- **Focus restoration:**
  - Preserve per-route focused node ID.
  - Restore after async list refresh/navigation pop using post-frame callbacks.
- **Gamepad mapping:**
  - D-pad -> directional focus.
  - A/B (or South/East) -> select/back.
  - Shoulder/trigger optional for page jump/seek (later milestone).
- **Linux specifics:**
  - Validate on Wayland and X11.
  - Ensure global key handling does not depend on compositor-specific behavior.

## 5) Plugin/FFI boundary proposal (concrete)

Transport can be either direct Dart:FFI or Pigeon/platform channel facade over native shim. Preferred near-term: platform channel API with typed payloads; evolve to direct FFI where latency-sensitive.

### Control API surface (command-oriented)

- `core.initialize(clientInfo, paths)`
- `auth.login(serverUrl, username, password | token)`
- `auth.restoreSession()`
- `auth.logout()`
- `library.getHomeSections()`
- `library.getItems(sectionId, page)`
- `library.search(query, filters)`
- `playback.prepare(itemId, startPositionTicks?)`
- `playback.play()`
- `playback.pause()`
- `playback.stop(reason?)`
- `playback.seek(positionTicks)`
- `playback.selectTracks(audioTrackId?, subtitleTrackId?)`
- `settings.get(key)` / `settings.set(key, value)`

### Event streams (push from core)

- `session.events`: logged_in, restored, expired, logged_out
- `library.events`: cache_updated, refresh_started, refresh_done
- `playback.state`: idle, buffering, playing, paused, stopped, ended, error
- `playback.position`: periodic ticks/progress snapshots
- `playback.tracks`: available tracks + selected track changes
- `reporting.events`: start_sent, progress_sent, pause_sent, stop_sent, failed_retrying
- `core.health`: backend_connected, backend_restarting, fatal_error

### Data contracts (typed DTOs)

- `SessionInfo { userId, accessToken, serverUrl, deviceId, expiresAt? }`
- `MediaItemSummary { id, type, title, posterUrl, runtimeTicks?, year?, rating? }`
- `PlaybackStateSnapshot { itemId?, state, positionTicks, durationTicks?, speed, isMuted, volume }`
- `TrackInfo { id, kind(audio|subtitle), label, language, codec, channels?, isDefault, isForced }`
- `CoreError { code, message, isRetriable, details? }`

### Track mapping + persistence hooks

- Core owns precedence logic and persistence store.
- Flutter sends user intents (`preferredAudioLanguage`, `subtitleMode`, explicit track override).
- Core emits resolved selection + reason (`user_override`, `language_match`, `default_track`, `none`).

## 6) Risks and mitigations

- **Risk: contract drift across Flutter/CEF/native teams**
  - Mitigation: versioned shared contract doc + changelog + weekly compatibility review.
- **Risk: event ordering race conditions (playback + reporting)**
  - Mitigation: monotonic sequence numbers on events and idempotent handling in UI.
- **Risk: Linux input fragmentation (Wayland/X11/gamepad stacks)**
  - Mitigation: explicit input adapter layer + test matrix by compositor/session type.
- **Risk: duplicated logic sneaks into Dart**
  - Mitigation: code review checklist requiring backend decisions to stay in core.
- **Risk: bridge latency or serialization overhead**
  - Mitigation: benchmark high-frequency streams (position updates), move hot paths to FFI.

## 7) Time estimates

- **Feasibility prototype:** 2-3 weeks
  - Includes M0 + M1 + partial M2 demo with one happy-path playback flow.
- **First usable HTPC build:** 6-8 weeks
  - Includes stable M2 + M3 input/focus hardening and basic browse/play loop.
- **Feature parity with current client:** 14-20 weeks
  - Depends on advanced playback controls, search/discovery depth, settings breadth, and QA across Linux configurations.

## Key conclusions

- Bloom Core contract-first approach is the critical enabler for parallel Flutter + CEF delivery.
- Flutter should remain presentation-focused; all domain/media logic stays native.
- A realistic first usable Linux HTPC milestone is ~6-8 weeks after contract lock.
