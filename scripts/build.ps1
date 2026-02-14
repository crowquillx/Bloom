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
    [bool]$AutoFetchMpvSdk = $true,
    [switch]$Clean
)

$ErrorActionPreference = "Stop"

function Test-CommandExists {
    param ($Command)
    $null -ne (Get-Command $Command -ErrorAction SilentlyContinue)
}

function Resolve-MsvcToolPath {
    param([string]$ToolName)

    $direct = Get-Command $ToolName -ErrorAction SilentlyContinue
    if ($direct -and $direct.Source) {
        return $direct.Source
    }

    $candidates = @()
    $vsRoot = "C:\Program Files (x86)\Microsoft Visual Studio"
    if (Test-Path $vsRoot) {
        $candidates = Get-ChildItem -Path $vsRoot -Recurse -Filter $ToolName -File -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match "\\VC\\Tools\\MSVC\\.*\\bin\\Hostx64\\x64\\" } |
            Sort-Object FullName -Descending |
            Select-Object -ExpandProperty FullName
    }

    if ($candidates -and $candidates.Count -gt 0) {
        return $candidates[0]
    }

    return $null
}

function Get-FirstExistingPath {
    param([string[]]$Candidates)
    foreach ($candidate in $Candidates) {
        if (-not [string]::IsNullOrWhiteSpace($candidate) -and (Test-Path $candidate)) {
            return (Resolve-Path $candidate).Path
        }
    }
    return $null
}

function Resolve-MpvSdkRoot {
    $roots = @()
    $projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path

    foreach ($envVar in @("MPV_ROOT", "MPV_DIR", "LIBMPV_ROOT")) {
        $value = [Environment]::GetEnvironmentVariable($envVar)
        if (-not [string]::IsNullOrWhiteSpace($value)) {
            $roots += $value
        }
    }

    $roots += (Join-Path $projectRoot "third_party\mpv\mpv-sdk")
    $roots += (Join-Path $projectRoot "third_party\mpv")
    $roots += "C:\ProgramData\chocolatey\lib\mpv\tools\mpv"
    $roots += "C:\ProgramData\chocolatey\bin"

    $mpvExe = Get-Command mpv.exe -ErrorAction SilentlyContinue
    if ($mpvExe) {
        $mpvDir = Split-Path -Path $mpvExe.Source -Parent
        $roots += $mpvDir
        $roots += (Split-Path -Path $mpvDir -Parent)
    }

    foreach ($root in ($roots | Select-Object -Unique)) {
        if ([string]::IsNullOrWhiteSpace($root) -or -not (Test-Path $root)) {
            continue
        }

        $hasHeader = Get-FirstExistingPath -Candidates @(
            (Join-Path $root "include\mpv\client.h"),
            (Join-Path $root "mpv\client.h")
        )
        if ($hasHeader) {
            return (Resolve-Path $root).Path
        }

        $nestedSdk = Get-ChildItem -Path $root -Directory -Recurse -ErrorAction SilentlyContinue |
            Where-Object { Test-Path (Join-Path $_.FullName "include\mpv\client.h") } |
            Select-Object -First 1
        if ($nestedSdk) {
            return $nestedSdk.FullName
        }
    }

    return $null
}

function Resolve-MpvRuntimeDll {
    param([string]$MpvRoot)
    return Get-FirstExistingPath -Candidates @(
        (Join-Path $MpvRoot "bin\libmpv-2.dll"),
        (Join-Path $MpvRoot "bin\mpv-2.dll"),
        (Join-Path $MpvRoot "bin\mpv.dll"),
        (Join-Path $MpvRoot "lib\libmpv-2.dll"),
        (Join-Path $MpvRoot "lib\mpv-2.dll"),
        (Join-Path $MpvRoot "lib\mpv.dll"),
        (Join-Path $MpvRoot "libmpv-2.dll"),
        (Join-Path $MpvRoot "mpv-2.dll"),
        (Join-Path $MpvRoot "mpv.dll")
    )
}

