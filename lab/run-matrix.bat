@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"
if not exist nr-lab.exe call build-lab.bat || exit /b 1
if exist matrix-results.txt del /q matrix-results.txt
set failures=0
for %%P in (srgb scrgb hdr10) do (
  for %%M in (1 2 3) do (
    echo ==== profile=%%P model=%%M ====>>matrix-results.txt
    nr-lab.exe --profile %%P --model %%M --input 960x540 --output 1920x1080 --frames 8 >>matrix-results.txt 2>&1
    set result=!errorlevel!
    echo exit=!result!>>matrix-results.txt
    if not "!result!"=="0" set /a failures+=1
    if exist nr-lab-result.json copy /y nr-lab-result.json result-%%P-model%%M.json >nul
    if exist nr-lab.log copy /y nr-lab.log log-%%P-model%%M.txt >nul
  )
)
echo Matrix complete: !failures! failing cases out of 9.
echo See %CD%\matrix-results.txt and the per-case JSON/log files.
exit /b !failures!
