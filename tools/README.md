# DLSS5 Performance Analyzer

This dependency-free Windows tool measures the addon's source cadence, accepted neural cadence, generated/presented cadence, per-stage GPU time, CPU submission cost, skips, coalescing, and compositor backpressure. It can also launch the official PresentMon console application and correlate its per-swapchain frame data with the addon's versioned shared-memory telemetry.

The analyzer does not modify the game. The addon publishes a read-only snapshot named `Local\DLSS5_AIO_Telemetry_<game PID>` using a seqlock so sampling never stalls `Present`.

## Quick benchmark

1. Start the game with the performance-lab addon build.
2. Open a command prompt in this folder.
3. Start a 180-second capture, replacing the process name:

```bat
DLSS5-PerformanceAnalyzer.cmd Record -ProcessName BatmanAK -Duration 180 -Output batman-telemetry.csv
```

4. Return to the game. Press `Ctrl+Alt+B` to advance through the benchmark modes. Remain in each desired mode for at least 30 seconds.
5. Generate the report:

```bat
DLSS5-PerformanceAnalyzer.cmd Analyze -Telemetry batman-telemetry.csv -Log "C:\path\to\ReShade.log" -Output batman-report
```

The report directory contains a readable `report.md` and machine-readable `report.json`.

## PresentMon capture

Download the official PresentMon console executable separately, then supply its path:

```bat
DLSS5-PerformanceAnalyzer.cmd Record -ProcessName BatmanAK -Duration 180 -Output batman-telemetry.csv -PresentMonExecutable "C:\Tools\PresentMon.exe" -PresentMonOutput batman-presentmon.csv
```

Analyze both streams together:

```bat
DLSS5-PerformanceAnalyzer.cmd Analyze -Telemetry batman-telemetry.csv -PresentMonCsv batman-presentmon.csv -Log "C:\path\to\ReShade.log" -Output batman-report
```

PresentMon sees the game's swapchain and the addon's proxy swapchain independently. The report intentionally keeps them in separate rows. Compare their rates with the addon's `SourceRate` and `PresentRate` rather than averaging the swapchains together.

## Benchmark modes

`Ctrl+Alt+B` cycles these temporary modes:

1. User settings
2. Addon disabled
3. DLSS/DLAA only
4. NR + DLSS/DLAA
5. DLSS/DLAA + Frame Generation
6. NR + DLSS/DLAA + Frame Generation
7. Restore the captured user settings

The mode and segment number appear briefly in the output and are embedded in every telemetry sample. Feature changes can take a moment while NGX handles are recreated, so the analyzer discards the first three seconds of each segment by default. Use `-WarmupSeconds` to change that.

The addon-disabled segment measures the loaded addon's bypass cost. A true baseline still requires another game launch with `standalone-dlssnr.addon64` physically absent. Use the same scene, resolution, display mode, frame cap, and capture duration for both runs.

## Existing logs only

The analyzer can summarize logs from released builds even without a shared-memory capture:

```bat
DLSS5-PerformanceAnalyzer.cmd Analyze -Log "C:\path\to\ReShade.log" -Output old-build-report
```
