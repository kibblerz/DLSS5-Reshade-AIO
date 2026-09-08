## DLSS5 ReShade AIO v2.1.3

This stable release makes source-resolution downsampling effective earlier in the 32-bit pipeline. When a lower **Pipeline source resolution override** is selected, supported legacy games now reduce the captured frame before its most expensive bridge and transport operations rather than carrying the full-resolution image into the x64 AIO host first.

### Included improvements

- Native D3D9 games downsample on the game GPU before classic CPU readback, reducing readback, upload, and shared-transport work.
- ReShade's D3D10.1-backed legacy route now downsamples before its D3D10-to-D3D11 shared handoff.
- Render-target-only D3D10.1 sources are handled through a shader-readable fallback instead of silently losing the optimization.
- The existing `host64` source-resolution override remains the single authoritative setting; no new installation steps or compatibility toggles are required.
- Normal 64-bit pipeline behavior is unchanged from v2.1.2.

The 32-bit changes were validated across Batman: Arkham City, BioShock, Fable Anniversary, and Fallout: New Vegas. BioShock additionally validated the new D3D10.1 path.

Download the ZIP matching the game's architecture and follow its included `README_FIRST.txt`. ReShade and NVIDIA's `nvngx_dlssnr.dll`, `nvngx_dlss.dll`, and `nvngx_dlssg.dll` runtimes are not bundled.
