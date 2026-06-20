# Building and Packaging Bloom

Nix flakes are Bloom's supported development, build, test, and release
interface. The lock file pins the complete Linux toolchain and dependency
graph.

## Requirements

- Nix 2.18 or newer with flakes enabled.
- Linux x86_64.
- Podman only when producing Ubuntu-compatible portable artifacts.
- Flatpak only when producing the Flatpak bundle.

The development shell provides the optional packaging tools:

```bash
nix develop
```

## Build and run

```bash
nix build
nix run
```

The package installs a lowercase `bloom` executable and desktop metadata. It
uses the dependency versions pinned by `flake.lock`; it does not use libraries
from the host distribution.

## Checks

```bash
BLOOM_BUILD_JOBS=2 nix flake check --print-build-logs \
  --option max-jobs 1 --option cores 2
```

This builds Bloom and runs reliable unit tests, QML lint, desktop/AppStream
validation, and release-manifest validation. `SeriesDetailsCacheTest` and
`VisualRegressionTest` are currently quarantined in a separate non-blocking CI
job because they have known environment-sensitive failures.

Each Nix derivation caps C++ compilation at two concurrent jobs by default.
The command above also serializes derivations, preventing concurrent flake
checks from saturating interactive workstations.
Set `BLOOM_BUILD_JOBS` only when a controlled builder needs a different
per-derivation limit.

## Portable Linux artifacts

```bash
nix run .#package-linux -- --output dist
```

Nix launches a digest-pinned Ubuntu 24.04 container and produces:

- `Bloom-<version>-linux-x86_64.AppImage`
- `bloom_<version>_amd64.deb`
- `Bloom-<version>-linux-x86_64.tar.gz`
- `SHA256SUMS-linux-x86_64.txt`

These artifacts target Ubuntu 24.04 and newer (glibc 2.39). The packaging
container is a compatibility boundary, not an alternate development
environment. Versions, URLs, hashes, and the container digest live in
`packaging/dependencies.json`.

## Flatpak

```bash
nix run .#package-flatpak -- --output dist
```

The command installs the exact KDE SDK/runtime commits recorded in the
dependency manifest, builds a temporary local repository, and emits a
single-file Flatpak bundle.

The Flatpak supports network access, Secret Service, Wayland/X11, audio, GPU
rendering, and gamepad devices. Host shutdown/restart and arbitrary display
command settings are intentionally unavailable in the sandbox.

## Windows

Windows remains a native MSVC build because Qt and embedded libmpv are runtime
validated in that environment:

```powershell
.\scripts\build.ps1 -Clean
.\scripts\run-windows-tests.ps1 -Config Release -OutputOnFailure
.\scripts\package-windows.ps1 -InstallDir install-windows -OutputDir Bloom-Windows
```

`build.ps1` defaults to `-DBUILD_TESTING=OFF` on Windows (CI builds the
application only). Pass `-BuildTests` to configure and build the test
targets so `run-windows-tests.ps1` has something to run:

```powershell
.\scripts\build.ps1 -Clean -BuildTests
.\scripts\run-windows-tests.ps1 -Config Release -OutputOnFailure
```

The Windows scripts and GitHub workflow read Qt and libmpv pins from
`packaging/dependencies.json`.

## Dependency policy

Bloom tracks current `nixos-unstable` and the latest portable/Windows
dependencies that pass builds, tests, packaging validation, and runtime smoke
tests. A scheduled workflow opens weekly flake update pull requests. GitHub
Actions updates are handled by Dependabot.

Temporary version holds must be documented in the dependency manifest or the
pull request that introduces them. The public `crowquillx-bloom` Cachix cache
only substitutes store paths whose hashes match the locked derivations; it
does not alter reproducibility.

As of June 20, 2026, nixpkgs supplies Qt 6.11.0 while portable Linux and
Windows builds use Qt 6.12.0 via aqtinstall. Windows CI pins aqtinstall to
a pre-release commit (miurahr/aqtinstall@bbfb1f7) because 3.3.0 cannot
install Qt 6.11+ on Windows (repo layout changed); revert to
`aqtinstall==3.3.*` once 3.3.1 ships.

## Release artifacts

Pushes to `main` publish a rolling `dev-latest` release. Version tags publish
stable releases. CI attaches SHA-256 manifests and GitHub build-provenance
attestations to AppImage, Flatpak, Debian, tarball, and Windows artifacts.

## Roadmap

- Linux aarch64 artifacts.
- RPM packaging.
- Flathub submission and portal-based host power/display integration.
- Windows ARM64 and macOS.
- Restore quarantined tests as blocking checks.
