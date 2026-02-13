# Build Guide

This guide covers building Bloom from source on Linux and Windows.

## Overview

Bloom uses CMake as its build system and supports multiple build methods:
- **Docker/Podman** (recommended for Linux): Containerized builds with all dependencies
- **Native Linux**: Direct compilation using system Qt6 packages
- **Windows Cross-compilation**: MinGW toolchain from Linux
- **Windows Installer**: NSIS-based installer creation

## Prerequisites

### Linux (Native Build)
- **Qt 6.10+** with modules: Core, Gui, Quick, QuickControls2, Network, Sql, Concurrent, Multimedia, Test, ShaderTools
- **CMake 3.16+**
- **Ninja** (recommended) or Make
- **mpv** (runtime dependency)
- **libmpv development package** (required for Linux embedded backend milestone work; package name varies by distro)
- **libsecret-1** (for secure credential storage)
- **C++23-capable compiler** (GCC 13+, Clang 16+)

Run `./scripts/check-deps.sh` to verify your system has compatible versions.

### Docker/Podman Build
- **Docker** or **Podman** container runtime
- No other dependencies needed (all handled in container)

### Windows Cross-compilation (from Linux)
- **mingw-w64-gcc**
- **mingw-w64-cmake**
- **mingw-w64-qt6-base**
- **mingw-w64-qt6-declarative**

On Arch Linux:
```bash
sudo pacman -S mingw-w64-gcc mingw-w64-cmake mingw-w64-qt6-base mingw-w64-qt6-declarative
```

### Windows Installer
All Windows cross-compilation prerequisites plus:
- **NSIS** (Nullsoft Scriptable Install System)

On Arch Linux:
```bash
sudo pacman -S nsis
```

On Ubuntu:
```bash
sudo apt install nsis
```

## Build Steps

## Known Limitations (Linux)

- Linux embedded libmpv playback (`linux-libmpv-opengl`) is still validation-in-progress and is not fully supported across all compositor/GPU combinations.
- Wayland sessions currently default to `external-mpv-ipc` unless explicitly opted in for embedded validation.
- Recommended stable Linux path today: `external-mpv-ipc`.

Backend selection examples:
```bash
# Stable fallback path (recommended on Linux currently)
BLOOM_PLAYER_BACKEND=external-mpv-ipc ./build-docker/src/Bloom

# Force embedded Linux backend for validation/debug only
BLOOM_PLAYER_BACKEND=linux-libmpv-opengl BLOOM_ENABLE_WAYLAND_LIBMPV=1 ./build-docker/src/Bloom
```

### Docker/Podman (Recommended for Linux)

The `build-docker.sh` script handles containerized builds with automatic dependency management:

```bash
# Standard build
./scripts/build-docker.sh

# Clean rebuild (removes build-docker/ directory)
./scripts/build-docker.sh --clean

# Cross-compile for Windows
./scripts/build-docker.sh --windows
```

**How it works:**
1. Detects available container runtime (Podman preferred, falls back to Docker)
2. Builds the `bloom-build` container image from the Dockerfile
3. Mounts the project directory at `/workspace` inside the container
4. Runs the build, preserving `build-docker/` (or `build-docker-windows/`) on the host for incremental builds
5. Outputs the executable to `build-docker/src/Bloom` (or `build-docker-windows/src/Bloom.exe`)

**Testing the build:**
```bash
# Linux build
./build-docker/src/Bloom

# Windows build (requires Wine)
wine ./build-docker-windows/src/Bloom.exe
```

### Native Linux Build

For development or when Docker/Podman is unavailable:

```bash
# Check dependencies first
./scripts/check-deps.sh

# Configure and build
mkdir -p build && cd build
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja

# Run
./src/Bloom
```

**Build types:**
- `Release`: Optimized for performance (recommended for testing)
- `Debug`: Includes debug symbols, no optimization
- `RelWithDebInfo`: Optimized with debug symbols

### Windows Cross-compilation

The `build-windows.sh` script cross-compiles for Windows from Linux:

```bash
./scripts/build-windows.sh
```

**What it does:**
1. Cleans previous `build-windows/` and `install-windows/` directories
2. Configures CMake with the MinGW toolchain (`/usr/share/mingw/toolchain-x86_64-w64-mingw32.cmake`)
3. Builds in parallel using all CPU cores
4. Installs to `install-windows/` staging directory

**Output:** `install-windows/bin/Bloom.exe`

**Next step:** Run `./scripts/package-windows.sh` to bundle Qt DLLs and dependencies.

### Native Windows test execution

After a native Windows build via `./scripts/build.ps1`, run tests through:

```powershell
.\scripts\run-windows-tests.ps1 -Config Release -OutputOnFailure
```

For targeted backend migration validation:

```powershell
.\scripts\run-windows-tests.ps1 -Config Release -OutputOnFailure -Regex "(PlayerBackendFactoryTest|PlayerControllerAutoplayContextTest)"
```

