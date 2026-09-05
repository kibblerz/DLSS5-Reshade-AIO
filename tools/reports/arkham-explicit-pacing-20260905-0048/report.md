# DLSS5 AIO performance report

Generated: 2026-09-05 00:52:36 -04:00

## Existing addon logs

| Log | Samples | Source FPS | Proxy FPS | Addon CPU ms | NR ms | SR/DLAA ms | FG ms | Pipeline ms | Final skips |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| ReShade.log | 25 | 109.4 | 73.6 | 1.064 | 7.964 | 2.331 | 2.065 | 12.358 | 9416 |

## Interpretation

- Compare **addon disabled** with **DLSS only** to see combined proxy plus DLSS cost.
- Compare **DLSS only** with **NR + DLSS** to isolate NR cost.
- Compare FG modes using both source FPS and displayed/proxy FPS. Generated frames must not be counted as recovered source cadence.
- A true no-addon baseline requires a separate launch with `standalone-dlssnr.addon64` physically absent.
