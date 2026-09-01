$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

& cmd.exe /c build-lab.bat
if ($LASTEXITCODE -ne 0) { throw "Laboratory build failed with exit code $LASTEXITCODE" }

& cmd.exe /c run-matrix.bat
if ($LASTEXITCODE -ne 0) { throw "Profile/model matrix failed with exit code $LASTEXITCODE" }

$cases = Get-ChildItem -LiteralPath $PSScriptRoot -Filter 'result-*-model*.json' | ForEach-Object {
    Get-Content -LiteralPath $_.FullName -Raw | ConvertFrom-Json
}
if ($cases.Count -ne 9) { throw "Expected 9 matrix results, found $($cases.Count)" }
if (($cases | Where-Object { -not $_.upscalingValidated }).Count -ne 0) { throw 'At least one matrix case did not pass' }
if (($cases | Where-Object { $_.changedPercent -le 95 }).Count -ne 0) { throw 'At least one matrix case did not cover the native output' }

$intensityChecks = @()
foreach ($intensity in @(0.0, 0.5, 1.0)) {
    & .\nr-lab.exe --profile hdr10 --model 1 --input 960x540 --output 1920x1080 --frames 4 --intensity $intensity
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
    intensityChecks = $intensityChecks
    generatedAt = (Get-Date).ToString('o')
}
$summary | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath .\validation-summary.json -Encoding utf8
$summary | ConvertTo-Json -Depth 4
