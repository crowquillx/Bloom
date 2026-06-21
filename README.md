<div align="center">
  <img src="src/resources/images/app/logo_trans.svg" width="100" height="100" />
  <h1>Bloom</h1>
</div>

> **⚠️ WARNING: This is pre-alpha software under active development. It is NOT production ready. Expect bugs, breaking changes, and missing features. Use at your own risk.**

**This project is highly opinionated and may not suit everyone's needs, it is made specifically to solve the problems I had with other clients and doesn't really take into consideration what other people may want. For that reason, it's not something I plan on advertising anytime soon, if at all. But, if you happened to stumble across it and find it useful, feel free to let me know if you have any feedback or suggestions.**

A Jellyfin HTPC client for 10-foot TV interfaces, built with Qt 6/QML and mpv/libmpv.

## What it does

Bloom connects to a Jellyfin server and provides a TV-friendly interface for browsing and playing media. It uses mpv for video playback with `vo=gpu-next` for HDR support.

**Current state:**
- Trickplay
- Skip intro/outro (including TheIntroDB and IntroDB support)
- Sub and Audio selection UI with track selection memory
- Playback progress reporting to Jellyfin
- 10-Foot Keyboard navigation
- On-screen playback controls
- MDBList ratings
- Seerr integration
- Auto HDR and Refresh Rate switching

**Not yet working or incomplete:**
- Search (barebones implementation exists)
- Playlists
- Filtering library/search
- Gamepad support
- Many edge cases

## Requirements

- Qt 6 (Core, Gui, Quick, Network)
- CMake 3.16+
- C++23 compiler
- mpv (in PATH or configured)
- libsecret (Linux) for credential storage

## Installation

### Windows
Download the latest release from the [Releases](https://github.com/yourusername/Bloom/releases) page.
Windows release artifacts include `libmpv` runtime DLLs in the package.

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

Windows CI and `scripts/build.ps1` are pinned to `mpv-dev-x86_64-20260610-git-304426c.7z` from [shinchiro/mpv-winbuild-cmake](https://github.com/shinchiro/mpv-winbuild-cmake/releases/tag/20260610).

## Known Issues

### Linux
#### This is mildly outdated already, by default linux will use the embedded player but it is quite experimental and untested so you may want to use external. 

- Linux embedded libmpv playback (`linux-libmpv-opengl`) is currently less tested and not fully supported across all Wayland compositor/GPU combinations.
- Recommended stable Linux path today:
  - `BLOOM_PLAYER_BACKEND=external-mpv-ipc`

Examples:
```bash
# Stable path (recommended on Linux currently)
BLOOM_PLAYER_BACKEND=external-mpv-ipc nix run

# Embedded Linux validation path (experimental)
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
Built with [Qt](https://www.qt.io/), [mpv](https://mpv.io/), for [Jellyfin](https://jellyfin.org/).
Playback architecture and embedded mpv design were inspired by [Plezy](https://github.com/edde746/plezy).
