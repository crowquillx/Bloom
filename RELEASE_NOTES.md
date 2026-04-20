## What's Changed in v0.6.1

### New Features

- **settings**: introduce rail-panel architecture with per-section pages

### Bug Fixes

- harden replacement playback and display restore flows
- harden playback stop and HDR restore flow
- **playback**: harden windows embedded stop teardown
- harden settings rail selection sync
- restore settings saved toast
- preserve default mpv profile selection
- repair mpv settings qml syntax
- address settings review findings
- **settings**: normalize layouts, focus rings, and keyboard navigation
- remove Qt::UniqueConnection flag from lambda slot connections
- **ui**: persist update channel on keyboard selection and use theme tokens
- **ui**: restyle updates settings section for consistency and keyboard nav

### Other

- Fix accent bar fade-out animation in SettingsRail (#36)

**Full changelog:** [v0.6.0...v0.6.1](https://github.com/crowquillx/Bloom/compare/v0.6.0...v0.6.1)
