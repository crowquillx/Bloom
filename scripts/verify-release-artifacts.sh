#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
# Under `nix run`, BASH_SOURCE lives at the store root (/nix/store/<hash>.sh)
# so the path above resolves to /nix; fall back to the invocation directory.
[[ -f "$ROOT/VERSION" ]] || ROOT="$PWD"
[[ -f "$ROOT/VERSION" ]] || { echo "Cannot locate Bloom source root (VERSION not found) from $PWD." >&2; exit 1; }
DIR="${1:-$ROOT/dist}"
VERSION="$(tr -d '\n' < "$ROOT/VERSION")"
required=(
    "Bloom-${VERSION}-linux-x86_64.AppImage"
    "Bloom-${VERSION}-linux-x86_64.flatpak"
    "Bloom-${VERSION}-linux-x86_64.tar.gz"
    "bloom_${VERSION}_amd64.deb"
)
for artifact in "${required[@]}"; do
    test -s "$DIR/$artifact" || {
        echo "Missing release artifact: $DIR/$artifact" >&2
        exit 1
    }
done
(cd "$DIR" && sha256sum -c SHA256SUMS-linux-x86_64.txt)
(cd "$DIR" && sha256sum -c SHA256SUMS-flatpak-x86_64.txt)
