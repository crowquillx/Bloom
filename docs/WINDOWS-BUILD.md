# Building Bloom for Windows

## Prerequisites

### On Arch Linux

```bash
# Install MinGW cross-compilation toolchain
sudo pacman -S mingw-w64-gcc mingw-w64-cmake

# Install Qt6 for MinGW (from AUR)
yay -S mingw-w64-qt6-base mingw-w64-qt6-declarative mingw-w64-qt6-tools

# Install NSIS for installer creation
sudo pacman -S nsis

# Optional: Wine for testing
sudo pacman -S wine
```

### On Ubuntu/Debian

```bash
# Install MinGW toolchain
sudo apt install mingw-w64 cmake ninja-build

# Install NSIS
sudo apt install nsis

# Note: You may need to build Qt6 for MinGW manually or use MXE
```

## Build Process

### Option 1: Complete Build with Installer

```bash
# Build everything and create installer
./scripts/build-windows-installer.sh
```

This will:
1. Cross-compile Bloom.exe using MinGW
2. Package Qt6 DLLs and dependencies
3. Create `Bloom-Setup-0.2.0.exe` installer

### Option 2: Step-by-Step Build

```bash
# Step 1: Cross-compile
./scripts/build-windows.sh

# Step 2: Package dependencies
./scripts/package-windows.sh

# Step 3: Build installer
makensis installer.nsi
```

## Testing

### On Linux with Wine

```bash
# Install the application
wine Bloom-Setup-0.2.0.exe

# Or run directly without installing
wine Bloom-Windows/Bloom.exe
```

### On Windows

1. Copy `Bloom-Setup-0.2.0.exe` to Windows machine
2. Run installer
3. Application will be installed to `C:\Program Files\Bloom\`
4. Start menu shortcut created

## Troubleshooting

### Missing Qt6 MinGW Packages

If `mingw-w64-qt6-*` packages aren't available on your system:

**Option A: Use MXE (M cross environment)**
```bash
git clone https://github.com/mxe/mxe.git
cd mxe
make MXE_TARGETS='x86_64-w64-mingw32.shared' qt6
```

**Option B: Build on Windows**
- Use MSYS2 on Windows
- Or use Visual Studio with Qt6
- Or use GitHub Actions for automated builds

### DLL Not Found Errors

If the packaged app fails with missing DLL errors:

1. Check MinGW Qt6 installation location
2. Update paths in `package-windows.sh`
3. Use `ldd` equivalent for Windows:
   ```bash
   x86_64-w64-mingw32-objdump -p Bloom.exe | grep DLL
   ```

### Build Failures

**CMake can't find Qt6:**
- Verify `mingw-w64-qt6-base` is installed
- Check CMAKE_PREFIX_PATH in `build-windows.sh`

**Linking errors:**
- Ensure all Qt6 modules are installed (base, declarative, tools, multimedia)
- Check that MinGW toolchain version matches Qt6 build

## Distribution

### Installer Features

The NSIS installer provides:
- Installation to `Program Files`
- Start menu shortcuts
- Desktop shortcut
- Uninstaller
- Add/Remove Programs registry entries
- Administrator privileges for installation

### Portable Version

To create a portable version without installer:

```bash
./scripts/package-windows.sh
zip -r Bloom-Portable.zip Bloom-Windows/
```

Users can extract and run `Bloom.exe` directly.

## GitHub Actions (Recommended)

For automated Windows builds, create `.github/workflows/windows.yml`:

```yaml
name: Windows Build

on:
  push:
    branches: [ main ]
  pull_request:
    branches: [ main ]

jobs:
  build:
    runs-on: windows-latest
    
    steps:
    - uses: actions/checkout@v3
    
    - name: Install Qt
      uses: jurplel/install-qt-action@v3
      with:
        version: '6.10.0'
        arch: 'win64_mingw'
    
    - name: Build
      run: |
        mkdir build
        cd build
        cmake .. -G "MinGW Makefiles"
        cmake --build .
    
    - name: Package
      run: |
        windeployqt build/src/Bloom.exe --qmldir src/ui
    
    - name: Upload artifact
      uses: actions/upload-artifact@v3
      with:
        name: Bloom-Windows
        path: build/src/
```

This builds natively on Windows with proper Qt deployment.

## Notes

- **mpv dependency**: Currently not bundled. Users must install mpv separately or you need to include `mpv.exe` and `mpv-1.dll` in the installer
- **Icons**: Create `icon.ico` for Windows application icon
- **Version**: Update version numbers in `installer.nsi`
- **License**: Ensure `LICENSE` file exists for installer license page
