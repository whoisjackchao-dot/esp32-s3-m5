# induce_accept_transient.ps1 —— 受控触发 accept() 瞬时错误（验证 #1 的 continue 分支）
# 手法：控制端点是单线程串行 accept。先开一条"只连不发"的连接让 http_handle_conn 阻塞在 recv，
#       使监听 backlog 积压；再把积压连接 SO_LINGER(0)+close 成 RST；最后释放阻塞连接，
#       服务端回到 accept() 时即可能收到 WSAECONNRESET(10054)。
# 期望：日志出现 "control accept transient err=10054; continue"，且随后 GET 仍 200。
# 复跑：powershell -ExecutionPolicy Bypass -File tools\induce_accept_transient.ps1
param(
  [int]$Port = 45978,
  [string]$Token = "accept-policy-test-token",
  [int]$Queued = 8
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root "host\src\state_host.exe"
$tmp = Join-Path $env:TEMP "sticks3-accept-induce"
New-Item -ItemType Directory -Force -Path $tmp | Out-Null
$env:LOCALAPPDATA    = $tmp
$env:STICKS3_HOST_PORT    = "$Port"
$env:STICKS3_HOST_TOKEN   = $Token
$env:STICKS3_HOST_SECONDS = "60"
$env:STICKS3_BRIDGE_JSON  = Join-Path $tmp "nonexistent-bridge.json"
$log = Join-Path $tmp "induce.log"
Remove-Item $log -ErrorAction SilentlyContinue
$p = Start-Process -FilePath $exe -RedirectStandardOutput $log -RedirectStandardError "$log.err" -PassThru -WindowStyle Hidden
Start-Sleep -Milliseconds 1200

# 1) blocker: connect but never send -> server blocks in recv (serial loop stalled)
$blocker = New-Object System.Net.Sockets.TcpClient
$blocker.Connect("127.0.0.1", $Port)
Start-Sleep -Milliseconds 300

# 2) queue connections, then RST them while server is stalled
$conns = @()
for ($i = 0; $i -lt $Queued; $i++) {
  try {
    $c = New-Object System.Net.Sockets.TcpClient
    $ok = $c.ConnectAsync("127.0.0.1", $Port).Wait(200)
    if ($ok) { $conns += $c }
  } catch { }
}
Write-Host "queued conns = $($conns.Count)"
foreach ($c in $conns) {
  try { $c.LingerState = New-Object System.Net.Sockets.LingerOption($true, 0); $c.Close() } catch { }
}
Start-Sleep -Milliseconds 300

# 3) release the blocker -> server returns to accept()
$blocker.Close()
Start-Sleep -Milliseconds 500

# 4) endpoint must still answer
$hdr = @{ Authorization = "Bearer $Token" }
$ok = 0
1..5 | ForEach-Object {
  try {
    $r = Invoke-WebRequest -Uri "http://127.0.0.1:$Port/v1/retry/status" -Headers $hdr -UseBasicParsing -TimeoutSec 3
    if ([int]$r.StatusCode -eq 200) { $ok++ }
    Write-Host "  GET #$_ -> $([int]$r.StatusCode)"
  } catch { Write-Host "  GET #$_ -> ERR" }
}

Write-Host "== accept log lines =="
$lines = @()
if (Test-Path $log) { $lines = Select-String -Path $log -Pattern "control accept|control endpoint" | ForEach-Object { $_.Line } }
$lines | ForEach-Object { Write-Host ("  " + $_) }
$induced = ($lines | Where-Object { $_ -match "transient err=" }).Count

try { if (-not $p.HasExited) { $p.Kill() } } catch {}
Write-Host "induced_transient_errors=$induced  endpoint_ok=$ok/5"
Write-Host "log=$log"
if ($induced -gt 0 -and $ok -eq 5) { Write-Host "INDUCED_PASS"; exit 0 }
if ($induced -eq 0) { Write-Host "NOT_INDUCED"; exit 3 }
exit 1
