#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# Under `nix run`, BASH_SOURCE lives at the store root (/nix/store/<hash>.sh)
# so the path above resolves to /nix; fall back to the invocation directory.
[[ -f "$ROOT/VERSION" ]] || ROOT="$PWD"
[[ -f "$ROOT/VERSION" ]] || { echo "Cannot locate Bloom source root (VERSION not found) from $PWD." >&2; exit 1; }
OUTPUT="$ROOT/dist"
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
flatpak-builder --user --force-clean --repo="$ROOT/.flatpak-builder/repo" \
    "$ROOT/.flatpak-builder/build" "$ROOT/packaging/flatpak/com.github.crowquillx.Bloom.yml"
flatpak-builder --run "$ROOT/.flatpak-builder/build" \
    "$ROOT/packaging/flatpak/com.github.crowquillx.Bloom.yml" \
    env QT_QPA_PLATFORM=offscreen bloom --version
flatpak-builder --run "$ROOT/.flatpak-builder/build" \
    "$ROOT/packaging/flatpak/com.github.crowquillx.Bloom.yml" mpv --version
flatpak build-bundle "$ROOT/.flatpak-builder/repo" \
    "$OUTPUT/Bloom-${VERSION}-linux-x86_64.flatpak" com.github.crowquillx.Bloom
sha256sum "$OUTPUT/Bloom-${VERSION}-linux-x86_64.flatpak" \
    > "$OUTPUT/SHA256SUMS-flatpak-x86_64.txt"
sha256sum -c "$OUTPUT/SHA256SUMS-flatpak-x86_64.txt"
