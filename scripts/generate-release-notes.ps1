<#
.SYNOPSIS
    Generates release notes from git commits since the last vX.Y.Z tag.

.DESCRIPTION
    Reads git log since the previous release tag, groups commits by Conventional
    Commit type, and writes a markdown file suitable for use as a GitHub Release body.

.PARAMETER Version
    The new version in X.Y.Z format (e.g. 0.4.0).

.PARAMETER OutputFile
    Path to write the markdown output. Defaults to RELEASE_NOTES.md in the repo root.

.EXAMPLE
    .\scripts\generate-release-notes.ps1 0.4.0
    .\scripts\generate-release-notes.ps1 0.4.0 -OutputFile release-body.md
#>
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [ValidatePattern('^\d+\.\d+\.\d+$')]
    [string]$Version,

    [string]$OutputFile = ""
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot

if (-not $OutputFile) {
    $OutputFile = Join-Path $repoRoot "RELEASE_NOTES.md"
}

Push-Location $repoRoot

# Find the previous release tag (latest vX.Y.Z, excluding the current version)
$allTags = git tag --list 'v*' --sort=-version:refname 2>$null
$prevTag = $allTags | Where-Object { $_ -ne "v$Version" } | Select-Object -First 1

if ($prevTag) {
    $logRange = "${prevTag}..HEAD"
    $sinceDesc = $prevTag
} else {
    $logRange = ""
    $sinceDesc = "the beginning of the project"
}

# Collect commits: hash + subject, no merges
if ($logRange) {
    $rawCommits = git log $logRange --pretty=format:"%H %s" --no-merges
} else {
    $rawCommits = git log --pretty=format:"%H %s" --no-merges
}

# Buckets for Conventional Commit types
$buckets = @{
    feat     = [System.Collections.Generic.List[string]]::new()
    fix      = [System.Collections.Generic.List[string]]::new()
    perf     = [System.Collections.Generic.List[string]]::new()
    refactor = [System.Collections.Generic.List[string]]::new()
    docs     = [System.Collections.Generic.List[string]]::new()
    build    = [System.Collections.Generic.List[string]]::new()
    ci       = [System.Collections.Generic.List[string]]::new()
    test     = [System.Collections.Generic.List[string]]::new()
    chore    = [System.Collections.Generic.List[string]]::new()
    style    = [System.Collections.Generic.List[string]]::new()
    revert   = [System.Collections.Generic.List[string]]::new()
    other    = [System.Collections.Generic.List[string]]::new()
}

$breakingNotes = [System.Collections.Generic.List[string]]::new()

foreach ($line in $rawCommits) {
    if (-not $line) { continue }

    $spaceIdx = $line.IndexOf(' ')
    if ($spaceIdx -lt 0) { continue }
    $hash    = $line.Substring(0, $spaceIdx)
    $subject = $line.Substring($spaceIdx + 1)

    # Check full commit body for BREAKING CHANGE footer
    $fullMsg = git log -1 --pretty=format:"%B" $hash
    foreach ($msgLine in $fullMsg) {
        if ($msgLine -match '^BREAKING CHANGE:\s*(.+)') {
            $breakingNotes.Add("- $($Matches[1])")
        }
    }

    # Parse Conventional Commit: type(scope)!: description
    if ($subject -match '^([a-zA-Z]+)(\([^)]*\))?(!)?\s*:\s*(.+)$') {
        $type  = $Matches[1].ToLower()
        $scope = $Matches[2] -replace '^\(|\)$', ''
        $bang  = $Matches[3]
        $desc  = $Matches[4]

        if ($bang) {
            $breakingNotes.Add("- $desc")
        }

        if ($scope) {
            $entry = "- **${scope}**: $desc"
        } else {
            $entry = "- $desc"
        }

        if ($buckets.ContainsKey($type)) {
            $buckets[$type].Add($entry)
        } else {
            $buckets['other'].Add($entry)
        }
    } else {
        $buckets['other'].Add("- $subject")
    }
}

# Build the markdown output
$labels = @{
    feat     = "New Features"
    fix      = "Bug Fixes"
    perf     = "Performance"
    refactor = "Refactoring"
    docs     = "Documentation"
    build    = "Build"
    ci       = "CI"
    test     = "Tests"
    chore    = "Chores"
    style    = "Style"
    revert   = "Reverts"
    other    = "Other"
}

$sectionOrder = @('feat','fix','perf','refactor','docs','build','ci','test','chore','style','revert','other')

$sb = [System.Text.StringBuilder]::new()
[void]$sb.AppendLine("## What's Changed in v${Version}")
[void]$sb.AppendLine()

if ($breakingNotes.Count -gt 0) {
    [void]$sb.AppendLine("### Breaking Changes")
    [void]$sb.AppendLine()
    foreach ($note in $breakingNotes) {
        [void]$sb.AppendLine($note)
    }
    [void]$sb.AppendLine()
}

$hasContent = $false
foreach ($type in $sectionOrder) {
    $items = $buckets[$type]
    if ($items.Count -gt 0) {
        $hasContent = $true
        [void]$sb.AppendLine("### $($labels[$type])")
        [void]$sb.AppendLine()
        foreach ($item in $items) {
            [void]$sb.AppendLine($item)
        }
        [void]$sb.AppendLine()
    }
}

if (-not $hasContent) {
    [void]$sb.AppendLine("_No changes recorded since ${sinceDesc}._")
    [void]$sb.AppendLine()
}

if ($prevTag) {
    $remoteUrl = (git remote get-url origin 2>$null) -replace '\.git$', '' -replace 'git@github\.com:', 'https://github.com/'
    if ($remoteUrl) {
        [void]$sb.AppendLine("**Full changelog:** [${prevTag}...v${Version}](${remoteUrl}/compare/${prevTag}...v${Version})")
    }
}

$content = $sb.ToString().TrimEnd()
Set-Content -Path $OutputFile -Value $content -Encoding utf8 -NoNewline

Pop-Location

$fileName = Split-Path -Leaf $OutputFile
Write-Host "  Updated $fileName" -ForegroundColor Green
