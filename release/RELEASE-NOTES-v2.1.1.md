## DLSS5 ReShade AIO v2.1.1

This release completes the first native 32-bit Direct3D 9 path. A D3D9 game no longer needs dgVoodoo: the x86 addon captures ReShade's completed D3D10.1 presentation frame and forwards it into the same 64-bit NR, DLSS/DLAA, Frame Generation, pacing, and output pipeline used everywhere else.

### Included fixes

- Native x86 D3D9 transport through ReShade 6's D3D10.1 effects runtime.
- Direct D3D9 shared-surface fallback for alternate runtime layouts.
- Lightweight `DLSS5_Feed_D3D9.fx` trigger, automatically enabled and compatible with the legacy shader compiler.
- Processed output remains fullscreen after the ReShade menu closes.
- Clear troubleshooting for addon load error 1359 caused by duplicate game-root ReShade proxies.

### 32-bit D3D9 installation note

Install 32-bit ReShade with addon support as `d3d9.dll` beside the game executable. Do not keep another ReShade proxy named `dxgi.dll` in that same game directory. The required 64-bit ReShade `dxgi.dll` still belongs inside `host64`.

Download the ZIP matching the game's architecture and follow its included `README_FIRST.txt`. ReShade and NVIDIA's `nvngx_dlssnr.dll`, `nvngx_dlss.dll`, and `nvngx_dlssg.dll` runtimes are not bundled.
