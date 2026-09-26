# host_accept_resilience.ps1 —— M4 否决项② 修复验证（执行清单 #1）
# 目的：证明控制端点在 accept 瞬时错误下不再永久失效：RST 风暴前后反复 GET /v1/retry/status 始终可连。
# 隔离：独立端口 + 独立 LOCALAPPDATA + 指向不存在的 bridge.json（不触碰真实 BLE/桥接/真实 host.json）。
# 复跑：powershell -ExecutionPolicy Bypass -File tools\host_accept_resilience.ps1
param(
  [int]$Port = 45977,
  [string]$Token = "accept-policy-test-token",
  [int]$GetRounds = 12,
  [int]$StormRounds = 800
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root "host\src\state_host.exe"
if (-not (Test-Path $exe)) { Write-Host "MISSING $exe"; exit 2 }

$tmp = Join-Path $env:TEMP "sticks3-accept-test"
New-Item -ItemType Directory -Force -Path $tmp | Out-Null
$env:LOCALAPPDATA    = $tmp
$env:STICKS3_HOST_PORT    = "$Port"
$env:STICKS3_HOST_TOKEN   = $Token
$env:STICKS3_HOST_SECONDS = "120"
$env:STICKS3_BRIDGE_JSON  = Join-Path $tmp "nonexistent-bridge.json"

$log = Join-Path $tmp "host_accept_resilience.log"
Remove-Item $log -ErrorAction SilentlyContinue
$p = Start-Process -FilePath $exe -RedirectStandardOutput $log -RedirectStandardError "$log.err" -PassThru -WindowStyle Hidden
Start-Sleep -Milliseconds 1200

$hdr = @{ Authorization = "Bearer $Token" }
function Get-RetryStatus {
  try {
    $r = Invoke-WebRequest -Uri "http://127.0.0.1:$Port/v1/retry/status" -Headers $hdr -UseBasicParsing -TimeoutSec 3
    return [int]$r.StatusCode
  } catch {
    if ($_.Exception.Response) { return [int]$_.Exception.Response.StatusCode }
    return -1
  }
}

$fail = 0
Write-Host "== A. RST 风暴前：串行 GET x$GetRounds =="
1..$GetRounds | ForEach-Object {
  $code = Get-RetryStatus
  if ($code -ne 200) { $fail++; Write-Host "  GET #$_ -> $code  <FAIL>" } else { Write-Host "  GET #$_ -> 200" }
}

Write-Host "== B. RST 风暴：$StormRounds 次 连接+SO_LINGER(0)+close =="
$stormOk = 0
for ($i = 0; $i -lt $StormRounds; $i++) {
  try {
    $c = New-Object System.Net.Sockets.TcpClient
    $c.Connect("127.0.0.1", $Port)
    $c.LingerState = New-Object System.Net.Sockets.LingerOption($true, 0)
    $c.Close()
    $stormOk++
  } catch { }
}
Write-Host "  storm sent=$stormOk / $StormRounds"
Start-Sleep -Milliseconds 400

Write-Host "== C. 风暴后：串行 GET x$GetRounds =="
1..$GetRounds | ForEach-Object {
  $code = Get-RetryStatus
  if ($code -ne 200) { $fail++; Write-Host "  GET #$_ -> $code  <FAIL>" } else { Write-Host "  GET #$_ -> 200" }
}

Write-Host "== host 侧 accept 记录 =="
if (Test-Path $log) {
  Select-String -Path $log -Pattern "control accept|control endpoint" |
    ForEach-Object { Write-Host ("  " + $_.Line) }
}

Write-Host "== 结果 =="
$verdict = if ($fail -eq 0) { "PASS" } else { "FAIL" }
Write-Host "$verdict : endpoint_get_failures=$fail"
try { if (-not $p.HasExited) { $p.Kill() } } catch {}
Write-Host "log=$log"
if ($fail -ne 0) { exit 1 } else { exit 0 }
