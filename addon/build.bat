@echo off
setlocal
cd /d "%~dp0"
set "FEEDER_ROOT=%~dp0..\external\DLSS5-Feeder"
call "%FEEDER_ROOT%\tools\vcvars.bat" x64 || exit /b 1
if not exist build mkdir build
if exist "build\DLSS5_Feed.fx" del /q "build\DLSS5_Feed.fx"

cl /nologo /std:c++17 /O2 /EHsc /W4 /MD /LD ^
  src\nvngx-bridge.cpp /Fo:build\nvngx-bridge.obj /Fe:build\nvngx.dll ^
  /link /IMPLIB:build\nvngx.lib d3d12.lib
if errorlevel 1 exit /b 1

cl /nologo /LD /EHsc /O2 /MD /W4 /std:c++20 ^
  /I"%FEEDER_ROOT%\external\ngx" ^
  /I"%FEEDER_ROOT%\external\reshade\include" ^
  /I"%FEEDER_ROOT%\external\imgui" ^
  /I"%FEEDER_ROOT%\external\vulkan" ^
  /I"%FEEDER_ROOT%\external\minhook\include" ^
  /Fobuild\ /Fdbuild\standalone-dlssnr.pdb src\nr-standalone.cpp ^
  "%FEEDER_ROOT%\external\minhook\src\buffer.c" ^
  "%FEEDER_ROOT%\external\minhook\src\hook.c" ^
  "%FEEDER_ROOT%\external\minhook\src\trampoline.c" ^
  "%FEEDER_ROOT%\external\minhook\src\hde\hde64.c" ^
  /link /OUT:build\standalone-dlssnr.addon64 kernel32.lib user32.lib d3d9.lib d3d11.lib d3d12.lib dxgi.lib d3dcompiler.lib dcomp.lib
if errorlevel 1 exit /b 1

for %%F in (nvngx_dlssnr.dll nvngx_dlss.dll nvngx_dlssg.dll) do (
  if exist "..\runtime\%%F" (
    copy /y "..\runtime\%%F" "build\%%F" >nul || exit /b 1
  ) else (
    echo WARNING: runtime\%%F is absent; it will not be included in the package.
  )
)
copy /y "shaders\DLSS5_AIO_Feed.fx" "build\DLSS5_AIO_Feed.fx" >nul || exit /b 1
copy /y "shaders\StandaloneBoundary.fx" "build\StandaloneBoundary.fx" >nul || exit /b 1
echo Building Vulkan fallback layer from %CD%
call "%FEEDER_ROOT%\layer\build-layer.bat" || exit /b 1
cd /d "%~dp0"
if not exist "build\VulkanLayer" mkdir "build\VulkanLayer"
copy /y "%FEEDER_ROOT%\layer\VkLayer_feed_vk.dll" "build\VulkanLayer\VkLayer_feed_vk.dll" >nul || exit /b 1
copy /y "vulkan-layer\VkLayer_feed_vk.json" "build\VulkanLayer\VkLayer_feed_vk.json" >nul || exit /b 1
copy /y "vulkan-layer\run-with-standalone-vulkan-layer.bat" "build\VulkanLayer\run-with-standalone-vulkan-layer.bat" >nul || exit /b 1
echo Standalone addon package built in %CD%\build
endlocal
