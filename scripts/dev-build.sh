#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT/build-dev"
BUILD_TYPE="Debug"
BUILD_TESTS="OFF"
JOBS="${BLOOM_BUILD_JOBS:-$(nproc)}"
CLEAN=false
CONFIGURE_ONLY=false
RUN=false

usage() {
    cat <<'EOF'
Usage: ./scripts/dev-build.sh [options]

Creates or updates a persistent local CMake/Ninja build using the Nix-pinned
development environment. Subsequent runs rebuild only changed targets.

Options:
  --build-dir DIR       Build directory (default: build-dev)
  --build-type TYPE     CMake build type (default: Debug)
  --tests               Configure test targets
  --jobs N              Parallel build jobs (default: BLOOM_BUILD_JOBS or CPU count)
  --clean               Remove the build directory before configuring
  --configure-only      Configure without building
  --run                 Run Bloom after a successful build
  -h, --help            Show this help
EOF
}

while (($#)); do
    case "$1" in
        --build-dir) [[ $# -ge 2 ]] || { echo "--build-dir requires a value" >&2; exit 2; }; BUILD_DIR="$2"; shift 2 ;;
        --build-type) [[ $# -ge 2 ]] || { echo "--build-type requires a value" >&2; exit 2; }; BUILD_TYPE="$2"; shift 2 ;;
        --tests) BUILD_TESTS="ON"; shift ;;
        --jobs) [[ $# -ge 2 ]] || { echo "--jobs requires a value" >&2; exit 2; }; JOBS="$2"; shift 2 ;;
        --clean) CLEAN=true; shift ;;
        --configure-only) CONFIGURE_ONLY=true; shift ;;
        --run) RUN=true; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown argument: $1" >&2; usage >&2; exit 2 ;;
    esac
done

[[ "$BUILD_DIR" == /* ]] || BUILD_DIR="$ROOT/$BUILD_DIR"

if [[ -z "${IN_NIX_SHELL:-}" ]]; then
    args=(--build-dir "$BUILD_DIR" --build-type "$BUILD_TYPE" --jobs "$JOBS")
    [[ "$BUILD_TESTS" == "ON" ]] && args+=(--tests)
    $CLEAN && args+=(--clean)
    $CONFIGURE_ONLY && args+=(--configure-only)
    $RUN && args+=(--run)
    exec nix develop "$ROOT" --command "$ROOT/scripts/dev-build.sh" "${args[@]}"
fi

for required_command in cmake ninja ccache; do
    command -v "$required_command" >/dev/null || {
        echo "Required command not found in development shell: $required_command" >&2
        exit 1
    }
done

if $CLEAN; then
    echo "Removing $BUILD_DIR"
    rm -rf -- "$BUILD_DIR"
fi

export CCACHE_DIR="${CCACHE_DIR:-$HOME/.cache/ccache}"
mkdir -p "$CCACHE_DIR"

echo "Configuring Bloom"
echo "  build directory: $BUILD_DIR"
echo "  build type:      $BUILD_TYPE"
echo "  tests:           $BUILD_TESTS"
echo "  ccache:          $CCACHE_DIR"

cmake -S "$ROOT" -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DBUILD_TESTING="$BUILD_TESTS" \
    -DBLOOM_BUNDLE_LIBMPV=OFF

if ! $CONFIGURE_ONLY; then
    cmake --build "$BUILD_DIR" --parallel "$JOBS"
    ccache --show-stats
fi

if $RUN; then
    exec "$BUILD_DIR/src/Bloom"
fi
