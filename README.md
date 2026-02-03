# Bloom

> **⚠️ WARNING: This is pre-alpha software under active development. It is NOT production ready. Expect bugs, breaking changes, and missing features. Use at your own risk.**

A Jellyfin HTPC client for 10-foot TV interfaces, built with Qt 6/QML and mpv.

## What it does

Bloom connects to a Jellyfin server and provides a TV-friendly interface for browsing and playing media. It uses mpv for video playback with `vo=gpu-next` for HDR support.

**Current state:**
- Basic library browsing (movies, series, seasons, episodes)
- Skip intro/outro
- Video playback via external mpv process
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
- C++17 compiler
- mpv (in PATH or configured)
- libsecret (Linux) for credential storage

## Installation

### Windows

Download the latest release from the [Releases](https://github.com/yourusername/Bloom/releases) page.

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

## Acknowledgments

Built with [Qt](https://www.qt.io/), [mpv](https://mpv.io/), for [Jellyfin](https://jellyfin.org/).
