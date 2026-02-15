#!/usr/bin/env bash
# Build a Bloom AppImage from a CMake install tree.
#
# Usage:
#   scripts/package-linux-appimage.sh [--install-dir DIR] [--output-dir DIR]
#
# Prerequisites (installed automatically in CI):
#   - linuxdeploy (downloaded if not on PATH)
#   - Qt 6 development environment with qmake on PATH

set -euo pipefail

INSTALL_DIR="${INSTALL_DIR:-install-linux}"
OUTPUT_DIR="${OUTPUT_DIR:-artifacts}"

while [[ "$#" -gt 0 ]]; do
    case $1 in
        --install-dir) INSTALL_DIR="$2"; shift ;;
        --output-dir) OUTPUT_DIR="$2"; shift ;;
        -h|--help)
            echo "Usage: $0 [--install-dir DIR] [--output-dir DIR]"
            exit 0 ;;
        *) echo "Unknown parameter: $1"; exit 1 ;;
    esac
    shift
done

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
APPDIR="$PROJECT_ROOT/Bloom.AppDir"

echo "=== Building Bloom AppImage ==="
echo "Install dir: $INSTALL_DIR"
echo "Output dir:  $OUTPUT_DIR"

# Clean previous AppDir
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr"
mkdir -p "$OUTPUT_DIR"

# Copy install tree into AppDir
cp -a "$INSTALL_DIR"/* "$APPDIR/usr/" 2>/dev/null || true

# If install put things at top level (bin/, share/), move into usr/
if [ -d "$APPDIR/usr/bin" ]; then
    echo "Install tree has bin/ at usr/bin — good."
elif [ -f "$INSTALL_DIR/bin/bloom" ] || [ -f "$INSTALL_DIR/bin/Bloom" ]; then
    mkdir -p "$APPDIR/usr/bin"
    cp -a "$INSTALL_DIR/bin/"* "$APPDIR/usr/bin/"
    [ -d "$INSTALL_DIR/share" ] && cp -a "$INSTALL_DIR/share" "$APPDIR/usr/"
    [ -d "$INSTALL_DIR/lib" ] && cp -a "$INSTALL_DIR/lib" "$APPDIR/usr/"
fi

# Ensure binary name is lowercase
if [ -f "$APPDIR/usr/bin/Bloom" ] && [ ! -f "$APPDIR/usr/bin/bloom" ]; then
    mv "$APPDIR/usr/bin/Bloom" "$APPDIR/usr/bin/bloom"
fi

# Fetch linuxdeploy if not available
LINUXDEPLOY="linuxdeploy"
if ! command -v linuxdeploy &>/dev/null; then
    LINUXDEPLOY="$PROJECT_ROOT/linuxdeploy-x86_64.AppImage"
    if [ ! -f "$LINUXDEPLOY" ]; then
        echo "Downloading linuxdeploy..."
        curl -fSL -o "$LINUXDEPLOY" \
            "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage"
        chmod +x "$LINUXDEPLOY"
    fi
fi

# Fetch Qt plugin for linuxdeploy if not available
LINUXDEPLOY_QT="linuxdeploy-plugin-qt-x86_64.AppImage"
if [ ! -f "$PROJECT_ROOT/$LINUXDEPLOY_QT" ]; then
    echo "Downloading linuxdeploy Qt plugin..."
    curl -fSL -o "$PROJECT_ROOT/$LINUXDEPLOY_QT" \
        "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage"
    chmod +x "$PROJECT_ROOT/$LINUXDEPLOY_QT"
fi

export PATH="$PROJECT_ROOT:$PATH"
export EXTRA_QT_PLUGINS="svg;waylandclient;multimedia"
export QML_SOURCES_PATHS="$PROJECT_ROOT/src/ui"

# Desktop file and icon
DESKTOP_FILE="$APPDIR/usr/share/applications/com.github.crowquillx.Bloom.desktop"
ICON_FILE="$APPDIR/usr/share/icons/hicolor/scalable/apps/com.github.crowquillx.Bloom.svg"

if [ ! -f "$DESKTOP_FILE" ]; then
    echo "Warning: Desktop file not found at $DESKTOP_FILE"
    echo "Attempting to use source desktop file..."
    DESKTOP_FILE="$PROJECT_ROOT/src/resources/linux/com.github.crowquillx.Bloom.desktop"
fi

if [ ! -f "$ICON_FILE" ]; then
    echo "Warning: Icon not found at $ICON_FILE"
    echo "Attempting to use source icon..."
    ICON_FILE="$PROJECT_ROOT/src/resources/images/app/logo_trans.svg"
fi

echo "Running linuxdeploy..."
"$LINUXDEPLOY" \
    --appdir "$APPDIR" \
    --desktop-file "$DESKTOP_FILE" \
    --icon-file "$ICON_FILE" \
    --plugin qt \
    --output appimage

# Move the generated AppImage
APPIMAGE_FILE=$(ls -1 Bloom-*.AppImage 2>/dev/null | head -1)
if [ -z "$APPIMAGE_FILE" ]; then
    echo "Error: AppImage file not found after build"
    exit 1
fi

mv "$APPIMAGE_FILE" "$OUTPUT_DIR/"
echo "AppImage created: $OUTPUT_DIR/$APPIMAGE_FILE"
