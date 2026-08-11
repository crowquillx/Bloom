#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BLOOM_ANALYSIS_BUILD_DIR:-$ROOT/build-analysis/coverage}"
REPORT_DIR="${BLOOM_COVERAGE_REPORT_DIR:-$ROOT/build-analysis/coverage-report}"
JOBS="${BLOOM_BUILD_JOBS:-$(nproc)}"

if [[ -z "${BLOOM_ANALYSIS_SHELL:-}" ]]; then
    exec nix develop "$ROOT#analysis" --command "$ROOT/scripts/run-coverage.sh"
fi

for required_command in cmake gcovr jq; do
    command -v "$required_command" >/dev/null || {
        echo "Required coverage command not found: $required_command" >&2
        exit 1
    }
done

cmake -S "$ROOT" -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DBUILD_TESTING=ON \
    -DBLOOM_BUILD_VISUAL_TESTS=OFF \
    -DBLOOM_BUNDLE_LIBMPV=OFF \
    -DBLOOM_ENABLE_COVERAGE=ON
cmake --build "$BUILD_DIR" --parallel "$JOBS"

configured_root="$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "$BUILD_DIR/CMakeCache.txt")"
if [[ "$configured_root" != "$ROOT" ]]; then
    echo "Refusing to clear coverage counters outside Bloom's configured build tree: $BUILD_DIR" >&2
    exit 1
fi
find "$BUILD_DIR" -type f -name '*.gcda' -delete

export QT_QPA_PLATFORM=offscreen
ctest --test-dir "$BUILD_DIR" --output-on-failure --timeout 120 \
    --exclude-regex '^VisualRegressionTest$'

cmake -E make_directory "$REPORT_DIR"
gcovr --root "$ROOT" \
    --filter "$ROOT/src" \
    --exclude "$ROOT/src/test" \
    --exclude-unreachable-branches \
    --exclude-throw-branches \
    --xml "$REPORT_DIR/coverage.xml" \
    --xml-pretty \
    --html-details "$REPORT_DIR/coverage.html" \
    --json-summary "$REPORT_DIR/coverage-summary.json" \
    --json-summary-pretty \
    "$BUILD_DIR"
jq -r '
    "Lines:     \(.line_covered)/\(.line_total) (\(.line_percent)%)",
    "Functions: \(.function_covered)/\(.function_total) (\(.function_percent)%)",
    "Branches:  \(.branch_covered)/\(.branch_total) (\(.branch_percent)%)"
' "$REPORT_DIR/coverage-summary.json" > "$REPORT_DIR/coverage.txt"
cat "$REPORT_DIR/coverage.txt"
