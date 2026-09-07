DLSS5 ReShade AIO - 64-BIT INSTALL
==================================

USE THIS ZIP ONLY FOR A 64-BIT GAME.

1. Install 64-bit ReShade WITH ADDON SUPPORT beside the game's real executable.
2. Extract everything from this ZIP into that same folder.
3. Keep the included reshade-shaders folder structure intact.
4. Separately obtain these NVIDIA runtimes and put them directly in the game folder:
     nvngx_dlssnr.dll
     nvngx_dlss.dll
     nvngx_dlssg.dll   (required for Frame Generation)
5. Launch the game and open ReShade's Add-ons tab.

The included nvngx.dll is the AIO caller bridge. It is required and is NOT a
replacement for the three NVIDIA runtime files above.

Do not use the 32-bit ZIP for this game and do not mix the two packages.

Full setup and troubleshooting:
https://github.com/kibblerz/DLSS5-Reshade-AIO
