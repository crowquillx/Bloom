Bloom — Project Rules & Architecture (Condensed)

Purpose: Single-page developer reference for architecture, conventions, and quick build/run steps. Keep this file short — deep dives live in `docs/` (playback, theme, services, config, developer_notes).

Goals: 10-foot Jellyfin HTPC client; high-quality mpv playback; Wayland-first Linux support. MUST be completely keyboard/gamepad navigable. 

QML & Focus
- Use `FocusScope` as the root of navigable views; QML `Item` should be used if the view is not intended to be focusable.
- For keyboard/remote UX, `InputModeManager` should detect pointer vs keyboard navigation; hide cursor while navigating via keyboard.
- Use `Qt.callLater` to restore focus after async view/model updates or cache reloads.

Stack: C++23 (Qt 6/QML) • CMake • mpv (external JSON IPC)
Keep as much logic as possible in C++ (network, data models, services); QML for UI, animations, and theming.

Key files: `src/core/ServiceLocator.h`, `utils/ConfigManager.*`, `network/JellyfinClient.*`, `player/*`, `ui/Theme.qml`, `ui/ResponsiveLayoutManager.*`.

Conventions: C++ PascalCase classes, camelCase methods, `m_` prefix; QML PascalCase components, camelCase props; use `FocusScope` for navigable views; keep `Theme.qml` as the single source for tokens.

Playback & API: Key endpoints `/Users/{userId}/Items`, `/Shows/NextUp`, `/PlaybackInfo`, `/Sessions/Playing`. Report start/progress/pause/stop and mark watched via `ConfigManager` thresholds.

Windows embedded playback guardrail (regression prevention):
- When using `win-libmpv` embedded playback (`--wid` child window), playback controls MUST render in a dedicated transparent top-level overlay `Window` synced to the app window geometry.
- Do NOT rely on in-scene sibling layering above `VideoSurface` for Windows embedded playback; native child-window composition can occlude QML overlays.
- Any PR touching Windows playback layering/geometry (`src/ui/Main.qml`, `src/ui/VideoSurface.qml`, `src/ui/EmbeddedPlaybackOverlay.qml`, `src/player/backend/WindowsMpvBackend.*`) must include manual runtime validation: controls visible above video, and no video move/clip during show/hide, resize, minimize/restore, maximize, and fullscreen transitions.

Backend policy (high level): Windows always uses embedded libmpv (`win-libmpv`) for playback. Linux embedded libmpv remains experimental/validation-only; keep current Linux fallback behavior (Wayland defaults to `external-mpv-ipc` unless explicitly opted in for validation, and unsupported embedded runtime conditions fall back to `external-mpv-ipc`).

Build & run:
```
./scripts/build-docker.sh 
```
DO NOT run `cmake` or `make` directly; use the provided script to ensure dependencies are set up correctly.

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
- docs/build.md     — Build instructions for Linux, Docker, and Windows
- docs/playback.md  — mpv and playback/reporting integration
- docs/theme.md     — Theme.qml tokens and design system
- docs/services.md  — ServiceLocator pattern & initialization order
- docs/updates.md   — Windows updater flow, release manifests, channels, and notify-only behavior
- docs/config.md    — ConfigManager Q_PROPERTY and runtime settings
- docs/seerr.md     — Seerr/Jellyseerr integration (search, request flow, and similar-title hook)
- docs/responsive.md — Deprecated: see docs/theme.md for ResponsiveLayoutManager
- docs/developer_notes.md — Conventions, focus, QML patterns, and best practices
- docs/viewmodels.md — BaseViewModel patterns and MovieDetailsViewModel
- docs/conventional-commits.md — commit messages must follow the Conventional Commits spec so tooling and changelogs stay accurate

License: See `LICENSE`.

## Cursor Cloud specific instructions

### Building
- Docker must be running before building. Start the daemon with `dockerd &` and wait a few seconds.
- Run `xhost +local:` before launching the app inside a Docker container so it can access the X display.
- The canonical build command is `./scripts/build-docker.sh`. For development iteration where you only need the main binary (no tests), you can build faster inside the container with `-DBUILD_TESTING=OFF`:
  ```
  docker run --rm --network=host -v "$(pwd):/workspace" bloom-build bash -c \
    'cd /workspace && mkdir -p build-docker && cd build-docker && cmake .. -G Ninja -DBUILD_TESTING=OFF && ninja'
  ```
- A full build with tests uses the same command without `-DBUILD_TESTING=OFF` (or simply `./scripts/build-docker.sh`).

### Running tests
- Tests must be run inside the Docker container since the binary is linked against the container's Qt6/mpv libraries.
- Set `QT_QPA_PLATFORM=offscreen` when running tests in a headless environment:
  ```
  docker run --rm --network=host -e QT_QPA_PLATFORM=offscreen -v "$(pwd):/workspace" bloom-build \
    bash -c 'cd /workspace/build-docker && ctest --output-on-failure'
  ```
- `VisualRegressionTest` golden image mismatches are expected in different environments (pixel-perfect comparisons).
- `SeriesDetailsCacheTest` has two known pre-existing failures (`seriesCacheRespectsFreshness`, `itemsCacheRespectsFreshness`).

### Running the application
- Launch inside the container with display forwarding:
  ```
  docker run --rm --network=host -e DISPLAY=:1 -e QT_QPA_PLATFORM=xcb -e XDG_RUNTIME_DIR=/tmp \
    -v /tmp/.X11-unix:/tmp/.X11-unix -v "$(pwd):/workspace" bloom-build /workspace/build-docker/src/Bloom
  ```
- The app requires a Jellyfin server to be fully functional beyond the login screen.

### QML lint
- QML lint runs as part of `./scripts/build-docker.sh` automatically. To run it manually:
  ```
  docker run --rm -v "$(pwd):/workspace" bloom-build bash -c \
    'qmllint -I /workspace/build-docker/src -I /usr/lib/qt6/qml $(find /workspace/build-docker/src/BloomUI/ui -name "*.qml" | sort)'
  ```
