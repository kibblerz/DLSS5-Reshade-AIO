@echo off
setlocal
cd /d "%~dp0"
call "..\..\work\DLSS5-Feeder\tools\vcvars.bat" x64 || exit /b 1
if not exist build mkdir build
cl /nologo /EHsc /O2 /MD /W4 /std:c++20 vulkan-wsi-extent-smoke.cpp ^
  /I"..\..\work\DLSS5-Feeder\external\vulkan" ^
  /Fe:build\vulkan-wsi-extent-smoke.exe user32.lib
if errorlevel 1 exit /b 1
build\vulkan-wsi-extent-smoke.exe
endlocal
