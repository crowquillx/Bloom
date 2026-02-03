#!/usr/bin/env bash
# Build Windows installer for Bloom
# Requires: mingw-w64, makensis (NSIS)

set -e

echo "=== Building Bloom Windows Installer ==="

# Resolve script directory so sibling scripts are invoked reliably
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# Step 1: Cross-compile for Windows
echo "Step 1: Cross-compiling with MinGW..."
"$SCRIPT_DIR/build-windows.sh" "$@"

# Step 2: Package DLLs and dependencies
echo "Step 2: Packaging Windows binaries..."
"$SCRIPT_DIR/package-windows.sh" "$@"

# Step 3: Build NSIS installer
echo "Step 3: Building NSIS installer..."

# Check if makensis is available
if ! command -v makensis &> /dev/null; then
    echo "Error: makensis (NSIS) not found. Install it with:"
    echo "  Arch: sudo pacman -S nsis"
    echo "  Ubuntu: sudo apt install nsis"
    exit 1
fi

# Build installer
makensis installer.nsi

echo ""
echo "=== Build Complete ==="
echo "Installer created: Bloom-Setup-0.1.0.exe"
echo ""
echo "To test on Linux with Wine:"
echo "  wine Bloom-Setup-0.1.0.exe"
echo ""
echo "Copy to Windows machine to test native installation."
