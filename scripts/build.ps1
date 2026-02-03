<#
.SYNOPSIS
    Builds the Bloom project on Windows.

.DESCRIPTION
    This script checks for prerequisites, configures the project using CMake,
    builds the application, and optionally deploys Qt dependencies.

.PARAMETER BuildDir
    The directory to build in. Default is "build".

.PARAMETER InstallDir
    The directory to install/deploy to. Default is "install".

.PARAMETER Config
    The build configuration (Debug, Release, RelWithDebInfo, MinSizeRel). Default is "Release".

.PARAMETER QtDir
    Optional path to the Qt6 installation directory (containing bin/qmake.exe or lib/cmake).
    If not provided, CMake will look in standard locations.

.PARAMETER Generator
    Optional CMake generator to use (e.g., "Visual Studio 17 2022", "MinGW Makefiles", "Ninja").
    If not provided, CMake chooses the default.

.PARAMETER Clean
    If set, removes the build directory before building.

.EXAMPLE
    .\scripts\build.ps1
    Builds in 'build' directory with Release configuration.

.EXAMPLE
    .\scripts\build.ps1 -QtDir "C:\Qt\6.10.0\msvc2019_64" -Generator "Visual Studio 16 2019"
    Builds using a specific Qt version and VS generator.
#>

param (
    [string]$BuildDir = "build-windows",
    [string]$InstallDir = "install-windows",
    [string]$Config = "Release",
    [string]$QtDir = "",
    [string]$Generator = "",
    [switch]$Clean
)

$ErrorActionPreference = "Stop"

function Test-CommandExists {
    param ($Command)
    $null -ne (Get-Command $Command -ErrorAction SilentlyContinue)
}

function Resolve-QtEnvironment {
    Write-Host "Attempting to auto-detect Qt environment..." -ForegroundColor Cyan
    
    # Standard Qt location
    $QtRoot = "C:\Qt"
    if (-not (Test-Path $QtRoot)) { return }

    # Find latest version (e.g., 6.6.0, 6.10.1) - sort descending
    $Versions = Get-ChildItem -Path $QtRoot -Directory | 
                Where-Object { $_.Name -match '^\d+\.\d+\.\d+$' } | 
                Sort-Object Name -Descending

    if ($Versions.Count -eq 0) { return }
    $LatestVersion = $Versions[0].FullName
    Write-Host "Found Qt version: $($Versions[0].Name)" -ForegroundColor Gray

    # Check for installed folders (mingw_64 vs msvc*)
    $MingwPath = Join-Path $LatestVersion "mingw_64"
    $MsvcPaths = Get-ChildItem -Path $LatestVersion -Directory | Where-Object { $_.Name -match "msvc" }

    # Preference Logic: Use explicit arguments first, otherwise auto-detect
    # If standard C++ compiler (MSVC) is found in path, prefer MSVC Qt if available.
    # If not, or if only MinGW is available, setup MinGW.

    if ($Generator -match "MinGW" -or $Generator -match "Ninja" -or (-not $MsvcPaths -and (Test-Path $MingwPath))) {
        # SETUP MINGW ENVIRONMENT
        Write-Host "Configuring for MinGW..." -ForegroundColor Cyan
        
        if (-not (Test-Path $MingwPath)) {
            Write-Warning "MinGW Qt files not found in $LatestVersion"
            return
        }

        # Set QtDir if not provided
        if ([string]::IsNullOrWhiteSpace($script:QtDir)) {
            $script:QtDir = $MingwPath
        }

        # Find Tools
        $ToolsDir = Join-Path $QtRoot "Tools"
        
        # Add MinGW Compiler to Path
        $MingwTools = Get-ChildItem -Path $ToolsDir -Directory | Where-Object { $_.Name -match "mingw" } | Sort-Object Name -Descending
        if ($MingwTools) {
            $MingwBin = Join-Path $MingwTools[0].FullName "bin"
            if (Test-Path $MingwBin) {
                Write-Host "Adding MinGW to PATH: $MingwBin" -ForegroundColor Gray
                $env:PATH = "$MingwBin;$env:PATH"
            }
        }

        # Add Ninja to Path
        $NinjaDir = Join-Path $ToolsDir "Ninja"
        if (Test-Path $NinjaDir) {
            Write-Host "Adding Ninja to PATH: $NinjaDir" -ForegroundColor Gray
            $env:PATH = "$NinjaDir;$env:PATH"
            
            if ([string]::IsNullOrWhiteSpace($script:Generator)) {
                $script:Generator = "Ninja"
            }
        } elseif ([string]::IsNullOrWhiteSpace($script:Generator)) {
             $script:Generator = "MinGW Makefiles"
        }

    } elseif ($MsvcPaths) {
        # SETUP MSVC ENVIRONMENT
        Write-Host "Configuring for MSVC..." -ForegroundColor Cyan
        # Pick the latest MSVC matching system arch (usually just one)
        $TargetMsvc = $MsvcPaths | Select-Object -Last 1
        
        if ([string]::IsNullOrWhiteSpace($script:QtDir)) {
            $script:QtDir = $TargetMsvc.FullName
        }
    }
}

