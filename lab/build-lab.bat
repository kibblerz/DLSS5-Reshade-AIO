@echo off
setlocal
cd /d "%~dp0"
call "..\..\work\DLSS5-Feeder\tools\vcvars.bat" x64 || exit /b 1
cl /nologo /std:c++17 /O2 /EHsc /W4 /MD /LD ^
  nvngx-bridge.cpp /Fe:nvngx.dll ^
  /link d3d12.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /O2 /EHsc /W4 /MD ^
  /I"..\..\work\DLSS5-Feeder\external\ngx" ^
  nr-lab.cpp /Fe:nr-lab.exe ^
  /link "..\..\work\DLSS5-Feeder\external\ngx\libs\nvsdk_ngx_d.lib" ^
  d3d12.lib dxgi.lib dxguid.lib user32.lib advapi32.lib ole32.lib
if errorlevel 1 exit /b 1
copy /y "..\..\nvngx_dlss.dll" "." >nul || exit /b 1
if exist "nvngx_dlssnr.sf.dll" (
  copy /y "nvngx_dlssnr.sf.dll" "nvngx_dlssnr.dll" >nul || exit /b 1
) else (
  copy /y "..\..\nvngx_dlssnr.dll" "." >nul || exit /b 1
)
echo nr-lab built.
endlocal
