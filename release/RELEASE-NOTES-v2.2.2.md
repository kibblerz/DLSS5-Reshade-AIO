## DLSS5 ReShade AIO v2.2.2

This release substantially reduces NVIDIA Optical Flow cost by giving motion analysis its own configurable working resolution.

### Highlights

- Adds **Optical Flow working resolution** choices for Auto, Native, 1440p, 1080p, and 720p.
- **Auto now caps Optical Flow at 720p by default** while preserving the source aspect ratio.
- Reconstructs full-source-resolution motion vectors and confidence for NR, DLSS/DLAA, and Frame Generation, including correct vector-magnitude scaling.
- Keeps the game and NR/DLSS pipeline source resolutions unchanged; only the Optical Flow analysis surface is reduced.
- Fixes an unsafe live reconfiguration path that could release staged D3D11 Optical Flow resources before the pending frame consumed them.
- Includes the same updated x64 processing pipeline inside the 32-bit `host64` package.

In Conan Exiles testing, increasing Optical Flow from 720p to 1080p produced negligible visible quality improvement, so 720p is the new general performance default. Native and higher-resolution modes remain available for games where thin geometry or small moving details benefit from additional precision.

ReShade and the NVIDIA NGX runtime files remain user-supplied and are not bundled in the release ZIPs.