### Windows Installer

The `build-windows-installer.sh` script creates a complete Windows installer:

```bash
./scripts/build-windows-installer.sh
```

**Pipeline:**
1. Runs `build-windows.sh` (cross-compilation)
2. Runs `package-windows.sh` (DLL bundling)
3. Runs `makensis installer.nsi` (NSIS installer creation)

**Output:** `Bloom-Setup-0.1.0.exe`

**Testing:**
```bash
# On Linux with Wine
wine Bloom-Setup-0.1.0.exe

# On Windows
# Copy the .exe to a Windows machine and run natively
```

## Troubleshooting

### Missing Qt Dependencies
**Symptom:** CMake fails with "Could not find Qt6" or missing Qt modules.

**Solution:**
- Ensure Qt 6.10+ is installed with all required modules (see Prerequisites)
- On Arch: `sudo pacman -S qt6-base qt6-declarative qt6-multimedia qt6-shadertools`
- On Ubuntu: `sudo apt install qt6-base-dev qt6-declarative-dev qt6-multimedia-dev qml6-module-*`
- Set `CMAKE_PREFIX_PATH` if Qt is in a non-standard location:
  ```bash
  cmake .. -DCMAKE_PREFIX_PATH=/path/to/qt6
  ```

### libsecret Not Found (Linux)
**Symptom:** CMake error: "Could not find libsecret-1".

**Solution:**
- On Arch: `sudo pacman -S libsecret`
- On Ubuntu: `sudo apt install libsecret-1-dev`

### libmpv Development Files Missing (Linux)
**Symptom:** Configure output warns that libmpv development package was not found and Linux embedded backend remains scaffold-only.

**Solution:**
- On Arch: install an mpv package that provides `pkg-config` metadata + `libmpv` headers/runtime (for example `mpv` or `mpv-full`, depending on your setup)
- On Ubuntu: `sudo apt install libmpv-dev`
- Re-run the project build script after installing dependencies.

**Verify quickly:**
```bash
pkg-config --modversion mpv
pkg-config --libs mpv
```

If both commands work, CMake should detect libmpv.

### Linux libmpv Bundling (Current)
When `pkg-config mpv` is detected, the Linux build now:
- links against libmpv (`BLOOM_HAS_LIBMPV=1`)
- copies the resolved `libmpv` runtime library next to `build-docker/src/Bloom` after build
- installs that same `libmpv` runtime into `bin/` with the app

This is the first step toward release bundling parity with Windows; dependency-chain bundling/CI packaging hardening is still planned.

### MinGW Toolchain Issues
**Symptom:** `build-windows.sh` fails with "x86_64-w64-mingw32-cmake: command not found".

**Solution:**
- Install MinGW packages (see Windows Cross-compilation prerequisites)
- Verify toolchain file exists: `/usr/share/mingw/toolchain-x86_64-w64-mingw32.cmake`

### Docker/Podman Permission Denied
**Symptom:** `scripts/build-docker.sh` fails with "permission denied while trying to connect to the Docker daemon socket".

**Solution:**
- Add your user to the `docker` group: `sudo usermod -aG docker $USER`
- Log out and back in for group changes to take effect
- Or use Podman (rootless by default): `sudo pacman -S podman`

### NSIS Not Found
**Symptom:** `scripts/build-windows-installer.sh` fails with "makensis: command not found".

**Solution:**
- Install NSIS (see Windows Installer prerequisites)

### Build Artifacts Stale
**Symptom:** Changes not reflected after rebuild.

**Solution:**
- For Docker builds: `./scripts/build-docker.sh --clean`
- For native builds: `rm -rf build && mkdir build && cd build && cmake .. && ninja`

### Windows tests fail with 0xc0000135
**Symptom:** `ctest` reports `0xc0000135` for tests that do not launch.

**Solution:**
- Run tests via `./scripts/run-windows-tests.ps1` to prepend deployed runtime directories (`install-windows/bin`, build output paths, and Qt bin when available).

## Dependencies

Bloom's dependencies are managed through:
- **CMakeLists.txt**: Defines Qt6 modules and platform-specific requirements (libsecret on Linux)
- **Dockerfile**: Specifies container image dependencies for Docker builds
- **check-deps.sh**: Validates local system dependencies for native builds

**Key dependencies:**
- **Qt6**: UI framework (QML, Quick, Network, Multimedia)
- **mpv**: Media playback engine (external process, JSON IPC)
- **libsecret-1** (Linux): Secure credential storage via GNOME Keyring
- **Windows Credential Manager** (Windows): Secure credential storage (native API)

## See Also

- [AGENTS.md](file:///mnt/hdd/repo/Reef/AGENTS.md): Project architecture and conventions
- [docs/developer_notes.md](file:///mnt/hdd/repo/Reef/docs/developer_notes.md): Development best practices
- [docs/services.md](file:///mnt/hdd/repo/Reef/docs/services.md): Service architecture
