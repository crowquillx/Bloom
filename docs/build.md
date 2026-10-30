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

The package installs a lowercase `bloom` executable and desktop metadata. CMake
names the Linux executable `bloom` (`Bloom.exe` on Windows) to match the desktop
file's `Exec=bloom`, so no packaging step renames it. It uses the dependency versions pinned by `flake.lock` and the mpv stack in
`packaging/dependencies.json` / `nix/mpv-stack.nix`; it does not use libraries
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

The `BloomUI` application and visual-test modules use Qt's normal QML cache
generation; changes must continue to pass both cache generation and QML lint.

## Local checks

```bash
nix flake check --print-build-logs \
  --option max-jobs 1 --option cores 2
```

This builds Bloom and runs the deterministic unit/contract tests, QML lint,
desktop/AppStream validation, release-manifest validation, and an offscreen
Release startup/version smoke test. It also checks the deployed mpv/libmpv FEL
capabilities and dependency pins. ShellCheck validates every `scripts/*.sh` and
`packaging/portable/*.sh`, and actionlint every workflow in
`.github/workflows/`. The CI `nix`
job builds the same `.#checks.x86_64-linux.tests` derivation alongside the
other checks, so pull requests and dependency updates exercise automated
tests. Live provider/server, display, GPU, and playback contracts that require
hosts or runtime hardware remain manual validation; unavailable environments
are recorded as unavailable rather than treated as automated passes.

QML lint treats structural diagnostics such as duplicate bindings, cycles,
invalid directives, assignments in conditions, and unreachable code as
errors. Context-property and generated-type diagnostics remain visible but are
not promoted globally because several runtime-injected QML objects cannot yet
be modeled accurately by `qmllint`.

## Deep analysis

The normal pull-request workflow stays focused on the reproducible Release
build, deterministic tests, QML lint, and Windows packaging. More expensive
compiler instrumentation runs every Sunday and on demand through the `Deep
analysis` workflow:

```bash
./scripts/run-clang-tidy.sh
./scripts/run-sanitizers.sh address
./scripts/run-sanitizers.sh thread
./scripts/run-coverage.sh
```

Each script enters the pinned `.#analysis` Nix shell automatically, leaving the
normal development shell unchanged, and uses a separate ignored directory
below `build-analysis/`. The clang-tidy gate covers
the image-cache, process/IPC, display, playback-policy, and updater ownership
boundaries with high-confidence analyzer, lifetime, optional, move, and copy
checks. The address job combines ASan with UBSan; vptr instrumentation alone is
disabled because focused updater tests intentionally link a narrow
`PlayerController` stub instead of the full facade. TSAN is limited to the
process-manager and display-manager concurrency tests. The image-cache worker
remains covered by direct concurrent-access tests and ASan/UBSan; TSAN against
the system's uninstrumented Qt library reports the queued
`QMetaObject::invokeMethod` allocation handoff as a framework race, so Bloom
does not hide that report with a broad Qt-event suppression.

Coverage runs the deterministic Linux suite and writes Cobertura XML, HTML,
JSON summary, and text output to `build-analysis/coverage-report/`. Scheduled
runs publish the text totals in the GitHub Actions run summary and retain the
full report for 90 days, providing a lightweight history without adding a
third-party reporting credential. Coverage is a trend signal rather than a
line-percentage merge gate; behavior-specific regression tests remain the
primary requirement. The initial deterministic-suite baseline measured on
August 11, 2026 is 54.9% lines (16,352/29,781), 57.5% functions
(1,422/2,471), and 45.4% branches (19,633/43,247).

For custom CMake-based instrumentation, `BLOOM_SANITIZER` accepts `none`,
`address`, or `thread`, and `BLOOM_ENABLE_COVERAGE=ON` enables gcov-compatible
coverage. Sanitizer and coverage instrumentation must use separate build
trees. These options require GCC or Clang and are intentionally off for MSVC,
packaging, and release builds.

Production models, configuration, transport, provider, and network code is
compiled through reusable `Bloom::*` CMake libraries. Tests link those targets
instead of recompiling the same `.cpp` files into every executable. Keep new
production sources in the narrowest reusable layer and link tests to production
targets; do not add a second test-only compilation of shared implementation
files. A `PRIVATE BLOOM_TESTING` define on a production library may add test
hook methods, but must never change data members in a header other targets
include: consumers compiled without the define allocate a smaller object, and
the library's constructor overruns the heap in any `BUILD_TESTING=ON` build.
Clean Nix builds use Ninja, matching the incremental development path.
The Nix source sets contain only build inputs, so documentation and workflow
changes do not invalidate unrelated C++ derivations.

