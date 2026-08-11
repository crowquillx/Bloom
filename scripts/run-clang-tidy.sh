#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BLOOM_ANALYSIS_BUILD_DIR:-$ROOT/build-analysis/clang-tidy}"
JOBS="${BLOOM_BUILD_JOBS:-$(nproc)}"

if [[ -z "${BLOOM_ANALYSIS_SHELL:-}" ]]; then
    exec nix develop "$ROOT#analysis" --command "$ROOT/scripts/run-clang-tidy.sh"
fi

for required_command in cmake clang-tidy xargs; do
    command -v "$required_command" >/dev/null || {
        echo "Required analysis command not found: $required_command" >&2
        exit 1
    }
done

cmake -S "$ROOT" -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DBUILD_TESTING=OFF \
    -DBLOOM_BUNDLE_LIBMPV=OFF

# Keep the normal PR signal high by statically analyzing the ownership and
# security boundaries changed by the production refactor. Scheduled runs use
# this same deterministic list.
sources=(
    "$ROOT/src/player/PlaybackPolicy.cpp"
    "$ROOT/src/player/PlayerProcessManager.cpp"
    "$ROOT/src/ui/ImageCacheStore.cpp"
    "$ROOT/src/updates/UpdateManifestVerifier.cpp"
    "$ROOT/src/updates/UpdateNetworkPolicy.cpp"
    "$ROOT/src/updates/WindowsNsisUpdateApplier.cpp"
    "$ROOT/src/utils/DisplayManager.cpp"
)

printf '%s\0' "${sources[@]}" \
    | xargs -0 -n 1 -P "$JOBS" clang-tidy -p "$BUILD_DIR" --quiet
