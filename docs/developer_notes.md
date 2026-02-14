Developer Notes & Conventions

Purpose
- Keep this doc short — examples and code snippets are small references. Prefer component-specific examples in the code or dedicated how-to docs.

QML & Focus
- Use `FocusScope` as the root of navigable views; QML `Item` should be used if the view is not intended to be focusable.
- For keyboard/remote UX, `InputModeManager` should detect pointer vs keyboard navigation; hide cursor while navigating via keyboard.
- Use `Qt.callLater` to restore focus after async view/model updates or cache reloads.
- Settings UX: keep explicit `KeyNavigation`/`Keys` wiring on interactive rows and preserve the home-style rotating backdrop + gradient readability overlay used by `SettingsScreen.qml`.

C++ conventions
- PascalCase class names, camelCase method names, `m_` prefix for member variables.
- Prefer smart pointers and use `QObject` parent ownership where appropriate.
- Use `Q_PROPERTY` for values that will be bound in QML; emit change signals on setter change.

ViewModels & Models
- Use `QAbstractListModel` for heavy lists and expose roles via `roleNames()`.
- Loading/error state conventions: see `docs/viewmodels.md` for `BaseViewModel` usage and migration notes.
- Use `ListView` with `cacheBuffer` tweaks for long lists (optimize based on device perf).
- Use a simple pattern for SWR: serve cached data immediately, background refresh for fresh data, update UI only if data changed.

Performance
- Lazy-load images and cache them (image caching service exists in project).
- Offload heavy JSON parsing or expensive calculations to background threads and report results back to the UI thread.
- Reuse delegates and avoid excessive property bindings that cause frequent re-evaluations.

Coding style
- Keep components small and single-purpose.
- QML: props down, events up. Expose events via signals.
- Write small unit tests for C++ core logic and integration tests for critical flows if feasible.

Useful utilities
- `PlayerProcessManager` for mpv lifecycle.
- `JellyfinClient` for network operations and request retriers.
- `ConfigManager` for settings and QML exposures.

Where to add new docs
- For new architectural changes, add a targeted doc under `/docs` and link it from `AGENTS.md`.

Contact & credits
- Add contributor credits and the LICENSE file as the canonical source for licensing.
