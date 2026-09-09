## DLSS5 ReShade AIO v2.2.1

This maintenance release makes live resolution changes safer and improves ReShade menu responsiveness without changing normal gameplay presentation.

### Highlights

- Quiesces the detached presentation worker before ReShade destroys or recreates a game's primary backbuffers.
- Rechecks the resize-transition state immediately before proxy DXGI presentation, safely cancelling a stale presentation that was prepared before the resize began.
- Fixes a null-pointer crash observed when an overlay such as Discord intercepted the addon's detached `Present` during a D3D12 resolution change.
- Keeps processed output visible behind ReShade while temporarily removing Frame Generation presentation pacing whenever the menu is open.
- Restores the configured generated/real presentation cadence automatically when the menu closes.
- Includes the same fixed x64 processing pipeline inside the 32-bit `host64` package.

The resize fix was validated in Conan Exiles while switching between 4K and 1440p. ReShade and the NVIDIA NGX runtime files remain user-supplied and are not bundled in the release ZIPs.
