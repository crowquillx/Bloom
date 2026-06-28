Input Bindings

Overview
- Bloom routes configurable keyboard and controller shortcuts through `InputBindingManager`.
- Bindings are grouped by context: Navigation, Playback, and Advanced Playback.
- Existing keyboard defaults remain code-owned defaults; user config stores only overrides.

Storage
- Overrides are stored in `settings.input_bindings`.
- Schema:
  - `schema`: binding schema version, currently `1`.
  - `keyboard`: object keyed by action id, each value an array of normalized key bindings.
  - `gamepad`: object keyed by action id, each value an array of normalized controller bindings.
- An omitted action uses the built-in default. An empty array intentionally unassigns that action.

Runtime
- `InputBindingManager.actionForKeyboardEvent(key, modifiers)` maps QML key events to action ids.
- SDL2 gamepad input is polled in C++ when SDL2 is available at build time.
- Controller navigation actions synthesize the same Qt key events already used by QML focus handlers.
- Playback controller actions emit `InputBindingManager.actionTriggered(actionId)` and are handled by `EmbeddedPlaybackOverlay.qml`.

Settings UI
- Settings > Input edits bindings by context and device.
- Keyboard rows capture the next key press.
- Controller rows choose from known SDL game-controller inputs.
- Reset is available per action, per context, and globally.

Adding Actions
- Add action metadata and defaults in `InputBindingManager::initializeActions()`.
- Handle playback-specific actions in `EmbeddedPlaybackOverlay.qml`.
- Prefer action ids shaped as `area.name`, for example `playback.volumeUp`.
- Keep defaults conflict-free within each device map.
