@echo off
setlocal
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0DLSS5-PerformanceAnalyzer.ps1" %*
exit /b %errorlevel%
