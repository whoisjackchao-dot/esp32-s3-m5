# e2e_state_v11.ps1 —— 受控端到端：mock 桥接(WSL:45100) → state_host → BLE → 设备 STATE(v1.1)
# 前置：mock_bridge_sse.py 已在 127.0.0.1:45100 运行；设备在 COM5。
# 复跑：powershell -ExecutionPolicy Bypass -File tools\e2e_state_v11.ps1
param([string]$Com="COM5",[int]$HostSeconds=30)
$ErrorActionPreference="SilentlyContinue"
$root = Split-Path -Parent $PSScriptRoot
$tmp = Join-Path $env:TEMP "sticks3-e2e"
New-Item -ItemType Directory -Force -Path $tmp | Out-Null
Set-Content -Path (Join-Path $tmp "bridge.json") -Value '{"url":"http://127.0.0.1:45100","token":"e2e-token","tuiEpoch":"e2e"}' -Encoding ascii
$env:LOCALAPPDATA=$tmp
$env:STICKS3_BRIDGE_JSON=(Join-Path $tmp "bridge.json")
$env:STICKS3_HOST_TOKEN="e2e"
$env:STICKS3_HOST_PORT="45990"
$env:STICKS3_HOST_SECONDS="$HostSeconds"
$serial = Join-Path $tmp "serial.log"
$hostlog = Join-Path $tmp "host.log"
Remove-Item $serial,$hostlog -ErrorAction SilentlyContinue

$cap = Start-Process -FilePath "powershell" -ArgumentList @("-NoProfile","-ExecutionPolicy","Bypass","-File",(Join-Path $root "tools\serial_capture.ps1"),"-Com",$Com,"-Out",$serial,"-Seconds",($HostSeconds+10)) -WindowStyle Hidden -PassThru
Start-Sleep -Seconds 4
$exe = Join-Path $root "host\src\state_host.exe"
& $exe *> $hostlog
Start-Sleep -Seconds 3
try{ if(-not $cap.HasExited){ $cap.Kill() } }catch{}
Write-Host "=== serial: STATE/READY/MTU/ble ==="
Select-String -Path $serial -Pattern "host STATE|host READY|MTU exchange|ble |ble_conn" | ForEach-Object { Write-Host $_.Line }
Write-Host "=== host log tail ==="
Get-Content $hostlog -ErrorAction SilentlyContinue | Select-Object -Last 15
