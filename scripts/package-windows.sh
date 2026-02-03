#!/bin/bash
# Package Bloom for Windows with all dependencies

set -e

INSTALL_DIR="install-windows"
PACKAGE_DIR="Bloom-Windows"

echo "Packaging Bloom for Windows..."

# Clean previous package
rm -rf "$PACKAGE_DIR"
mkdir -p "$PACKAGE_DIR"

# Copy executable
cp "$INSTALL_DIR/bin/Bloom.exe" "$PACKAGE_DIR/"

# Use windeployqt to bundle Qt dependencies
# Note: You'll need to run this on Windows or use a wine wrapper
# For cross-compile, we'll manually copy DLLs from MinGW

MINGW_PREFIX="/usr/x86_64-w64-mingw32"

echo "Copying Qt6 libraries..."
cp "$MINGW_PREFIX/bin/Qt6Core.dll" "$PACKAGE_DIR/"
cp "$MINGW_PREFIX/bin/Qt6Gui.dll" "$PACKAGE_DIR/"
cp "$MINGW_PREFIX/bin/Qt6Quick.dll" "$PACKAGE_DIR/"
cp "$MINGW_PREFIX/bin/Qt6Qml.dll" "$PACKAGE_DIR/"
cp "$MINGW_PREFIX/bin/Qt6Network.dll" "$PACKAGE_DIR/"
cp "$MINGW_PREFIX/bin/Qt6Sql.dll" "$PACKAGE_DIR/"
cp "$MINGW_PREFIX/bin/Qt6Concurrent.dll" "$PACKAGE_DIR/"

echo "Copying MinGW runtime..."
cp "$MINGW_PREFIX/bin/libgcc_s_seh-1.dll" "$PACKAGE_DIR/"
cp "$MINGW_PREFIX/bin/libstdc++-6.dll" "$PACKAGE_DIR/"
cp "$MINGW_PREFIX/bin/libwinpthread-1.dll" "$PACKAGE_DIR/"

echo "Copying Qt6 plugins..."
mkdir -p "$PACKAGE_DIR/platforms"
cp "$MINGW_PREFIX/lib/qt6/plugins/platforms/qwindows.dll" "$PACKAGE_DIR/platforms/"

mkdir -p "$PACKAGE_DIR/qmltooling"
cp -r "$MINGW_PREFIX/lib/qt6/qml/QtQuick" "$PACKAGE_DIR/"
cp -r "$MINGW_PREFIX/lib/qt6/qml/QtQml" "$PACKAGE_DIR/"

echo "Creating qt.conf..."
cat > "$PACKAGE_DIR/qt.conf" << EOF
[Paths]
Plugins = .
Qml2Imports = .
EOF

echo ""
echo "Package created in $PACKAGE_DIR/"
echo "Test it with Wine: wine $PACKAGE_DIR/Bloom.exe"
echo "Or run ./create-installer.sh to build NSIS installer"
