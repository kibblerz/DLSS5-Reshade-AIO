@echo off
rem Fallback launcher for Vulkan games whose vkCreateDevice path bypasses the
rem standalone addon's in-process extension hook.
setlocal
if "%~1"=="" (
    echo Usage: run-with-standalone-vulkan-layer.bat "path\to\game.exe" [args...]
    exit /b 1
)
set "VK_LAYER_PATH=%~dp0"
set "VK_INSTANCE_LAYERS=VK_LAYER_feed_vk"
echo Launching with the standalone DLSS-NR Vulkan interop layer.
start "" %*
endlocal
