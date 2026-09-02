@echo off
setlocal
cd /d "%~dp0"
call "..\..\work\DLSS5-Feeder\tools\vcvars.bat" x64 || exit /b 1
if not exist build mkdir build

cl /nologo /std:c++17 /O2 /EHsc /W4 /MD /LD ^
  src\nvngx-bridge.cpp /Fo:build\nvngx-bridge.obj /Fe:build\nvngx.dll ^
  /link /IMPLIB:build\nvngx.lib d3d12.lib
if errorlevel 1 exit /b 1

cl /nologo /LD /EHsc /O2 /MD /W4 /std:c++20 ^
  /I"..\..\work\DLSS5-Feeder\external\ngx" ^
  /I"..\..\work\DLSS5-Feeder\external\reshade\include" ^
  /I"..\..\work\DLSS5-Feeder\external\imgui" ^
  /I"..\..\work\DLSS5-Feeder\external\vulkan" ^
  /I"..\..\work\DLSS5-Feeder\external\minhook\include" ^
  /Fobuild\ /Fdbuild\standalone-dlssnr.pdb src\nr-standalone.cpp ^
  "..\..\work\DLSS5-Feeder\external\minhook\src\buffer.c" ^
  "..\..\work\DLSS5-Feeder\external\minhook\src\hook.c" ^
  "..\..\work\DLSS5-Feeder\external\minhook\src\trampoline.c" ^
  "..\..\work\DLSS5-Feeder\external\minhook\src\hde\hde64.c" ^
  /link /OUT:build\standalone-dlssnr.addon64 kernel32.lib user32.lib d3d9.lib d3d11.lib d3d12.lib dxgi.lib d3dcompiler.lib
if errorlevel 1 exit /b 1

copy /y "..\lab\nvngx_dlssnr.sf.dll" "build\nvngx_dlssnr.dll" >nul || exit /b 1
copy /y "..\..\nvngx_dlss.dll" "build\nvngx_dlss.dll" >nul || exit /b 1
copy /y "..\..\renodx\external\DLSS\lib\Windows_x86_64\rel\nvngx_dlssg.dll" "build\nvngx_dlssg.dll" >nul || exit /b 1
copy /y "shaders\DLSS5_Feed.fx" "build\DLSS5_Feed.fx" >nul || exit /b 1
copy /y "shaders\StandaloneBoundary.fx" "build\StandaloneBoundary.fx" >nul || exit /b 1
echo Building Vulkan fallback layer from %CD%
call "%~dp0..\..\work\DLSS5-Feeder\layer\build-layer.bat" || exit /b 1
cd /d "%~dp0"
if not exist "build\VulkanLayer" mkdir "build\VulkanLayer"
copy /y "..\..\work\DLSS5-Feeder\layer\VkLayer_feed_vk.dll" "build\VulkanLayer\VkLayer_feed_vk.dll" >nul || exit /b 1
copy /y "vulkan-layer\VkLayer_feed_vk.json" "build\VulkanLayer\VkLayer_feed_vk.json" >nul || exit /b 1
copy /y "vulkan-layer\run-with-standalone-vulkan-layer.bat" "build\VulkanLayer\run-with-standalone-vulkan-layer.bat" >nul || exit /b 1
echo Standalone addon package built in %CD%\build
endlocal
