@echo off
REM Starts the always-on ESP32 watcher in its own console.
REM Keep this running (or let the Startup shortcut do it at logon): it starts the
REM bridge whenever the board is plugged in, and the bridge launches kiosk_app.py.

cd /d "%~dp0.."

echo MUDRA AI watcher
echo   Waiting for the ESP32 to appear on a COM port...
echo   Log: %TEMP%\mudra_watcher.log
echo.

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0watch_esp32.ps1"

echo Watcher stopped. Press any key to close.
pause >nul
