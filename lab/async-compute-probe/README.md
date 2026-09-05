# DLSS-NR asynchronous-compute probe

This isolated executable answers one question before the addon risks submitting
private NGX work to a new queue type: can raw feature 18 be created and evaluated
on a D3D12 `COMPUTE` command queue?

Run `build-probe.bat`, then `async-nr-probe.exe`. A successful run writes full
frame coverage and `ASYNC PROBE VERDICT: PASS` to `async-nr-probe.log`. This is a
compatibility gate, not yet a performance benchmark; the lab intentionally waits
for every frame so that failures are deterministic.

Optional arguments:

    async-nr-probe.exe --size 960x540 --frames 8 --model 1
    async-nr-probe.exe --size 960x540 --full
    async-nr-probe.exe --size 960x540 --framegen

`--full` tests NR followed by DLSS SR on the compute queue. `--framegen`
additionally tests DLSS-G there. With either option, the output is twice the
requested input dimensions.
