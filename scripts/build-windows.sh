#!/bin/bash
# Cross-compile Bloom for Windows from Linux

set -e

# Install dependencies (run once):
# sudo pacman -S mingw-w64-gcc mingw-w64-cmake mingw-w64-qt6-base mingw-w64-qt6-declarative

BUILD_DIR="build-windows"
INSTALL_DIR="install-windows"

echo "Building Bloom for Windows..."

# Clean previous builds
rm -rf "$BUILD_DIR" "$INSTALL_DIR"
mkdir -p "$BUILD_DIR" "$INSTALL_DIR"

cd "$BUILD_DIR"

# Configure with MinGW toolchain
x86_64-w64-mingw32-cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="../$INSTALL_DIR" \
    -DCMAKE_TOOLCHAIN_FILE=/usr/share/mingw/toolchain-x86_64-w64-mingw32.cmake

# Build
cmake --build . --parallel $(nproc)

# Install to staging directory
cmake --install .

cd ..

echo "Windows build complete in $INSTALL_DIR/"
echo "Run ./scripts/package-windows.sh to create installer"
