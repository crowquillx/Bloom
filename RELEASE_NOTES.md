## What's Changed in v0.5.0

### New Features

- **ui**: add playback version selection dialog
- **player**: resolve multipart playback and version affinity
- **player**: add playlist support for multipart playback
- **network**: load additional parts and media source metadata
- **playback**: resolve next episodes canonically

### Bug Fixes

- **playback**: address final review follow-ups
- **playback**: address review follow-ups
- **player**: address multipart playback review feedback
- **ui**: handle sparse episode selection data
- **ui**: align episode selection fallback data
- **playback**: preserve canonical user data
- **ui**: preserve up next return navigation
- **ui**: wait for target season before next up selection
- package SeerrComboBox in QML module
- **playback**: clean autoplay wait state
- **track-memory**: retry failed autosaves safely
- **playback**: preserve autoplay series context
- **track-memory**: avoid autosave retry loops
- **playback**: preserve autoplay subtitle intent
- **playback**: stabilize autoplay track fallback
- **track-memory**: harden preference persistence
- **ui**: stabilize next-up episode focus and selection

### Refactoring

- **playback**: rename resolver entry members
- **playback**: remove dead season cleanup

### Documentation

- **playback**: document canonical next episode resolution

### Build

- **tests**: copy episode selection script after build

### Other

- Use output handles to detect inherited Windows console
- Build Bloom as Windows GUI app and reattach parent console
- Refactor track preference persistence and autoplay track resolution
- v0.4.2

**Full changelog:** [v0.4.2...v0.5.0](https://github.com/crowquillx/Bloom/compare/v0.4.2...v0.5.0)
