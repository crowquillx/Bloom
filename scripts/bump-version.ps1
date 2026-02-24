<#
.SYNOPSIS
    Bumps the Bloom project version across all relevant files.

.DESCRIPTION
    Updates version strings in CMakeLists.txt, PKGBUILD, flake.nix, installer.nsi,
    and ci.yml (installer filename references). Optionally commits, tags, and pushes.

.PARAMETER Version
    The new version in X.Y.Z format (e.g. 0.4.0).

.PARAMETER Tag
    If set, commits changes, creates an annotated tag, and pushes to origin.

.EXAMPLE
    .\scripts\bump-version.ps1 0.4.0
    .\scripts\bump-version.ps1 0.4.0 -Tag
#>
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [ValidatePattern('^\d+\.\d+\.\d+$')]
    [string]$Version,

    [switch]$Tag
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot

$parts = $Version.Split('.')
$major = $parts[0]
$minor = $parts[1]
$patch = $parts[2]

Write-Host "Bumping version to $Version" -ForegroundColor Cyan

# --- CMakeLists.txt ---
$cmakePath = Join-Path $repoRoot 'CMakeLists.txt'
$cmake = Get-Content $cmakePath -Raw
$cmake = $cmake -replace 'project\(Bloom VERSION \d+\.\d+\.\d+', "project(Bloom VERSION $Version"
Set-Content $cmakePath $cmake -NoNewline
Write-Host "  Updated CMakeLists.txt" -ForegroundColor Green

# --- PKGBUILD ---
$pkgPath = Join-Path $repoRoot 'PKGBUILD'
$pkg = Get-Content $pkgPath -Raw
$pkg = $pkg -replace 'pkgver=\d+\.\d+\.\d+', "pkgver=$Version"
Set-Content $pkgPath $pkg -NoNewline
Write-Host "  Updated PKGBUILD" -ForegroundColor Green

# --- flake.nix ---
$flakePath = Join-Path $repoRoot 'flake.nix'
$flake = Get-Content $flakePath -Raw
$flake = $flake -replace 'version = "\d+\.\d+\.\d+"', "version = `"$Version`""
Set-Content $flakePath $flake -NoNewline
Write-Host "  Updated flake.nix" -ForegroundColor Green

# --- installer.nsi ---
$nsiPath = Join-Path $repoRoot 'installer.nsi'
$nsi = Get-Content $nsiPath -Raw
$nsi = $nsi -replace '!define VERSIONMAJOR \d+', "!define VERSIONMAJOR $major"
$nsi = $nsi -replace '!define VERSIONMINOR \d+', "!define VERSIONMINOR $minor"
$nsi = $nsi -replace '!define VERSIONBUILD \d+', "!define VERSIONBUILD $patch"
Set-Content $nsiPath $nsi -NoNewline
Write-Host "  Updated installer.nsi" -ForegroundColor Green

# --- ci.yml (installer filename references) ---
$ciPath = Join-Path $repoRoot '.github\workflows\ci.yml'
$ci = Get-Content $ciPath -Raw
$ci = $ci -replace 'Bloom-Setup-\d+\.\d+\.\d+\.exe', "Bloom-Setup-$Version.exe"
Set-Content $ciPath $ci -NoNewline
Write-Host "  Updated ci.yml" -ForegroundColor Green

Write-Host "`n========================================" -ForegroundColor Cyan
Write-Host " Version bumped to $Version" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan

# --- Generate release notes ---
Write-Host "`nGenerating release notes..." -ForegroundColor Cyan
& "$PSScriptRoot\generate-release-notes.ps1" -Version $Version

if ($Tag) {
    Write-Host "`nCommitting and tagging..." -ForegroundColor Cyan
    Push-Location $repoRoot
    git add -A
    git commit -m "release: v$Version"
    $releaseNotesPath = Join-Path $repoRoot "RELEASE_NOTES.md"
    git tag -a "v$Version" -F $releaseNotesPath
    Pop-Location

    Write-Host "`n Next steps:" -ForegroundColor Yellow
    Write-Host "  1. Push to trigger the release CI:" -ForegroundColor White
    Write-Host "     git push origin main --tags" -ForegroundColor Gray
    Write-Host "  2. CI will automatically create a GitHub Release 'Bloom v$Version'" -ForegroundColor White
    Write-Host "     with Windows (ZIP + installer) and Linux (AppImage + .deb + tarball) artifacts." -ForegroundColor White
    Write-Host "  3. Scoop stable manifest will be updated via repository dispatch." -ForegroundColor White
}
else {
    Write-Host "`n Next steps:" -ForegroundColor Yellow
    Write-Host "  1. Review the changes:  git diff" -ForegroundColor White
    Write-Host "  2. Commit:              git add -A && git commit -m `"release: v$Version`"" -ForegroundColor White
    Write-Host "  3. Tag:                 git tag -a v$Version -F RELEASE_NOTES.md" -ForegroundColor White
    Write-Host "  4. Push:                git push origin main --tags" -ForegroundColor White
    Write-Host "`n  Or re-run with -Tag to do steps 2-3 automatically:" -ForegroundColor Gray
    Write-Host "     .\scripts\bump-version.ps1 $Version -Tag" -ForegroundColor Gray
}
