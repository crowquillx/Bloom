# Building and Packaging Bloom

Nix flakes are Bloom's supported development, build, test, and release
interface. The lock file pins the complete Linux toolchain and dependency
graph.

## Requirements

- Nix 2.18 or newer with flakes enabled.
- Linux x86_64.
- Podman (the local default) or Docker when producing Ubuntu-compatible
  portable artifacts.
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

## Fast incremental development builds

For local edit/build cycles, use the persistent Ninja build wrapper:

```bash
./scripts/dev-build.sh
./scripts/dev-build.sh --run
```

The script enters `nix develop` automatically, configures `build-dev/`, enables
ccache, supplies the pinned Qt plugin/QML runtime paths, and rebuilds only
changed targets on later runs. Useful options include:

```bash
./scripts/dev-build.sh --tests
./scripts/dev-build.sh --build-type RelWithDebInfo
./scripts/dev-build.sh --clean
./scripts/dev-build.sh --jobs 4
```

`build-dev/` is intentionally outside the Nix store and is ignored by Git.
Use `nix build` and `nix flake check` for clean reproducibility and final
verification.

## Local checks

```bash
BLOOM_BUILD_JOBS=2 nix flake check --print-build-logs \
  --option max-jobs 1 --option cores 2
```

This builds Bloom and runs unit tests, QML lint, desktop/AppStream validation,
and release-manifest validation. Tests are currently local-only; CI builds the
application and runs the non-test validation derivations.

The CI `nix` job wraps each `nix build` invocation in a retry loop with
exponential backoff. `cache.nixos.org` nar downloads occasionally drop
mid-transfer on GitHub-hosted runners (broken pipe or `HTTP 416` on a resumed
range), which cascades through `mpv-with-scripts`/`qtmultimedia` and fails
otherwise-unrelated derivations such as `qml-lint`. The retry recovers from
these transient substituter failures without code changes; a genuinely missing
path still fails after three attempts.

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

Set `BLOOM_CONTAINER_ENGINE=docker` to use Docker instead of Podman. CI uses
Docker because rootless Podman re-exec is restricted on GitHub-hosted runners.
The packaging wrapper converts the manifest's `docker://` transport reference
to Docker's native image-reference format when Docker is selected.

## Flatpak

```bash
nix run .#package-flatpak -- --output dist
```

The command installs the exact KDE SDK/runtime commits recorded in the
dependency manifest, builds a temporary local repository, and emits a
single-file Flatpak bundle. The manifest builds mpv's required libass and
libplacebo dependencies explicitly instead of relying on incidental SDK
contents.

The Flatpak supports network access, Secret Service, Wayland/X11, audio, GPU
rendering, and gamepad devices. Host shutdown/restart and arbitrary display
command settings are intentionally unavailable in the sandbox.

Flatpak Builder requires unprivileged user and network namespaces. Ubuntu
24.04 GitHub-hosted runners restrict these through AppArmor by default, so the
Flatpak job disables `kernel.apparmor_restrict_unprivileged_userns` on its
ephemeral runner and verifies a minimal Bubblewrap sandbox before building.

Portable artifacts bundle Qt's XCB and Wayland platform plugins. Packaging
validates that both platform backends exist, rejects unresolved ELF
dependencies, and smoke-tests Bloom and mpv before publishing artifacts.

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

Windows CI installs NSIS through `.github/actions/setup-nsis`. The action first
tries Chocolatey, then falls back to the checksum-pinned official NSIS archive
and exposes the selected `makensis.exe` path through `BLOOM_NSIS_EXE`.

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

Pull requests run validation and produce workflow artifacts, but do not publish
GitHub releases. Successful pushes to `main` replace the rolling
`dev-latest` prerelease named `Development Build`. Its tag follows the current
`main` commit, its assets use stable development filenames such as
`Bloom-Setup-dev.exe` and `bloom_dev_amd64.deb`, and the previous prerelease is
deleted before the new one is published. Development binaries embed a
timestamp plus short commit SHA as their build ID and never become GitHub's
latest stable release.

Stable releases are created only from matching `vX.Y.Z` tags. Use
`./scripts/bump-version.sh X.Y.Z --tag` or
`.\scripts\bump-version.ps1 X.Y.Z -Tag`, review the generated release commit
and annotated tag, then push both with `git push origin main vX.Y.Z`. CI rejects
a version tag that does not match `VERSION`, publishes the committed
`RELEASE_NOTES.md`, and retains versioned artifact filenames.

Both release channels attach SHA-256 manifests and GitHub build-provenance
attestations to AppImage, Flatpak, Debian, tarball, and Windows artifacts. Once
published, CI dispatches the Windows ZIP URL, SHA-256, and version to
`crowquillx/scoop-bloom`: main builds update `bloom-dev.json`, while version
tags update `bloom.json`. This requires the `SCOOP_REPO_TOKEN` repository
secret.

## Roadmap

- Linux aarch64 artifacts.
- RPM packaging.
- Flathub submission and portal-based host power/display integration.
- Windows ARM64 and macOS.
- Restore automated tests when the CI strategy is revisited.
