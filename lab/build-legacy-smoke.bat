@echo off
setlocal
cd /d "%~dp0"
call "..\..\work\DLSS5-Feeder\tools\vcvars.bat" x64 || exit /b 1
if not exist build mkdir build
cl /nologo /EHsc /O2 /MD /W4 /std:c++20 legacy-interop-smoke.cpp ^
  /Fe:build\legacy-interop-smoke.exe ^
  /link user32.lib d3d9.lib d3d11.lib d3d12.lib dxgi.lib
if errorlevel 1 exit /b 1
build\legacy-interop-smoke.exe
endlocal
