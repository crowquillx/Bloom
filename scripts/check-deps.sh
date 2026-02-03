#!/bin/bash
# Bloom dependency checker
# Run this before building to verify you have compatible versions

echo "Checking Bloom build dependencies..."

check_version() {
    local cmd=$1
    local min_ver=$2
    local name=$3
    
    if ! command -v $cmd &> /dev/null; then
        echo "❌ $name not found"
        return 1
    fi
    
    echo "✅ $name found"
}

# Check Qt6 (>=6.10)
check_version "qmake6" "6.10" "Qt6"

# Check CMake (>=3.16)
check_version "cmake" "3.16" "CMake"

# Check Ninja
check_version "ninja" "" "Ninja"

# Check mpv
check_version "mpv" "" "mpv"

echo ""
echo "If all checks passed, you can build with:"
echo "  mkdir -p build && cd build"
echo "  cmake .. -G Ninja"
echo "  ninja"
