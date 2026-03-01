## What's Changed in v0.4.2

### New Features

- **playback**: rely on server watched-state and enrich playstate reports

### Bug Fixes

- **player**: trigger Up Next on thresholded manual stop
- **player**: guard next-episode loopback and parse play method URL
- **player**: reset seek-progress latch and classify direct stream
- **player**: report progress after seek position update

### Refactoring

- **player**: share completion threshold predicate

### Documentation

- **playback**: clarify threshold and reporting behavior

**Full changelog:** [v0.4.1...v0.4.2](https://github.com/crowquillx/Bloom/compare/v0.4.1...v0.4.2)
