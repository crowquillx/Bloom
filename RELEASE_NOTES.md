## What's Changed in v0.6.0

### New Features

- **windows**: add auto-update system

### Bug Fixes

- **ui**: refine detail actions and startup fullscreen
- read Windows CI version from VERSION
- unify version source across build tooling
- address sidebar review feedback
- **sidebar**: resolve Qt layout polish freeze on NixOS
- **sidebar**: resolve Qt layout polish freeze on NixOS
- **qml**: address review follow-ups
- **qml**: defer heavyweight startup components
- **qml**: reduce runtime warning regressions
- **ci**: standardize notes file errors
- **ci**: normalize manifest channel input
- **ci**: validate manifest identifiers
- **updater**: tighten update state handling
- **updater**: address review nits
- **ui**: polish updater dialog focus flow
- **updater**: address review follow-ups
- **ui**: improve update navigation and labels
- **updater**: harden update checks and downloads

### Documentation

- **updates**: align updater documentation

### Build

- fetch pinned mpv sdk with curl
- use direct sourceforge mpv sdk urls
- pin windows mpv sdk version
- **nix**: add Nix package build with Qt 6.10 isolation
- **nix**: add Nix package build with Qt 6.10 isolation

### Other

- Run qmllint in Docker build and fix update dialog footer

**Full changelog:** [v0.5.2...v0.6.0](https://github.com/crowquillx/Bloom/compare/v0.5.2...v0.6.0)
