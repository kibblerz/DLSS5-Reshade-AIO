@echo off
setlocal
cd /d "%~dp0"
call "..\external\DLSS5-Feeder\tools\vcvars.bat" x64 || exit /b 1
if not exist build mkdir build
cl /nologo /std:c++20 /O2 /EHsc /MD /I"..\external\DLSS5-Feeder\external\reshade\include" /I"..\external\DLSS5-Feeder\external\imgui" src\test-sod2-hud.cpp /Fo:build\test-sod2-hud.obj /Fe:build\test-sod2-hud.exe /link d3d11.lib d3dcompiler.lib user32.lib
if errorlevel 1 exit /b 1
build\test-sod2-hud.exe
if errorlevel 1 exit /b 1
build\test-sod2-hud.exe 10bit
if errorlevel 1 exit /b 1
build\test-sod2-hud.exe bgra
exit /b %errorlevel%
