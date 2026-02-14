param (
    [string]$BuildDir = "build-windows",
    [string]$InstallDir = "install-windows",
    [string]$Config = "Release",
    [string]$Regex = "",
    [switch]$OutputOnFailure
)

$ErrorActionPreference = "Stop"

function Prepend-PathIfExists {
    param([string]$PathToAdd)

    if ([string]::IsNullOrWhiteSpace($PathToAdd)) {
        return
    }

    if (Test-Path $PathToAdd) {
        $env:PATH = "$PathToAdd;$env:PATH"
        Write-Host "Added to PATH: $PathToAdd" -ForegroundColor DarkGray
    }
}

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$buildRoot = if ([System.IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $projectRoot $BuildDir }
$installRoot = if ([System.IO.Path]::IsPathRooted($InstallDir)) { $InstallDir } else { Join-Path $projectRoot $InstallDir }
$testsDir = Join-Path $buildRoot "tests"

if (-not (Test-Path $testsDir)) {
    throw "Tests directory not found: $testsDir. Run ./scripts/build.ps1 first."
}

$runtimeCandidates = @(
    (Join-Path $installRoot "bin"),
    (Join-Path $buildRoot "src/$Config"),
    (Join-Path $buildRoot "src"),
    (Join-Path $testsDir $Config),
    $testsDir
)

$qtPrefix = ""
$cachePath = Join-Path $buildRoot "CMakeCache.txt"
if (Test-Path $cachePath) {
    $cacheMatch = Select-String -Path $cachePath -Pattern '^CMAKE_PREFIX_PATH:.*=(.+)$' | Select-Object -First 1
    if ($cacheMatch -and $cacheMatch.Matches.Count -gt 0) {
        $qtPrefix = $cacheMatch.Matches[0].Groups[1].Value.Trim()
    }
}

if (-not [string]::IsNullOrWhiteSpace($qtPrefix)) {
    $qtFirst = $qtPrefix.Split(';')[0]
    Prepend-PathIfExists (Join-Path $qtFirst "bin")
}

foreach ($candidate in $runtimeCandidates) {
    Prepend-PathIfExists $candidate
}

$pluginCandidates = @(
    (Join-Path $installRoot "plugins"),
    (Join-Path $installRoot "bin/plugins")
)

if (-not [string]::IsNullOrWhiteSpace($qtPrefix)) {
    $qtFirst = $qtPrefix.Split(';')[0]
    $pluginCandidates += (Join-Path $qtFirst "plugins")
}

$platformPluginPath = ""
foreach ($pluginRoot in $pluginCandidates) {
    $platformsPath = Join-Path $pluginRoot "platforms"
    if (Test-Path $platformsPath) {
        $platformPluginPath = $platformsPath
        if ([string]::IsNullOrWhiteSpace($env:QT_PLUGIN_PATH)) {
            $env:QT_PLUGIN_PATH = $pluginRoot
        } else {
            $env:QT_PLUGIN_PATH = "$pluginRoot;$env:QT_PLUGIN_PATH"
        }
        Write-Host "Configured QT_PLUGIN_PATH entry: $pluginRoot" -ForegroundColor DarkGray
    }
}

if (-not [string]::IsNullOrWhiteSpace($platformPluginPath)) {
    $env:QT_QPA_PLATFORM_PLUGIN_PATH = $platformPluginPath
    Write-Host "Using QT_QPA_PLATFORM_PLUGIN_PATH: $platformPluginPath" -ForegroundColor DarkGray
}

$ctestArgs = @("--test-dir", $testsDir, "-C", $Config)
if ($OutputOnFailure) {
    $ctestArgs += "--output-on-failure"
}
if (-not [string]::IsNullOrWhiteSpace($Regex)) {
    $ctestArgs += @("-R", $Regex)
}

Write-Host "Running ctest $($ctestArgs -join ' ')" -ForegroundColor Cyan
& ctest @ctestArgs
$exitCode = $LASTEXITCODE

if ($exitCode -ne 0) {
    exit $exitCode
}
