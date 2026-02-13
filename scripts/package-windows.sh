#!/bin/bash
# Stage a portable Windows package from the CMake install tree.

set -e

INSTALL_DIR="install-windows"
PACKAGE_DIR="Bloom-Windows"

echo "Packaging Bloom for Windows..."

# Clean previous package
rm -rf "$PACKAGE_DIR"
mkdir -p "$PACKAGE_DIR"

if [[ ! -f "$INSTALL_DIR/bin/Bloom.exe" ]]; then
    echo "Error: $INSTALL_DIR/bin/Bloom.exe not found. Run build/install first."
    exit 1
fi

# Stage install/bin into portable package root.
cp -r "$INSTALL_DIR/bin/"* "$PACKAGE_DIR/"

# Stage optional share assets (translations/icons) if present.
if [[ -d "$INSTALL_DIR/share" ]]; then
    mkdir -p "$PACKAGE_DIR/share"
    cp -r "$INSTALL_DIR/share/"* "$PACKAGE_DIR/share/"
fi

# MinGW cross-build fallback: copy runtime Qt/MinGW DLLs if available.
MINGW_PREFIX="/usr/x86_64-w64-mingw32"
if [[ -d "$MINGW_PREFIX" ]]; then
    echo "Detected MinGW prefix, staging runtime DLLs/plugins..."

    cp -f "$MINGW_PREFIX/bin/Qt6Core.dll" "$PACKAGE_DIR/" 2>/dev/null || true
    cp -f "$MINGW_PREFIX/bin/Qt6Gui.dll" "$PACKAGE_DIR/" 2>/dev/null || true
    cp -f "$MINGW_PREFIX/bin/Qt6Quick.dll" "$PACKAGE_DIR/" 2>/dev/null || true
    cp -f "$MINGW_PREFIX/bin/Qt6Qml.dll" "$PACKAGE_DIR/" 2>/dev/null || true
    cp -f "$MINGW_PREFIX/bin/Qt6Network.dll" "$PACKAGE_DIR/" 2>/dev/null || true
    cp -f "$MINGW_PREFIX/bin/Qt6Sql.dll" "$PACKAGE_DIR/" 2>/dev/null || true
    cp -f "$MINGW_PREFIX/bin/Qt6Concurrent.dll" "$PACKAGE_DIR/" 2>/dev/null || true
    cp -f "$MINGW_PREFIX/bin/libgcc_s_seh-1.dll" "$PACKAGE_DIR/" 2>/dev/null || true
    cp -f "$MINGW_PREFIX/bin/libstdc++-6.dll" "$PACKAGE_DIR/" 2>/dev/null || true
    cp -f "$MINGW_PREFIX/bin/libwinpthread-1.dll" "$PACKAGE_DIR/" 2>/dev/null || true

    mkdir -p "$PACKAGE_DIR/plugins/platforms"
    cp -f "$MINGW_PREFIX/lib/qt6/plugins/platforms/qwindows.dll" "$PACKAGE_DIR/plugins/platforms/" 2>/dev/null || true

    mkdir -p "$PACKAGE_DIR/qml"
    cp -r "$MINGW_PREFIX/lib/qt6/qml/QtQuick" "$PACKAGE_DIR/qml/" 2>/dev/null || true
    cp -r "$MINGW_PREFIX/lib/qt6/qml/QtQml" "$PACKAGE_DIR/qml/" 2>/dev/null || true
fi

cat > "$PACKAGE_DIR/qt.conf" << EOF
[Paths]
Prefix=.
Plugins=plugins
Qml2Imports=qml
EOF

echo ""
echo "Package created in $PACKAGE_DIR/"
echo "On Windows, run scripts/package-windows.ps1 to deploy Qt runtime with windeployqt."
