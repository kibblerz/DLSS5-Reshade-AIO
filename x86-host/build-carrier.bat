@echo off
rem Build the experimental x64 carrier used by the existing DLSS5-Feeder addon32.
cd /d "%~dp0"
if not exist build\host64 mkdir build\host64
setlocal
call "%~dp0..\external\DLSS5-Feeder\tools\vcvars.bat" x64 || exit /b 1
cl /nologo /O2 /EHsc /W3 /MD /std:c++20 ^
  /I"%~dp0src" /I"%~dp0..\external\DLSS5-Feeder\external\ngx" ^
  "%~dp0src\aio-carrier-host64.cpp" /Fe:"%~dp0build\host64\AIO DLSS5 32-bit Wrapper.exe" ^
  /link "%~dp0..\external\DLSS5-Feeder\external\ngx\libs\nvsdk_ngx_d.lib" ^
  version.lib d3d12.lib dxgi.lib kernel32.lib user32.lib gdi32.lib advapi32.lib ole32.lib
if errorlevel 1 exit /b 1
endlocal
echo AIO carrier host built.

if not exist build\x86 mkdir build\x86
call "%~dp0..\external\DLSS5-Feeder\tools\vcvars.bat" amd64_x86 || exit /b 1
cl /nologo /LD /EHsc /O2 /MD /W3 /std:c++20 ^
  /I"%~dp0src" ^
  /I"%~dp0..\addon\include" ^
  /I"%~dp0..\external\DLSS5-Feeder\src" ^
  /I"%~dp0..\external\DLSS5-Feeder\external\reshade\include" ^
  /I"%~dp0..\external\DLSS5-Feeder\external\imgui" ^
  /I"%~dp0..\external\DLSS5-Feeder\external\vulkan" ^
  /I"%~dp0..\external\DLSS5-Feeder\external\minhook\include" ^
  /Fo"%~dp0build\x86\\" /Fd"%~dp0build\x86\\" ^
  "%~dp0src\aio-wrapper32.cpp" ^
  "%~dp0..\external\DLSS5-Feeder\external\minhook\src\buffer.c" ^
  "%~dp0..\external\DLSS5-Feeder\external\minhook\src\hook.c" ^
  "%~dp0..\external\DLSS5-Feeder\external\minhook\src\trampoline.c" ^
  "%~dp0..\external\DLSS5-Feeder\external\minhook\src\hde\hde32.c" ^
  /link /OUT:"%~dp0build\x86\standalone-dlssnr.addon32" ^
  d3d9.lib d3d10_1.lib d3d11.lib dxgi.lib kernel32.lib user32.lib advapi32.lib
if errorlevel 1 exit /b 1
echo AIO x86 wrapper built.
