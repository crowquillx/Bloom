# Building Bloom on Windows

This guide explains how to build Bloom natively on Windows using the provided PowerShell script.

## Prerequisites

Before you begin, ensure you have the following installed:

1.  **CMake**: [Download CMake](https://cmake.org/download/)
    *   Ensure it is added to your system PATH.
2.  **C++ Compiler**:
    *   **Visual Studio 2022** (Recommended) with "Desktop development with C++" workload.
    *   OR **MinGW-w64** toolchain.
3.  **Qt 6**: [Download Qt](https://www.qt.io/download)
    *   Install the version corresponding to your compiler (e.g., MSVC 2019/2022 64-bit or MinGW 64-bit).
    *   Make sure to install: Qt Multimedia, Qt Shader Tools, Qt 5 Compatibility Module (if needed), etc.

## Building using build.ps1

The easiest way to build is using the helper script `build.ps1` in the root of the repository.

1.  Open PowerShell in the repository folder.
2.  Run the script:

    ```powershell
    .\scripts\build.ps1
    ```

    This will:
    *   **Auto-detect** your Qt installation (MinGW or MSVC).
    *   Configure the project.
    *   Build the project in `build-windows/`.
    *   Install the output to `install-windows/`.

### Customizing the Build

**Specify Qt Location explicitly:**
If CMake cannot find Qt, point it to your Qt installation:

```powershell
.\scripts\build.ps1 -QtDir "C:\Qt\6.10.0\msvc2019_64"
```

**Clean Rebuild:**
To remove previous build artifacts and start fresh:

```powershell
.\scripts\build.ps1 -Clean
```

**Use a specific Generator:**
To force using Ninja or MinGW Makefiles:

```powershell
.\scripts\build.ps1 -Generator "Ninja"
.\scripts\build.ps1 -Generator "MinGW Makefiles"
```

## Manual Build (Advanced)

If you prefer running CMake manually:

```cmd
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="C:\Qt\6.10.0\msvc2019_64"
cmake --build . --config Release
cmake --install .
```
