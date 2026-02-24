# Conventional Commits

Bloom follows the [Conventional Commits](https://www.conventionalcommits.org/) specification for every git message so that changelogs, releases, and automation stay predictable.

## Format

Commit messages must follow this structure:

```
<type>(<scope>): <subject>
```

- `<type>` is mandatory. Use one of the types listed below.
- `<scope>` is optional but encouraged when the change targets a specific subsystem.
- `<subject>` is a short imperative summary (lowercase, no trailing period).

If the change has more to explain, add a blank line after the subject and write a body with:

- Motivation for the change
- Details that would help reviewers, testers, or future maintainers

If the change introduces behavior that requires a special release or breaking changes, document that in a footer prefixed with `BREAKING CHANGE:` or `BREAKING CHANGES:`.

## Common Types

- `feat`: Adds a new feature.
- `fix`: Fixes a bug.
- `docs`: Documentation-only changes.
- `style`: Formatting that does not affect logic (whitespace, semicolons, etc.).
- `refactor`: Refactors code without adding features or fixing bugs.
- `perf`: Performance improvements.
- `test`: Adds or updates tests.
- `build`: Builds, dependency updates, or tooling changes.
- `ci`: Continuous integration configurations and scripts.
- `chore`: Routine maintenance that does not touch src/test code.
- `revert`: Reverts a previous commit (subject should include the reverted commit hash).
- `release`: Changes to release tooling or version metadata.

## Scope guidance

Use scopes that describe the area being changed, for example `player`, `docs`, `scripts`, or `ui`. Scopes help reviewers quickly see the intent when scanning the history. When a change spans multiple areas, pick the scope that best describes where the most impactful work happened.

## Writing good subjects

- Keep it under 50 characters if possible.
- Use imperative mood: “add”, “fix”, “drop”.
- Avoid redundant words (“add fix”); the type already explains intent.

## Examples

```
feat(player): add Windows overlay sync
fix(ui): keep focus after navigation wrap
docs(contrib): document conventional commit process
ci(build): pin Docker image to avoid drift
chore: tidy QML imports
BREAKING CHANGE: drop support for embedded Linux libmpv
```

## Verification

Linting hooks (e.g., `commitlint`) run automatically inside our release tooling. If you need to enforce the spec locally, install `@commitlint/config-conventional` and a git hook so that commits are validated before pushing.

## Why this matters

Following the spec keeps `git log`, release notes, and automated changelog generation accurate. It also helps reviewers understand the purpose of every change without opening the diff.
