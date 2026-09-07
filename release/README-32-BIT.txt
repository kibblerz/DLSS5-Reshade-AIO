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

Native 32-bit D3D9 and D3D11 are supported. For a D3D9 game, the 32-bit
ReShade proxy beside the game must be named d3d9.dll. Do not also leave a
second ReShade proxy named dxgi.dll beside that D3D9 game: the AIO bridge
needs Windows' real DXGI library and a duplicate local proxy can prevent the
addon from loading. DLSS5_Feed_D3D9.fx is included and enabled automatically.
No dgVoodoo wrapper is required. OpenGL and Vulkan x86 remain experimental.

Full setup and troubleshooting:
https://github.com/kibblerz/DLSS5-Reshade-AIO
