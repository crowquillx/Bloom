## What's Changed in v0.8.0

### Support

- Preserve Jellyfin as Bloom's existing provider baseline while adding **experimental native Silo support** across authentication, profiles, catalog, artwork, playback, and provider-neutral parity boundaries from the merged #111–#113 graduation work.
- Native Silo remains pinned to revision `8044eb84dd0cfa512ce8f2448cfd51cb7899a4c6`; optional native gaps and live/platform gates are explicitly unavailable until observed.
- Compatibility mode remains a separate deployment path. See [manual validation](docs/manual-validation.md) for the reproducible evidence ledger and pass criteria.

### Release labeling

- This release uses only the label **experimental native Silo support**. It does not claim first-class support: live Jellyfin/Silo validation and Windows runtime validation must be completed before that label can change.

**Full changelog:** [v0.7.1...v0.8.0](https://github.com/crowquillx/Bloom/compare/v0.7.1...v0.8.0)

## What's Changed in v0.7.1

### New Features

- add configurable gamepad/keyboard input bindings (#70)
- hero synopsis
- add screensaver (#69)
- add hero banner
- follow system audio output device and add output device selector (#64)

### Bug Fixes

- **ci**: retry nix builds on transient cache failures
- **ci**: restore development release publishing
- **ci**: harden Linux packaging pipeline
- **ci**: validate Linux packaging environments
- **ci**: repair Linux packaging jobs
- remote mount buffering

### Build

- add incremental Nix development builds

### Chores

- **deps**: bump the github-actions group with 3 updates (#71)
- **deps**: bump actions/github-script from 8.0.0 to 9.0.0 in the github-actions group (#68)
- **deps**: bump the github-actions group with 6 updates (#67)

### Other

- ci:nixification (#66)

**Full changelog:** [v0.7.0...v0.7.1](https://github.com/crowquillx/Bloom/compare/v0.7.0...v0.7.1)
