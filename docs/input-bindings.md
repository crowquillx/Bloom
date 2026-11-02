Input Bindings

Overview
- Bloom routes configurable keyboard and controller shortcuts through `InputBindingManager`.
- Bindings are grouped by context: Navigation, Playback, and Advanced Playback.
- Runtime resolution uses `navigation` and `playback` contexts. The Advanced Playback settings section resolves as playback at runtime.
- Existing keyboard defaults remain code-owned defaults; user config stores only overrides.
- SDL2 is the controller backend for Linux and Windows builds when the dependency is present.

Storage
- Overrides are stored in `settings.input_bindings`.
- Schema:
  - `schema`: binding schema version, currently `1`.
  - `keyboard`: object keyed by action id, each value an array of normalized key bindings.
  - `gamepad`: object keyed by action id, each value an array of normalized controller bindings.
- An omitted action uses the built-in default. An empty array intentionally unassigns that action.

Runtime
- `InputBindingManager.actionForKeyboardEvent(key, modifiers, runtimeContext)` maps QML key events to action ids.
- Passing a runtime context allows the same key to mean different things in different modes, for example Space is navigation select in `navigation` and play/pause in `playback`.
- During playback, `Main.qml` registers a window `Shortcut` for every keyboard binding in the `playback` runtime context, built from `InputBindingManager.keyboardShortcuts("playback")` and rebuilt when bindings change. Each one runs the bound action through `EmbeddedPlaybackOverlay.handlePlaybackAction`, so a key does what its binding says even while no playback control has focus. Under external mpv, which has no Bloom overlay, play/pause, seek, subtitle style override, deband and the stats actions still work. Playback keys are never hardcoded to a different action.
- SDL2 gamepad input is polled in C++ when SDL2 is available at build time. The backend handles hotplug and disconnect, reports `gamepadAvailable`, and repeats held navigation/seek/volume inputs while keeping toggles edge-triggered.
- Controller navigation actions synthesize the same Qt key events already used by QML focus handlers.
- Playback controller actions emit `InputBindingManager.actionTriggeredWithContext(actionId, "playback")` and are handled by `EmbeddedPlaybackOverlay.qml`.
- Default controller playback bindings: D-pad/left stick move or seek based on overlay state, A selects the focused playback control or active Skip Intro/Credits prompt, B mirrors Escape dismissal/stop behavior, Start play/pause, LB/RB seek, LT/RT chapters, Y opens the Audio page and X the Subtitles page of the playback settings pop-out, Back/View volume control, right stick volume, L3 subtitle ASS override, R3 deband.
- Default controller navigation bindings: D-pad/left stick move, A selects, B goes back, and Start (the Menu/Options button) opens the focused card's actions (`nav.menu`). Start keeps play/pause during playback, a separate runtime context.
- The runtime context is `playback` whenever playback is active, embedded or external mpv, unless a navigation modal (the screensaver) is up (`InputBindingManager::runtimeContextFor`, bound in `Main.qml`). So controller buttons never resolve `nav.*` actions during playback, and Start never opens a card menu on the page behind the player. External mpv has no Bloom overlay: `Main.qml` runs the controller's playback actions like the bound playback keys (`triggerPlaybackKeyAction`: play/pause, LB/RB seek, L3/R3 toggles, stats). B does not stop external playback, and the D-pad, Y, X and Back do nothing there.
- In the settings pop-out, D-pad/left stick Up/Down move through rows, Right or A opens a category page, A selects an option, and Left returns to the root list. On the Subtitles page, Left/Right on the delay row adjust the current season/movie subtitle delay in 1 ms steps (persisted with subtitle preferences) and A resets it. "Search subtitles…" (Right or A) opens the subtitle search page: Left/Right on its language row change the language and search again, A on a result downloads and selects it, A on the error row searches again, and B/Back (or Left on any other row) returns to the Subtitles page with the entry focused.

Keyboard defaults

| Action | Context | Default keys |
| --- | --- | --- |
| Up / Down / Left / Right (`nav.*`) | Navigation | Arrow keys |
| Select (`nav.select`) | Navigation | Return, Enter, Space |
| Back (`nav.back`) | Navigation | Esc |
| Actions (`nav.menu`) | Navigation | Menu, Shift+F10 |
| Play/Pause (`playback.playPause`) | Playback | Space |
| Seek Back (`playback.seekBack`) | Playback | J |
| Seek Forward (`playback.seekForward`) | Playback | L |
| Volume Up (`playback.volumeUp`) | Playback | +, =, Volume Up |
| Volume Down (`playback.volumeDown`) | Playback | -, Volume Down |
| Volume Panel (`playback.volumePanel`) | Playback | V |
| Audio Selector (`playback.audioSelector`) | Playback | A |
| Subtitle Selector (`playback.subtitleSelector`) | Playback | S, T, C |
| Sleep Timer (`playback.sleepTimer`) | Playback | None |
| Subtitle Style Override (`playback.subtitleOverride`) | Advanced Playback | K |
| Deband (`playback.deband`) | Advanced Playback | B |
| Stats (`playback.stats`) | Advanced Playback | I |
| Stats Once (`playback.statsOnce`) | Advanced Playback | Shift+I |

