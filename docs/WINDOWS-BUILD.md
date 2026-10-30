# Building Bloom Packages for Windows

Windows builds are native MSVC x64 builds with the embedded `win-libmpv`
backend. There is no cross-compiled or MinGW build: Windows always uses
embedded libmpv, and only `scripts/build.ps1` fetches and verifies the pinned
mpv SDK. For a first local build, see [WINDOWS-README.md](WINDOWS-README.md).

## Pinned versions

`packaging/dependencies.json` is the single source for the Windows toolchain
inputs, and CI reads it directly:

- `qt.version` and `qt.windows_arch` (for example `6.12.0` and
  `win64_msvc2022_64`): the Qt SDK, installed with aqtinstall into
  `<root>\<version>\msvc2022_64`.
- `mpv.windows_sdk_*`: the libmpv SDK archive, its SHA-256, and mirrors.
  `build.ps1` downloads it into `third_party\mpv`, verifies the checksum, and
  checks the DLL's Dolby Vision FEL support.

CI builds on `windows-2025` with the runner image's newest Visual Studio MSVC
x64 tools and Ninja.

## Build, test, and package

From a PowerShell prompt where the MSVC x64 tools are available (for example
the "x64 Native Tools" prompt, or after `Enter-VsDevShell -Arch x64`):

```powershell
$qt = "C:\Qt\6.12.0\msvc2022_64"   # match qt.version in dependencies.json
.\scripts\build.ps1 -QtDir $qt -Generator Ninja -Clean -RequireFelCapableMpv
.\scripts\package-windows.ps1 -InstallDir install-windows -OutputDir Bloom-Windows -QtDir $qt
makensis installer.nsi
```

`build.ps1` configures `build-windows\`, installs to `install-windows\`, and
runs `windeployqt`. `package-windows.ps1` stages the portable `Bloom-Windows\`
tree with Qt, libmpv, SDL2, `qt.conf`, and `mpv-provenance.json`.
`installer.nsi` packages that tree into `Bloom-Setup-<version>.exe`.

To build and run the tests, use a separate build tree so the packaged binaries
stay free of test code (CI does the same for stable releases):

```powershell
.\scripts\build.ps1 -QtDir $qt -Generator Ninja -BuildDir build-windows-tests `
  -InstallDir install-windows-tests -Clean -BuildTests
.\scripts\run-windows-tests.ps1 -BuildDir build-windows-tests `
  -InstallDir install-windows-tests -Config Release -OutputOnFailure
```

## Installer

The installer version comes from `VERSION`. CI passes it explicitly with
`makensis /DVERSION=X.Y.Z installer.nsi`; without `/DVERSION` the script reads
the `VERSION` file. The installer and uninstaller use
`src\resources\images\app\logo.ico`.

Install:

- Requires administrator rights and installs for all users. Start menu and
  desktop shortcuts go to the all-users locations. Shortcuts that older
  installers put in the elevating administrator's profile are removed.
- Defaults to `C:\Program Files\Bloom`. An existing install's registered
  `InstallLocation` replaces that default, and `/D=` overrides both.
- Always installs into a folder named `Bloom`. A new install into a typed
  folder such as `C:\Apps` goes to `C:\Apps\Bloom`. A folder that already
  contains `Bloom.exe` is upgraded in place.
- Before copying, removes the previous version's payload, so plugins and QML
  modules dropped by a newer Qt do not linger. `package-windows.ps1` stages
  `bloom-install-manifest.txt`, which lists every packaged file and then every
  packaged directory deepest-first. The installer deletes exactly those files
  and removes a directory only once it is empty, so other files in a shared
  folder survive. An install made before the manifest existed is cleaned with
  a fixed list (`Bloom.exe`, DLLs, `qml\`, `share\`, the Qt plugin folders)
  only when its folder is named `Bloom`; in any other folder it is
  overwritten in place.
- Writes the 64-bit uninstall registry key the in-app updater checks (see
  [updates](updates.md#install-handoff)).

Uninstall removes the manifest's entries (or, for a pre-manifest install in a
`Bloom` folder, the fixed list) plus `Uninstall.exe`, then the install folder
only if it is empty. Settings in `%APPDATA%\Bloom` are kept.

## CI

The `windows` job in `.github/workflows/ci.yml` runs the steps above through
`.github/actions/windows-build` and `.github/actions/setup-nsis`, and uploads
`Bloom-Windows.zip` and the installer. The `windows-tests` job runs only for
stable tags (or a manual run with **windows_tests**) and blocks the stable
release if it fails. See [build.md](build.md#windows).