function Resolve-MpvImportLib {
    param([string]$MpvRoot)
    return Get-FirstExistingPath -Candidates @(
        (Join-Path $MpvRoot "lib\libmpv-2.lib"),
        (Join-Path $MpvRoot "lib\mpv-2.lib"),
        (Join-Path $MpvRoot "lib\libmpv.lib"),
        (Join-Path $MpvRoot "lib\mpv.lib"),
        (Join-Path $MpvRoot "lib64\libmpv-2.lib"),
        (Join-Path $MpvRoot "lib64\mpv-2.lib"),
        (Join-Path $MpvRoot "x86_64\libmpv-2.lib"),
        (Join-Path $MpvRoot "libs\libmpv-2.lib"),
        (Join-Path $MpvRoot "bin\libmpv-2.lib"),
        (Join-Path $MpvRoot "libmpv-2.lib")
    )
}

function Ensure-MpvImportLibForMsvc {
    param([string]$MpvRoot)

    $existing = Resolve-MpvImportLib -MpvRoot $MpvRoot
    if ($existing) {
        return $existing
    }

    $dumpbinPath = Resolve-MsvcToolPath -ToolName "dumpbin.exe"
    $libToolPath = Resolve-MsvcToolPath -ToolName "lib.exe"
    if (-not $dumpbinPath -or -not $libToolPath) {
        return $null
    }

    $runtimeDll = Resolve-MpvRuntimeDll -MpvRoot $MpvRoot
    if (-not $runtimeDll) {
        return $null
    }

    $libDir = Join-Path $MpvRoot "lib"
    if (-not (Test-Path $libDir)) {
        New-Item -ItemType Directory -Path $libDir | Out-Null
    }

    $exportsRaw = & $dumpbinPath /exports $runtimeDll
    if ($LASTEXITCODE -ne 0) {
        return $null
    }

    $exportNames = New-Object System.Collections.Generic.List[string]
    foreach ($line in $exportsRaw) {
        if ($line -match "^\s+\d+\s+[0-9A-F]+\s+[0-9A-F]+\s+(\S+)$") {
            $name = $matches[1]
            if ($name -and $name -ne "[NONAME]") {
                $exportNames.Add($name)
            }
        }
    }

    if ($exportNames.Count -eq 0) {
        return $null
    }

    $defPath = Join-Path $libDir "libmpv-2.def"
    $defContent = @("LIBRARY libmpv-2.dll", "EXPORTS") + ($exportNames | Sort-Object -Unique)
    Set-Content -Path $defPath -Value $defContent -Encoding ascii

    $outLib = Join-Path $libDir "libmpv-2.lib"
    & $libToolPath /def:$defPath /machine:x64 /out:$outLib
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $outLib)) {
        return $null
    }

    return (Resolve-Path $outLib).Path
}

function Fetch-MpvSdk {
    param([string]$DestinationRoot)

    if (-not (Test-Path $DestinationRoot)) {
        New-Item -ItemType Directory -Path $DestinationRoot | Out-Null
    }

    Write-Host "Attempting to fetch latest mpv-dev SDK..." -ForegroundColor Cyan
    $release = Invoke-RestMethod -Uri "https://api.github.com/repos/shinchiro/mpv-winbuild-cmake/releases/latest"
    $asset = $release.assets |
        Where-Object { $_.name -match '^mpv-dev-x86_64-.*\.(7z|zip)$' } |
        Select-Object -First 1

    if (-not $asset) {
        throw "Could not find mpv-dev-x86_64 SDK asset in latest shinchiro/mpv-winbuild-cmake release."
    }

    $downloadPath = Join-Path $DestinationRoot $asset.name
    Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $downloadPath

    $extractRoot = Join-Path $DestinationRoot "mpv-sdk"
    if (Test-Path $extractRoot) {
        Remove-Item -Recurse -Force $extractRoot
    }
    New-Item -ItemType Directory -Path $extractRoot | Out-Null

    if ($asset.name.ToLower().EndsWith(".zip")) {
        Expand-Archive -Path $downloadPath -DestinationPath $extractRoot -Force
    } else {
        if (-not (Test-CommandExists 7z)) {
            throw "7z is required to extract mpv-dev .7z archives. Install 7-Zip or set MPV_ROOT manually."
        }
        & 7z x $downloadPath "-o$extractRoot" -y | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to extract $($asset.name) with 7z."
        }
    }

    $mpvRootCandidate = Get-ChildItem -Path $extractRoot -Directory -Recurse -ErrorAction SilentlyContinue |
        Where-Object { Test-Path (Join-Path $_.FullName "include\mpv\client.h") } |
        Select-Object -First 1

    if ($mpvRootCandidate) {
        return $mpvRootCandidate.FullName
    }
    if (Test-Path (Join-Path $extractRoot "include\mpv\client.h")) {
        return $extractRoot
    }

    throw "Extracted mpv-dev SDK does not contain include\mpv\client.h"
}

