# Updates

## Overview
- Bloom currently supports first-party update checks on all builds.
- Automatic in-app install is phase-1 Windows only and only for NSIS-installed builds.
- Portable Windows builds and non-Windows builds stay in notify-only mode and expose direct download links instead.

## Channels
- `stable`: latest non-prerelease tagged release.
- `dev`: rolling `dev-latest` release built from `main`.

## Runtime behavior
- Bloom can check for updates automatically at startup when `settings.updates.auto_check_enabled` is true. This is opt-in for now and defaults to false.
- The startup popup is startup-only; after dismissal the update remains visible through the sidebar update entry and Settings > Updates.
- Manual `Check for updates` is always available from Settings, even on notify-only builds.

Manifest format
- Bloom reads JSON manifests from the `update-manifests` branch by default.
- Files:
  - `stable.json`
  - `dev.json`

Example shape:

```json
{
  "channel": "stable",
  "version": "0.5.2",
  "build_id": "0.5.2",
  "release_tag": "v0.5.2",
  "published_at": "2026-03-27T00:00:00Z",
  "notes": "Release notes text",
  "installer": {
    "url": "https://github.com/crowquillx/Bloom/releases/download/v0.5.2/Bloom-Setup-0.5.2.exe",
    "filename": "Bloom-Setup-0.5.2.exe",
    "sha256": "..."
  },
  "portable": {
    "url": "https://github.com/crowquillx/Bloom/releases/download/v0.5.2/Bloom-Windows.zip",
    "filename": "Bloom-Windows.zip",
    "sha256": "..."
  },
  "rollout": {
    "enabled": true,
    "minimum_supported_version": "0.5.2"
  }
}
```

Windows apply flow
- Bloom downloads the official installer to `%LOCALAPPDATA%/Bloom/updates/<channel>/`.
- The download is SHA-256 checked against the manifest before launch.
- NSIS-installed Windows builds running from their registered install location can launch the installer in elevated silent mode, targeting that same install directory, and then Bloom exits.
- Portable Windows builds and non-Windows builds stay in notify-only mode.

Future AppImage support
- The update logic is split into provider and applier layers so an AppImage applier can be added later without redesigning the UI, config keys, or manifest format.
