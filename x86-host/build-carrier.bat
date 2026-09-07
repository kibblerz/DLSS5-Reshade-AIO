@echo off
rem Build the experimental x64 carrier used by the existing DLSS5-Feeder addon32.
cd /d "%~dp0"
if not exist build\host64 mkdir build\host64
setlocal
call "%~dp0..\external\DLSS5-Feeder\tools\vcvars.bat" x64 || exit /b 1
cl /nologo /O2 /EHsc /W3 /MD /std:c++20 ^
  /I"%~dp0src" /I"%~dp0..\external\DLSS5-Feeder\external\ngx" ^
  "%~dp0src\aio-carrier-host64.cpp" /Fe:"%~dp0build\host64\dlss5-feed-host64.exe" ^
  /link "%~dp0..\external\DLSS5-Feeder\external\ngx\libs\nvsdk_ngx_d.lib" ^
  version.lib d3d12.lib dxgi.lib kernel32.lib user32.lib gdi32.lib advapi32.lib ole32.lib
if errorlevel 1 exit /b 1
endlocal
echo AIO carrier host built.
