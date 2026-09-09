## DLSS5 ReShade AIO v2.2.0

This stable release makes NVIDIA Optical Flow the recommended default motion source. In broad D3D11, D3D12, and Vulkan testing, the new pipelined implementation substantially reduced temporal boiling, smearing, and ghosting—especially with 2x and 3x Neural Rendering—while retaining far more throughput than the original experimental implementation.

### Highlights

- Enables NVIDIA Optical Flow motion by default for new per-game configurations.
- Keeps the setting persistent, so users can disable it for a game with unusual motion artifacts or excessive GPU cost.
- Pipelines Optical Flow ahead of Neural Rendering on D3D11, D3D12, and Vulkan rather than serializing both stages.
- Adds completion-driven D3D11 mailbox processing and per-slot Vulkan capture snapshots.
- Adds saturation recovery and liveness watchdogs so a delayed Optical Flow job fails safely instead of freezing the pipeline.
- Preserves automatic fallback to VORT or zero-motion guides when NVIDIA Optical Flow is unavailable or disabled.
- Retains the motion-vector and confidence/rejection diagnostic views introduced by the prerelease.
- Keeps the optional depth/geometry refinement disabled by default; it remains an advanced experimental setting.
- Preserves x86 host logs across carrier restarts for easier 32-bit troubleshooting.

No separate Optical Flow DLL should be downloaded or installed. The addon dynamically uses `nvofapi64.dll` supplied by the NVIDIA display driver.

ReShade and the NVIDIA NGX runtime files are still user-supplied and are not bundled in the release ZIPs. Download only the ZIP matching the game's architecture and follow `README_FIRST.txt` inside it.
