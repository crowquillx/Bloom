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
        --output)
            if [[ $# -lt 2 ]]; then
                echo "--output requires a value" >&2
                exit 2
            fi
            OUTPUT="$(realpath -m "$2")"; shift 2 ;;
        -h|--help) echo "Usage: $0 [--output DIR]"; exit 0 ;;
        *) echo "Unknown argument: $1" >&2; exit 2 ;;
    esac
done

VERSION="$(tr -d '\n' < "$ROOT/VERSION")"
BUILD_ID="${BUILD_ID:-$VERSION}"
BRANCH="$(jq -r .flatpak.branch "$ROOT/packaging/dependencies.json")"
SDK_ID="$(jq -r .flatpak.sdk "$ROOT/packaging/dependencies.json")"
PLATFORM_ID="$(jq -r .flatpak.runtime "$ROOT/packaging/dependencies.json")"
SDK_COMMIT="$(jq -r .flatpak.sdk_commit "$ROOT/packaging/dependencies.json")"
PLATFORM_COMMIT="$(jq -r .flatpak.platform_commit "$ROOT/packaging/dependencies.json")"
mkdir -p "$OUTPUT"

appstreamcli validate --no-net \
    "$ROOT/src/resources/linux/com.github.crowquillx.Bloom.metainfo.xml"

bwrap --unshare-user --unshare-net --ro-bind / / true || {
    echo "Bubblewrap cannot create the namespaces required by flatpak-builder." >&2
    echo "On Ubuntu 24.04 CI, disable kernel.apparmor_restrict_unprivileged_userns first." >&2
    exit 1
}

if ! flatpak remote-info --user flathub >/dev/null 2>&1; then
    flatpak remote-add --user --if-not-exists flathub \
        https://dl.flathub.org/repo/flathub.flatpakrepo
fi
flatpak install --user --noninteractive flathub \
    "$SDK_ID/x86_64/$BRANCH" \
    "$PLATFORM_ID/x86_64/$BRANCH"
flatpak update --user --noninteractive --commit="$SDK_COMMIT" "$SDK_ID/x86_64/$BRANCH"
flatpak update --user --noninteractive --commit="$PLATFORM_COMMIT" "$PLATFORM_ID/x86_64/$BRANCH"

rm -rf "$ROOT/.flatpak-builder/build" "$ROOT/.flatpak-builder/repo"
# flatpak-builder names its stage-cache refs after the manifest file, so a
# stable name lets later builds reuse unchanged dependency stages.
MANIFEST="$ROOT/packaging/flatpak/.build-manifest.yml"
trap 'rm -f "$MANIFEST"' EXIT
sed \
    -e "s|-DBLOOM_BUILD_CHANNEL=stable|-DBLOOM_BUILD_CHANNEL=$CHANNEL|" \
    -e "/-DBLOOM_BUILD_CHANNEL=$CHANNEL/a\\      - -DBLOOM_BUILD_ID=$BUILD_ID\\
      - -DBLOOM_GIT_SHA=$GIT_SHA" \
    "$ROOT/packaging/flatpak/com.github.crowquillx.Bloom.yml" > "$MANIFEST"
# Unchanged dependency modules come from flatpak-builder's stage cache in
# .flatpak-builder/cache; ccache covers the Bloom module, which changes every
# build. Bound ccache so a persisted directory does not grow to its 5 GB default.
mkdir -p "$ROOT/.flatpak-builder/ccache"
[[ -f "$ROOT/.flatpak-builder/ccache/ccache.conf" ]] ||
    echo "max_size = 1G" > "$ROOT/.flatpak-builder/ccache/ccache.conf"
flatpak-builder --user --force-clean --ccache --repo="$ROOT/.flatpak-builder/repo" \
    "$ROOT/.flatpak-builder/build" "$MANIFEST"
flatpak-builder --run "$ROOT/.flatpak-builder/build" \
    "$MANIFEST" \
    env QT_QPA_PLATFORM=offscreen bloom --version
# The bundled mpv/libmpv must prove Dolby Vision Profile 7 FEL support: mpv
# parses the EL option, and the built libraries contain the FFmpeg dovi_split
# BSF and the libplacebo EL renderer.
flatpak-builder --run "$ROOT/.flatpak-builder/build" \
    "$MANIFEST" mpv --no-config --vf=format=dolbyvision=yes:enhancement-layer=yes --version
FILES="$ROOT/.flatpak-builder/build/files"
python3 -I "$ROOT/scripts/check-mpv-fel-capability.py" \
    --dependencies "$ROOT/packaging/dependencies.json" \
    --library "$FILES/lib/libmpv.so.2" \
    --library "$(find "$FILES/lib" -maxdepth 1 -name 'libplacebo.so.*' -type f | head -1)" \
    --library "$(find "$FILES/lib" -maxdepth 1 -name 'libavcodec.so.*' -type f | head -1)" \
    --require-fel
flatpak build-bundle "$ROOT/.flatpak-builder/repo" \
    "$OUTPUT/Bloom-${VERSION}-linux-x86_64.flatpak" com.github.crowquillx.Bloom
sha256sum "$OUTPUT/Bloom-${VERSION}-linux-x86_64.flatpak" \
    > "$OUTPUT/SHA256SUMS-flatpak-x86_64.txt"
sha256sum -c "$OUTPUT/SHA256SUMS-flatpak-x86_64.txt"
