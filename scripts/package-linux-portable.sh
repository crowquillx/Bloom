#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUTPUT="$ROOT/dist"
CHANNEL="${BLOOM_BUILD_CHANNEL:-stable}"
BUILD_ID="${BLOOM_BUILD_ID:-}"
GIT_SHA="${BLOOM_GIT_SHA:-$(git -C "$ROOT" rev-parse HEAD 2>/dev/null || true)}"

while (($#)); do
    case "$1" in
        --output) OUTPUT="$(realpath -m "$2")"; shift 2 ;;
        --channel) CHANNEL="$2"; shift 2 ;;
        --build-id) BUILD_ID="$2"; shift 2 ;;
        -h|--help)
            echo "Usage: $0 [--output DIR] [--channel stable|dev] [--build-id ID]"
            exit 0
            ;;
        *) echo "Unknown argument: $1" >&2; exit 2 ;;
    esac
done

command -v podman >/dev/null || {
    echo "podman is required; run this command through 'nix run .#package-linux'." >&2
    exit 1
}

VERSION="$(tr -d '\n' < "$ROOT/VERSION")"
BUILD_ID="${BUILD_ID:-$VERSION}"
IMAGE="$(jq -r .portable.image "$ROOT/packaging/dependencies.json")"
mkdir -p "$OUTPUT"

podman run --rm --network=host \
    --userns=keep-id \
    -e BLOOM_VERSION="$VERSION" \
    -e BLOOM_BUILD_CHANNEL="$CHANNEL" \
    -e BLOOM_BUILD_ID="$BUILD_ID" \
    -e BLOOM_GIT_SHA="$GIT_SHA" \
    -v "$ROOT:/workspace:ro" \
    -v "$OUTPUT:/output" \
    "$IMAGE" \
    bash /workspace/packaging/portable/build.sh

echo "Portable Linux artifacts written to $OUTPUT"
