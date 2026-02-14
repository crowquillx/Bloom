<div align="center">
  <img src="src/resources/images/app/logo_trans.svg" width="100" height="100" />
  <h1>Bloom</h1>
</div>

> **⚠️ WARNING: This is pre-alpha software under active development. It is NOT production ready. Expect bugs, breaking changes, and missing features. Use at your own risk.**

A Jellyfin HTPC client for 10-foot TV interfaces, built with Qt 6/QML and mpv/libmpv.

## What it does

Bloom connects to a Jellyfin server and provides a TV-friendly interface for browsing and playing media. It uses mpv for video playback with `vo=gpu-next` for HDR support.

**Current state:**
- Basic library browsing (movies, series, seasons, episodes)
- Skip intro/outro
- Windows embedded playback via bundled `libmpv` (`win-libmpv`)
- External mpv playback path retained for Linux/non-Windows fallback
- Playback progress reporting to Jellyfin
- Keyboard/remote navigation
- Linux (Wayland) and Windows support

**Not yet working or incomplete:**
- Search (barebones implementation exists)
- Subtitle selection UI (barebones implementation exists)
- Gamepad support
- On-screen playback controls (working but needs polish)
- Many edge cases
- Build systems are very tailored towards my local setup

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

### Using Docker (recommended)

```bash
./scripts/build-docker.sh
# Binary: build-docker/src/Bloom
```

### Using Nix

```bash
nix develop
mkdir build && cd build
cmake .. -G Ninja
ninja
```

### Manual

Install Qt 6 and dependencies for your platform, then:

```bash
mkdir build && cd build
cmake .. -G Ninja
ninja
```

See [docs/build.md](docs/build.md) for detailed instructions.

### Windows local build note (`libmpv`)
For local Windows builds, provide a libmpv SDK via one of:
- `MPV_ROOT`
- `MPV_DIR`
- `LIBMPV_ROOT`

The SDK must include:
- headers (`include/mpv/client.h`)
- import library (`*.lib`)
- runtime DLL (`libmpv-2.dll` or `mpv-2.dll`)

CI currently uses `mpv-dev` artifacts from `shinchiro/mpv-winbuild-cmake`.

## Known Issues

### Linux

- Linux embedded libmpv playback (`linux-libmpv-opengl`) is currently less tested and not fully supported across all Wayland compositor/GPU combinations.
- On Wayland, Bloom currently defaults to `external-mpv-ipc` unless embedded is explicitly opted in for validation.
- Recommended stable Linux path today:
  - `BLOOM_PLAYER_BACKEND=external-mpv-ipc`

Examples:
```bash
# Stable path (recommended on Linux currently)
BLOOM_PLAYER_BACKEND=external-mpv-ipc ./build-docker/src/Bloom

# Embedded Linux validation path (experimental)
BLOOM_PLAYER_BACKEND=linux-libmpv-opengl BLOOM_ENABLE_WAYLAND_LIBMPV=1 ./build-docker/src/Bloom
```

### Windows

- Windows hi-dpi scaling causes issues.

## Configuration

Config file location:
- Linux: `~/.config/Bloom/app.json`
- Windows: `%APPDATA%/Bloom/app.json`

Credentials are stored in the platform keychain, not in the config file.

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
