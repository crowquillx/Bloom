#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MODE="${1:-address}"
JOBS="${BLOOM_BUILD_JOBS:-$(nproc)}"

case "$MODE" in
    address)
        BUILD_DIR="${BLOOM_ANALYSIS_BUILD_DIR:-$ROOT/build-analysis/address}"
        TEST_REGEX='^(ImageCacheStoreTest|PlayerProcessManagerTest|PlaybackPolicyTest|DisplayManagerTest|ProviderTransportTest|UpdateServiceTest)$'
        ;;
    thread)
        BUILD_DIR="${BLOOM_ANALYSIS_BUILD_DIR:-$ROOT/build-analysis/thread}"
        TEST_REGEX='^(PlayerProcessManagerTest|DisplayManagerTest)$'
        ;;
    *)
        echo "Usage: $0 address|thread" >&2
        exit 2
        ;;
esac

if [[ -z "${BLOOM_ANALYSIS_SHELL:-}" ]]; then
    exec nix develop "$ROOT#analysis" --command "$ROOT/scripts/run-sanitizers.sh" "$MODE"
fi

cmake -S "$ROOT" -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DBUILD_TESTING=ON \
    -DBLOOM_BUILD_VISUAL_TESTS=OFF \
    -DBLOOM_BUNDLE_LIBMPV=OFF \
    -DBLOOM_SANITIZER="$MODE"

if [[ "$MODE" == "address" ]]; then
    targets=(
        ImageCacheStoreTest
        PlayerProcessManagerTest
        PlaybackPolicyTest
        DisplayManagerTest
        ProviderTransportTest
        UpdateServiceTest
    )
    export ASAN_OPTIONS="detect_leaks=1:halt_on_error=1:strict_string_checks=1"
    export UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1"
else
    targets=(PlayerProcessManagerTest DisplayManagerTest)
    # QtTest intentionally leaves its process-wide watchdog thread to process
    # teardown. Ignore thread-leak reports from that framework watchdog while
    # retaining all data-race failures; lifecycle completion has direct tests.
    export TSAN_OPTIONS="halt_on_error=1:history_size=7:report_thread_leaks=0"
fi

cmake --build "$BUILD_DIR" --parallel "$JOBS" --target "${targets[@]}"
export QT_QPA_PLATFORM=offscreen
ctest --test-dir "$BUILD_DIR" --output-on-failure --timeout 120 \
    --tests-regex "$TEST_REGEX"
