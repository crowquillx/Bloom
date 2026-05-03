## What's Changed in v0.6.4

### New Features

- aggressive playback caching and auto-recovery on server outage (#47)
- **ui**: add playback cache size control to settings
- **player**: add auto-recovery mechanism for server outages
- **network**: add server ping health check to LibraryService
- **config**: add playback cache size and auto-recovery settings

### Bug Fixes

- **config**: pass demuxer cache sizes with unit suffix to avoid int overflow
- **player**: clear recovery context on 401 auth expiry
- **player**: prevent recovery reply abort from restarting timer and guard segment URL
- **player**: clear stale recovery context on non-recoverable errors and queue segment restoration
- **player, config**: address recovery review feedback
- **player**: guard recovery resume against in-flight terminal Stop transition
- **player**: recovery only for network errors, multipart support, stop from Error
- **network**: isolate ping reply and handle 401 auth expiry
- **config**: clamp playback cache size to [50, 2048] on read and write
- **playback**: verify exact refresh before fps override
- **playback**: improve refresh-rate frame pacing
- **playback**: resolve library profile via ancestors for home autoplay

### Documentation

- **playback**: document cache and recovery behavior

**Full changelog:** [v0.6.3...v0.6.4](https://github.com/crowquillx/Bloom/compare/v0.6.3...v0.6.4)
