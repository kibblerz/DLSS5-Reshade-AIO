@echo off
setlocal
cd /d "%~dp0"
set "FEEDER_ROOT=%~dp0..\..\external\DLSS5-Feeder"
call "%FEEDER_ROOT%\tools\vcvars.bat" x64 || exit /b 1

cl /nologo /std:c++17 /O2 /EHsc /W4 /MD /LD ^
  ..\nvngx-bridge.cpp /Fe:nvngx.dll ^
  /link d3d12.lib
if errorlevel 1 exit /b 1

cl /nologo /std:c++17 /O2 /EHsc /W4 /MD ^
  /I"%FEEDER_ROOT%\external\ngx" ^
  async-nr-probe.cpp /Fe:async-nr-probe.exe ^
  /link "%FEEDER_ROOT%\external\ngx\libs\nvsdk_ngx_d.lib" ^
  d3d12.lib dxgi.lib dxguid.lib user32.lib advapi32.lib ole32.lib
if errorlevel 1 exit /b 1

for %%F in (nvngx_dlssnr.dll nvngx_dlss.dll nvngx_dlssg.dll) do copy /y "..\..\runtime\%%F" "." >nul || exit /b 1
echo async-nr-probe built.
endlocal