Write-Host "Checking prerequisites..." -ForegroundColor Cyan

# Check for CMake
if (-not (Test-CommandExists cmake)) {
    Write-Error "CMake is not found in PATH. Please install CMake."
}

# Auto-detect Qt if QtDir is missing
if ([string]::IsNullOrWhiteSpace($QtDir)) {
    Resolve-QtEnvironment
}

# Clean build directory if requested
if ($Clean) {
    if (Test-Path $BuildDir) {
        Write-Host "Cleaning build directory '$BuildDir'..." -ForegroundColor Yellow
        Remove-Item -Path $BuildDir -Recurse -Force
    }
    if (Test-Path $InstallDir) {
        Write-Host "Cleaning install directory '$InstallDir'..." -ForegroundColor Yellow
        Remove-Item -Path $InstallDir -Recurse -Force
    }
}

# Create directories
if (-not (Test-Path $BuildDir)) {
    New-Item -ItemType Directory -Path $BuildDir | Out-Null
}

# Configure CMake arguments
$CMakeArgs = @(
    "-S", ".",
    "-B", $BuildDir,
    "-DCMAKE_BUILD_TYPE=$Config",
    "-DCMAKE_INSTALL_PREFIX=$InstallDir"
)

if ($QtDir) {
    Write-Host "Using Qt directory: $QtDir" -ForegroundColor Gray
    $CMakeArgs += "-DCMAKE_PREFIX_PATH=$QtDir"
}

if ($Generator) {
    Write-Host "Using Generator: $Generator" -ForegroundColor Gray
    $CMakeArgs += "-G", "$Generator"
}

Write-Host "Configuring project..." -ForegroundColor Cyan
Write-Host "cmake $CMakeArgs" -ForegroundColor DarkGray
& cmake $CMakeArgs

if ($LASTEXITCODE -ne 0) {
    Write-Error "CMake configuration failed."
}

Write-Host "Building project..." -ForegroundColor Cyan
& cmake --build $BuildDir --config $Config --parallel

if ($LASTEXITCODE -ne 0) {
    Write-Error "Build failed."
}

Write-Host "Installing/Deploying..." -ForegroundColor Cyan
& cmake --install $BuildDir --config $Config

# Windeployqt (Optional but recommended for Windows)
if ($QtDir) {
    $Windeployqt = Join-Path $QtDir "bin\windeployqt.exe"
    if (Test-Path $Windeployqt) {
        Write-Host "Running windeployqt..." -ForegroundColor Cyan
        $ExePath = "$InstallDir/bin/Bloom.exe" # Adjust based on actual output structure
        if (-not (Test-Path $ExePath)) {
             $ExePath = Get-ChildItem -Path $InstallDir -Filter "*.exe" -Recurse | Select-Object -First 1 -ExpandProperty FullName
        }
        
        if ($ExePath) {
            # Use --qmldir to ensure all QML plugins (like QtQuick.Controls) are deployed
            $QmlDir = Join-Path $PSScriptRoot "src\ui"
            & $Windeployqt $ExePath --qmldir $QmlDir
        }
    }
}

Write-Host "Build complete!" -ForegroundColor Green
Write-Host "You can find the artifacts in: $InstallDir"
