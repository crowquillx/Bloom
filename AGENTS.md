# Bloom

10-foot HTPC media client for Jellyfin and Silo servers: high-quality mpv
playback, Wayland-first Linux, Windows via embedded libmpv. Every screen MUST be
fully keyboard/gamepad navigable.

Stack: C++23, Qt 6/QML, CMake, mpv (external JSON IPC or embedded libmpv).
Logic (network, data models, services, policy) lives in C++; QML is UI,
animation, and theming.

## Build, test, verify

All builds go through Nix; run CMake/Ninja only via `./scripts/dev-build.sh`.

- Incremental build: `./scripts/dev-build.sh` (`--run` to launch, `--tests` to
  build test targets into `build-dev/`).
- Run tests: from `build-dev/`,
  `LC_ALL=C QT_QPA_PLATFORM=offscreen nix develop .. --command ctest --output-on-failure`
  (add `-R '^NameTest$'` for one test). `LC_ALL=C` is required: under a UTF-8
  locale `mpv_create()` returns null and libmpv tests fail spuriously.
- `VisualRegressionTest` always skips locally, so its pass proves nothing.
  `./src/Bloom --test-mode` ignores SIGTERM; kill it explicitly.
- QML lint: `nix build .#checks.x86_64-linux.qml-lint`.
- Final verification: `nix flake check` (tests, QML lint, release smoke, pins).
  CI runs the same checks; sanitizers, TSAN, clang-tidy and coverage run in the
  scheduled deep-analysis workflow.
- Live server, display, GPU, and playback contracts are manual validation
  (`docs/manual-validation.md`); an unavailable environment is not a pass.

Tests link the narrowest production `Bloom::*` target instead of compiling
shared `.cpp` files. Library dependencies stay acyclic: models →
configuration/transport → providers → network services.

## UI rules

- Navigable views use a `FocusScope` root; non-focusable views use `Item`.
  Restore focus with `Qt.callLater` after async model updates.
- Size everything with `Theme` tokens or `Math.round(N * Theme.layoutScale)`.
  Scaling is resolution- and OS-scaling-independent (`docs/responsive.md`).
- Show focus with the shared `FocusRing` and selection with `selection*` tokens
  (the Home hero uses edge light cues instead). Build from shared components
  (`ActionButton`, `BloomMenu`, `DialogSurface`, `StateMessage`, ...) and
  semantic tokens in `src/ui/Theme.qml`, the single token source.
- Pages reserve `Theme.navRailReservedWidth`; the `Sidebar.qml` rail expands as
  an overlay without shifting content.
- Full QML/focus conventions: `docs/developer_notes.md`.

## Ownership map

Each area has one owner. Read its doc before changing it.

- **External mpv process/IPC**: `Bloom::PlayerProcess` (`docs/playback.md`).
  Fully event-driven; no UI-thread `QProcess::waitFor*`. Preserve graceful
  quit/escalation, queued replacement playback, bounded IPC deadlines/queues,
  and per-user/per-instance private endpoints.
- **Playback backend selection** (`docs/playback.md`): Windows always uses
  embedded `win-libmpv`. Linux embedded libmpv is validation-only; Wayland
  defaults to `external-mpv-ipc`, and unsupported embedded conditions fall back
  to it.
- **Playback policy**: `Bloom::PlayerPolicy` (`docs/playback.md`). Language,
  track, HDR, version, completion, and autoplay decisions stay Qt-Core-only,
  free of I/O and `QObject`. `PlayerController` remains the QML-facing facade.
- **Display/HDR**: `Bloom::Display`. Asynchronous and generation-safe; external
  display commands keep deadlines and bounded output. HDR is applied before
  refresh at startup and restored before refresh.
- **Audio output**: policy in `Bloom::PlayerPolicy`, endpoints in
  `Bloom::AudioOutput` (`docs/audio.md`). Provider decode claims stay
  independent of encoded output support. Selectors list only the active mpv
  driver's endpoints. A missing saved device resolves to `auto` without being
  overwritten. Only WASAPI gets exclusive access; every other driver keeps
  `audio-exclusive=no`.
- **HTTP**: `HttpTransport` (`docs/services.md`). Bounded attempt and
  unauthorized-recovery deadlines. Retries are chosen by operation semantics:
  safe reads (including read-only `POST` queries) use `Idempotent`, mutations
  default to `Never`, replayable mutations need a stable idempotency mechanism.
  `RetryPolicy::maxAttempts` includes the first attempt. Provider parsing stays
  outside the transport; network, HTTP, and provider error fields stay separate.
