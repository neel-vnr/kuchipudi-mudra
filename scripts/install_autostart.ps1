# ---------------------------------------------------------------------------
# Installs the ESP32 watcher to run automatically at logon.
#
# Creates a Scheduled Task "MUDRA ESP32 Watcher" that runs
#   powershell -ExecutionPolicy Bypass -WindowStyle Hidden -File scripts\watch_esp32.ps1
# The watcher then starts the bridge whenever the board is plugged in, and the
# bridge launches kiosk_app.py.
#
# Run once:   scripts\install_autostart.ps1
# Remove:     scripts\install_autostart.ps1 -Remove
# ---------------------------------------------------------------------------

param([switch]$Remove)

$ErrorActionPreference = "Stop"
$taskName = "MUDRA ESP32 Watcher"
$repo = Split-Path -Parent $PSScriptRoot
$script = Join-Path $PSScriptRoot "watch_esp32.ps1"

if ($Remove) {
    Unregister-ScheduledTask -TaskName $taskName -Confirm:$false -ErrorAction SilentlyContinue
    Write-Host "Removed scheduled task '$taskName'."
    return
}

if (-not (Test-Path $script)) {
    throw "Watcher script not found: $script"
}

$action = New-ScheduledTaskAction -Execute "powershell.exe" `
    -Argument "-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File `"$script`"" `
    -WorkingDirectory $repo

$trigger = New-ScheduledTaskTrigger -AtLogOn

$settings = New-ScheduledTaskSettingsSet `
    -AllowStartIfOnBatteries `
    -DontStopIfGoingOnBatteries `
    -StartWhenAvailable `
    -RestartCount 3 -RestartInterval (New-TimeSpan -Minutes 1)

try {
    Register-ScheduledTask -TaskName $taskName -Action $action -Trigger $trigger -Settings $settings `
        -Description "Starts the MUDRA AI bridge when the ESP32 is plugged in." -Force | Out-Null
    Write-Host "Installed scheduled task '$taskName' (runs at every logon)."
    Write-Host "Start it now with:  Start-ScheduledTask -TaskName '$taskName'"
} catch {
    # Registering a scheduled task needs elevation; the per-user Startup folder
    # does not, so fall back to a shortcut there.
    Write-Host "Scheduled task not permitted ($($_.Exception.Message))."
    Write-Host "Falling back to the user Startup folder ..."

    $startup = [Environment]::GetFolderPath('Startup')
    $shortcutPath = Join-Path $startup "MUDRA ESP32 Watcher.lnk"

    $shell = New-Object -ComObject WScript.Shell
    $shortcut = $shell.CreateShortcut($shortcutPath)
    $shortcut.TargetPath = "powershell.exe"
    $shortcut.Arguments = "-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File `"$script`""
    $shortcut.WorkingDirectory = $repo
    $shortcut.WindowStyle = 7
    $shortcut.Description = "Starts the MUDRA AI bridge when the ESP32 is plugged in."
    $shortcut.Save()

    Write-Host "Installed startup shortcut: $shortcutPath"
    Write-Host "It runs automatically the next time you log in."
}
