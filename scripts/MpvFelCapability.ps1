# Shared Windows helpers proving that a libmpv runtime DLL carries the
# Dolby Vision Profile 7 FEL stack (mpv PR #17932 + FFmpeg dovi_split BSF +
# libplacebo API >= 370). The shinchiro SDK links FFmpeg and libplacebo
# statically, so all evidence lives in the one DLL. Mirrors the markers in
# scripts/check-mpv-fel-capability.py; see docs/playback.md.

function Get-BloomDependencyPins {
    $projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
    $manifest = Join-Path $projectRoot "packaging\dependencies.json"
    if (-not (Test-Path $manifest)) {
        throw "Dependency manifest not found: $manifest"
    }
    return Get-Content $manifest -Raw | ConvertFrom-Json
}

function Test-MpvDllFelCapability {
    param(
        [Parameter(Mandatory = $true)][string]$DllPath,
        [int]$MinimumLibplaceboApi = 370,
        [string]$ExpectedDescribe = ""
    )

    $bytes = [System.IO.File]::ReadAllBytes($DllPath)
    $text = [System.Text.Encoding]::GetEncoding(28591).GetString($bytes)
    $markers = [ordered]@{
        "mpv-el-splitter"   = "Dolby Vision Profile 7 splitter"
        "mpv-gpu-next-el"   = "Failed setting up enhancement layer; rendering base layer only."
        "libplacebo-el"     = "Failed sampling enhancement layer"
        "ffmpeg-dovi-split" = "Which Dolby Vision components to keep in the output bitstream"
    }

    $missing = @()
    foreach ($name in $markers.Keys) {
        if (-not $text.Contains($markers[$name])) {
            $missing += $name
        }
    }

    $apis = @([regex]::Matches($text, "v7\.(\d{3})\.\d+") | ForEach-Object { [int]$_.Groups[1].Value })
    $maxApi = if ($apis.Count -gt 0) { ($apis | Measure-Object -Maximum).Maximum } else { 0 }
    if ($maxApi -lt $MinimumLibplaceboApi) {
        $missing += "libplacebo-api>=$MinimumLibplaceboApi (found $maxApi)"
    }

    $mpvVersion = [regex]::Match($text, "mpv v[0-9][0-9A-Za-z.\-+]*").Value
    if (-not [string]::IsNullOrWhiteSpace($ExpectedDescribe) -and $mpvVersion -ne "mpv $ExpectedDescribe") {
        $missing += "pinned-describe (expected 'mpv $ExpectedDescribe', found '$mpvVersion')"
    }

    return [pscustomobject]@{
        Path          = $DllPath
        MpvVersion    = $mpvVersion
        LibplaceboApi = $maxApi
        Missing       = $missing
        Capable       = ($missing.Count -eq 0)
    }
}

function Assert-MpvDllFelCapability {
    param(
        [Parameter(Mandatory = $true)][string]$DllPath,
        [switch]$RequirePinned,
        [switch]$WarnOnly
    )

    $pins = Get-BloomDependencyPins
    $expected = if ($RequirePinned) { $pins.mpv.describe } else { "" }
    $result = Test-MpvDllFelCapability -DllPath $DllPath `
        -MinimumLibplaceboApi ([int]$pins.dolby_vision_fel.libplacebo_min_api) `
        -ExpectedDescribe $expected

    if ($result.Capable) {
        Write-Host "libmpv FEL capability verified: $($result.MpvVersion), libplacebo API $($result.LibplaceboApi) ($DllPath)" -ForegroundColor Gray
        return $result
    }

    $message = "libmpv runtime at $DllPath lacks Dolby Vision Profile 7 FEL support: $($result.Missing -join ', ')"
    if ($WarnOnly) {
        Write-Warning $message
        return $result
    }
    throw $message
}
