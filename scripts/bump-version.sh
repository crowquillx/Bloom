#!/usr/bin/env bash
#
# Bumps the Bloom project version across all relevant files.
#
# Usage:
#   ./scripts/bump-version.sh 0.4.0
#   ./scripts/bump-version.sh 0.4.0 --tag
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"

TAG=false

usage() {
    echo "Usage: $0 <X.Y.Z> [--tag]"
    echo ""
    echo "Arguments:"
    echo "  X.Y.Z    New version (e.g. 0.4.0)"
    echo "  --tag    Commit changes, create annotated tag, and show push instructions"
    exit 1
}

# Parse arguments
[[ $# -lt 1 ]] && usage
VERSION="$1"
shift

if [[ ! "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "Error: Version must be in X.Y.Z format (got: $VERSION)" >&2
    exit 1
fi

for arg in "$@"; do
    case "$arg" in
        --tag) TAG=true ;;
        *) echo "Unknown option: $arg" >&2; usage ;;
    esac
done

IFS='.' read -r MAJOR MINOR PATCH <<< "$VERSION"

echo -e "\033[36mBumping version to $VERSION\033[0m"

# --- CMakeLists.txt ---
sed -i "s/project(Bloom VERSION [0-9]\+\.[0-9]\+\.[0-9]\+/project(Bloom VERSION $VERSION/" "$REPO_ROOT/CMakeLists.txt"
echo -e "  \033[32mUpdated CMakeLists.txt\033[0m"

# --- PKGBUILD ---
sed -i "s/pkgver=[0-9]\+\.[0-9]\+\.[0-9]\+/pkgver=$VERSION/" "$REPO_ROOT/PKGBUILD"
echo -e "  \033[32mUpdated PKGBUILD\033[0m"

# --- flake.nix ---
sed -i "s/version = \"[0-9]\+\.[0-9]\+\.[0-9]\+\"/version = \"$VERSION\"/" "$REPO_ROOT/flake.nix"
echo -e "  \033[32mUpdated flake.nix\033[0m"

# --- installer.nsi ---
sed -i "s/!define VERSIONMAJOR [0-9]\+/!define VERSIONMAJOR $MAJOR/" "$REPO_ROOT/installer.nsi"
sed -i "s/!define VERSIONMINOR [0-9]\+/!define VERSIONMINOR $MINOR/" "$REPO_ROOT/installer.nsi"
sed -i "s/!define VERSIONBUILD [0-9]\+/!define VERSIONBUILD $PATCH/" "$REPO_ROOT/installer.nsi"
echo -e "  \033[32mUpdated installer.nsi\033[0m"

# --- ci.yml (installer filename references) ---
sed -i "s/Bloom-Setup-[0-9]\+\.[0-9]\+\.[0-9]\+\.exe/Bloom-Setup-$VERSION.exe/g" "$REPO_ROOT/.github/workflows/ci.yml"
echo -e "  \033[32mUpdated ci.yml\033[0m"

echo ""
echo -e "\033[36m========================================\033[0m"
echo -e "\033[36m Version bumped to $VERSION\033[0m"
echo -e "\033[36m========================================\033[0m"

# --- Generate release notes ---
echo ""
echo -e "\033[36mGenerating release notes...\033[0m"
bash "$SCRIPT_DIR/generate-release-notes.sh" "$VERSION"

if $TAG; then
    echo -e "\n\033[36mCommitting and tagging...\033[0m"
    cd "$REPO_ROOT"
    git add -A
    git commit -m "release: v$VERSION"
    git tag -a "v$VERSION" -F "$REPO_ROOT/RELEASE_NOTES.md"

    echo -e "\n\033[33m Next steps:\033[0m"
    echo "  1. Push to trigger the release CI:"
    echo "     git push origin main --tags"
    echo "  2. CI will automatically create a GitHub Release 'Bloom v$VERSION'"
    echo "     with Windows (ZIP + installer) and Linux (AppImage + .deb + tarball) artifacts."
    echo "  3. Scoop stable manifest will be updated via repository dispatch."
else
    echo -e "\n\033[33m Next steps:\033[0m"
    echo "  1. Review the changes:  git diff"
    echo "  2. Commit:              git add -A && git commit -m \"release: v$VERSION\""
    echo "  3. Tag:                 git tag -a v$VERSION -F RELEASE_NOTES.md"
    echo "  4. Push:                git push origin main --tags"
    echo ""
    echo "  Or re-run with --tag to do steps 2-3 automatically:"
    echo "     ./scripts/bump-version.sh $VERSION --tag"
fi
