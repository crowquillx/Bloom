#!/usr/bin/env bash
# Build a Bloom .deb package from a CMake install tree.
#
# Usage:
#   scripts/package-linux-deb.sh [--install-dir DIR] [--output-dir DIR] [--version VER]

set -euo pipefail

INSTALL_DIR="${INSTALL_DIR:-install-linux}"
OUTPUT_DIR="${OUTPUT_DIR:-artifacts}"
VERSION="${VERSION:-0.2.0}"
ARCH="amd64"

while [[ "$#" -gt 0 ]]; do
    case $1 in
        --install-dir) INSTALL_DIR="$2"; shift ;;
        --output-dir) OUTPUT_DIR="$2"; shift ;;
        --version) VERSION="$2"; shift ;;
        -h|--help)
            echo "Usage: $0 [--install-dir DIR] [--output-dir DIR] [--version VER]"
            exit 0 ;;
        *) echo "Unknown parameter: $1"; exit 1 ;;
    esac
    shift
done

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

DEB_NAME="bloom_${VERSION}_${ARCH}"
DEB_ROOT="$PROJECT_ROOT/$DEB_NAME"

echo "=== Building Bloom .deb package ==="
echo "Install dir: $INSTALL_DIR"
echo "Output dir:  $OUTPUT_DIR"
echo "Version:     $VERSION"

# Clean previous deb tree
rm -rf "$DEB_ROOT"

# Create deb directory structure
mkdir -p "$DEB_ROOT/DEBIAN"
mkdir -p "$DEB_ROOT/usr"

# Copy install tree into deb root
cp -a "$INSTALL_DIR"/* "$DEB_ROOT/usr/" 2>/dev/null || true

# If files are at top level instead of under usr/
if [ -d "$INSTALL_DIR/bin" ]; then
    mkdir -p "$DEB_ROOT/usr/bin"
    cp -a "$INSTALL_DIR/bin/"* "$DEB_ROOT/usr/bin/" 2>/dev/null || true
fi
if [ -d "$INSTALL_DIR/share" ]; then
    mkdir -p "$DEB_ROOT/usr/share"
    cp -a "$INSTALL_DIR/share/"* "$DEB_ROOT/usr/share/" 2>/dev/null || true
fi
if [ -d "$INSTALL_DIR/lib" ]; then
    mkdir -p "$DEB_ROOT/usr/lib"
    cp -a "$INSTALL_DIR/lib/"* "$DEB_ROOT/usr/lib/" 2>/dev/null || true
fi

# Ensure binary name is lowercase
if [ -f "$DEB_ROOT/usr/bin/Bloom" ] && [ ! -f "$DEB_ROOT/usr/bin/bloom" ]; then
    mv "$DEB_ROOT/usr/bin/Bloom" "$DEB_ROOT/usr/bin/bloom"
fi

# Calculate installed size in KB
INSTALLED_SIZE=$(du -sk "$DEB_ROOT" | cut -f1)

# Generate control file
cat > "$DEB_ROOT/DEBIAN/control" << EOF
Package: bloom
Version: $VERSION
Section: video
Priority: optional
Architecture: $ARCH
Installed-Size: $INSTALLED_SIZE
Depends: mpv, libsecret-1-0, libsqlite3-0, libgl1
Recommends: libqt6core6, libqt6gui6, libqt6qml6, libqt6quick6, libqt6network6, libqt6sql6, libqt6multimedia6, libqt6waylandclient6
Maintainer: crowquillx <crowquillx@users.noreply.github.com>
Homepage: https://github.com/crowquillx/Bloom
Description: Jellyfin HTPC client with 10-foot UI
 Bloom is a dedicated Jellyfin media client built for the living room.
 It features a 10-foot interface designed for keyboard, gamepad, and remote
 control navigation, with high-quality mpv-based video playback.
EOF

mkdir -p "$OUTPUT_DIR"

# Build the .deb
dpkg-deb --build --root-owner-group "$DEB_ROOT" "$OUTPUT_DIR/${DEB_NAME}.deb"

echo ".deb package created: $OUTPUT_DIR/${DEB_NAME}.deb"

# Cleanup
rm -rf "$DEB_ROOT"