Measured on the pinned Release toolchain on August 10, 2026, the test build
phase fell from 13m24s at the former two-core baseline to 4m47s with reusable
targets and two-core Ninja, then to 2m46s with the four-core CI setting. All 26
Nix-selected tests passed in each optimized run.

The CI `nix` job wraps each `nix build` invocation in a retry loop with
exponential backoff. `cache.nixos.org` nar downloads occasionally drop
mid-transfer on GitHub-hosted runners (broken pipe or `HTTP 416` on a resumed
range), which cascades through `mpv-with-scripts`/`qtmultimedia` and fails
otherwise-unrelated derivations such as `qml-lint`. The retry recovers from
these transient substituter failures without code changes; a genuinely missing
path still fails after three attempts. A retry happens only when one of Nix's
own `error:` lines (not prefixed builder output) reports a download
or connection failure. Compile, lint, and test failures stop after the first
attempt instead of rebuilding the same failing derivation three times.

Each Nix derivation honors Nix's standard `cores` setting. The command above
uses two compile jobs and serializes derivations, preventing concurrent flake
checks from saturating interactive workstations. CI uses four compile jobs to
match the hosted runner; controlled builders can choose another value with
`--option cores`.

The `nix` job is a matrix of three groups, each on its own runner: `app`
(the `bloom` package plus the smoke, seek-preview, and FEL checks that use
it), `tests`, and `lint` (QML lint, automation lint, metadata, release
manifest, and dependency pins). Each group compiles Bloom separately, so
running them on one runner serialized three full builds (about 20 minutes);
in parallel the gate takes about as long as one build.

