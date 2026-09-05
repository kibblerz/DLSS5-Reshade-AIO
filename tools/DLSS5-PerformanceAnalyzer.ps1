[CmdletBinding()]
param(
    [ValidateSet('Help', 'Record', 'Analyze')]
    [string]$Command = 'Help',
    [string]$ProcessName,
    [int]$TargetProcessId = 0,
    [ValidateRange(1, 86400)]
    [int]$Duration = 60,
    [ValidateRange(16, 5000)]
    [int]$IntervalMs = 100,
    [ValidateRange(0, 600)]
    [int]$WaitSeconds = 30,
    [string]$PresentMonExecutable,
    [string]$PresentMonOutput,
    [string[]]$Telemetry,
    [string[]]$PresentMonCsv,
    [string[]]$Log,
    [ValidateRange(0, 120)]
    [double]$WarmupSeconds = 3,
    [string]$Output
)

$ErrorActionPreference = 'Stop'
$script:TelemetryMagic = [uint32]0x4F494144
$script:TelemetryVersion = [uint32]1
$script:TelemetrySize = 320
$script:Invariant = [Globalization.CultureInfo]::InvariantCulture

function Show-Usage {
    @'
DLSS5 Performance Analyzer

Record the addon's synchronized shared-memory telemetry:
  DLSS5-PerformanceAnalyzer.cmd Record -ProcessName BatmanAK -Duration 90 -Output telemetry.csv

Record telemetry and run the official PresentMon console capture beside it:
  DLSS5-PerformanceAnalyzer.cmd Record -ProcessName BatmanAK -Duration 90 -Output telemetry.csv `
    -PresentMonExecutable C:\Tools\PresentMon.exe -PresentMonOutput presentmon.csv

Analyze new captures, old ReShade logs, or both:
  DLSS5-PerformanceAnalyzer.cmd Analyze -Telemetry telemetry.csv -PresentMonCsv presentmon.csv `
    -Log ReShade.log -Output report

During recording, Ctrl+Alt+B cycles:
  User settings -> addon disabled -> DLSS/DLAA only -> NR + DLSS/DLAA ->
  DLSS/DLAA + FG -> NR + DLSS/DLAA + FG -> restored user settings

Remain in each mode for at least 30 seconds. The first three seconds of every
segment are excluded by default. Benchmark overrides are not saved to the INI.
'@ | Write-Host
}

