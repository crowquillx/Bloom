## What's Changed in v0.6.5

### New Features

- add media segment providers in app and support on server (#53)
- extend skip popup duration setting
- **playback**: runtime external subtitle injection (PlayerController::addExternalSubtitleTrack) (#52)
- **home**: split Continue Watching from Next Up with merge setting (#51)
- **playback**: add global track language defaults (#48)
- add buffered cache progress indicator to playback overlay

### Bug Fixes

- **playback**: handle missing next episode (#50)
- **tests**: add missing include paths + docs: Cursor Cloud setup instructions (#49)
- **network**: use BLOOM_VERSION in Jellyfin auth header instead of hardcoded 1.0.0
- **ui, config**: ensure spinbox value is committed on destruction and trace cache size

**Full changelog:** [v0.6.4...v0.6.5](https://github.com/crowquillx/Bloom/compare/v0.6.4...v0.6.5)
