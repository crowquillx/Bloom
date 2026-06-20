#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUTPUT="$ROOT/dist"
while (($#)); do
    case "$1" in
        --output) OUTPUT="$(realpath -m "$2")"; shift 2 ;;
        -h|--help) echo "Usage: $0 [--output DIR]"; exit 0 ;;
        *) echo "Unknown argument: $1" >&2; exit 2 ;;
    esac
done

VERSION="$(tr -d '\n' < "$ROOT/VERSION")"
BRANCH="$(jq -r .flatpak.branch "$ROOT/packaging/dependencies.json")"
SDK_COMMIT="$(jq -r .flatpak.sdk_commit "$ROOT/packaging/dependencies.json")"
PLATFORM_COMMIT="$(jq -r .flatpak.platform_commit "$ROOT/packaging/dependencies.json")"
mkdir -p "$OUTPUT"

flatpak install --user --noninteractive flathub \
    "org.kde.Sdk/x86_64/$BRANCH@$SDK_COMMIT" \
    "org.kde.Platform/x86_64/$BRANCH@$PLATFORM_COMMIT"

rm -rf "$ROOT/.flatpak-builder/build" "$ROOT/.flatpak-builder/repo"
flatpak-builder --user --force-clean --repo="$ROOT/.flatpak-builder/repo" \
    "$ROOT/.flatpak-builder/build" "$ROOT/packaging/flatpak/com.github.crowquillx.Bloom.yml"
flatpak build-bundle "$ROOT/.flatpak-builder/repo" \
    "$OUTPUT/Bloom-${VERSION}-linux-x86_64.flatpak" com.github.crowquillx.Bloom
sha256sum "$OUTPUT/Bloom-${VERSION}-linux-x86_64.flatpak" \
    > "$OUTPUT/SHA256SUMS-flatpak-x86_64.txt"
