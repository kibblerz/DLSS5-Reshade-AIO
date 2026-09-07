param(
    [Parameter(Mandatory = $true)]
    [string] $Destination,
    [Parameter(Mandatory = $true)]
    [string] $ReShade64,
    [Parameter(Mandatory = $true)]
    [string] $RuntimeSource,
    [Parameter(Mandatory = $true)]
    [string] $ReShadeShaderSource,
    [string] $VortSource
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$game = $Destination
$hostDirectory = Join-Path $Destination 'host64'
$gameShaders = Join-Path $game 'reshade-shaders\Shaders'
$hostShaders = Join-Path $hostDirectory 'reshade-shaders\Shaders'
New-Item -ItemType Directory -Force -Path $game, $hostDirectory, $gameShaders, $hostShaders | Out-Null

Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'build\x86\standalone-dlssnr.addon32') -Destination (Join-Path $game 'standalone-dlssnr.addon32') -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'build\host64\AIO DLSS5 32-bit Wrapper.exe') -Destination (Join-Path $hostDirectory 'AIO DLSS5 32-bit Wrapper.exe') -Force
Copy-Item -LiteralPath (Join-Path $root 'addon\build\standalone-dlssnr.addon64') -Destination (Join-Path $hostDirectory 'standalone-dlssnr.addon64') -Force
Copy-Item -LiteralPath (Join-Path $root 'addon\build\nvngx.dll') -Destination (Join-Path $hostDirectory 'nvngx.dll') -Force
Copy-Item -LiteralPath $ReShade64 -Destination (Join-Path $hostDirectory 'dxgi.dll') -Force

foreach ($runtime in 'nvngx_dlssnr.dll', 'nvngx_dlss.dll', 'nvngx_dlssg.dll') {
    Copy-Item -LiteralPath (Join-Path $RuntimeSource $runtime) -Destination (Join-Path $hostDirectory $runtime) -Force
}
Copy-Item -LiteralPath (Join-Path $root 'addon\shaders\DLSS5_AIO_Feed.fx') -Destination (Join-Path $hostShaders 'DLSS5_AIO_Feed.fx') -Force
Copy-Item -LiteralPath (Join-Path $root 'external\DLSS5-Feeder\shaders\DLSS5_Feed.fx') -Destination (Join-Path $gameShaders 'DLSS5_Feed.fx') -Force
foreach ($include in 'ReShade.fxh', 'ReShadeUI.fxh') {
    Copy-Item -LiteralPath (Join-Path $ReShadeShaderSource $include) -Destination (Join-Path $gameShaders $include) -Force
    Copy-Item -LiteralPath (Join-Path $ReShadeShaderSource $include) -Destination (Join-Path $hostShaders $include) -Force
}

Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'stage-template\game\dlss5-feed.cfg') -Destination (Join-Path $game 'dlss5-aio-x86.cfg') -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'stage-template\host64\ReShade.ini') -Destination (Join-Path $hostDirectory 'ReShade.ini') -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'stage-template\host64\ReShadePreset.ini') -Destination (Join-Path $hostDirectory 'ReShadePreset.ini') -Force

if ($VortSource -and (Test-Path -LiteralPath $VortSource)) {
    Copy-Item -LiteralPath $VortSource -Destination (Join-Path $gameShaders 'VortShaders') -Recurse -Force
}

Write-Host "Staged experimental x86 client + x64 AIO carrier in $Destination"