- Playback navigation/select/back, previous/next chapter, skip segment, mute, audio/subtitle cycling, and the sleep timer have no keyboard default. `playback.sleepTimer` opens the Sleep Timer page of the playback settings pop-out during embedded playback.
- Under `external-mpv-ipc`, keys go to whichever window has focus. While mpv's window has focus, mpv handles them and Bloom's keyboard bindings (including `playback.sleepTimer`) do not fire; a Bloom keyboard binding only fires while Bloom's own window has focus (the runtime context is `playback` then, as above), where it steps through the sleep-timer choices on mpv's OSD. Bind the sleep timer in mpv instead, in `~/.config/Bloom/mpv/input.conf`:

  ```
  Z      script-message bloom-sleep-timer        # step through Off, 15 … 90 min, end of item
  Ctrl+z script-message bloom-sleep-timer off    # or: end, 15, 30, 45, 60, 90
  ```

  `bloom-skip-intro` and `bloom-skip-outro` work the same way (see [media segments](media-segments.md)).
- Fixed playback keys that are not actions: Esc dismisses the playback overlay or stops playback, Return/Enter triggers an active skip prompt or brings up the controls, Left/Right seek 10 seconds (or move overlay focus), Up/Down move overlay focus, and 0-9 open mpv stats pages. Each of these yields to a user binding of the same key in the playback context.
- F11 and Alt+Enter toggle fullscreen everywhere.

Card actions
- `nav.menu` opens the actions menu of the focused Continue Watching or Up Next card on Home (remove or hide the card, go to the series or details, watched, favorite, watchlist, not interested; see [view models](viewmodels.md#continuewatchingviewmodel)). The controller binding posts `Key_Menu`, which Home always accepts; a keyboard key bound to `nav.menu` works too (`actionForKeyboardEvent` in the navigation context). With a pointer, right-click or press-and-hold a card (`MediaCard.menuEnabled`).
- The menu is a `BloomMenu`: Up/Down move (disabled actions are skipped), Select runs an action, and Back/Esc closes it. Focus starts on the card's first action and returns to the card, or after a removal to the card that took its place (the shelf below if the row emptied). A shelf refresh never takes focus from the open menu, another shelf, or the navigation bar.
- "Stop watching ‹series›" on a Silo episode asks first in a dialog (Cancel takes focus), because the server drops the whole show from both rows and also on the watch services connected to the profile, such as Trakt.
- While a removal can be undone, "Undo removal" ends the menu, and the actions key anywhere on Home opens a menu with just that entry. The undo toast names the key ("To undo, press Start and choose Undo removal", from the current `nav.menu` binding: the controller's when one is connected, else the keyboard's), and its timeout waits while the menu or the dialog is open. Picking Undo after it expired says it can no longer be undone. The toast's Undo button is for the pointer and takes no keyboard focus.

Input observed for playback
- `PlayerController` filters application events to restart the "Still watching?" episode count and to cancel a sleep-timer grace countdown: key presses, including keys that match a window `Shortcut` (Qt delivers those as `ShortcutOverride` and `Shortcut` events, never as a key press) and keys synthesized for controller navigation; mouse button presses; wheel; touch and tablet presses; and pointer travel of at least 8 logical pixels from the last reference point. The pointer entering a window or a window activating resets the reference point without counting, so jitter and resent hovers do not count. Controller playback actions count through `actionTriggeredWithContext`.
- Under external mpv, Bloom sees a pause or resume made in mpv's window and the `bloom-*` script messages bound in mpv's `input.conf` (above); both count. Other scripts' messages do not, since scripts send them on their own, and seeks, volume and track changes made with mpv's own keys cannot be told apart from Bloom's commands. See [Still watching? and the sleep timer](playback.md).

Settings UI
- Settings > Input edits bindings by context and device.
- Keyboard rows capture the next key press.
- Controller rows capture the next SDL game-controller input.
- The page preserves keyboard/controller focus across context/device switches, capture completion/cancel, and reset operations.
- Conflicts are checked within the same device and runtime context. Rebinding clears the conflicting binding from the other action; conflicts across navigation and playback are allowed.
- Reset is available per action, per context, and globally.

Adding Actions
- Add action metadata and defaults in `InputBindingManager::initializeActions()`.
- Handle playback-specific actions in `EmbeddedPlaybackOverlay.qml`.
- Prefer action ids shaped as `area.name`, for example `playback.volumeUp`.
- Keep defaults conflict-free within each device/runtime-context pair.
