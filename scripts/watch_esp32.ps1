# ---------------------------------------------------------------------------
# Watches for the MUDRA ESP32 and starts the bridge when it appears.
#
# Runs forever. Every 2 seconds it checks whether the board is plugged in and
# whether the bridge process is alive; if the board is present and the bridge is
# not running, it starts it. The bridge then launches kiosk_app.py itself.
#
# Start it with:   powershell -ExecutionPolicy Bypass -File scripts\watch_esp32.ps1
# Or install it to run at logon:  scripts\install_autostart.ps1
# ---------------------------------------------------------------------------

$ErrorActionPreference = "Continue"

$repo = Split-Path -Parent $PSScriptRoot
$logPath = Join-Path $env:TEMP "mudra_watcher.log"

# USB vendor/product ids that identify the ESP32 boards used here:
#   10C4:EA60  Silicon Labs CP210x
#   1A86:55D3  WCH CH343
#   1A86:7523  WCH CH340  (some ESP32 dev boards; also the UNO clone)
# Comment out the CH340 line if the Arduino UNO should be ignored.
$portIdPattern = 'VID_10C4|PID_55D3|PID_7523'

function Write-Log([string]$message) {
    $line = "[{0}] {1}" -f (Get-Date -Format 'HH:mm:ss'), $message
    Add-Content -Path $logPath -Value $line -ErrorAction SilentlyContinue
}

function Test-BoardPresent {
    $ports = Get-PnpDevice -Class Ports -Status OK -ErrorAction SilentlyContinue
    if (-not $ports) { return $false }
    foreach ($port in $ports) {
        if ($port.InstanceId -match $portIdPattern) { return $true }
    }
    return $false
}

function Get-BridgeProcess {
    Get-CimInstance Win32_Process -Filter "Name='python.exe'" -ErrorAction SilentlyContinue |
        Where-Object { $_.CommandLine -match 'esp32_bridge' }
}

Write-Log "watcher started (repo: $repo)"

# Single instance via a named mutex: a second watcher would start a second bridge
# and studio, and two studios fight over the camera.
$createdNew = $false
$script:watcherMutex = New-Object System.Threading.Mutex($true, "Local\MUDRA_ESP32_WATCHER", [ref]$createdNew)
if (-not $createdNew) {
    Write-Log "another watcher already holds the lock; exiting"
    exit 0
}

$wasPresent = $false

while ($true) {
    $present = Test-BoardPresent
    $bridge = Get-BridgeProcess

    if ($present -and -not $bridge) {
        Write-Log "board detected, starting bridge"
        try {
            Start-Process -FilePath "python" `
                -ArgumentList "src\esp32_bridge.py" `
                -WorkingDirectory $repo `
                -WindowStyle Minimized
        } catch {
            Write-Log "failed to start bridge: $_"
        }
    } elseif ($present -ne $wasPresent) {
        if ($present) { Write-Log "board present" } else { Write-Log "board removed" }
    }

    $wasPresent = $present
    Start-Sleep -Seconds 2
}
