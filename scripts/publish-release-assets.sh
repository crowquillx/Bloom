#!/usr/bin/env bash
# Attach late-finishing platform assets to an already-published GitHub release
# and merge their hashes into the release-wide SHA256SUMS.txt.
#
# Usage: publish-release-assets.sh <release-tag> <asset>...
# Requires GH_TOKEN, GITHUB_REPOSITORY, and GITHUB_SHA. Per-platform
# SHA256SUMS-*.txt files are uploaded but not listed in SHA256SUMS.txt.
set -euo pipefail

if [[ $# -lt 2 ]]; then
    echo "Usage: $0 <release-tag> <asset>..." >&2
    exit 2
fi
: "${GH_TOKEN:?GH_TOKEN is required}"
: "${GITHUB_REPOSITORY:?GITHUB_REPOSITORY is required}"
: "${GITHUB_SHA:?GITHUB_SHA is required}"

TAG="$1"
shift
assets=("$@")

# A newer main push moves dev-latest and replaces its release. Never attach
# this run's assets to a release built from another commit.
target="$(gh api "repos/$GITHUB_REPOSITORY/commits/$TAG" --jq .sha)"
if [[ "$target" != "$GITHUB_SHA" ]]; then
    echo "::warning::$TAG now points at $target, not $GITHUB_SHA; skipping asset upload."
    exit 0
fi

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

gh release download "$TAG" --repo "$GITHUB_REPOSITORY" \
    --pattern SHA256SUMS.txt --dir "$work"

hashed=()
for asset in "${assets[@]}"; do
    test -s "$asset" || { echo "Missing release asset: $asset" >&2; exit 1; }
    name="$(basename "$asset")"
    [[ "$name" == SHA256SUMS* ]] || hashed+=("$asset")
done

# Replace any existing lines for these files so reruns stay idempotent.
{
    awk 'NR == FNR { skip[$0]; next } !($2 in skip)' \
        <(for asset in "${hashed[@]}"; do basename "$asset"; done) \
        "$work/SHA256SUMS.txt"
    for asset in "${hashed[@]}"; do
        (cd "$(dirname "$asset")" && sha256sum "$(basename "$asset")")
    done
} | sort -k2 > "$work/SHA256SUMS.next"
mv "$work/SHA256SUMS.next" "$work/SHA256SUMS.txt"

gh release upload "$TAG" --repo "$GITHUB_REPOSITORY" --clobber \
    "${assets[@]}" "$work/SHA256SUMS.txt"
