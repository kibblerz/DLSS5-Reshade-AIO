param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^v\d+\.\d+\.\d+$')]
    [string] $Version
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$dist = Join-Path $PSScriptRoot 'dist'
$stage = Join-Path $PSScriptRoot ("stage-{0}-{1}" -f $Version, $PID)
$stage64 = Join-Path $stage '64-bit'
$stage32 = Join-Path $stage '32-bit'

New-Item -ItemType Directory -Force -Path $dist, $stage64, $stage32 | Out-Null

function Copy-RequiredFile([string] $Source, [string] $Destination) {
    if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) {
        throw "Required release input is missing: $Source"
    }
    $parent = Split-Path -Parent $Destination
    if ($parent) { New-Item -ItemType Directory -Force -Path $parent | Out-Null }
    Copy-Item -LiteralPath $Source -Destination $Destination -Force
}

$addonBuild = Join-Path $repo 'addon\build'
$shaderSource = Join-Path $repo 'addon\shaders'
$x86Build = Join-Path $repo 'x86-host\build'
$x86Template = Join-Path $repo 'x86-host\stage-template'
$feeder = Join-Path $repo 'external\DLSS5-Feeder'

$versionNumber = $Version.TrimStart('v')
$x64Source = Get-Content -LiteralPath (Join-Path $repo 'addon\src\nr-standalone.cpp') -Raw
$x86Source = Get-Content -LiteralPath (Join-Path $repo 'x86-host\src\aio-wrapper32.cpp') -Raw
if (-not $x64Source.Contains("ADDON_VERSION `"$versionNumber`"") -or
    -not $x86Source.Contains("FEED_VERSION `"$versionNumber`"")) {
    throw "Source version macros do not both match $Version. Rebuild after updating them."
}

# 64-bit: every project-owned file already sits at its final game-relative path.
Copy-RequiredFile (Join-Path $addonBuild 'standalone-dlssnr.addon64') (Join-Path $stage64 'standalone-dlssnr.addon64')
Copy-RequiredFile (Join-Path $addonBuild 'nvngx.dll') (Join-Path $stage64 'nvngx.dll')
Copy-RequiredFile (Join-Path $shaderSource 'DLSS5_AIO_Feed.fx') (Join-Path $stage64 'reshade-shaders\Shaders\DLSS5_AIO_Feed.fx')
Copy-RequiredFile (Join-Path $shaderSource 'StandaloneBoundary.fx') (Join-Path $stage64 'reshade-shaders\Shaders\StandaloneBoundary.fx')
Copy-RequiredFile (Join-Path $PSScriptRoot 'README-64-BIT.txt') (Join-Path $stage64 'README_FIRST.txt')

# 32-bit game-side capture/settings layer.
Copy-RequiredFile (Join-Path $x86Build 'x86\standalone-dlssnr.addon32') (Join-Path $stage32 'standalone-dlssnr.addon32')
Copy-RequiredFile (Join-Path $x86Template 'game\dlss5-feed.cfg') (Join-Path $stage32 'dlss5-aio-x86.cfg')
Copy-RequiredFile (Join-Path $repo 'x86-host\shaders\DLSS5_Feed.fx') (Join-Path $stage32 'reshade-shaders\Shaders\DLSS5_Feed.fx')
Copy-RequiredFile (Join-Path $feeder 'shaders\DLSS5_Feed.fx') (Join-Path $stage32 'reshade-shaders\Shaders\DLSS5_Feed_impl.fxh')
Copy-RequiredFile (Join-Path $repo 'x86-host\shaders\DLSS5_Feed_D3D9.fx') (Join-Path $stage32 'reshade-shaders\Shaders\DLSS5_Feed_D3D9.fx')
Copy-RequiredFile (Join-Path $PSScriptRoot 'README-32-BIT.txt') (Join-Path $stage32 'README_FIRST.txt')

# 32-bit host side: the normal x64 AIO implementation remains isolated here.
$hostDirectory = Join-Path $stage32 'host64'
Copy-RequiredFile (Join-Path $x86Build 'host64\AIO DLSS5 32-bit Wrapper.exe') (Join-Path $hostDirectory 'AIO DLSS5 32-bit Wrapper.exe')
Copy-RequiredFile (Join-Path $addonBuild 'standalone-dlssnr.addon64') (Join-Path $hostDirectory 'standalone-dlssnr.addon64')
Copy-RequiredFile (Join-Path $addonBuild 'nvngx.dll') (Join-Path $hostDirectory 'nvngx.dll')
Copy-RequiredFile (Join-Path $x86Template 'host64\ReShade.ini') (Join-Path $hostDirectory 'ReShade.ini')
Copy-RequiredFile (Join-Path $x86Template 'host64\ReShadePreset.ini') (Join-Path $hostDirectory 'ReShadePreset.ini')
Copy-RequiredFile (Join-Path $shaderSource 'DLSS5_AIO_Feed.fx') (Join-Path $hostDirectory 'reshade-shaders\Shaders\DLSS5_AIO_Feed.fx')
Copy-RequiredFile (Join-Path $shaderSource 'StandaloneBoundary.fx') (Join-Path $hostDirectory 'reshade-shaders\Shaders\StandaloneBoundary.fx')
Copy-RequiredFile (Join-Path $PSScriptRoot 'HOST64-REQUIRED-FILES.txt') (Join-Path $hostDirectory 'ADD_REQUIRED_64BIT_FILES_HERE.txt')

# Preserve attribution inside both redistributable archives.
foreach ($package in $stage64, $stage32) {
    Copy-RequiredFile (Join-Path $repo 'LICENSE') (Join-Path $package 'licenses\DLSS5-ReShade-AIO-LICENSE.txt')
    Copy-RequiredFile (Join-Path $repo 'NOTICE') (Join-Path $package 'licenses\DLSS5-ReShade-AIO-NOTICE.txt')
    Copy-RequiredFile (Join-Path $repo 'THIRD_PARTY_NOTICES.md') (Join-Path $package 'licenses\THIRD_PARTY_NOTICES.md')
}
Copy-RequiredFile (Join-Path $feeder 'LICENSE') (Join-Path $stage32 'licenses\DLSS5-Feeder-LICENSE.txt')

# Guard public packages against accidental proprietary-runtime bundling.
$forbidden = Get-ChildItem -LiteralPath $stage -Recurse -File | Where-Object {
    $_.Name -in @('nvngx_dlssnr.dll', 'nvngx_dlss.dll', 'nvngx_dlssg.dll', 'dxgi.dll')
}
if ($forbidden) {
    throw "Forbidden third-party binary entered release staging: $($forbidden.FullName -join ', ')"
}

$zip64 = Join-Path $dist ("DLSS5-ReShade-AIO-$Version-64-bit.zip")
$zip32 = Join-Path $dist ("DLSS5-ReShade-AIO-$Version-32-bit.zip")
foreach ($zip in $zip64, $zip32) {
    if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
}
Compress-Archive -Path (Join-Path $stage64 '*') -DestinationPath $zip64 -CompressionLevel Optimal
Compress-Archive -Path (Join-Path $stage32 '*') -DestinationPath $zip32 -CompressionLevel Optimal

Get-FileHash -Algorithm SHA256 -LiteralPath $zip64, $zip32 |
    ForEach-Object { "{0}  {1}" -f $_.Hash.ToLowerInvariant(), (Split-Path $_.Path -Leaf) } |
    Set-Content -LiteralPath (Join-Path $dist ("DLSS5-ReShade-AIO-$Version-SHA256.txt")) -Encoding ascii

Write-Host "Created:"
Write-Host "  $zip64"
Write-Host "  $zip32"
