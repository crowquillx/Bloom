#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
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
