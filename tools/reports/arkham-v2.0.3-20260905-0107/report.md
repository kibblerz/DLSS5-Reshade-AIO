# DLSS5 AIO performance report

Generated: 2026-09-05 01:07:45 -04:00

## Existing addon logs

| Log | Samples | Source FPS | Proxy FPS | Addon CPU ms | NR ms | SR/DLAA ms | FG ms | Pipeline ms | Final skips |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| ReShade.log | 16 | 81.7 | 85.6 | 3.708 | 8.244 | 1.419 | 1.903 | 11.571 | 3053 |

## Interpretation

- Compare **addon disabled** with **DLSS only** to see combined proxy plus DLSS cost.
- Compare **DLSS only** with **NR + DLSS** to isolate NR cost.
- Compare FG modes using both source FPS and displayed/proxy FPS. Generated frames must not be counted as recovered source cadence.
- A true no-addon baseline requires a separate launch with `standalone-dlssnr.addon64` physically absent.
