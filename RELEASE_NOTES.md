## What's Changed in v0.4.0

### New Features

- **search**: add async seerr results and request dialog
- **seerr**: add service layer and app wiring
- **settings**: add seerr url/api key and third-party section
- Introduce an "Up Next" interstitial screen to manage post-playback navigation and autoplay.
- add 'rewatch' button to the seriesdetails view

### Bug Fixes

- **seerr**: stabilize per-season checkbox state sync
- **seerr**: restore season grid toggle behavior
- **seerr**: sync all-seasons toggle and season visibility
- **seerr**: enlarge dialog and show season grid via scroll
- **seerr**: correct popup enter handling and focus scrolling
- **seerr**: complete modal keyboard actions and focus visibility
- **seerr**: restore modal keyboard focus and dropdown styling
- **seerr**: correct settings and modal keyboard navigation
- Fix autoplay next-episode ordering/season context and harden SettingsScreen countdown sync, focus-disabled styling and PlayerController docs/state cleanup
- fix autoplay state cleanup and index clamping; realign PlayerController docs
- **autoplay/up-next**: preserve saved tracks and harden Up Next/input flow
- various bugfixes
- exclude current episode from filtering when looking for next up
- load next up faster so there is no flash after episode plays
- **ui**: adjust cropping on season posters to make sure we arent cropping

### Documentation

- add Doxygen/JSDoc docstrings to all seerr-branch changed files
- add Conventional Commits guidelines for commit messages
- add note on using Conventional Commits for commits
- **seerr**: add integration and service/config references
- update README to clarify incomplete features and playback options

### CI

- fix setup-nsis PowerShell interpolation
- verify NSIS fallback installer checksum

### Other

- close combo popup on outside click
- fix combo fallback for simple models and similar results media type
- seerr-ui: add reusable combo and delayed success close
- defer Seerr grid focus and fix loading fade
- fix validation errors and similar result mapping
- Harden CI NSIS setup with retry fallback
- Guard Seerr submit inputs and text cursor mode
- Fix Seerr search result refresh and focus restore
- Fix Seerr validation/config handling
- 📝 Add docstrings to `nextup`

**Full changelog:** [v0.3.3...v0.4.0](https://github.com/crowquillx/Bloom/compare/v0.3.3...v0.4.0)
