# v0.8.0 manual validation ledger

This ledger is the release-graduation record for Bloom epic [#73](https://github.com/crowquillx/Bloom/issues/73) / issue [#81](https://github.com/crowquillx/Bloom/issues/81), after PRs #111–#113. It covers the provider-neutral client for Jellyfin and Silo servers while preserving the existing Jellyfin path.

The release label is **experimental native Silo support**. This page records evidence, not expectations: `PENDING` means Main must run and record the check; `NOT RUN` means no check was performed; `UNAVAILABLE` means the required live host, platform runtime, or physical device is not available. A row must not be changed to `PASS` from source inspection or a successful HTTP status alone. Current configuration writes keep credentials and tokens only in `CredentialStore`; `app.json` contains provider-neutral connection metadata, except that legacy `settings.jellyfin` rollback material may remain until a secure-store copy is written and verified.

## Reproducible setup

Use the exact Silo source revision and immutable image documented in [provider compatibility](provider-compatibility.md): `8044eb84dd0cfa512ce8f2448cfd51cb7899a4c6`. For live checks, use a disposable fixture account and media library. Keep the native listener on `8090` and the Jellyfin-compatibility listener on `8096`; never infer native behavior from the compatibility listener. Set `BLOOM_CONTRACT_USERNAME` and `BLOOM_CONTRACT_PASSWORD` in the environment (and `BLOOM_CONTRACT_PROFILE_PIN` only for a PIN profile); never include their values in a command or evidence artifact.

From an existing local checkout, pin and start the disposable Silo fixture as follows (the compose file supplies PostgreSQL/Redis and the two listeners):

```fish
git -C /path/to/silo checkout 8044eb84dd0cfa512ce8f2448cfd51cb7899a4c6
cd /path/to/silo
set -x SILO_IMAGE ghcr.io/silo-server/silo-server@sha256:944ee9821de1d6a61876c9b7b06daa358118163d1e5f9b3aa9f5437856fd06e9
mkdir -p .contract-data
docker compose up -d
curl --fail --silent http://127.0.0.1:8090/api/v1/health | jq
```

The stable Linux policy is unchanged: Wayland defaults to `external-mpv-ipc`; embedded libmpv is validation-only and unsupported embedded conditions must fall back to `external-mpv-ipc`. The offscreen smoke below verifies startup/version reporting, not embedded playback or hardware acceleration.

The contract runner writes JSON evidence without credentials. Read-only native health:

```fish
python3 tests/contracts/run_live_contracts.py \
  --deployment silo-8044eb8-native \
  --base-url http://127.0.0.1:8090 \
  --output .contract-data/silo-native-health-report.json
```

Credentialed native lifecycle (only with the disposable fixture):

```fish
python3 tests/contracts/run_live_contracts.py \
  --deployment silo-8044eb8-native \
  --base-url http://127.0.0.1:8090 \
  --allow-mutations \
  --output .contract-data/silo-native-playback-report.json
```

Compatibility and Jellyfin contract runs:

```fish
python3 tests/contracts/run_live_contracts.py \
  --deployment silo-8044eb8-compat \
  --base-url http://127.0.0.1:8096 \
  --allow-mutations \
  --output .contract-data/silo-compat-report.json

python3 tests/contracts/run_live_contracts.py \
  --deployment jellyfin-supported \
  --base-url http://127.0.0.1:18096 \
  --allow-mutations \
  --output .contract-data/jellyfin-report.json
```

## Evidence ledger

Record the date, operator, exact commit, command output path, and a short observation in the Evidence column. Keep `NOT RUN` and `UNAVAILABLE` rows when their prerequisites cannot be supplied.

| Gate | Status at ledger creation | Exact command or action | Pass criteria | Evidence / operator / date |
|---|---|---|---|---|
| Commit and release build | **PENDING — Main** | `git rev-parse HEAD`; `cat VERSION`; `nix build --print-build-logs` | Record the full commit; `VERSION` is exactly `0.8.0`; `nix build` exits 0 and produces the release package without errors. | — |
| Linux offscreen smoke | **PENDING — Main** | `env QT_QPA_PLATFORM=offscreen nix run . -- --version` | Process exits 0, prints Bloom version `0.8.0`, and emits no Qt platform/plugin error. | — |
| Deterministic test evidence | **PENDING — Main** | `env BLOOM_BUILD_JOBS=2 nix flake check --print-build-logs --option max-jobs 1 --option cores 2` | Command exits 0 and its recorded output shows the deterministic tests and validation derivations completed; record any skipped/unavailable checks rather than treating them as passes. | — |
| Live Jellyfin preservation | **NOT RUN / UNAVAILABLE** | Start the pinned Jellyfin deployment, run `jellyfin-supported` above, then run `nix run` and exercise login, home/library/search/detail, artwork, playback, progress, stop, and logout against Jellyfin. | Existing Jellyfin login/catalog/artwork/playback/reporting behavior remains usable; no provider-neutral connection or credential migration regresses; observed unsupported optional routes are recorded as unavailable. | Live Jellyfin host, disposable credentials, and media are not available for this ledger. |
| Silo native auth/profile/catalog | **NOT RUN / UNAVAILABLE** | Start Silo at the pinned revision with compatibility disabled; run the native health and credentialed commands above; in `nix run`, exercise provider discovery, login, refresh, profile/PIN selection, library paging, search, detail, versions, hierarchy, watched/favorite state, and artwork refetch. | Health is `status: "ok"`; valid and invalid auth have the expected outcomes; profile/PIN state is preserved; catalog paging uses snapshots/totals correctly; artwork is fetched from opaque server URLs; current writes keep secrets in `CredentialStore`, with any legacy rollback material handled by the documented verified migration. | Live Silo host, disposable credentials, and media are not available for this ledger. |
| Silo compatibility path | **NOT RUN / UNAVAILABLE** | Enable Silo's compatibility listener, run `silo-8044eb8-compat` above, then connect Bloom with `nix run` to `http://127.0.0.1:8096`. | Compatibility login and core browsing/playback work where the matrix says supported; missing/stubbed routes degrade explicitly and are not reported as native capability. | Live Silo compatibility host and fixture are not available for this ledger. |
| Native direct playback | **NOT RUN / UNAVAILABLE** | With the native fixture running, use `nix run`, select a direct-play file, start it, seek, pause/resume, and stop; retain the native playback report. | Native start returns a usable opaque URL; mpv plays and seeks; progress and stop retain the selected `file_id`/session identity; no Jellyfin route is substituted. | Live native playback runtime unavailable. |
| Native remux playback | **NOT RUN / UNAVAILABLE** | With a fixture requiring remux, use `nix run`, start the title, seek, pause/resume, and stop; retain the native playback report. | The server-selected remux method plays through the provider-neutral descriptor, seeking/reporting remain coherent, and teardown succeeds. | Live native playback runtime unavailable. |
| Native HLS/transcode playback | **NOT RUN / UNAVAILABLE** | With a fixture and server policy that return HLS/transcode, use `nix run`, play through completion or stop, and retain the native playback report. | HLS/transcode is accepted only when the server/runtime returns it; the selected method and opaque URL are preserved; failures are surfaced rather than silently falling back. | Live native policy/runtime unavailable. |
| Windows `win-libmpv` overlay runtime | **NOT RUN / UNAVAILABLE** | On a Windows runtime with embedded libmpv, run `scripts\build.ps1 -Clean`, `scripts\run-windows-tests.ps1 -Config Release -OutputOnFailure`, then launch Bloom and exercise show/hide controls, resize, minimize/restore, maximize, and fullscreen. | Controls and screensaver remain visible in the synced top-level overlay; video does not move or clip during every transition; Windows uses embedded `win-libmpv` with no alternate backend. | Windows runtime hardware and display are unavailable for this ledger. |
| Real keyboard navigation | **NOT RUN / UNAVAILABLE** | On a real desktop, run `nix run`; navigate login, library, detail, playback, dialogs, and settings using only the physical keyboard (including Back/Escape/Enter/arrow actions). | Every interactive path has visible focus, no pointer is required, Back/Escape semantics are deterministic, and no focus is lost after asynchronous loads or overlays. | Physical keyboard run unavailable for this ledger. |
| Real gamepad navigation | **NOT RUN / UNAVAILABLE** | On a real desktop with a supported controller connected, run `nix run`; repeat the same journeys using the configured gamepad bindings and record the controller/device used. | All required journeys are operable without a pointer or keyboard; button/axis bindings, focus transitions, playback controls, and overlay dismissal are reliable. | Physical gamepad run unavailable for this ledger. |
| Playback reporting | **NOT RUN / UNAVAILABLE** | With a disposable native and Jellyfin fixture, play, pause, seek, resume, and stop from `nix run`; inspect the corresponding contract reports and subsequent item state. | Provider-specific report payloads are serialized by the owning adapter; position/paused state and teardown are observable server-side; Jellyfin reporting remains unchanged. | Live providers and disposable accounts unavailable. |
| Audio/subtitle track selection | **NOT RUN / UNAVAILABLE** | Play a title with at least two audio tracks and subtitles from `nix run`; change audio and subtitle selections during playback and restart the item. | Native audio switch uses the server replacement URL when provided; selected tracks are reflected in playback state/preferences; Jellyfin track selection still works; unavailable tracks degrade clearly. | Live media with multiple tracks unavailable. |
| Markers and chapters | **NOT RUN / UNAVAILABLE** | Use a fixture containing intro/recap/credits markers and file-version chapters; inspect playback in `nix run`, enter the chapter rail, seek to a chapter, and exercise marker behavior. | Marker provenance and per-type precedence are preserved; chapter seeks use seconds and the selected file version; absent native artwork/optional routes are visibly degraded, not fabricated. | Live marker/chapter fixture unavailable. |
| Auth-session revocation | **NOT RUN / UNAVAILABLE** | With a disposable native fixture, run the credentialed native contract command with `--allow-mutations`; create a second auth session, revoke it through the app/API, then retry an authenticated request. | The revoked session receives the expected unauthorized result, the active session remains valid until logout, and no playback-session report is mistaken for auth-session revocation. | Live Silo credentials and host unavailable. |

## Graduation decision

Main must attach the commit/build, Linux smoke, and deterministic-test output plus any available live/platform evidence before changing statuses. Until the live Jellyfin preservation, native/compatibility Silo, and Windows runtime gates are observed, first-class labeling is blocked. Missing optional native routes (including trickplay/theme-song surfaces) remain unavailable rather than supported. This release therefore uses only **experimental native Silo support**; it does not claim first-class support.

Trademark review: “Jellyfin” and “Silo” appear only as referential server/provider names in this ledger. Bloom ships no Silo logos, artwork, fonts, or other Silo assets.
