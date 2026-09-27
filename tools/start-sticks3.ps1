# StickS3 x OpenCode one-shot startup (idempotent). Called by /esp32-s3m5; runnable manually.
# ASCII-only on purpose: Windows PowerShell reads BOM-less scripts as GBK on this machine.
param()
$ErrorActionPreference = "SilentlyContinue"

$Project  = "C:\Users\home\esp32-s3-m5"
$Launcher = Join-Path $Project "host\src\launcher.exe"
$Distro   = if ($env:STICKS3_WSL_DISTRO) { $env:STICKS3_WSL_DISTRO } else { "Ubuntu" }
$Bridge   = if ($env:STICKS3_BRIDGE_JSON) { $env:STICKS3_BRIDGE_JSON } else { "\\wsl.localhost\$Distro\home\home\.local\share\sticks3-opencode\bridge.json" }
$TuiJson  = "\\wsl.localhost\$Distro\home\home\.config\opencode\tui.json"
$Logs     = Join-Path $env:LOCALAPPDATA "sticks3\logs"

function Get-ProcId([string]$n) {
    $p = Get-Process $n -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($p) { $p.Id } else { "" }
}

Write-Host "=== StickS3 x OpenCode startup ==="

Write-Host "[1/3] bridge (OpenCode plugin) readiness"
if (Test-Path $Bridge) {
    $j = Get-Content $Bridge -Raw | ConvertFrom-Json
    Write-Host ("      bridge OK  url=" + $j.url + "  plugin=" + $j.pluginVersion)
} else {
    Write-Host ("      bridge NOT ready: missing " + $Bridge)
    if (Test-Path $TuiJson) {
        $tui = Get-Content $TuiJson -Raw
        if ($tui -match "esp32-s3-m5") {
            Write-Host "      tui.json has the plugin -> restart OpenCode so it writes bridge.json"
        } else {
            Write-Host "      tui.json lacks the plugin -> add it and restart OpenCode"
        }
    } else {
        Write-Host "      tui.json not found -> check OpenCode global config"
    }
}

Write-Host "[2/3] start / bring up host stack"
if (Get-ProcId "launcher") {
    Write-Host ("      launcher already running (PID " + (Get-ProcId "launcher") + ") - skip")
} elseif (-not (Test-Path $Launcher)) {
    Write-Host ("      FAIL: missing " + $Launcher)
} else {
    Start-Process -FilePath $Launcher
    Write-Host "      launcher launched (will start state_host + tray)"
}

Write-Host "[3/3] verify"
Start-Sleep -Seconds 5
Write-Host ("      procs: launcher=" + (Get-ProcId "launcher") + " state_host=" + (Get-ProcId "state_host") + " tray=" + (Get-ProcId "tray"))
$trayLog = Get-ChildItem $Logs -Filter "tray-*.log" -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if ($trayLog) { Write-Host ("      tray: " + ((Get-Content $trayLog.FullName -Tail 1) -join "")) }
$shLog = Get-ChildItem $Logs -Filter "state_host-*.log" -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if ($shLog) { Write-Host ("      host: " + ((Get-Content $shLog.FullName | Select-String "push" | Select-Object -Last 1) -join "")) }
Write-Host "=== done ==="
