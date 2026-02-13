<#
.SYNOPSIS
    Stages a portable Windows package from the CMake install tree.

.DESCRIPTION
    Copies install artifacts into a portable output directory, runs windeployqt,
    and writes a qt.conf suited for portable distribution.
#>

param(
    [string]$InstallDir = "install-windows",
    [string]$OutputDir = "Bloom-Windows",
    [string]$QtDir = ""
)

$ErrorActionPreference = "Stop"

function Resolve-Windeployqt {
    param([string]$QtRoot)

    if (-not [string]::IsNullOrWhiteSpace($QtRoot)) {
        $candidate = Join-Path $QtRoot "bin\windeployqt.exe"
        if (Test-Path $candidate) {
            return $candidate
        }
    }

    $fromPath = Get-Command windeployqt.exe -ErrorAction SilentlyContinue
    if ($fromPath) {
        return $fromPath.Source
    }

    return $null
}

$installBin = Join-Path $InstallDir "bin"
$exePath = Join-Path $installBin "Bloom.exe"
if (-not (Test-Path $exePath)) {
    $fallback = Get-ChildItem -Path . -Filter "Bloom.exe" -Recurse -File -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match "\\(install-windows|build-windows\\src)\\" } |
        Select-Object -First 1

    if ($fallback) {
        Write-Warning "Expected executable not found at $exePath; using fallback: $($fallback.FullName)"
        $exePath = $fallback.FullName
        $installBin = Split-Path -Path $exePath -Parent
    } else {
        throw "Expected executable not found: $exePath"
    }
}

if (Test-Path $OutputDir) {
    Remove-Item -Recurse -Force $OutputDir
}
New-Item -ItemType Directory -Path $OutputDir | Out-Null

# Stage runtime files from install/bin to portable package root.
Copy-Item -Path (Join-Path $installBin "*") -Destination $OutputDir -Recurse -Force

# Stage optional shared assets if they exist.
$installShare = Join-Path $InstallDir "share"
if (Test-Path $installShare) {
    Copy-Item -Path $installShare -Destination (Join-Path $OutputDir "share") -Recurse -Force
}

$windeployqt = Resolve-Windeployqt -QtRoot $QtDir
if (-not $windeployqt) {
    throw "windeployqt.exe not found. Pass -QtDir or ensure it is on PATH."
}

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$qmlDir = Join-Path $projectRoot "src\ui"
$packagedExe = Join-Path $OutputDir "Bloom.exe"

Write-Host "Running windeployqt from: $windeployqt" -ForegroundColor Cyan
& $windeployqt $packagedExe --qmldir $qmlDir --release --no-translations
if ($LASTEXITCODE -ne 0) {
    throw "windeployqt failed with exit code $LASTEXITCODE"
}

# Remove duplicate app module if emitted by windeployqt.
Get-ChildItem -Path $OutputDir -Filter "BloomUI" -Recurse -Directory -ErrorAction SilentlyContinue |
    ForEach-Object { Remove-Item -Recurse -Force $_.FullName }

@"
[Paths]
Prefix=.
Plugins=plugins
Qml2Imports=qml
"@ | Set-Content -Path (Join-Path $OutputDir "qt.conf") -NoNewline

Write-Host "Portable package staged at: $OutputDir" -ForegroundColor Green
