Developer Notes & Conventions

Detailed QML, focus, C++, and logging conventions. `AGENTS.md` carries the short version and the subsystem ownership map.

QML & Focus
- Use `FocusScope` as the root of navigable views; QML `Item` should be used if the view is not intended to be focusable.
- For keyboard/remote UX, `InputModeManager` should detect pointer vs keyboard navigation; hide cursor while navigating via keyboard.
- Keyboard/controller shortcuts should be added as `InputBindingManager` actions instead of hard-coded key checks; see `docs/input-bindings.md`.
- Use `Qt.callLater` to restore focus after async view/model updates or cache reloads.
- For query controls that reload models (sort, search, filters), record the initiating control and restore it after load completion so keyboard/gamepad users stay in context.
- Settings UX: keep explicit `KeyNavigation`/`Keys` wiring on interactive rows and preserve the home-style rotating backdrop + gradient readability overlay used by `SettingsScreen.qml`.
- Settings dropdowns should use `SettingsComboBox` instead of raw `ComboBox` so primitive and role-based models render the selected text reliably after async model/index updates.
- Settings boolean controls should use `SettingsToggleRow` for consistent focus treatment, accessibility metadata, and keyboard/gamepad toggling.
- Global app-shell shortcuts live in `Main.qml`; ESC on the root home screen opens the power menu, while deeper screens keep the normal back-stack behavior.
- Modal dialogs opened over a page that restores focus asynchronously (Home, after loads or a pop transition) declare a `ModalFocusGuard`, so a late page restore cannot strand focus behind the modal.
- The sidebar's Exit entry opens the same power menu, and Sign Out (sidebar or Settings → About & Account) opens a confirmation dialog with Cancel focused, so a stray Select press never signs out or quits.
- The sidebar is a floating navigation rail (see `docs/theme.md#navigation-rail`). Moving Left into it (`Sidebar.focusRail()`) focuses the current page's entry (Home, Search, the active library, or Settings). Cancelling a sign-out or power dialog opened from the rail returns focus to the entry that opened it (`Sidebar.focusDialogOrigin()`).
- Pages reserve `Theme.navRailReservedWidth` on the left and never respond to rail expansion. Full-bleed backdrops extend under the rail with a negative left margin.
- Show focus only with the shared `FocusRing` (`inset: true` inside clipped lists and popups) and selection with `selection*` tokens. Prefer the shared components (`ActionButton`, `BloomMenu`, `DialogSurface`, `StateMessage`, `SectionHeader`, `MetadataLine`, `ArtworkPlaceholder`, `ArtworkLogo`) over one-off styling.
- Grids/lists that auto-select an index when their model loads must not move focus unless the view already has active focus; otherwise async results steal focus from text inputs (see `SearchScreen.qml`).
- The screensaver is controlled by `ScreensaverController` and rendered by `ScreensaverOverlay.qml`. It may activate during app navigation or paused playback, but playing/loading/buffering video must always cancel or block activation.

C++ conventions
- PascalCase class names, camelCase method names, `m_` prefix for member variables.
- Use `Q_PROPERTY` for values that will be bound in QML; emit change signals on setter change.
- Keep playback/track-selection policy in C++ where possible. QML should render available options and forward explicit user intent, not duplicate track mapping or default-resolution logic.

ViewModels & Models
- Use `QAbstractListModel` for heavy lists and expose roles via `roleNames()`.
- Loading/error state conventions: see `docs/viewmodels.md` for `BaseViewModel` usage and migration notes.
- Use a simple pattern for SWR: serve cached data immediately, background refresh for fresh data, update UI only if data changed.

Performance
- Offload heavy JSON parsing or expensive calculations to background threads and report results back to the UI thread.

Useful utilities
- `PlayerProcessManager` for external mpv lifecycle. Link `Bloom::PlayerProcess`
  in tests; keep process and IPC control asynchronous and bounded as documented
  in `docs/playback.md`.
- `DisplayManager` for HDR and refresh-rate lifecycle. Link `Bloom::Display` in
  tests; never wait for an external display command on the GUI thread, and keep
  playback preparation/restoration generation-safe.
- `PlaybackPolicy` for pure language, track, HDR, version, completion, and
  next-episode decisions. Link `Bloom::PlayerPolicy` in focused tests and keep
  the target Qt-Core-only with all inputs supplied explicitly.
- `HttpTransport` for shared HTTP execution/retry/cancellation policy.
- `IProviderRequestFactory` implementations for provider URL/header construction.
- `ConfigManager` for settings and QML exposures.

Logging
- Bloom routes Qt diagnostics through `Logger` with a default `info` level. Chatty areas use `Q_LOGGING_CATEGORY` (for example `bloom.imagecache`, `bloom.library`); debug for those categories is off unless you set `settings.logging.level` to `debug`, pass `--verbose`, or add rules under `settings.logging.qt_rules`.
- Include `utils/BloomLogging.h` and use the declared categories (`lcImageCache`, `lcViewModels`, etc.) — never bare `qDebug()` in new code.
- Prefer `qCDebug(category)` over bare `qDebug()` for high-volume paths so logs can be filtered without silencing the whole app.
