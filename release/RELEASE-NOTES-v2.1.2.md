## DLSS5 ReShade AIO v2.1.2

This stable release substantially broadens native 32-bit Direct3D 9 compatibility. The same x64 AIO pipeline now accepts ReShade effect targets exposed as D3D9 surfaces, D3D9 textures, or internal D3D10.1 resources.

### Included fixes

- Adds a compatibility-gated CPU capture bridge for classic non-D3D9Ex devices that reject shared textures.
- Keeps the original game presentation responsive while the x64 NR/DLSS/FG carrier initializes or pauses during a loading transition.
- Prevents the bridge from disabling itself moments before a slow NGX initialization completes.
- Removes the unnecessary 15-second native-resolution stabilization delay from controlled x86 carrier sessions; normal 64-bit startup protection is unchanged.
- Resolves the D3D9 capture technique after asynchronous ReShade compilation.
- Avoids compiling the full motion-provider feed on shader-model-3 D3D9 games.
- Activates the game during its first exclusive-fullscreen-to-borderless conversion so it no longer remains hidden behind Steam. Later resets remain non-activating.

### Classic D3D9 troubleshooting

The new compatibility options remain disabled by default:

- Enable **Allow classic D3D9 CPU bridge** when the game remains on Ready, reports unsupported D3D9/D3D11 shared textures, or never displays processed output.
- Enable **Virtualize classic D3D9 fullscreen at startup** when exclusive fullscreen minimizes, remains behind Steam, or cannot coexist with the detached processed output. Restart after changing it.

The build was validated across the maintained 32-bit test set, including Batman: Arkham City, BioShock, Fable Anniversary, and Fallout: New Vegas.

Download the ZIP matching the game's architecture and follow its included `README_FIRST.txt`. ReShade and NVIDIA's `nvngx_dlssnr.dll`, `nvngx_dlss.dll`, and `nvngx_dlssg.dll` runtimes are not bundled.
