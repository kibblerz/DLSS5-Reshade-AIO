$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot

if (-not (Test-Path -LiteralPath '.\nr-lab.exe')) {
    & cmd.exe /c build-lab.bat
    if ($LASTEXITCODE -ne 0) { throw "Laboratory build failed with exit code $LASTEXITCODE" }
}

$cases = foreach ($scale in 0.25, 0.5, 0.75, 1.0, 1.5, 2.0) {
    & .\nr-lab.exe --profile srgb --model 1 --style 0 --perf-quality 2 `
        --runtime-scale $scale --input 320x180 --output 640x360 --frames 1 --nr-only *> $null
    $exitCode = $LASTEXITCODE
    $result = Get-Content -LiteralPath '.\nr-lab-result.json' -Raw | ConvertFrom-Json
    [pscustomobject]@{
        requestedRuntimeScale = $scale
        exitCode = $exitCode
        created = [bool]$result.created
        evaluationsSucceeded = [int]$result.evaluationsSucceeded
        providerCreationScale = [double]$result.nrResolvedScalingRatio
        providerScalingCallbackResult = [string]$result.nrScalingCallbackResult
        runtimeParamsCalls = [int]$result.runtimeParamsCalls
        changedPercent = [double]$result.changedPercent
        quadrantChangedPercent = @($result.quadrantChangedPercent)
        checksum = [string]$result.checksumFnv1a64
        upscalingValidated = [bool]$result.upscalingValidated
    }
}

$summary = [ordered]@{
    generatedAt = (Get-Date).ToString('o')
    input = '320x180'
    output = '640x360'
    caseCount = @($cases).Count
    fullCoverageCount = @($cases | Where-Object upscalingValidated).Count
    distinctChangedPercent = @($cases.changedPercent | Sort-Object -Unique)
    distinctChecksums = @($cases.checksum | Sort-Object -Unique)
    cases = @($cases)
}
$summary | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath '.\runtime-scale-probe.json' -Encoding utf8
$summary | ConvertTo-Json -Depth 5
