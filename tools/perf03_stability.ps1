# perf03_stability.ps1 — PERF-03 2h 稳定性测试编排（Windows 侧）
# 前置：WSL mock bridge(45100,mock_bridge_sse.py) + mock-asr(18765) 已起。
# 复跑：powershell -ExecutionPolicy Bypass -File tools\perf03_stability.ps1
param([int]$Seconds=7200)
$root = Split-Path -Parent $PSScriptRoot
$ev = Join-Path $root "evidence\M5"
New-Item -ItemType Directory -Force -Path $ev | Out-Null
$tmp = Join-Path $env:TEMP "sticks3-perf03"
New-Item -ItemType Directory -Force -Path $tmp | Out-Null
Set-Content -Path (Join-Path $tmp "bridge.json") -Value '{"url":"http://127.0.0.1:45100","token":"e2e-token","tuiEpoch":"perf03"}' -Encoding ascii
$env:LOCALAPPDATA=$tmp
$env:STICKS3_BRIDGE_JSON=(Join-Path $tmp "bridge.json")
$env:STICKS3_HOST_TOKEN="e2e"
$env:STICKS3_HOST_PORT="45912"
$env:STICKS3_HOST_SECONDS="$Seconds"
$env:STICKS3_ASR_HOST="127.0.0.1"
$env:STICKS3_ASR_PORT="18765"
$env:STICKS3_HOST_LOAD_TRIGGER_MS="300000"
$env:STICKS3_HOST_LOAD_DROP_MS="900000"

$serial = Join-Path $ev "perf03-serial.log"
$mem = Join-Path $ev "perf03-mem.csv"
$hostlog = Join-Path $ev "perf03-host.log"
Remove-Item $serial,$mem,$hostlog -ErrorAction SilentlyContinue

Start-Process -FilePath "powershell" -ArgumentList @("-NoProfile","-ExecutionPolicy","Bypass","-File",(Join-Path $root "tools\serial_capture.ps1"),"-Com","COM5","-Out",$serial,"-Seconds",($Seconds+120)) -WindowStyle Hidden
Start-Process -FilePath "powershell" -ArgumentList @("-NoProfile","-ExecutionPolicy","Bypass","-File",(Join-Path $root "tools\perf03_memlog.ps1"),"-Seconds",$Seconds,"-Out",$mem) -WindowStyle Hidden
Start-Sleep -Seconds 2
Start-Process -FilePath (Join-Path $root "host\src\state_host.exe") -RedirectStandardOutput $hostlog -RedirectStandardError (Join-Path $ev "perf03-host.err") -WindowStyle Hidden
Write-Host "PERF-03 started: ev=$ev seconds=$Seconds"
