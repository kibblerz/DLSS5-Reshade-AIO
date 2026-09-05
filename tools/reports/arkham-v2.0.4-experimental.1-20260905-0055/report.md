# DLSS5 AIO performance report

Generated: 2026-09-05 00:57:18 -04:00

## Existing addon logs

| Log | Samples | Source FPS | Proxy FPS | Addon CPU ms | NR ms | SR/DLAA ms | FG ms | Pipeline ms | Final skips |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| ReShade.log | 14 | 96.6 | 77.4 | 0.794 | 8.771 | 1.863 | 2.165 | 12.807 | 4349 |

## Interpretation

- Compare **addon disabled** with **DLSS only** to see combined proxy plus DLSS cost.
- Compare **DLSS only** with **NR + DLSS** to isolate NR cost.
- Compare FG modes using both source FPS and displayed/proxy FPS. Generated frames must not be counted as recovered source cadence.
- A true no-addon baseline requires a separate launch with `standalone-dlssnr.addon64` physically absent.