- **Providers** (`docs/provider-architecture.md`,
  `docs/canonical-models.md`): wire DTOs stay inside provider adapters and
  `LibraryService` request handling; view models, QML, and player code consume
  connection-scoped canonical projections only, with camelCase keys. Providers
  own playback lifecycle and report serialization. Jellyfin requests use only
  non-obsolete operations and parameters from the pinned OpenAPI contract
  (`docs/provider-compatibility.md`).
- **Image cache**: `Bloom::ImageCache`. SQLite and disk files
  belong to its dedicated worker; its `QSqlDatabase` stays on that thread.
  Provider image jobs coalesce identical identity-and-size requests; QML
  responses subscribe to jobs and own no download/decode work.
- **Updates**: `Bloom::Updater` (`docs/updates.md`). Manifests stay
  Ed25519-signed, HTTPS/origin-restricted, and size/deadline-bounded. Installer
  bytes are hashed incrementally and finalized atomically before any Windows
  elevation.
- **Bloom profiles (spaces in the UI)**: `Bloom::Profiles` (repository, merge
  policy) and `Bloom::Workspace` (`WorkspaceController`, `MergedCatalogService`)
  (`docs/profiles.md`). `AuthenticationService` keeps one live session;
  switching never deletes credentials, and merged items route to their owning
  connection before details, playback, or writes. Merge logic stays in C++,
  never in QML; `memberId` never enters `MediaRef` or caches.

## Windows embedded playback guardrail

With `win-libmpv` (`--wid` child window), native composition occludes in-scene
QML layered above `VideoSurface`. Render playback controls, and any app overlay
that must show during playback (for example the paused screensaver), in a
dedicated transparent top-level `Window` synced to the app window geometry.

Changes to `src/ui/Main.qml`, `src/ui/VideoSurface.qml`,
`src/ui/EmbeddedPlaybackOverlay.qml`, or `src/player/backend/WindowsMpvBackend.*`
need manual runtime validation: controls visible above video, and no video
move/clip across show/hide, resize, minimize/restore, maximize, and fullscreen.

## Conventions

- C++: PascalCase classes, camelCase methods, `m_` members. QML: PascalCase
  components, camelCase properties.
- Commits follow Conventional Commits (`docs/conventional-commits.md`).
- Update the relevant `docs/` page in the same change. A new major component
  gets its own page plus a line below. Edit this file only for architecture or
  global-policy changes.

## Docs

- Building, packaging, CI, Windows MSVC → `docs/build.md`,
  `docs/WINDOWS-BUILD.md`, `docs/WINDOWS-README.md`
- mpv, playback, reporting → `docs/playback.md`; Dolby Vision/FEL and mpv pins
  → `docs/dolby-vision.md`
- Server and local seek previews → `docs/seek-previews.md`
- Audio metadata, output policy, device selection → `docs/audio.md`
- Service registration and init order → `docs/services.md`
- Settings and `ConfigManager` properties → `docs/config.md`
- Server connections, credentials, adapters → `docs/provider-architecture.md`;
  Jellyfin/Silo contract pins → `docs/provider-compatibility.md`
- Canonical media/artwork/playback models → `docs/canonical-models.md`
- Bloom profiles (spaces), merged reads, routing → `docs/profiles.md`
- View models and loading/error state → `docs/viewmodels.md`
- Theme tokens, rail, shared components → `docs/theme.md`; scaling math and
  ultrawide layout → `docs/responsive.md`
- QML, focus, logging conventions → `docs/developer_notes.md`
- Keyboard/controller bindings → `docs/input-bindings.md`
- Accessibility roles and names → `CONTRIBUTING.md`
- Home hero → `docs/hero-banner.md`; screensaver → `docs/screensaver.md`
- Rounded images and the `ImageCacheProvider` path → `docs/images.md`
- Intro/recap/credits segments → `docs/media-segments.md`
- Seerr requests → `docs/seerr.md`
- Updater, manifests, key rotation → `docs/updates.md`
- Credential storage, keyring troubleshooting → `docs/SECURITY.md`
- Release validation ledger → `docs/manual-validation.md`