The portable Linux, Flatpak, and Windows packaging jobs start as soon as build
metadata is resolved and run in parallel with the `nix` validation job. The
`release` job depends only on `nix` and the Windows build, plus the Windows
test suite for stable tags (see [Windows](#windows)). It publishes the
GitHub release, the signed update manifest, and the Scoop update as soon as
both pass. A failing check still blocks publishing. The `release-linux` and
`release-flatpak` jobs then attach their artifacts to that release and merge
their hashes into `SHA256SUMS.txt` (see `scripts/publish-release-assets.sh`).
A failed Linux or Flatpak build marks the run as failed but does not withdraw
or block the release. Rerun the failed jobs to attach the missing assets. Each
attach job skips the upload if a newer `main` push has already moved
`dev-latest` to another commit.

A newer push cancels the superseded run only for pull requests. Runs for `main`
and tags are never cancelled, so a second push cannot stop the `release` job
after it has deleted the previous `dev-latest` release or pushed a manifest.
Every job in `ci.yml` and `update-dependencies.yml` has a `timeout-minutes`
bound, set well above its longest observed run so a cold cache still fits.

### Packaging caches

Packaging jobs reuse GitHub Actions caches so unchanged work is not rebuilt:

| Job | Cache | Key | Saved |
|---|---|---|---|
| Windows | sccache directory | per run, restores the newest | `main` pushes only |
| Flatpak | `.flatpak-builder/{cache,downloads,git}` stages | manifest + `dependencies.json` | once per key |
| Flatpak | `.flatpak-builder/ccache` (Bloom module) | per run, restores the newest | `main` pushes only |
| Portable Linux | finished `/opt/bloom-mpv` prefix archive | `build-mpv.sh` + `dependencies.json` | once per key |
| Portable Linux | ccache directory (Bloom compile) | per run, restores the newest | `main` pushes only |

Compiler caches are saved only from `main` so pull requests and tags reuse
them without filling the repository's cache quota. Build metadata
(`BLOOM_BUILD_ID`, `BLOOM_GIT_SHA`, channel) reaches only the files that
include the generated `version.h`, so the rest of the build stays cacheable;
keep it out of global compile definitions. Release MSVC flags carry no `/Zi`,
which sccache cannot cache. A restored portable mpv prefix still passes the
FEL capability check before it is used, and flatpak-builder validates its own
stage checksums, so a stale cache only causes a rebuild.

Locally, `BLOOM_PORTABLE_CACHE_DIR=<dir> nix run .#package-linux` mounts
`<dir>` at `/cache` in the packaging container to get the same reuse.
`package-flatpak` always enables flatpak-builder's ccache and caps it at
1 GB through `.flatpak-builder/ccache/ccache.conf`. It writes the
channel-patched manifest to the stable, ignored
`packaging/flatpak/.build-manifest.yml`: flatpak-builder names its stage-cache
refs after the manifest file, so a randomized name would never hit the cache.

## Portable Linux artifacts

```bash
nix run .#package-linux -- --output dist
```

Nix launches a digest-pinned Ubuntu 24.04 container and produces:

- `Bloom-<version>-linux-x86_64.AppImage`
- `bloom_<version>_amd64.deb`
- `Bloom-<version>-linux-x86_64.tar.gz`
- `SHA256SUMS-linux-x86_64.txt`

These artifacts target Ubuntu 24.04 and newer (glibc 2.39).

The tarball and AppImage contain the deployed tree: `bin/` (Bloom, the seek
preview helper, the pinned `mpv`, and a `qt.conf`), `lib/`, Qt `plugins/` and
`qml/`, and `share/`. The Debian package installs that tree under `/opt/bloom`,
so its bundled Qt, FFmpeg, libmpv, Wayland/Vulkan clients, and `mpv` never
replace or shadow distribution packages such as `mpv`. `/usr` receives only the
`/usr/bin/bloom` symlink and the desktop entry, icon, and AppStream metainfo
(`packaging/portable/build-deb.sh` fails if anything else lands there). Qt's
`applicationDirPath()` and the loader's `$ORIGIN` resolve the symlink, so Bloom
still finds `qt.conf`, its libraries, plugins, QML, and `mpv`. Packaging
installs the `.deb` in the build container and starts Bloom, `mpv`, and the
seek preview helper without `LD_LIBRARY_PATH` to prove the relocated tree is
self-contained. The packaging
container is a compatibility boundary, not an alternate development
environment. Versions, URLs, hashes, and the container digest live in
`packaging/dependencies.json`.

Portable builds compile the pinned post-FEL mpv, libplacebo, and FFmpeg stack.
They enable FFmpeg's libzimg support for HDR seek preview conversion and deploy
`bloom-seek-preview` next to `bloom`. Packaging runs the helper's `--self-test`
against the deployed libraries. The same check runs in Flatpak and Windows
packaging. See [seek previews](seek-previews.md) for cache and process ownership.
They bundle the required Wayland client libraries and Vulkan loader while
retaining host GPU drivers. The executable linker searches `/opt/bloom-mpv/lib`
first for indirect dependencies. This prevents the Qt SDK's older Wayland
client library from replacing the pinned library required by mpv.
Packaging checks the deployed executable and
libraries, and fails if the FEL capabilities are absent. See
[Dolby Vision dependencies](dolby-vision.md#dependency-audit-and-pins).

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
single-file Flatpak bundle. The canonical manifest in `packaging/flatpak/`
builds the pinned mpv, FFmpeg, libplacebo, and libass dependencies under `/app`.
It checks FEL support before creating the bundle. The old root-level manifest
has been removed to prevent builds against a different mpv stack.

The Flatpak supports network access, Secret Service, Wayland/X11, audio, GPU
rendering, and gamepad devices. Gamepads use `--device=input`, which needs
Flatpak 1.15.6 or newer on the host; older hosts ignore the permission and SDL
sees no controllers. Host shutdown/restart and arbitrary display command
settings are intentionally unavailable in the sandbox. The bundle records the
Flathub repository as its runtime source, so `flatpak install` can fetch the
KDE runtime on hosts without a Flathub remote.

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
.\scripts\build.ps1 -Clean -BuildTests -RequireFelCapableMpv
.\scripts\run-windows-tests.ps1 -Config Release -OutputOnFailure
.\scripts\package-windows.ps1 -InstallDir install-windows -OutputDir Bloom-Windows
```

`build.ps1` defaults to `-DBUILD_TESTING=OFF`. Pass `-BuildTests` to configure
and build the deterministic test targets so `run-windows-tests.ps1` has
something to run. Environment-sensitive visual tests remain opt-in through
`-BuildVisualTests`:

```powershell
.\scripts\build.ps1 -Clean -BuildTests -BuildVisualTests
.\scripts\run-windows-tests.ps1 -Config Release -OutputOnFailure
```

The Windows CI job builds and packages only the application
(`BUILD_TESTING=OFF`), so shipped binaries never contain test-only code paths.
For stable `vX.Y.Z` tags, a separate `windows-tests` job builds Bloom with
`-BuildTests` in its own `build-windows-tests/` tree and runs
`run-windows-tests.ps1`. The `release` job waits for it and does not publish a
stable release if it fails. Pull requests and `main` pushes skip the job and
never wait for it, so development builds are no slower. To run it on any
branch, start the `CI and Release` workflow manually with **windows_tests**
enabled. Run the Windows test suite locally with the commands above when
touching Windows-specific code. Both jobs share the Qt, mpv SDK, sccache, and
MSVC setup in `.github/actions/windows-build`. CI caches the checksum-pinned Qt
and mpv SDKs.
The Windows scripts and workflow read those pins from
`packaging/dependencies.json`.

The current SDK is `20261017-git-dde0671072`. The build verifies its archive
SHA-256, ignores stale cached extractions, and checks the DLL's compiled FEL
support. Packaging checks the staged runtime again and writes
`mpv-provenance.json`. Exact mpv/FFmpeg/libplacebo revisions and SDK retention
limits are in [Dolby Vision dependencies](dolby-vision.md#dependency-audit-and-pins).

Windows CI installs NSIS through `.github/actions/setup-nsis`. The action first
tries Chocolatey, then falls back to the checksum-pinned official NSIS archive
and exposes the selected `makensis.exe` path through `BLOOM_NSIS_EXE`. CI builds
the installer with `makensis /DVERSION=<version> installer.nsi`. Without
`/DVERSION`, `installer.nsi` reads the `VERSION` file. See
[WINDOWS-BUILD.md](WINDOWS-BUILD.md) for what the installer does on install,
upgrade, and uninstall.

## Arch Linux

The root `PKGBUILD` is a `bloom-jellyfin-git` package that builds the current
`main` branch with the distribution's Qt, SDL2, and mpv
(`-DBLOOM_BUNDLE_LIBMPV=OFF`). Its `pkgver()` combines `VERSION` with the
commit count and short hash, so a version bump needs no `PKGBUILD` edit. It
provides and conflicts with the old `bloom-jellyfin` package, so installing it
offers to remove that package:

```bash
makepkg -si
```

## Dependency policy

Bloom tracks current `nixos-unstable` and the latest portable/Windows
dependencies that pass builds, tests, packaging validation, and runtime smoke
tests. A scheduled workflow opens weekly flake update pull requests. GitHub
Actions updates are handled by Dependabot.

Temporary version holds must be documented in the dependency manifest or the
pull request that introduces them. The public `crowquillx-bloom` Cachix cache
only substitutes store paths whose hashes match the locked derivations; it
does not alter reproducibility. The CI, deep-analysis, and dependency-update
workflows configure it through `cachix-action`. Pull requests, and scheduled
or manual analysis and update runs outside `main`, only read from it.
The flake has no `nixConfig`, so local builds use it only after
`cachix use crowquillx-bloom`.

As of October 25, 2026, nixpkgs supplies Qt 6.12.0, matching the portable
Linux and Windows builds, which install it via aqtinstall. Windows CI pins aqtinstall to
a pre-release commit (miurahr/aqtinstall@bbfb1f7) because 3.3.0 cannot
install Qt 6.11+ on Windows (repo layout changed); revert to
`aqtinstall==3.3.*` once 3.3.1 ships.

## Release artifacts

Pull requests run validation and produce workflow artifacts, but do not publish
GitHub releases. Linux packages, the Windows installer, and `PKGBUILD` all take
their version from `VERSION`, so the bump scripts edit only that file and
generate release notes. Successful pushes to `main` replace the rolling
`dev-latest` prerelease named `Development Build`. Its tag follows the current
`main` commit, its assets use stable development filenames such as
`Bloom-Setup-dev.exe` and `bloom_dev_amd64.deb`, and the previous prerelease is
deleted before the new one is published. Linux and Flatpak assets therefore
appear a few minutes after the Windows assets. Development binaries embed a
timestamp plus short commit SHA as their build ID and never become GitHub's
latest stable release.

Stable releases are created only from matching `vX.Y.Z` tags. Use
`./scripts/bump-version.sh X.Y.Z --tag` or
`.\scripts\bump-version.ps1 X.Y.Z -Tag`, review the generated release commit
and annotated tag, then push both with `git push origin main vX.Y.Z`. CI rejects
a version tag that does not match `VERSION`, runs the Windows test suite before
publishing, publishes the committed `RELEASE_NOTES.md`, and retains versioned
artifact filenames.

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
- Live Silo/Jellyfin server, display, GPU, and Windows runtime validation remains
  manual because those hosts and runtime environments are not available in CI.
