DLSS5 ReShade AIO - 32-BIT INSTALL
==================================

USE THIS ZIP ONLY FOR A 32-BIT GAME.

1. Install 32-bit ReShade WITH ADDON SUPPORT beside the game's real executable.
   Select the standard shader package so ReShade.fxh is installed.
2. Extract everything from this ZIP into that same game folder.
3. DO NOT move the included x64 processing files out of host64.
4. Run the ReShade installer again. Select host64\AIO DLSS5 32-bit Wrapper.exe,
   choose DirectX 10/11/12, and install 64-bit ReShade WITH ADDON SUPPORT.
   Its DLL must be:
     game folder\host64\dxgi.dll
5. Separately obtain these NVIDIA runtimes and put them INSIDE host64:
     game folder\host64\nvngx_dlssnr.dll
     game folder\host64\nvngx_dlss.dll
     game folder\host64\nvngx_dlssg.dll
6. Start the game normally. The 32-bit addon starts the x64 wrapper automatically.

IMPORTANT: 64-bit ReShade and all NVIDIA runtime DLLs belong in host64, NOT
beside the 32-bit game executable. The included host64\nvngx.dll is the AIO
caller bridge and must remain there too.

Use the AIO page in the game's normal ReShade Add-ons tab. Applying settings
restarts only the 64-bit AIO carrier.

The x86 path is currently validated with D3D11 output. Native 32-bit D3D9 games
need a D3D9-to-D3D11 wrapper such as dgVoodoo2, which is not included. OpenGL
and Vulkan x86 transport remain experimental.

Full setup and troubleshooting:
https://github.com/kibblerz/DLSS5-Reshade-AIO