function Resolve-TargetProcessId {
    if ($TargetProcessId -gt 0) {
        $null = Get-Process -Id $TargetProcessId
        return $TargetProcessId
    }
    if ([string]::IsNullOrWhiteSpace($ProcessName)) {
        throw 'Record requires -ProcessName or -TargetProcessId.'
    }
    $name = [IO.Path]::GetFileNameWithoutExtension($ProcessName)
    $deadline = [DateTime]::UtcNow.AddSeconds($WaitSeconds)
    do {
        $matches = @(Get-Process -Name $name -ErrorAction SilentlyContinue)
        if ($matches.Count -eq 1) { return [int]$matches[0].Id }
        if ($matches.Count -gt 1) {
            throw "Multiple '$name' processes are running; use -TargetProcessId."
        }
        Start-Sleep -Milliseconds 250
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Process '$name' was not found."
}

function Open-TelemetryMapping([int]$ProcessId) {
    $name = "Local\DLSS5_AIO_Telemetry_$ProcessId"
    $deadline = [DateTime]::UtcNow.AddSeconds($WaitSeconds)
    do {
        try {
            return [IO.MemoryMappedFiles.MemoryMappedFile]::OpenExisting(
                $name, [IO.MemoryMappedFiles.MemoryMappedFileRights]::Read)
        }
        catch [IO.FileNotFoundException] {
            if ([DateTime]::UtcNow -ge $deadline) { throw }
            Start-Sleep -Milliseconds 250
        }
    } while ($true)
}

function Get-U32([byte[]]$Bytes, [int]$Offset) {
    return [BitConverter]::ToUInt32($Bytes, $Offset)
}
function Get-U64([byte[]]$Bytes, [int]$Offset) {
    return [BitConverter]::ToUInt64($Bytes, $Offset)
}
function Get-I64([byte[]]$Bytes, [int]$Offset) {
    return [BitConverter]::ToInt64($Bytes, $Offset)
}

function Get-ModeName([uint32]$Mode) {
    switch ($Mode) {
        1 { 'Addon disabled' }
        2 { 'DLSS/DLAA only' }
        3 { 'NR + DLSS/DLAA' }
        4 { 'DLSS/DLAA + FG' }
        5 { 'NR + DLSS/DLAA + FG' }
        default { 'User settings' }
    }
}

function Read-TelemetrySnapshot($View) {
    $bytes = New-Object byte[] $script:TelemetrySize
    for ($attempt = 0; $attempt -lt 20; $attempt++) {
        $before = $View.ReadInt64(16)
        if (($before -band 1) -ne 0) { Start-Sleep -Milliseconds 1; continue }
        $null = $View.ReadArray(0, $bytes, 0, $bytes.Length)
        $after = $View.ReadInt64(16)
        if ($before -ne $after -or ($after -band 1) -ne 0) { continue }
        if ((Get-U32 $bytes 0) -ne $script:TelemetryMagic) {
            throw 'Telemetry mapping has an invalid magic value.'
        }
        if ((Get-U32 $bytes 4) -ne $script:TelemetryVersion -or
            (Get-U32 $bytes 8) -lt $script:TelemetrySize) {
            throw "Unsupported telemetry ABI v$(Get-U32 $bytes 4), size $(Get-U32 $bytes 8)."
        }
        $mode = Get-U32 $bytes 44
        return [pscustomobject][ordered]@{
            CapturedUtc = [DateTime]::UtcNow.ToString('o')
            Sequence = $after
            QpcTimestamp = Get-I64 $bytes 24
            QpcFrequency = Get-I64 $bytes 32
            BenchmarkEpoch = Get-U32 $bytes 40
            BenchmarkMode = $mode
            Mode = Get-ModeName $mode
            Flags = ('0x{0:X8}' -f (Get-U32 $bytes 48))
            GraphicsApi = Get-U32 $bytes 52
            InputWidth = Get-U32 $bytes 56
            InputHeight = Get-U32 $bytes 60
            OutputWidth = Get-U32 $bytes 64
            OutputHeight = Get-U32 $bytes 68
            SourceFps = Get-U32 $bytes 72
            ProxyFps = Get-U32 $bytes 76
            ActiveNrModel = Get-U32 $bytes 80
            DlssPreset = Get-U32 $bytes 84
            PipelineSlotStates = ('0x{0:X3}' -f (Get-U32 $bytes 88))
            GpuPrepUs = Get-U32 $bytes 96
            GpuNrUs = Get-U32 $bytes 100
            GpuSrUs = Get-U32 $bytes 104
            GpuFgUs = Get-U32 $bytes 108
            GpuCleanupUs = Get-U32 $bytes 112
            GpuTotalUs = Get-U32 $bytes 116
            GpuVortUs = Get-U32 $bytes 120
            GpuFeedUs = Get-U32 $bytes 124
            GpuGuidesTotalUs = Get-U32 $bytes 128
            GpuProxyGeneratedUs = Get-U32 $bytes 132
            GpuProxyRealUs = Get-U32 $bytes 136
            GpuProxyTotalUs = Get-U32 $bytes 140
            AddonCpuCurrentUs = Get-U32 $bytes 144
            AddonCpuAverageUs = Get-U32 $bytes 148
            SourceFrameAverageUs = Get-U32 $bytes 152
            SourceFrameP99Us = Get-U32 $bytes 156
            CpuProxyMailboxUs = Get-U32 $bytes 160
            CpuProxyFenceWaitUs = Get-U32 $bytes 164
            CpuProxySwapWaitUs = Get-U32 $bytes 168
            CpuProxyPresentUs = Get-U32 $bytes 172
            CpuProxyWorkerUs = Get-U32 $bytes 176
            CpuSharedTelemetryUs = Get-U32 $bytes 180
            SourceFrameSequence = Get-U64 $bytes 184
            LastNeuralSourceSequence = Get-U64 $bytes 192
            NrFrames = Get-U64 $bytes 200
            SrFrames = Get-U64 $bytes 208
            FgFrames = Get-U64 $bytes 216
            FramesPresented = Get-U64 $bytes 224
            NeuralSkips = Get-U64 $bytes 232
            ProxySkips = Get-U64 $bytes 240
            ProxyCoalesced = Get-U64 $bytes 248
            ProxyTimeouts = Get-U64 $bytes 256
            DisplayBackpressureDrops = Get-U64 $bytes 264
            TemporalDiscontinuities = Get-U64 $bytes 272
            ProxyRequests = Get-U64 $bytes 280
            ProxyCompleted = Get-U64 $bytes 288
            TelemetrySamples = Get-U64 $bytes 296
            PrimarySwapchainAddress = ('0x{0:X}' -f (Get-U64 $bytes 304))
            ProxySwapchainAddress = ('0x{0:X}' -f (Get-U64 $bytes 312))
        }
    }
    throw 'Could not obtain a stable telemetry snapshot.'
}

function ConvertTo-CsvField([object]$Value) {
    $text = [string]$Value
    if ($text.IndexOfAny([char[]]",`"`r`n") -ge 0) {
        return '"' + $text.Replace('"', '""') + '"'
    }
    return $text
}

function Start-PresentMonCapture([int]$ProcessId, [string]$CapturePath) {
    if ([string]::IsNullOrWhiteSpace($PresentMonExecutable)) { return $null }
    $executable = [IO.Path]::GetFullPath($PresentMonExecutable)
    if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
        throw "PresentMon executable not found: $executable"
    }
    $arguments = '--process_id {0} --output_file "{1}" --timed {2} --terminate_after_timed --v2_metrics --qpc_time --write_display_metadata --track_frame_type --session_name DLSS5AIO-{0}' -f
        $ProcessId, $CapturePath.Replace('"', '\"'), $Duration
    return Start-Process -FilePath $executable -ArgumentList $arguments -PassThru -WindowStyle Hidden
}

function Invoke-Record {
    $processId = Resolve-TargetProcessId
    if ([string]::IsNullOrWhiteSpace($Output)) {
        $Output = "dlss5-telemetry-$processId-$(Get-Date -Format yyyyMMdd-HHmmss).csv"
    }
    $capturePath = [IO.Path]::GetFullPath($Output)
    $captureDirectory = Split-Path -Parent $capturePath
    if (-not (Test-Path -LiteralPath $captureDirectory)) {
        $null = New-Item -ItemType Directory -Path $captureDirectory
    }
    $mapping = Open-TelemetryMapping $processId
    $view = $mapping.CreateViewAccessor(0, $script:TelemetrySize,
        [IO.MemoryMappedFiles.MemoryMappedFileAccess]::Read)
    try {
        $initial = Read-TelemetrySnapshot $view
        Write-Host "Connected to PID $processId, telemetry ABI v$script:TelemetryVersion."
        if ([string]::IsNullOrWhiteSpace($PresentMonOutput)) {
            $PresentMonOutput = Join-Path $captureDirectory (([IO.Path]::GetFileNameWithoutExtension($capturePath)) + '-presentmon.csv')
        }
        $presentMonPath = [IO.Path]::GetFullPath($PresentMonOutput)
        $presentMonProcess = Start-PresentMonCapture $processId $presentMonPath
        if ($null -ne $presentMonProcess) { Write-Host "PresentMon capture: $presentMonPath" }

        $columns = @($initial.PSObject.Properties.Name)
        $writer = New-Object IO.StreamWriter($capturePath, $false, (New-Object Text.UTF8Encoding($false)))
        try {
            $writer.WriteLine((($columns | ForEach-Object { ConvertTo-CsvField $_ }) -join ','))
            $recordingClock = [Diagnostics.Stopwatch]::StartNew()
            $lastSequence = -1L
            while ($recordingClock.Elapsed.TotalSeconds -lt $Duration) {
                $sample = Read-TelemetrySnapshot $view
                if ($sample.Sequence -ne $lastSequence) {
                    $lastSequence = $sample.Sequence
                    $values = foreach ($column in $columns) { ConvertTo-CsvField $sample.$column }
                    $writer.WriteLine(($values -join ','))
                }
                Start-Sleep -Milliseconds $IntervalMs
            }
            $recordingClock.Stop()
        }
        finally { $writer.Dispose() }
        Write-Host "Telemetry capture complete: $capturePath"
        if ($null -ne $presentMonProcess) {
            if (-not $presentMonProcess.WaitForExit(15000)) {
                Write-Warning 'PresentMon is still stopping; its CSV may finalize shortly.'
            }
            elseif ($presentMonProcess.ExitCode -ne 0) {
                Write-Warning "PresentMon exited with code $($presentMonProcess.ExitCode)."
            }
            $presentMonProcess.Dispose()
        }
    }
    finally {
        $view.Dispose()
        $mapping.Dispose()
    }
}

function Get-Number($Row, [string]$Name) {
    $property = $Row.PSObject.Properties[$Name]
    if ($null -eq $property) { return [double]::NaN }
    $value = 0.0
    if ([double]::TryParse([string]$property.Value,
        [Globalization.NumberStyles]::Float, $script:Invariant, [ref]$value)) { return $value }
    return [double]::NaN
}
function Get-NumberAny($Row, [string[]]$Names) {
    foreach ($name in $Names) {
        $value = Get-Number $Row $name
        if (-not [double]::IsNaN($value)) { return $value }
    }
    return [double]::NaN
}
function Get-Mean([object[]]$Values) {
    $valid = @($Values | Where-Object { -not [double]::IsNaN([double]$_) })
    if ($valid.Count -eq 0) { return 0.0 }
    return [double](($valid | Measure-Object -Average).Average)
}
function Get-Percentile([object[]]$Values, [double]$Percentile) {
    $sorted = @($Values | Where-Object { -not [double]::IsNaN([double]$_) } | Sort-Object)
    if ($sorted.Count -eq 0) { return 0.0 }
    $position = ($sorted.Count - 1) * $Percentile / 100.0
    $lower = [Math]::Floor($position)
    $upper = [Math]::Ceiling($position)
    if ($lower -eq $upper) { return [double]$sorted[$lower] }
    return [double]$sorted[$lower] +
        (([double]$sorted[$upper] - [double]$sorted[$lower]) * ($position - $lower))
}
function Get-Delta([object[]]$Rows, [string]$Name) {
    if ($Rows.Count -lt 2) { return 0.0 }
    return (Get-Number $Rows[-1] $Name) - (Get-Number $Rows[0] $Name)
}

function Get-TelemetryReports([string]$Path) {
    $rows = @(Import-Csv -LiteralPath $Path)
    $groups = $rows | Group-Object { "$($_.BenchmarkEpoch)|$($_.Mode)" }
    foreach ($group in $groups) {
        $ordered = @($group.Group | Sort-Object { [int64]$_.QpcTimestamp })
        if ($ordered.Count -eq 0) { continue }
        $frequency = [Math]::Max(1.0, (Get-Number $ordered[0] 'QpcFrequency'))
        $cutoff = (Get-Number $ordered[0] 'QpcTimestamp') + ($WarmupSeconds * $frequency)
        $samples = @($ordered | Where-Object { (Get-Number $_ 'QpcTimestamp') -ge $cutoff })
        if ($samples.Count -lt 2) { $samples = $ordered }
        $durationSeconds = [Math]::Max(0.001,
            ((Get-Number $samples[-1] 'QpcTimestamp') - (Get-Number $samples[0] 'QpcTimestamp')) / $frequency)
        $average = { param($name) Get-Mean @($samples | ForEach-Object { Get-Number $_ $name }) }
        $rate = { param($name) (Get-Delta $samples $name) / $durationSeconds }
        [pscustomobject][ordered]@{
            File = [IO.Path]::GetFileName($Path)
            Epoch = [uint32]$samples[0].BenchmarkEpoch
            Mode = [string]$samples[0].Mode
            Contract = "$($samples[-1].InputWidth)x$($samples[-1].InputHeight) -> $($samples[-1].OutputWidth)x$($samples[-1].OutputHeight)"
            Samples = $samples.Count
            DurationSeconds = $durationSeconds
            StartQpc = [int64](Get-Number $samples[0] 'QpcTimestamp')
            EndQpc = [int64](Get-Number $samples[-1] 'QpcTimestamp')
            QpcFrequency = [int64]$frequency
            SourceFpsAverage = & $average 'SourceFps'
            ProxyFpsAverage = & $average 'ProxyFps'
            SourceRate = & $rate 'SourceFrameSequence'
            RealRate = & $rate 'SrFrames'
            PresentRate = & $rate 'FramesPresented'
            GpuPrepMs = (& $average 'GpuPrepUs') / 1000.0
            GpuNrMs = (& $average 'GpuNrUs') / 1000.0
            GpuSrMs = (& $average 'GpuSrUs') / 1000.0
            GpuFgMs = (& $average 'GpuFgUs') / 1000.0
            GpuTotalMs = (& $average 'GpuTotalUs') / 1000.0
            ProxyGpuMs = (& $average 'GpuProxyTotalUs') / 1000.0
            AddonCpuMs = (& $average 'AddonCpuCurrentUs') / 1000.0
            ProxyPresentCpuMs = (& $average 'CpuProxyPresentUs') / 1000.0
            SharedTelemetryCpuMs = (& $average 'CpuSharedTelemetryUs') / 1000.0
            NeuralSkips = Get-Delta $samples 'NeuralSkips'
            ProxySkips = Get-Delta $samples 'ProxySkips'
            Coalesced = Get-Delta $samples 'ProxyCoalesced'
            BackpressureDrops = Get-Delta $samples 'DisplayBackpressureDrops'
            PrimarySwapchainAddress = [string]$samples[-1].PrimarySwapchainAddress
            ProxySwapchainAddress = [string]$samples[-1].ProxySwapchainAddress
        }
    }
}

function Get-PresentMonReports([string]$Path, [object[]]$Segments) {
    $allRows = @(Import-Csv -LiteralPath $Path)
    $hasQpc = $allRows.Count -gt 0 -and $null -ne $allRows[0].PSObject.Properties['CPUStartQPC']
    $windows = @()
    if ($hasQpc -and $Segments.Count -gt 0) {
        foreach ($segment in $Segments) {
            $windows += [pscustomobject]@{
                Epoch = $segment.Epoch; Mode = $segment.Mode
                PrimarySwapchainAddress = $segment.PrimarySwapchainAddress
                ProxySwapchainAddress = $segment.ProxySwapchainAddress
                Rows = @($allRows | Where-Object {
                    $qpc = Get-Number $_ 'CPUStartQPC'
                    $qpc -ge $segment.StartQpc -and $qpc -le $segment.EndQpc
                })
            }
        }
    }
    else {
        $windows += [pscustomobject]@{
            Epoch = 0; Mode = 'Whole capture'; PrimarySwapchainAddress = ''
            ProxySwapchainAddress = ''; Rows = $allRows
        }
    }
    foreach ($window in $windows) {
      foreach ($group in ($window.Rows | Group-Object SwapChainAddress)) {
        $frameTimes = @($group.Group | ForEach-Object { Get-NumberAny $_ @('FrameTime', 'MsBetweenPresents') } |
            Where-Object { -not [double]::IsNaN($_) -and $_ -gt 0 })
        $displayedTimes = @($group.Group | ForEach-Object { Get-NumberAny $_ @('DisplayedTime', 'MsBetweenDisplayChange') } |
            Where-Object { -not [double]::IsNaN($_) -and $_ -gt 0 })
        $gpuBusy = @($group.Group | ForEach-Object { Get-NumberAny $_ @('GPUBusy', 'MsGPUBusy') })
        $gpuTime = @($group.Group | ForEach-Object { Get-NumberAny $_ @('GPUTime', 'MsGPUTime') })
        $latency = @($group.Group | ForEach-Object { Get-NumberAny $_ @('DisplayLatency', 'MsUntilDisplayed') })
        $meanFrame = Get-Mean $frameTimes
        $meanDisplay = Get-Mean $displayedTimes
        $p99 = Get-Percentile $frameTimes 99
        $p999 = Get-Percentile $frameTimes 99.9
        $dropped = @($group.Group | Where-Object {
            $_.Dropped -eq '1' -or $_.DisplayedTime -eq 'NA' -or $_.DisplayedTime -eq 'N/A'
        }).Count
        $normalizedAddress = ([string]$group.Name).ToUpperInvariant().TrimStart('0').TrimStart('X').TrimStart('0')
        $primaryAddress = ([string]$window.PrimarySwapchainAddress).ToUpperInvariant().TrimStart('0').TrimStart('X').TrimStart('0')
        $proxyAddress = ([string]$window.ProxySwapchainAddress).ToUpperInvariant().TrimStart('0').TrimStart('X').TrimStart('0')
        $role = if ($normalizedAddress -and $normalizedAddress -eq $primaryAddress) { 'Game' }
            elseif ($normalizedAddress -and $normalizedAddress -eq $proxyAddress) { 'Addon proxy' }
            else { 'Unclassified' }
        [pscustomobject][ordered]@{
            File = [IO.Path]::GetFileName($Path)
            Epoch = [uint32]$window.Epoch
            Mode = [string]$window.Mode
            Role = $role
            Swapchain = [string]$group.Name
            Runtime = [string]$group.Group[0].PresentRuntime
            PresentMode = [string]$group.Group[0].PresentMode
            Frames = $group.Count
            PresentedFps = if ($meanFrame -gt 0) { 1000.0 / $meanFrame } else { 0.0 }
            DisplayedFps = if ($meanDisplay -gt 0) { 1000.0 / $meanDisplay } else { 0.0 }
            P50Ms = Get-Percentile $frameTimes 50
            P95Ms = Get-Percentile $frameTimes 95
            P99Ms = $p99
            OnePercentLow = if ($p99 -gt 0) { 1000.0 / $p99 } else { 0.0 }
            PointOnePercentLow = if ($p999 -gt 0) { 1000.0 / $p999 } else { 0.0 }
            GpuBusyMs = Get-Mean $gpuBusy
            GpuTimeMs = Get-Mean $gpuTime
            DisplayLatencyMs = Get-Mean $latency
            Dropped = $dropped
        }
      }
    }
}

function Get-LogReport([string]$Path) {
    $pattern = 'performance telemetry: source=(?<source>\d+) fps.*?proxy=(?<proxy>\d+) fps; addon CPU current=(?<cpu>[\d.]+)ms.*?GPU prep=(?<prep>[\d.]+)ms NR=(?<nr>[\d.]+)ms (?:DLAA|DLSS SR)=(?<sr>[\d.]+)ms FG=(?<fg>[\d.]+)ms cleanup=(?<cleanup>[\d.]+)ms total=(?<total>[\d.]+)ms; skips neural=(?<nskip>\d+) proxy=(?<pskip>\d+)'
    $samples = foreach ($line in [IO.File]::ReadLines([IO.Path]::GetFullPath($Path))) {
        if ($line -match $pattern) {
            [pscustomobject]@{
                Source = [double]$Matches.source; Proxy = [double]$Matches.proxy
                Cpu = [double]$Matches.cpu; Prep = [double]$Matches.prep
                Nr = [double]$Matches.nr; Sr = [double]$Matches.sr
                Fg = [double]$Matches.fg; Cleanup = [double]$Matches.cleanup
                Total = [double]$Matches.total; NeuralSkip = [double]$Matches.nskip
                ProxySkip = [double]$Matches.pskip
            }
        }
    }
    $samples = @($samples)
    $avg = { param($name) Get-Mean @($samples | ForEach-Object { $_.$name }) }
    [pscustomobject][ordered]@{
        File = [IO.Path]::GetFileName($Path); Samples = $samples.Count
        SourceFps = & $avg 'Source'; ProxyFps = & $avg 'Proxy'
        AddonCpuMs = & $avg 'Cpu'; GpuPrepMs = & $avg 'Prep'
        GpuNrMs = & $avg 'Nr'; GpuSrMs = & $avg 'Sr'; GpuFgMs = & $avg 'Fg'
        GpuCleanupMs = & $avg 'Cleanup'; GpuTotalMs = & $avg 'Total'
        FinalNeuralSkips = if ($samples.Count) { $samples[-1].NeuralSkip } else { 0 }
        FinalProxySkips = if ($samples.Count) { $samples[-1].ProxySkip } else { 0 }
    }
}

function Invoke-Analyze {
    if ([string]::IsNullOrWhiteSpace($Output)) {
        $Output = "dlss5-report-$(Get-Date -Format yyyyMMdd-HHmmss)"
    }
    $reportDirectory = [IO.Path]::GetFullPath($Output)
    if (-not (Test-Path -LiteralPath $reportDirectory)) {
        $null = New-Item -ItemType Directory -Path $reportDirectory
    }
    $telemetryReports = @($Telemetry | Where-Object { $_ } | ForEach-Object { Get-TelemetryReports ([IO.Path]::GetFullPath($_)) })
    $presentMonReports = @($PresentMonCsv | Where-Object { $_ } | ForEach-Object { Get-PresentMonReports ([IO.Path]::GetFullPath($_)) $telemetryReports })
    $logReports = @($Log | Where-Object { $_ } | ForEach-Object { Get-LogReport ([IO.Path]::GetFullPath($_)) })
    if ($telemetryReports.Count + $presentMonReports.Count + $logReports.Count -eq 0) {
        throw 'Analyze requires -Telemetry, -PresentMonCsv, or -Log.'
    }
    $result = [ordered]@{
        GeneratedUtc = [DateTime]::UtcNow.ToString('o')
        TelemetrySegments = $telemetryReports
        PresentMonSwapchains = $presentMonReports
        AddonLogs = $logReports
    }
    $jsonPath = Join-Path $reportDirectory 'report.json'
    $result | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath $jsonPath -Encoding UTF8

    $markdown = New-Object Collections.Generic.List[string]
    $markdown.Add('# DLSS5 AIO performance report')
    $markdown.Add('')
    $markdown.Add("Generated: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss zzz')")
    $markdown.Add('')
    if ($telemetryReports.Count) {
        $markdown.Add('## Synchronized addon segments'); $markdown.Add('')
        $markdown.Add('| Segment | Mode | Contract | Source FPS | Accepted real FPS | Proxy presents/s | NR ms | SR/DLAA ms | FG ms | Pipeline ms | Proxy GPU ms | Addon CPU ms | Telemetry CPU ms | Skips |')
        $markdown.Add('|---:|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|')
        foreach ($row in $telemetryReports) {
            $skips = $row.NeuralSkips + $row.ProxySkips
            $markdown.Add(('| {0} | {1} | {2} | {3:F1} | {4:F1} | {5:F1} | {6:F3} | {7:F3} | {8:F3} | {9:F3} | {10:F3} | {11:F3} | {12:F3} | {13:F0} |' -f
                $row.Epoch, $row.Mode, $row.Contract, $row.SourceRate, $row.RealRate,
                $row.PresentRate, $row.GpuNrMs, $row.GpuSrMs, $row.GpuFgMs,
                $row.GpuTotalMs, $row.ProxyGpuMs, $row.AddonCpuMs,
                $row.SharedTelemetryCpuMs, $skips))
        }
        $markdown.Add('')
    }
    if ($presentMonReports.Count) {
        $markdown.Add('## PresentMon swapchains'); $markdown.Add('')
        $markdown.Add('Do not combine swapchains. Correlate the game and addon proxy rows with the addon source/proxy rates.'); $markdown.Add('')
        $markdown.Add('| Segment | Mode | Role | Swapchain | Runtime/mode | Frames | Presented FPS | Display FPS | 1% low | 0.1% low | P99 ms | GPU busy ms | Display latency ms | Dropped |')
        $markdown.Add('|---:|---|---|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|')
        foreach ($row in $presentMonReports) {
            $markdown.Add(('| {0} | {1} | {2} | `{3}` | {4} / {5} | {6} | {7:F1} | {8:F1} | {9:F1} | {10:F1} | {11:F3} | {12:F3} | {13:F3} | {14} |' -f
                $row.Epoch, $row.Mode, $row.Role, $row.Swapchain, $row.Runtime, $row.PresentMode, $row.Frames,
                $row.PresentedFps, $row.DisplayedFps, $row.OnePercentLow,
                $row.PointOnePercentLow, $row.P99Ms, $row.GpuBusyMs,
                $row.DisplayLatencyMs, $row.Dropped))
        }
        $markdown.Add('')
    }
    if ($logReports.Count) {
        $markdown.Add('## Existing addon logs'); $markdown.Add('')
        $markdown.Add('| Log | Samples | Source FPS | Proxy FPS | Addon CPU ms | NR ms | SR/DLAA ms | FG ms | Pipeline ms | Final skips |')
        $markdown.Add('|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|')
        foreach ($row in $logReports) {
            $skips = $row.FinalNeuralSkips + $row.FinalProxySkips
            $markdown.Add(('| {0} | {1} | {2:F1} | {3:F1} | {4:F3} | {5:F3} | {6:F3} | {7:F3} | {8:F3} | {9:F0} |' -f
                $row.File, $row.Samples, $row.SourceFps, $row.ProxyFps,
                $row.AddonCpuMs, $row.GpuNrMs, $row.GpuSrMs, $row.GpuFgMs,
                $row.GpuTotalMs, $skips))
        }
        $markdown.Add('')
    }
    $markdown.Add('## Interpretation'); $markdown.Add('')
    $markdown.Add('- Compare **addon disabled** with **DLSS only** to see combined proxy plus DLSS cost.')
    $markdown.Add('- Compare **DLSS only** with **NR + DLSS** to isolate NR cost.')
    $markdown.Add('- Compare FG modes using both source FPS and displayed/proxy FPS. Generated frames must not be counted as recovered source cadence.')
    $markdown.Add('- A true no-addon baseline requires a separate launch with `standalone-dlssnr.addon64` physically absent.')
    $markdownPath = Join-Path $reportDirectory 'report.md'
    $markdown | Set-Content -LiteralPath $markdownPath -Encoding UTF8
    Write-Host "Report written to $markdownPath"
    Write-Host "Machine-readable data written to $jsonPath"
}

switch ($Command) {
    'Record' { Invoke-Record }
    'Analyze' { Invoke-Analyze }
    default { Show-Usage }
}
