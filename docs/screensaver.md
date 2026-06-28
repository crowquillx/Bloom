In-App Screensaver

Components
- `ScreensaverController` (`src/ui/ScreensaverController.{h,cpp}`) is a `QObject` installed as an app-level event filter on `QGuiApplication`. It arms a single-shot `QTimer`; on timeout it sets `m_active` and emits `activeChanged`.
- `ScreensaverOverlay.qml` (`src/ui/ScreensaverOverlay.qml`) renders the active mode and fetches artwork via `LibraryService.getScreensaverItems(80)` when it becomes visible and `items.length === 0`.
- `Main.qml` hosts the overlay inline under `Overlay.overlay` for normal app idle states. During Windows embedded playback, it hosts the same overlay in a dedicated top-level `Window` synced to the app window so the native mpv child window cannot occlude it while playback is paused.
- Modes (ConfigManager `screensaverMode`): `libraryBackdrops` (cycling backdrops with metadata/logo placement), `bouncingLogo` (bouncing logo over artwork), `black` (no artwork; just a black screen).
- Debug env vars: `BLOOM_SCREENSAVER_DEBUG=1` forces the screensaver enabled regardless of config; `BLOOM_SCREENSAVER_DEBUG_TIMEOUT_MS=<ms>` overrides the timeout (clamped to 250..60000, default 3000). Intended for manual smoke testing.

Arming contract
- `canArm()` requires `effectiveEnabled && m_appWindowVisible && m_auth->isAuthenticated() && !playbackBlocksScreensaver()`.
- `playbackBlocksScreensaver()` returns true when `PlayerController` is in `Playing`, `Loading`, or `Buffering` state, OR when `PlayerController::awaitingNextEpisodeResolution()` is true (i.e. the Up Next overlay is awaiting the next-episode resolution). When any of these become true, the controller calls `dismiss()` (if active) and reschedules, so the screensaver cannot arm behind the Up Next overlay.
- The app-level event filter calls `dismiss()` (and swallows the event) on the first activity event while active; otherwise it calls `schedule()`. `noteActivity()` is the invokable equivalent.
- `AuthenticationService` `loggedOut`/`sessionExpired` dismiss and reschedule; `loginSuccess` reschedules.
- `appWindowVisible` is bound in `Main.qml` to `window.visible && window.visibility !== Window.Minimized` and dismissal/reschedule happens on hide.

Focus contract
- The overlay's `focus: visible` takes focus as soon as it becomes visible. The dismissing keypress is swallowed by the event filter, so the underlying view would not re-take focus on its own.
- `ScreensaverOverlay` snapshots the configured `focusWindow.activeFocusItem` on `ScreensaverController.activeChanged` (true) into `savedFocusItem`; inline hosts pass the app window, and detached hosts also pass the app window rather than the overlay window. This must run on `activeChanged` rather than `onVisibleChanged`, because `onVisibleChanged` runs after the overlay has already taken focus.
- On dismiss (`onVisibleChanged` else branch), it calls `Qt.callLater(function() { if (savedFocusItem && savedFocusItem.parent && typeof savedFocusItem.forceActiveFocus === "function") savedFocusItem.forceActiveFocus(); savedFocusItem = null })`. The guard handles the case where the previously focused item was destroyed (e.g. by a stack pop during idle).

Stale artwork on logout
- `ScreensaverOverlay` only fetches `getScreensaverItems` when `items.length === 0`, so a previous user's candidate set would otherwise persist across a logout/login as a different user.
- `Connections` to `AuthenticationService.onLoggedOut` and `onSessionExpired` call `resetArtwork()`, which clears `items`, `currentIndex`, `placementIndex`, resets `showBackdropA`, and blanks `backdropAUrl`/`backdropBUrl`. It does NOT fetch; the existing `onVisibleChanged` guard refetches on the next activation.

Known limitation (until a later phase ships)
- There is no OS-level screensaver/DPMS/system idle inhibit. On most Linux desktop environments the OS idle timer runs in parallel with Bloom's internal timer and may blank or lock the screen before the Bloom overlay is visible. This phase only addresses the in-app overlay.

Settings
- `Display > Screensaver` (`src/ui/settings/DisplaySettings.qml`):
  - Enable Screensaver toggle (`screensaverEnabled`).
  - Mode combo (`screensaverMode`): libraryBackdrops | bouncingLogo | black.
  - Timeout spinbox in minutes, range 1..1440 (24 h). Bound to `ConfigManager.screensaverTimeoutSeconds` via `Math.max(1, Math.round(.../60))` and persisted as seconds via `Math.max(1, newValue) * 60`.
- ConfigManager clamps `screensaverTimeoutSeconds` to 15..86400 (seconds). The spinbox ceiling of 1440 min (86400 s) matches the upper bound, so persisted values round-trip without silent downgrading. Config migration v24 -> v25 adds the `screensaver` object with these defaults.
