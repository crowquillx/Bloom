# Shared Core Contract Proposal (Kickoff Draft)

Last updated: 2026-05-25
Contributors: Worker A (Flutter track)
Status: Draft v0 for cross-track review

## Purpose

Define a single Bloom Core interface consumed by multiple frontends (Flutter, CEF, existing QML migration layers) so auth, library, playback, reporting, track persistence, and settings behavior remain consistent.

## v0 Scope (must-have)

- Auth/session: login, restore, logout, expiration signaling.
- Library: browse sections, list items, basic search.
- Playback: prepare/play/pause/stop/seek + state snapshots.
- Reporting lifecycle: start/progress/pause/stop lifecycle visibility.
- Track selection mapping + persistence hooks.
- Config/settings get/set with typed values.

## Contract shape

Command-response APIs + asynchronous event streams.

- Commands return `Result<T, CoreError>`.
- Events are emitted with `eventId`, `timestampUtc`, and `sequence`.
- State snapshots are authoritative; UI state is derived, not source-of-truth.

## Versioning + compatibility

- Semantic contract versioning: `major.minor.patch`.
- Additive fields/events: minor bump.
- Breaking rename/removal/semantic changes: major bump.
- Bridge must expose negotiated contract version at startup.

## Open decisions for joint review

1. Transport default for desktop: Platform channels first vs direct FFI first.
2. DTO source-of-truth: IDL/proto/schema generation vs hand-maintained typed models.
3. Position update frequency and backpressure policy for UI streams.
4. Unified error code taxonomy across network/playback/config domains.

---

## Worker B (CEF) append — 2026-05-25

### Proposed CEF alignment notes (MVP-first)

- Keep one shared `Bloom Core` contract for auth/session, library browse/search, playback control/state, reporting lifecycle, track mapping+persistence hooks, and settings.
- CEF frontend must consume typed command/event bridge only; no duplication of backend business logic in JS.
- Prefer command domains: `session`, `auth`, `library`, `playback`, `reporting`, `settings`.

### Bridge envelope proposal

```json
// command
{ "id": "uuid", "domain": "playback", "action": "start", "payload": {}, "schemaVersion": 1 }

// success response
{ "id": "uuid", "ok": true, "payload": {}, "schemaVersion": 1 }

// error response
{ "id": "uuid", "ok": false, "error": { "code": "...", "message": "...", "retriable": false }, "schemaVersion": 1 }

// event
{ "event": "playback.stateChanged", "domain": "playback", "payload": {}, "ts": "2026-05-25T00:00:00Z", "schemaVersion": 1 }
```

### Shared guardrails

- No direct Jellyfin HTTP from JS/Dart frontends.
- Reporting lifecycle (start/progress/pause/stop) must be emitted by shared C++ playback/reporting path.
- Track preferences are resolved/persisted in shared backend and surfaced as state/events.

## Worker A (Flutter) proposal append

### Shared contract constraints
- Bloom Core remains source-of-truth for auth/session, browse/search, playback state, reporting lifecycle, track preference resolution, and settings persistence.
- Flutter consumes command APIs + event streams and must not duplicate backend business logic.

### Flutter-oriented command/event/data additions

```text
Commands
- core.initialize({ clientName, clientVersion, platform, cachePath, configPath })
- auth.restoreSession()
- playback.prepare({ itemId, startPositionTicks? })
- playback.seek({ positionTicks })

Event streams
- session.events: logged_in | restored | expired | logged_out
- playback.position: periodic position ticks snapshots
- playback.tracks: options + selected track changes
- reporting.events: start_sent | progress_sent | pause_sent | stop_sent | failed_retrying
```

### Event ordering + reliability
- Every async event includes `{ sequence, ts, correlationId? }` to prevent UI race conditions.
- Commands are idempotent where practical (`pause` when already paused is success no-op).
- For high-rate telemetry (`playback.position`), support throttling/backpressure hints in bridge settings.
