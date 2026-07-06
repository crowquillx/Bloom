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
- SDL2 gamepad input is polled in C++ when SDL2 is available at build time. The backend handles hotplug and disconnect, reports `gamepadAvailable`, and repeats held navigation/seek/volume inputs while keeping toggles edge-triggered.
- Controller navigation actions synthesize the same Qt key events already used by QML focus handlers.
- Playback controller actions emit `InputBindingManager.actionTriggeredWithContext(actionId, "playback")` and are handled by `EmbeddedPlaybackOverlay.qml`.
- Default controller playback bindings: D-pad/left stick move or seek based on overlay state, A selects the focused playback control or active Skip Intro/Credits prompt, B mirrors Escape dismissal/stop behavior, Start play/pause, LB/RB seek, LT/RT chapters, Y audio selector, X subtitle selector, Back/View volume panel, right stick volume, L3 subtitle ASS override, R3 deband.
- In the subtitle selector, D-pad/left stick moves through subtitle tracks and the delay controls. The delay row adjusts the current season/movie subtitle delay in 1 ms steps and persists it with subtitle preferences.

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
