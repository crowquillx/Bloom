#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# Under `nix run`, BASH_SOURCE lives at the store root (/nix/store/<hash>.sh)
# so the path above resolves to /nix; fall back to the invocation directory.
[[ -f "$ROOT/VERSION" ]] || ROOT="$PWD"
[[ -f "$ROOT/VERSION" ]] || { echo "Cannot locate Bloom source root (VERSION not found) from $PWD." >&2; exit 1; }
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

CONTAINER_ENGINE="${BLOOM_CONTAINER_ENGINE:-podman}"
case "$CONTAINER_ENGINE" in
    podman|docker) ;;
    *) echo "Unsupported container engine: $CONTAINER_ENGINE (expected podman or docker)." >&2; exit 2 ;;
esac
command -v "$CONTAINER_ENGINE" >/dev/null || {
    echo "$CONTAINER_ENGINE is required; run this command through 'nix run .#package-linux'." >&2
    exit 1
}

VERSION="$(tr -d '\n' < "$ROOT/VERSION")"
BUILD_ID="${BUILD_ID:-$VERSION}"
IMAGE="$(jq -r .portable.image "$ROOT/packaging/dependencies.json")"
if [[ "$CONTAINER_ENGINE" == docker ]]; then
    IMAGE="${IMAGE#docker://}"
fi
mkdir -p "$OUTPUT"

ENGINE_ARGS=(run --rm --network=host)
if [[ "$CONTAINER_ENGINE" == podman ]]; then
    ENGINE_ARGS+=(--userns=keep-id)
fi

if [[ "$CONTAINER_ENGINE" == docker ]]; then
    docker image inspect "$IMAGE" >/dev/null 2>&1 || docker pull "$IMAGE" >/dev/null
else
    podman image exists "$IMAGE" || podman pull "$IMAGE" >/dev/null
fi

"$CONTAINER_ENGINE" "${ENGINE_ARGS[@]}" \
    -e BLOOM_VERSION="$VERSION" \
    -e BLOOM_BUILD_CHANNEL="$CHANNEL" \
    -e BLOOM_BUILD_ID="$BUILD_ID" \
    -e BLOOM_GIT_SHA="$GIT_SHA" \
    -v "$ROOT:/workspace:ro" \
    -v "$OUTPUT:/output" \
    "$IMAGE" \
    bash /workspace/packaging/portable/build.sh

echo "Portable Linux artifacts written to $OUTPUT"
