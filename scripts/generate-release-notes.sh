#!/usr/bin/env bash
#
# Generates release notes from git commits since the last vX.Y.Z tag.
# Commits are grouped by Conventional Commit type.
#
# Usage:
#   ./scripts/generate-release-notes.sh <X.Y.Z> [--output <file>]
#
# Arguments:
#   X.Y.Z          The new version being released
#   --output FILE  Write notes to FILE instead of stdout (default: RELEASE_NOTES.md)
#
# Output is a markdown document suitable for use as a GitHub Release body.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"

OUTPUT_FILE="$REPO_ROOT/RELEASE_NOTES.md"
VERSION=""

usage() {
    echo "Usage: $0 <X.Y.Z> [--output <file>]"
    exit 1
}

[[ $# -lt 1 ]] && usage
VERSION="$1"
shift

while [[ $# -gt 0 ]]; do
    case "$1" in
        --output) OUTPUT_FILE="$2"; shift 2 ;;
        *) echo "Unknown option: $1" >&2; usage ;;
    esac
done

if [[ ! "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "Error: Version must be in X.Y.Z format (got: $VERSION)" >&2
    exit 1
fi

cd "$REPO_ROOT"

# Find the previous release tag (latest vX.Y.Z tag that is not the current one)
PREV_TAG=$(git tag --list 'v*' --sort=-version:refname | grep -v "^v${VERSION}$" | head -1 || true)

if [[ -z "$PREV_TAG" ]]; then
    # No previous tag — use all commits
    LOG_RANGE=""
    SINCE_DESC="the beginning of the project"
else
    LOG_RANGE="${PREV_TAG}..HEAD"
    SINCE_DESC="$PREV_TAG"
fi

# Collect commits: format is "<hash> <subject>"
if [[ -z "$LOG_RANGE" ]]; then
    COMMITS=$(git log --pretty=format:"%H %s" --no-merges)
else
    COMMITS=$(git log "$LOG_RANGE" --pretty=format:"%H %s" --no-merges)
fi

# Buckets for each CC type
declare -A BUCKETS
BUCKETS=(
    [feat]=""
    [fix]=""
    [perf]=""
    [refactor]=""
    [docs]=""
    [build]=""
    [ci]=""
    [test]=""
    [chore]=""
    [style]=""
    [revert]=""
    [other]=""
)

BREAKING_NOTES=""

while IFS= read -r line; do
    [[ -z "$line" ]] && continue
    HASH="${line%% *}"
    SUBJECT="${line#* }"

    # Check full commit message body for BREAKING CHANGE footer
    FULL_MSG=$(git log -1 --pretty=format:"%B" "$HASH")
    if echo "$FULL_MSG" | grep -q "^BREAKING CHANGE:"; then
        BREAKING_TEXT=$(echo "$FULL_MSG" | grep "^BREAKING CHANGE:" | sed 's/^BREAKING CHANGE: *//')
        BREAKING_NOTES="${BREAKING_NOTES}- ${BREAKING_TEXT}\n"
    fi

    # Parse type and optional scope from subject: type(scope): subject  OR  type!: subject
    CC_PATTERN='^([a-zA-Z]+)(\([^)]*\))?(!)?: '
    if [[ "$SUBJECT" =~ $CC_PATTERN ]]; then
        TYPE="${BASH_REMATCH[1],,}"  # lowercase
        SCOPE="${BASH_REMATCH[2]}"
        BANG="${BASH_REMATCH[3]}"
        # Strip the type(scope): prefix to get the description
        DESC="${SUBJECT#*: }"

        if [[ -n "$BANG" ]]; then
            BREAKING_NOTES="${BREAKING_NOTES}- ${DESC}\n"
        fi

        if [[ -n "$SCOPE" ]]; then
            SCOPE_LABEL="${SCOPE#(}"
            SCOPE_LABEL=${SCOPE_LABEL%)}
            ENTRY="- **${SCOPE_LABEL}**: ${DESC}"
        else
            ENTRY="- ${DESC}"
        fi

        if [[ -v "BUCKETS[$TYPE]" ]]; then
            BUCKETS[$TYPE]="${BUCKETS[$TYPE]}${ENTRY}\n"
        else
            BUCKETS[other]="${BUCKETS[other]}${ENTRY}\n"
        fi
    else
        # Non-conventional commit — put in other
        BUCKETS[other]="${BUCKETS[other]}- ${SUBJECT}\n"
    fi
done <<< "$COMMITS"

# Build the markdown output
{
    echo "## What's Changed in v${VERSION}"
    echo ""

    if [[ -n "$BREAKING_NOTES" ]]; then
        echo "### Breaking Changes"
        echo ""
        printf "%b" "$BREAKING_NOTES"
        echo ""
    fi

    # Ordered sections with display labels
    declare -A LABELS=(
        [feat]="New Features"
        [fix]="Bug Fixes"
        [perf]="Performance"
        [refactor]="Refactoring"
        [docs]="Documentation"
        [build]="Build"
        [ci]="CI"
        [test]="Tests"
        [chore]="Chores"
        [style]="Style"
        [revert]="Reverts"
        [other]="Other"
    )

    SECTION_ORDER=(feat fix perf refactor docs build ci test chore style revert other)
    HAS_CONTENT=false

    for TYPE in "${SECTION_ORDER[@]}"; do
        CONTENT="${BUCKETS[$TYPE]}"
        if [[ -n "$CONTENT" ]]; then
            HAS_CONTENT=true
            echo "### ${LABELS[$TYPE]}"
            echo ""
            printf "%b" "$CONTENT"
            echo ""
        fi
    done

    if ! $HAS_CONTENT; then
        echo "_No changes recorded since ${SINCE_DESC}._"
        echo ""
    fi

    if [[ -n "$PREV_TAG" ]]; then
        REPO_URL=$(git remote get-url origin 2>/dev/null | sed 's/\.git$//' | sed 's|git@github.com:|https://github.com/|')
        if [[ -n "$REPO_URL" ]]; then
            echo "**Full changelog:** [${PREV_TAG}...v${VERSION}](${REPO_URL}/compare/${PREV_TAG}...v${VERSION})"
        fi
    fi
} > "$OUTPUT_FILE"

echo -e "  \033[32mGenerated release notes → $(basename "$OUTPUT_FILE")\033[0m"
