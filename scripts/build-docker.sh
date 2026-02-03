#!/usr/bin/env bash
# Build Bloom using Docker/Podman containerized environment
# Supports incremental builds by preserving build-docker/ between runs

set -e

# Parse arguments
CLEAN_BUILD=""
WINDOWS_TARGET=""
while [[ "$#" -gt 0 ]]; do
    case $1 in
        --clean) CLEAN_BUILD="--clean" ;;
        --windows|--target-windows) WINDOWS_TARGET="--windows" ;;
        -h|--help)
            echo "Usage: $0 [--clean] [--windows]"
            echo "  --clean     Force a clean rebuild (removes build dirs)"
            echo "  --windows   Cross-compile for Windows (MinGW toolchain)"
            exit 0 ;;
        *) echo "Unknown parameter: $1"; exit 1 ;;
    esac
    shift
done

# Detect container runtime
if command -v podman &> /dev/null; then
    RUNTIME="podman"
elif command -v docker &> /dev/null; then
    RUNTIME="docker"
else
    echo "Error: Neither podman nor docker found. Please install one of them."
    exit 1
fi

echo "Using container runtime: $RUNTIME"

# Build container image (uses Docker layer cache)
echo "Building container image..."
$RUNTIME build -t bloom-build .

# Run build inside container
# The build-docker/ directory is preserved on the host for incremental builds
echo "Running build inside container..."
if [ -n "$CLEAN_BUILD" ]; then
    echo "Clean build requested..."
fi
$RUNTIME run --rm -v "$(pwd):/workspace" bloom-build build-bloom.sh $CLEAN_BUILD $WINDOWS_TARGET

echo ""
echo "Build complete! Test with:"
if [ -n "$WINDOWS_TARGET" ]; then
    echo "  wine ./build-docker-windows/src/Bloom.exe"
else
    echo "  ./build-docker/src/Bloom"
fi
