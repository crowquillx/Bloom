## What's Changed in v0.5.2

### New Features

- **season-details**: rework season details

### Bug Fixes

- **playback**: honor specials in up next
- refresh season episodes on explicit playback return
- restore episode selection and watched state after playback
- **player**: report Jellyfin stop on early playback exit
- isolate series episode detail requests
- **season-details**: scope episode error cleanup
- **season-details**: tighten season restore state
- **cache**: prefer fresh memory detail cache
- **series-details**: recover episode detail loading
- **ui**: harden playback detail interactions
- **detail-cache**: respect stale cache freshness
- **ci**: skip linuxdeploy strip during AppImage packaging
- **scripts**: update push command to specify version tag for release

### Refactoring

- remove package version pinning and update mirrorlist for build dependencies
- enhance PlayerController to handle series details loading and caching
- change type of target in renderFrame to QQuickItem for better clarity
- add IMDb ID handling to SeriesDetailsViewModel; clear IMDb ID on cache clear
- add LinguistTools module to Qt prerequisites and update CI to include qt6-tools-dev
- consolidate rating functions into RatingMetadataChip; streamline SeriesDetailsViewModel logic and improve cleanup in tests
- remove redundant height and width assignments in MetadataChip component
- update CI workflow to streamline Qt installation and environment setup; remove unnecessary steps and dependencies
- update UI components for consistency; enhance theme properties and improve cache handling
- update CI workflow for improved Qt installation and caching; enhance detail view logo handling in theme and UI components
- update similar items loading logic and add retry tests
- streamline metadata extraction and similar items mapping in view models
- integrate ExternalRatingsHelper for external rating fetch logic

### Chores

- **ci**: remove AppImage packaging

### Other

- Refactor caching mechanism for detail views

**Full changelog:** [v0.5.1...v0.5.2](https://github.com/crowquillx/Bloom/compare/v0.5.1...v0.5.2)