function Ensure-MpvSdkEnvironment {
    $resolvedRoot = Resolve-MpvSdkRoot
    if (-not $resolvedRoot -and $AutoFetchMpvSdk) {
        try {
            $projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
            $thirdPartyRoot = Join-Path $projectRoot "third_party\mpv"
            $resolvedRoot = Fetch-MpvSdk -DestinationRoot $thirdPartyRoot
        } catch {
            Write-Warning "Auto-fetch mpv SDK failed: $($_.Exception.Message)"
        }
    }

    if (-not $resolvedRoot) {
        Write-Warning "Could not auto-detect libmpv SDK root. Set MPV_ROOT (or MPV_DIR/LIBMPV_ROOT) before building."
        return
    }

    $env:MPV_ROOT = $resolvedRoot
    Write-Host "Using MPV_ROOT: $resolvedRoot" -ForegroundColor Gray

    $importLib = Ensure-MpvImportLibForMsvc -MpvRoot $resolvedRoot
    if ($importLib) {
        Write-Host "libmpv import library: $importLib" -ForegroundColor Gray
    } else {
        Write-Warning "No MSVC libmpv import library detected/generated under MPV_ROOT. Configure may fail on Windows direct-libmpv builds."
    }
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

Ensure-MpvSdkEnvironment

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

$cachePath = Join-Path $BuildDir "CMakeCache.txt"
if (Test-Path $cachePath) {
    $hasMpvHeaders = Select-String -Path $cachePath -Pattern "^BLOOM_MPV_INCLUDE_DIR:PATH=.*$" | Select-Object -First 1
    $hasMpvLib = Select-String -Path $cachePath -Pattern "^BLOOM_MPV_LIBRARY:FILEPATH=.*$" | Select-Object -First 1
    $mpvMissing = (($hasMpvHeaders -and $hasMpvHeaders.Line -match "NOTFOUND") -or ($hasMpvLib -and $hasMpvLib.Line -match "NOTFOUND") -or (-not $hasMpvHeaders) -or (-not $hasMpvLib))
    if ($mpvMissing) {
        Write-Error "libmpv SDK not detected during configure (BLOOM_MPV_INCLUDE_DIR/BLOOM_MPV_LIBRARY). Windows embedded backend requires direct libmpv."
    }
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
            # Path should be relative to the root, not the script directory
            $ProjectRoot = Get-Item -Path $PSScriptRoot | Select-Object -ExpandProperty Parent
            $QmlDir = Join-Path $ProjectRoot.FullName "src\ui"
            
            Write-Host "QML Source Dir: $QmlDir" -ForegroundColor Gray
            & $Windeployqt $ExePath --qmldir $QmlDir
            
            if ($LASTEXITCODE -ne 0) {
                Write-Error "windeployqt failed."
            }
        }
    }
}

Write-Host "Build complete!" -ForegroundColor Green
Write-Host "You can find the artifacts in: $InstallDir"
