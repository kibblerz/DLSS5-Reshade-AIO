param(
    [int]$TimeoutSeconds = 20
)

$ErrorActionPreference = 'Stop'
$lab = Join-Path $PSScriptRoot 'nr-lab.exe'
if (-not (Test-Path -LiteralPath $lab)) { throw "Build nr-lab.exe first." }

$cases = [System.Collections.Generic.List[object]]::new()
function Add-Case([string]$Name, [string[]]$Arguments) {
    $cases.Add([pscustomobject]@{ Name = $Name; Arguments = $Arguments })
}

Add-Case 'baseline-manual-mask' @('--auto-mask','0','--ui-correction','0')
Add-Case 'baseline-auto-mask' @('--auto-mask','1','--ui-correction','0')

foreach ($format in @('r8','r16f','rgba8','rg16f','rgba16f','r32f')) {
    foreach ($variant in 0,1,2) {
        Add-Case "control-mask-$format-v$variant" @('--optional','control-mask','--optional-format',$format,
            '--optional-variant',"$variant",'--auto-mask','0','--ui-correction','0')
    }
}
foreach ($format in @('rgba8','rgb10a2','rgba16f')) {
    foreach ($variant in 0,2) {
        Add-Case "ui-$format-v$variant" @('--optional','ui','--optional-format',$format,
            '--optional-variant',"$variant",'--auto-mask','0','--ui-correction','1')
        Add-Case "backbuffer-$format-v$variant" @('--optional','backbuffer','--optional-format',$format,
            '--optional-variant',"$variant",'--auto-mask','0','--ui-correction','1')
    }
}
foreach ($format in @('r8','r16f','r32f')) {
    foreach ($variant in 0,1,2) {
        Add-Case "ui-alpha-$format-v$variant" @('--optional','ui-alpha','--optional-format',$format,
            '--optional-variant',"$variant",'--auto-mask','0','--ui-correction','1')
    }
}
foreach ($format in @('r8','r16f','rg16f','rgba16f','r32f','rg32f','rgba32f')) {
    foreach ($variant in 0,2) {
        Add-Case "distortion-$format-v$variant" @('--optional','distortion','--optional-format',$format,
            '--optional-variant',"$variant",'--auto-mask','0','--ui-correction','0')
    }
}
foreach ($format in @('rgba8','rgba16f')) {
    foreach ($correction in 0,1) {
        foreach ($variant in 0,2) {
            Add-Case "ui-bundle-$format-c$correction-v$variant" @('--optional','ui-bundle',
                '--optional-format',$format,'--optional-variant',"$variant",'--auto-mask','0',
                '--ui-correction',"$correction")
        }
    }
}

function Compare-Bytes([byte[]]$Baseline, [byte[]]$Candidate) {
    if ($null -eq $Baseline -or $Baseline.Length -ne $Candidate.Length) {
        return [pscustomobject]@{ meanAbsoluteByteDelta = $null; changedBytePercent = $null }
    }
    [double]$sum = 0
    [long]$changed = 0
    for ($i = 0; $i -lt $Candidate.Length; $i++) {
        $delta = [Math]::Abs([int]$Candidate[$i] - [int]$Baseline[$i])
        $sum += $delta
        if ($delta -ne 0) { $changed++ }
    }
    [pscustomobject]@{
        meanAbsoluteByteDelta = [Math]::Round($sum / $Candidate.Length, 6)
        changedBytePercent = [Math]::Round(100.0 * $changed / $Candidate.Length, 6)
    }
}

$common = @('--input','160x90','--output','160x90','--frames','1','--nr-only','--style','0')
$results = [System.Collections.Generic.List[object]]::new()
[byte[]]$baselineBytes = $null
foreach ($case in $cases) {
    $stdout = Join-Path $PSScriptRoot 'optional-probe.stdout.txt'
    $stderr = Join-Path $PSScriptRoot 'optional-probe.stderr.txt'
    $resultPath = Join-Path $PSScriptRoot 'nr-lab-result.json'
    $ppmPath = Join-Path $PSScriptRoot 'nr-lab-output-model1-srgb.ppm'
    Remove-Item -LiteralPath $resultPath,$ppmPath -Force -ErrorAction SilentlyContinue
    $process = Start-Process -FilePath $lab -ArgumentList ($common + $case.Arguments) -WorkingDirectory $PSScriptRoot `
        -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        $process.Kill($true)
        $results.Add([pscustomobject]@{ name=$case.Name; exitCode=$null; timedOut=$true; created=$false;
            evaluationsSucceeded=0; checksum=$null; meanAbsoluteByteDelta=$null; changedBytePercent=$null })
        continue
    }
    $json = if (Test-Path -LiteralPath $resultPath) { Get-Content -LiteralPath $resultPath -Raw | ConvertFrom-Json } else { $null }
    [byte[]]$candidateBytes = if (Test-Path -LiteralPath $ppmPath) { [IO.File]::ReadAllBytes($ppmPath) } else { $null }
    if ($case.Name -eq 'baseline-manual-mask' -and $null -ne $candidateBytes) { $baselineBytes = $candidateBytes }
    $difference = if ($null -ne $candidateBytes) { Compare-Bytes $baselineBytes $candidateBytes } else {
        [pscustomobject]@{ meanAbsoluteByteDelta = $null; changedBytePercent = $null }
    }
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'nr-lab.log') `
        -Destination (Join-Path $PSScriptRoot "optional-$($case.Name).log") -Force
    $results.Add([pscustomobject]@{
        name = $case.Name
        exitCode = $process.ExitCode
        timedOut = $false
        created = if ($null -ne $json) { $json.created } else { $false }
        evaluationsSucceeded = if ($null -ne $json) { $json.evaluationsSucceeded } else { 0 }
        checksum = if ($null -ne $json) { $json.checksumFnv1a64 } else { $null }
        meanAbsoluteByteDelta = $difference.meanAbsoluteByteDelta
        changedBytePercent = $difference.changedBytePercent
    })
    Write-Host "$($case.Name): exit=$($process.ExitCode) eval=$($json.evaluationsSucceeded) checksum=$($json.checksumFnv1a64) delta=$($difference.meanAbsoluteByteDelta)"
}

$summary = [pscustomobject]@{
    generatedAt = (Get-Date).ToString('o')
    runtime = (Get-Item -LiteralPath (Join-Path $PSScriptRoot 'nvngx_dlssnr.dll')).VersionInfo.ProductVersion
    cases = $results
}
$summary | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $PSScriptRoot 'optional-contract-probe.json') -Encoding utf8
Remove-Item -LiteralPath $stdout,$stderr -Force -ErrorAction SilentlyContinue
