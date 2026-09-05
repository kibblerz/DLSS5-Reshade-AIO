# Batman Arkham Knight A/B baseline — 2026-09-05

This note preserves the observations and measurements collected before testing the last stable release.

## Common test configuration

- Input/output: 2560x1440 -> 3840x2160
- Neural Rendering: enabled, model 3 / style 2
- DLSS: enabled, preset K
- Frame Generation: enabled
- VORT guides: disabled
- Game and addon settings were not changed between binary deployments.

## Explicit-pacing prototype

- Source commit: `62c6654` (`Explicitly pace generated output to monitor refresh`)
- Log: `arkham-explicit-pacing-20260905-0048/ReShade.log`
- Samples: 25
- Source FPS: 109.4 average
- Proxy FPS: 73.6 average
- Addon CPU: 1.064 ms
- NR: 7.964 ms
- SR: 2.331 ms
- FG: 2.065 ms
- Pipeline: 12.358 ms
- Stable gameplay was approximately 112 source FPS / 78 proxy FPS.
- User observation: motion appeared exceptionally/perfectly smooth.
- Presenter targeted the 120 Hz monitor cadence. Generated-to-real spacing was approximately 8.9 ms; real-to-generated spacing was generally 15-19 ms.

## v2.0.4-experimental.1 prerelease

- Published addon SHA-256: `7254642B51239B1DDBFBA1458DC29167F7CD9022863565BBB0587916D10A28B0`
- Log: `arkham-v2.0.4-experimental.1-20260905-0055/ReShade.log`
- Samples: 14
- Source FPS: 96.6 average
- Proxy FPS: 77.4 average, distorted upward by 130-132 FPS burst-counting while the ReShade overlay was open
- Addon CPU: 0.794 ms
- NR: 8.771 ms
- SR: 1.863 ms
- FG: 2.165 ms
- Pipeline: 12.807 ms
- Comparable stable gameplay was approximately 107 source FPS / 79 proxy FPS.
- User observation: visibly clunky and apparently considerably slower than the explicit-pacing prototype.

## Interpretation to carry forward

- The explicit-pacing prototype did not materially increase average displayed throughput, but it produced about 4-5% more real/source frames in comparable gameplay and reduced measured pipeline work by roughly 0.45 ms.
- The prerelease counter can count generated/real presentation bursts as high FPS even when cadence is uneven. Average FPS alone does not represent perceived smoothness.
- The user's strong smoothness preference for the explicit-pacing prototype is credible and should be evaluated with displayed-frame interval telemetry/PresentMon in subsequent comparisons.

## v2.0.3 stable-release comparison

- Log: `arkham-v2.0.3-20260905-0107/ReShade.log`
- Samples: 16
- Source FPS: 81.7 whole-run average; approximately 96 FPS during the sustained gameplay section
- Proxy FPS: 85.6 whole-run average; approximately 95 FPS during the sustained gameplay section
- NR: 8.244 ms
- SR: 1.419 ms
- FG: 1.903 ms
- Pipeline: 11.571 ms
- User observation: smoother than `v2.0.4-experimental.1`, but not as perfectly smooth as the explicit-pacing prototype.
- The log supports that ordering. v2.0.3 used uncapped generated/real pairs with essentially no swap wait and a roughly 0.4-0.6 ms presenter worker. It therefore avoided the prerelease's repeated 8-12 ms swap waits and 19-24 ms presenter-worker cycles, but did not deliberately space generated and real frames against the monitor refresh like the prototype.

## Three-way conclusion

1. `2.0.5-explicit-pacing-prototype`: best perceived smoothness; deliberate 120 Hz output spacing plus asynchronous NGX work.
2. `v2.0.3`: second-best perceived smoothness and the lowest measured GPU pipeline cost, but generated/real frames are emitted as uncapped pairs and can be uneven.
3. `v2.0.4-experimental.1`: worst perceived smoothness; unpredictable swap-chain backpressure stalls its presenter despite similar average FPS.

The apparent contradiction between FPS and smoothness is real: v2.0.3 reports more proxy frames than the prototype, while the prototype looks smoother because its frame delivery is better paced.

## Current deployment for the next comparison

Batman Arkham Knight now has stable release `v2.0.3`:

- `standalone-dlssnr.addon64`: `8BB82AC27B41963C1BC46865488EC6B71AB6EED15D596F0D48F8ED53C7288819`
- `nvngx.dll`: `9D3AB47559381EB78EF853D1D360932853839A80E0FDCDBEFCE2B7210BEE133A`
- `DLSS5_AIO_Feed.fx`: `B0EF9EE8F9C7675C0224B87A614905D4283363438BD7E104B132E7200AD84748`
