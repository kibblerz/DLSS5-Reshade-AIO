## DLSS5 ReShade AIO v2.2.0 Experimental 1

This prerelease adds optional NVIDIA Optical Flow motion guidance. The feature is **disabled by default**, so the established v2.1.3 VORT/zero-motion behavior remains active until a user explicitly enables it.

### Included experiment

- Derives full-resolution screen-space motion from consecutive captured source frames through NVIDIA Optical Flow hardware.
- Supplies that motion to Neural Rendering, DLSS/DLAA, and Frame Generation.
- Uses forward/backward consistency and NVIDIA cost data to reject unreliable history in DLSS.
- Adds live consistency and cost tolerance controls.
- Adds session-only motion direction/magnitude and confidence/rejection diagnostic views. Diagnostic views temporarily suppress Frame Generation.
- Preserves the normal guide path if Optical Flow is unsupported, unavailable, busy, or fails to initialize.
- Does **not** send the rejection texture to NR's private `ControlMask`; testing confirmed that contract is a spatial NR application mask rather than a temporal rejection input.

The addon loads the NVIDIA driver-provided `nvofapi64.dll` dynamically. Do not download or install a loose copy, and no additional Optical Flow runtime is bundled. A supported NVIDIA GPU and driver plus **Asynchronous NGX compute** are required.

This path has been validated in Batman: Arkham Knight through the D3D11-to-D3D12 host route. DX9, DX10, DX12, Vulkan, and bridged 32-bit paths share the same x64 processing host but require broader game testing. Optical Flow may reduce performance or produce unstable motion around thin geometry, transparency, particles, reflections, and disocclusions.

Use [v2.1.3](https://github.com/kibblerz/DLSS5-Reshade-AIO/releases/tag/v2.1.3) as the stable fallback if a game regresses. Download the ZIP matching the game's architecture and follow its included `README_FIRST.txt`. ReShade and NVIDIA's `nvngx_dlssnr.dll`, `nvngx_dlss.dll`, and `nvngx_dlssg.dll` runtimes are not bundled.
