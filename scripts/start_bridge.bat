@echo off
REM Start the MUDRA AI laptop bridge in its own console window.
REM The bridge auto-detects the ESP32, so no port argument is needed.
REM Pass --no-preview to hide the camera window, or --port COM8 to pin the port.

cd /d "%~dp0.."

echo Starting MUDRA AI bridge...
echo Camera window: yes (use --no-preview to disable)
echo.

python src\esp32_bridge.py %*

echo.
echo Bridge stopped. Press any key to close.
pause >nul
