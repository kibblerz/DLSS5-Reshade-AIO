$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot

if (-not (Test-Path -LiteralPath '.\nr-lab.exe')) {
    & cmd.exe /c build-lab.bat
    if ($LASTEXITCODE -ne 0) { throw "Laboratory build failed with exit code $LASTEXITCODE" }
}

$cases = [System.Collections.Generic.List[object]]::new()
foreach ($model in 1..3) {
    foreach ($style in 0..3) {
        foreach ($quality in 0..5) {
            & .\nr-lab.exe --profile srgb --model $model --style $style --perf-quality $quality `
                --input 320x180 --output 640x360 --frames 1 --nr-only *> $null
            $exitCode = $LASTEXITCODE
            $result = if (Test-Path -LiteralPath '.\nr-lab-result.json') {
                Get-Content -LiteralPath '.\nr-lab-result.json' -Raw | ConvertFrom-Json
            } else { $null }
            $cases.Add([pscustomobject]@{
                model = $model
                style = $style
                perfQuality = $quality
                exitCode = $exitCode
                created = if ($null -ne $result) { [bool]$result.created } else { $false }
                evaluationsSucceeded = if ($null -ne $result) { [int]$result.evaluationsSucceeded } else { 0 }
                resolvedScalingRatio = if ($null -ne $result) { [double]$result.nrResolvedScalingRatio } else { $null }
                scalingCallbackResult = if ($null -ne $result) { [string]$result.nrScalingCallbackResult } else { $null }
                scalingCallbackSucceeded = if ($null -ne $result) { [bool]$result.nrScalingCallbackSucceeded } else { $false }
                changedPercent = if ($null -ne $result) { [double]$result.changedPercent } else { $null }
                quadrantChangedPercent = if ($null -ne $result) { @($result.quadrantChangedPercent) } else { @() }
                checksum = if ($null -ne $result) { [string]$result.checksumFnv1a64 } else { $null }
                runtimeParamsCalls = if ($null -ne $result) { [int]$result.runtimeParamsCalls } else { 0 }
                statsOptLevel = if ($null -ne $result) { [int]$result.nrStatsOptLevel } else { 0 }
                upscalingValidated = if ($null -ne $result) { [bool]$result.upscalingValidated } else { $false }
            })
            Write-Host ("model={0} style={1} quality={2} exit={3} ratio={4} coverage={5}" -f `
                $model, $style, $quality, $exitCode, $cases[$cases.Count - 1].resolvedScalingRatio,
                $cases[$cases.Count - 1].changedPercent)
        }
    }
}

$summary = [ordered]@{
    generatedAt = (Get-Date).ToString('o')
    input = '320x180'
    output = '640x360'
    caseCount = $cases.Count
    createdCount = @($cases | Where-Object created).Count
    evaluatedCount = @($cases | Where-Object { $_.evaluationsSucceeded -eq 1 }).Count
    fullCoverageCount = @($cases | Where-Object upscalingValidated).Count
    distinctResolvedScalingRatios = @($cases.resolvedScalingRatio | Where-Object { $null -ne $_ } | Sort-Object -Unique)
    distinctChangedPercent = @($cases.changedPercent | Where-Object { $null -ne $_ } | Sort-Object -Unique)
    distinctChecksums = @($cases.checksum | Where-Object { $null -ne $_ } | Sort-Object -Unique)
    runtimeCallbackObservedCount = @($cases | Where-Object { $_.runtimeParamsCalls -gt 0 }).Count
    statsOptLevels = @($cases.statsOptLevel | Sort-Object -Unique)
    cases = $cases
}

$summary | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath '.\private-contract-matrix.json' -Encoding utf8
Write-Host "Wrote $PSScriptRoot\private-contract-matrix.json"
