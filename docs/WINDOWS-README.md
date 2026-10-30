# Building Bloom on Windows

This guide explains how to build Bloom natively on Windows using the provided PowerShell script.

## Prerequisites

Before you begin, ensure you have the following installed:

1.  **CMake**: [Download CMake](https://cmake.org/download/)
    *   Ensure it is added to your system PATH.
2.  **C++ Compiler**: **Visual Studio 2022** or newer with the "Desktop
    development with C++" workload (MSVC x64). MinGW is not supported: Windows
    playback requires the embedded libmpv SDK that `build.ps1` fetches.
3.  **Qt 6**: [Download Qt](https://www.qt.io/download)
    *   Install the version and MSVC 64-bit kit pinned in
        `packaging/dependencies.json` (`qt.version` and `qt.windows_arch`, for
        example Qt `6.12.0` with `msvc2022_64`).
    *   Include Qt Multimedia, Qt Shader Tools, Qt Image Formats, and Qt 5
        Compatibility Module.

## Building using build.ps1

The easiest way to build is using the helper script `build.ps1` in the root of the repository.

1.  Open PowerShell in the repository folder.
2.  Run the script:

    ```powershell
    .\scripts\build.ps1
    ```

    This will:
    *   **Auto-detect** your Qt installation.
    *   Configure the project.
    *   Build the project in `build-windows/`.
    *   Install the output to `install-windows/`.

Bloom is built as a GUI application on Windows, so launching `Bloom.exe` from Explorer does not open an extra console window. If you start it from `cmd`, PowerShell, or Windows Terminal, it reattaches to that parent console so log output still appears there.

### Customizing the Build

**Specify Qt Location explicitly:**
If CMake cannot find Qt, point it to your Qt installation:

```powershell
.\scripts\build.ps1 -QtDir "C:\Qt\6.12.0\msvc2022_64"
```

**Clean Rebuild:**
To remove previous build artifacts and start fresh:

```powershell
.\scripts\build.ps1 -Clean
```

**Use a specific Generator:**
To force Ninja (what CI uses) from an MSVC x64 developer prompt:

```powershell
.\scripts\build.ps1 -Generator "Ninja"
```

## Manual Build (Advanced)

If you prefer running CMake manually:

```cmd
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="C:\Qt\6.12.0\msvc2022_64"
cmake --build . --config Release
cmake --install .
```

## Packaging

To stage the portable package and build the installer, see
[WINDOWS-BUILD.md](WINDOWS-BUILD.md).

## Running tests on Windows

Build with `-BuildTests` first (see [WINDOWS-BUILD.md](WINDOWS-BUILD.md#build-test-and-package)).

If Windows tests fail to start with exit code `0xc0000135`, run tests through the helper script so runtime DLL paths are staged before `ctest` executes.

```powershell
.\scripts\run-windows-tests.ps1 -Config Release -OutputOnFailure
```

To run only the backend migration targeted tests:

```powershell
.\scripts\run-windows-tests.ps1 -Config Release -OutputOnFailure -Regex "(PlayerBackendFactoryTest|PlayerControllerAutoplayContextTest)"
```
