# Experimental 32-bit host

This prototype reuses the released 64-bit Standalone DLSS-NR + SR addon instead
of compiling a second NVIDIA pipeline for x86:

1. `standalone-dlssnr.addon32` captures a 32-bit game's final frame into a shared GPU texture
   and presents an AIO-branded proxy of the normal settings panel.
2. `dlss5-feed-host64.exe` opens that texture and presents it through a hidden x64 D3D12 carrier.
3. The normal `standalone-dlssnr.addon64` intercepts the carrier Present and runs the same
   NR, DLSS/DLAA, frame-generation, pacing, and native-output implementation used by x64 games.

The first target is D3D9 through dgVoodoo2's D3D11 wrapper. Native x86 D3D11 is
already supported by the transport. Vulkan and OpenGL remain transport-capable,
but are outside the Arkham City validation pass.

This is an experimental deployment, not a release artifact.

The wrapper writes the real addon's `[Standalone.DLSSNR]` configuration in
`host64/ReShade.ini`; **Apply settings and restart 64-bit AIO** cycles only the
carrier process. NR pass count is forwarded as a session-only launch setting and
therefore retains the same crash-safe reset-to-1x behavior as the x64 addon.
