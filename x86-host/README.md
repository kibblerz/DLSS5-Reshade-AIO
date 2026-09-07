# 32-bit AIO host

The 32-bit package reuses the released 64-bit Standalone DLSS-NR + SR addon instead
of compiling a second NVIDIA pipeline for x86:

1. `standalone-dlssnr.addon32` captures a 32-bit game's final frame into a shared GPU texture
   and presents an AIO-branded proxy of the normal settings panel.
2. `host64/AIO DLSS5 32-bit Wrapper.exe` opens that texture and presents it through an x64 D3D12 carrier.
3. The normal `standalone-dlssnr.addon64` intercepts the carrier Present and runs the same
   NR, DLSS/DLAA, frame-generation, pacing, and native-output implementation used by x64 games.

Native x86 D3D9 and D3D11 are supported by the transport. ReShade 6 renders
native D3D9 effects through an internal D3D10.1 presentation runtime; the
wrapper bridges that completed frame into its existing D3D11 carrier path.
A direct D3D9 shared-surface fallback is retained for other runtime layouts.
No dgVoodoo wrapper is required. Vulkan and OpenGL remain experimental.

Release ZIPs preserve this layout automatically. Extract the 32-bit ZIP beside
the 32-bit game executable, then put the separately obtained 64-bit ReShade
`dxgi.dll` and NVIDIA `nvngx_dlssnr.dll`, `nvngx_dlss.dll`, and
`nvngx_dlssg.dll` files inside `host64`. Never put those x64 DLLs directly beside
the x86 game executable.

The wrapper writes the real addon's `[Standalone.DLSSNR]` configuration in
`host64/ReShade.ini`; **Apply settings and restart 64-bit AIO** cycles only the
carrier process. NR pass count is forwarded as a session-only launch setting and
therefore retains the same crash-safe reset-to-1x behavior as the x64 addon.

Both builds compile `addon/include/aio-menu-schema.hpp`, the single definition
of persistent menu keys, labels, choices, defaults, ranges, groups, and concise
help text. The x86 panel renders that schema generically and writes the same keys
consumed directly by the x64 addon, so persistent options are no longer
maintained in a separate 32-bit menu table.
