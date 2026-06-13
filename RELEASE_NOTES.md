## What's Changed in v0.7.0

### New Features

- different cache modes for remote mounts
- add HDR and Windows mpv profile controls
- add Windows render API profiles
- normalize mpv profile imports and support rename
- import mpv config profiles
- library filters
- minor ui/ux upgrades
- add new themes and expand themeing system (#62)

### Bug Fixes

- shader path
- shader path
- **playback**: preserve multiple mpv shaders
- **player**: recover after premature mpv eof stop ordering
- **player**: recover premature mpv stream EOF
- **player**: add windows mpv termination diagnostics
- **library**: stabilize filter sorting and focus
- **player**: harden windows hdr playback startup
- hide unreliable library filters
- address library filter review feedback
- sync library filter resets
- race condition that caused the player to think it was still playing

### Chores

- **deps**: bump mpv

### Other

- feat(player):update hdr handling
- (feat): ux overhaul (#61)
- Reduce log spam with categorized logging and image filters (#60)

**Full changelog:** [v0.6.6...v0.7.0](https://github.com/crowquillx/Bloom/compare/v0.6.6...v0.7.0)
