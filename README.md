<div align="center">
  <img src="src/resources/images/app/logo_trans.svg" width="100" height="100" />
  <h1>Bloom</h1>
</div>

> **⚠️ WARNING: This is pre-alpha software under active development. It is NOT production ready. Expect bugs, breaking changes, and missing features. Use at your own risk.**

**This project is highly opinionated and may not suit everyone's needs, it is made specifically to solve the problems I had with other clients and doesn't really take into consideration what other people may want. For that reason, it's not something I plan on advertising anytime soon, if at all. But, if you happened to stumble across it and find it useful, feel free to let me know if you have any feedback or suggestions.**

A media client for Jellyfin and Silo servers, built for 10-foot TV interfaces with Qt 6/QML and mpv/libmpv.

## What it does

Bloom connects to a Jellyfin or Silo server and provides a TV-friendly interface for browsing and playing media. Native Silo support is experimental and has not been validated against a live Silo server. It uses mpv for video playback with `vo=gpu-next` for HDR support.

**Current state:**
- Trickplay
- Skip intro/outro (including TheIntroDB and IntroDB support)
- Sub and Audio selection UI with track selection memory
- Playback progress reporting to the connected server
- 10-Foot Keyboard navigation
- Configurable gamepad navigation (implemented; real-controller validation pending)
- Search across connected provider libraries
- On-screen playback controls
- MDBList ratings
- Seerr integration
- Auto HDR and Refresh Rate switching

**Not yet working or incomplete:**
- Playlists
- Many edge cases

## Requirements

- Qt 6 (Core, Gui, Quick, Network)
- CMake 3.16+
- C++23 compiler
- mpv (in PATH or configured)
- libsecret (Linux) for credential storage

## Installation

### Windows

[Scoop](https://scoop.sh) is the recommended way to install Bloom on Windows.
Add the Bloom bucket once, then install one of the two channels:

```powershell
scoop bucket add bloom https://github.com/crowquillx/scoop-bloom

# Stable: tagged releases (recommended)
scoop install bloom/bloom

# Dev: rolling builds from the latest main commit
scoop install bloom/bloom-dev
```

| Channel | Scoop app | Built from | Updates |
|---------|-----------|------------|---------|
| Stable | `bloom` | `vX.Y.Z` release tags | When a new version is released |
| Dev | `bloom-dev` | The latest `main` commit (the `dev-latest` prerelease) | After every push to `main`; newest fixes and features, least tested |

Update with `scoop update bloom` or `scoop update bloom-dev`
(`scoop update *` updates everything). Install only one channel at a time,
because both provide the same `bloom` command and Start menu shortcut. To switch
channels, uninstall one and install the other:

```powershell
scoop uninstall bloom
scoop install bloom/bloom-dev
```

Settings live in `%APPDATA%/Bloom`, outside the Scoop app directory, so they
survive updates, channel switches, and reinstalls. Bloom does not replace a
Scoop install itself: when it finds an update, it shows the `scoop update`
command to run.

Other options, from the [Releases](https://github.com/crowquillx/Bloom/releases) page:

- `Bloom-Setup-X.Y.Z.exe` installer: Bloom's built-in updater can download and
  install new versions for you (Settings → About & Account).
- `Bloom-Windows.zip` portable build: unzip and run; Bloom notifies you of
  updates but does not install them.

Every Windows package includes the `libmpv` runtime DLLs.

## Building

### Using Nix (supported)

```bash
nix build
nix run
nix flake check
```

Portable Linux release artifacts:

```bash
nix run .#package-linux -- --output dist
nix run .#package-flatpak -- --output dist
```

See [docs/build.md](docs/build.md) for development shells, packaging,
dependency policy, compatibility targets, and Windows instructions.

### Windows local build note (`libmpv`)
For local Windows builds, provide a libmpv SDK via one of:
- `MPV_ROOT`
- `MPV_DIR`
- `LIBMPV_ROOT`

The SDK must include:
- headers (`include/mpv/client.h`)
- import library (`*.lib`)
- runtime DLL (`libmpv-2.dll` or `mpv-2.dll`)

Windows CI and `scripts/build.ps1` pin `mpv-dev-x86_64-20261017-git-dde0671072.7z`
from [shinchiro/mpv-winbuild-cmake](https://github.com/shinchiro/mpv-winbuild-cmake/releases/tag/20261017).
Nix, portable Linux, and Flatpak use the matching post-merge mpv source stack.
See [Dolby Vision and FEL](docs/dolby-vision.md) for exact dependency pins,
capability checks, and backend limits.

## Known Issues

### Linux
Linux uses `external-mpv-ipc` automatically on Wayland; embedded libmpv playback (`linux-libmpv-opengl`) remains experimental and validation-only. Unsupported embedded runtime conditions fall back to external mpv IPC.

- Recommended stable Linux path:
  - `BLOOM_PLAYER_BACKEND=external-mpv-ipc`

Examples:
```bash
# Stable path (recommended on Linux currently)
BLOOM_PLAYER_BACKEND=external-mpv-ipc nix run

# Embedded Linux validation-only path (experimental)
BLOOM_PLAYER_BACKEND=linux-libmpv-opengl BLOOM_ENABLE_WAYLAND_LIBMPV=1 nix run
```

### Windows

- Windows hi-dpi scaling causes issues.

## Configuration

Config file location:
- Linux: `~/.config/Bloom/app.json`
- Windows: `%APPDATA%/Bloom/app.json`

## Controls

- Arrow keys: Navigate
- Enter: Select/Play
- Escape/Backspace: Go back
- Ctrl+Q: Quit
- F11: Toggle fullscreen

## Project Structure

See [AGENTS.md](AGENTS.md) for architecture details.

## License

See [LICENSE](LICENSE).
Third-party license and attribution details are in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Acknowledgments
Built with [Qt](https://www.qt.io/), [mpv](https://mpv.io/), for [Jellyfin](https://jellyfin.org/) and Silo servers.
Playback architecture and embedded mpv design were inspired by [Plezy](https://github.com/edde746/plezy).
