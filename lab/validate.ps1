$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

& cmd.exe /c build-lab.bat
if ($LASTEXITCODE -ne 0) { throw "Laboratory build failed with exit code $LASTEXITCODE" }

& cmd.exe /c run-matrix.bat
if ($LASTEXITCODE -ne 0) { throw "Profile/model matrix failed with exit code $LASTEXITCODE" }

# Establish the feature-18 boundary independently from the working two-stage
# pipeline. The tested package advertises a provider scaling ratio of 1.0 and
# writes only the render-sized top-left quadrant of a 2x target. Exit 5 is the
# expected, useful result: NR is active, but direct NR upscaling is disproven.
& .\nr-lab.exe --profile srgb --model 1 --input 640x360 --output 1280x720 --frames 1 --nr-only
if ($LASTEXITCODE -ne 5) { throw "NR-only boundary probe returned unexpected exit code $LASTEXITCODE" }
$nrOnly = Get-Content -LiteralPath .\nr-lab-result.json -Raw | ConvertFrom-Json
Copy-Item -LiteralPath .\nr-lab-result.json -Destination .\result-nr-only-boundary.json -Force
if ($nrOnly.upscalingValidated -or [math]::Abs($nrOnly.changedPercent - 25.0) -gt 0.01 -or
    [math]::Abs($nrOnly.nrResolvedScalingRatio - 1.0) -gt 0.0001) {
    throw 'NR-only boundary changed: expected 1.0 provider ratio and exactly one changed quadrant'
}

$cases = Get-ChildItem -LiteralPath $PSScriptRoot -Filter 'result-*-model*.json' | ForEach-Object {
    Get-Content -LiteralPath $_.FullName -Raw | ConvertFrom-Json
}
if ($cases.Count -ne 9) { throw "Expected 9 matrix results, found $($cases.Count)" }
if (($cases | Where-Object { -not $_.upscalingValidated }).Count -ne 0) { throw 'At least one matrix case did not pass' }
if (($cases | Where-Object { $_.changedPercent -le 95 }).Count -ne 0) { throw 'At least one matrix case did not cover the native output' }

# Regression for drivers that reject the optional UltraQuality enum. This
# common 21:9 scaling contract previously reached SR creation with quality=4
# and failed with NVSDK_NGX_Result_FAIL_UnsupportedParameter (0xBAD00010).
& .\nr-lab.exe --profile srgb --model 1 --input 2560x1080 --output 3440x1440 --frames 3 --compact-nr
if ($LASTEXITCODE -ne 0) { throw "Ultrawide SR compatibility probe failed with exit code $LASTEXITCODE" }
$ultrawide = Get-Content -LiteralPath .\nr-lab-result.json -Raw | ConvertFrom-Json
Copy-Item -LiteralPath .\nr-lab-result.json -Destination .\result-ultrawide-2560x1080-to-3440x1440.json -Force
if (-not $ultrawide.upscalingValidated -or $ultrawide.changedPercent -le 95) {
    throw 'Ultrawide SR compatibility probe did not cover the native output'
}

$intensityChecks = @()
foreach ($intensity in @(0.0, 0.5, 1.0)) {
    & .\nr-lab.exe --profile hdr10 --model 1 --input 960x540 --output 1920x1080 --frames 4 --intensity $intensity --compact-nr
    if ($LASTEXITCODE -ne 0) { throw "Intensity $intensity probe failed with exit code $LASTEXITCODE" }
    $result = Get-Content -LiteralPath .\nr-lab-result.json -Raw | ConvertFrom-Json
    $copy = "result-intensity-$($intensity.ToString('0.0', [Globalization.CultureInfo]::InvariantCulture)).json"
    Copy-Item -LiteralPath .\nr-lab-result.json -Destination $copy -Force
    $intensityChecks += [pscustomobject]@{
        intensity = $intensity
        checksum = $result.checksumFnv1a64
        pass = $result.upscalingValidated
    }
}
if (($intensityChecks.checksum | Select-Object -Unique).Count -lt 2) {
    throw 'NR intensity did not change the final native-output checksum; the NR stage may have been bypassed'
}

$summary = [ordered]@{
    pass = $true
    matrixCases = 9
    profiles = @('sRGB', 'scRGB', 'HDR10/PQ')
    models = @(1, 2, 3)
    fullCoverage = $true
    nrMateriallyActive = $true
    nrDirectUpscalingAdvertised = $false
    nrProviderScalingRatio = $nrOnly.nrResolvedScalingRatio
    compactNrResources = $true
    ultrawideSrCompatibility = $true
    intensityChecks = $intensityChecks
    generatedAt = (Get-Date).ToString('o')
}
$summary | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath .\validation-summary.json -Encoding utf8
$summary | ConvertTo-Json -Depth 4
