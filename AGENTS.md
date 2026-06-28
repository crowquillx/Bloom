Bloom — Project Rules & Architecture (Condensed)

Purpose: Single-page developer reference for architecture, conventions, and quick build/run steps. Keep this file short — deep dives live in `docs/` (playback, theme, services, config, developer_notes).

Goals: 10-foot Jellyfin HTPC client; high-quality mpv playback; Wayland-first Linux support. MUST be completely keyboard/gamepad navigable. 

QML & Focus
- Use `FocusScope` as the root of navigable views; QML `Item` should be used if the view is not intended to be focusable.
- For keyboard/remote UX, `InputModeManager` should detect pointer vs keyboard navigation; hide cursor while navigating via keyboard.
- Use `Qt.callLater` to restore focus after async view/model updates or cache reloads.

Stack: C++23 (Qt 6/QML) • CMake • mpv (external JSON IPC)
Keep as much logic as possible in C++ (network, data models, services); QML for UI, animations, and theming.

Key files: `src/core/ServiceLocator.h`, `utils/ConfigManager.*`, `network/JellyfinClient.*`, `player/*`, `ui/Theme.qml`, `ui/ResponsiveLayoutManager.*`, `src/ui/ScreensaverController.*`, `src/ui/ScreensaverOverlay.qml`.

Conventions: C++ PascalCase classes, camelCase methods, `m_` prefix; QML PascalCase components, camelCase props; use `FocusScope` for navigable views; keep `Theme.qml` as the single source for tokens.

Playback & API: Key endpoints `/Users/{userId}/Items`, `/Shows/NextUp`, `/PlaybackInfo`, `/Sessions/Playing`. Report start/progress/pause/stop and mark watched via `ConfigManager` thresholds.

Windows embedded playback guardrail (regression prevention):
- When using `win-libmpv` embedded playback (`--wid` child window), playback controls MUST render in a dedicated transparent top-level overlay `Window` synced to the app window geometry.
- Any app-level overlay that must be visible during Windows embedded playback (for example the screensaver while paused) MUST also render in a synced top-level overlay `Window`.
- Do NOT rely on in-scene sibling layering above `VideoSurface` for Windows embedded playback; native child-window composition can occlude QML overlays.
- Any PR touching Windows playback layering/geometry (`src/ui/Main.qml`, `src/ui/VideoSurface.qml`, `src/ui/EmbeddedPlaybackOverlay.qml`, `src/player/backend/WindowsMpvBackend.*`) must include manual runtime validation: controls visible above video, and no video move/clip during show/hide, resize, minimize/restore, maximize, and fullscreen transitions.

Backend policy (high level): Windows always uses embedded libmpv (`win-libmpv`) for playback. Linux embedded libmpv remains experimental/validation-only; keep current Linux fallback behavior (Wayland defaults to `external-mpv-ipc` unless explicitly opted in for validation, and unsupported embedded runtime conditions fall back to `external-mpv-ipc`).

Build & run (blessed path):
```
nix build
nix run
nix flake check
```
Use the flake for development, builds, tests, and packaging. For local builds
after making code changes, `./scripts/dev-build.sh` is the blessed incremental
build path; it runs CMake/Ninja inside `nix develop` and keeps a persistent
build directory. Do not run CMake or Make directly for supported Linux builds.
Use `nix build` and `nix flake check` for clean final verification. Portable
Linux artifacts are orchestrated by Nix through a pinned Ubuntu compatibility
container; Windows remains a native MSVC build.

When to update: Only edit this file for architecture, conventions, or global policy changes. Implementation details live in `docs/*` and should be updated there.

Documentation & update policy:
- Keep `AGENTS.md` as a short, high-level reference only. All implementation details, guides and examples belong in `docs/*`.
- When adding a new feature, component, or public API: update the relevant page(s) in `docs/`. If you add a new major component, create a `docs/` page describing it and link that file from `AGENTS.md` (short summary only).
- For small changes (UI tweaks, single-property changes): update the relevant `docs/` file (or add a small note). Include documentation updates in the same PR whenever practical.
- For major architecture or convention changes: update `AGENTS.md` (briefly), update relevant `docs/*` pages with details, and include a short migration/compatibility note.

If making a commit, use Conventional Commits spec. See [docs/conventional-commits.md](docs/conventional-commits.md) for the canonical guidance.



PR checklist (docs):
- [ ] Documentation updated in the appropriate `docs/` page(s).
- [ ] `AGENTS.md` updated if the change is architectural or alters project conventions.
- [ ] Link new docs in the PR description (or include the doc change in the PR).
- [ ] Ask a maintainer or reviewer to confirm that docs accurately reflect changes.
- [ ] (Optional) Add or update a changelog entry for large changes.

If unsure whether a change requires `AGENTS.md` or `docs/` updates, open an issue or tag a maintainer and include a short explanation and proposed doc edits.

See also:
- docs/build.md     — Nix build, packaging, CI, and Windows instructions
- docs/playback.md  — mpv and playback/reporting integration
- docs/media-segments.md — External intro/recap/credits provider integration and precedence
- docs/hero-banner.md — Home hero sources, settings, focus behavior, and backdrop synchronization
- docs/theme.md     — Theme.qml tokens and design system
- docs/services.md  — ServiceLocator pattern & initialization order
- docs/updates.md   — Windows updater flow, release manifests, channels, and notify-only behavior
- docs/config.md    — ConfigManager Q_PROPERTY and runtime settings
- docs/seerr.md     — Seerr/Jellyseerr integration (search, request flow, and similar-title hook)
- docs/responsive.md — Deprecated: see docs/theme.md for ResponsiveLayoutManager
- docs/developer_notes.md — Conventions, focus, QML patterns, and best practices
- docs/viewmodels.md — BaseViewModel patterns and MovieDetailsViewModel
- docs/screensaver.md — in-app OLED-safe screensaver controller, arming rules, focus contract, and known limitations
- docs/input-bindings.md — configurable keyboard/controller action bindings and SDL gamepad dispatch
- docs/conventional-commits.md — commit messages must follow the Conventional Commits spec so tooling and changelogs stay accurate

License: See `LICENSE`.

Testing policy:
- Automated tests are currently local-only and are not run in CI.
- `nix flake check` remains the blessed local command for the full check suite.
