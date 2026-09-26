# verify_recv_timeout.ps1 —— M4 收口新发现(§5.2) 修复验证：停顿连接不得永久拖死控制端点
# 手法：保持一条"连上但从不发数据"的连接常开，随后轮询 GET /v1/retry/status。
# 期望(修复后)：端点在收包超时(默认 5s)内自行释放停顿连接并恢复 200；停顿连接全程不关闭。
# 隔离：独立端口/LOCALAPPDATA/bridge 指向不存在文件，不触碰真实 BLE/桥接/host.json。
# 复跑：powershell -ExecutionPolicy Bypass -File tools\verify_recv_timeout.ps1
param(
  [int]$Port = 45980,
  [string]$Token = "recv-timeout-test-token",
  [int]$StallHoldSec = 25,
  [int]$RecoverBudgetSec = 15
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root "host\src\state_host.exe"
$tmp = Join-Path $env:TEMP "sticks3-recv-timeout"
New-Item -ItemType Directory -Force -Path $tmp | Out-Null
$env:LOCALAPPDATA = $tmp
$env:STICKS3_HOST_PORT = "$Port"
$env:STICKS3_HOST_TOKEN = $Token
$env:STICKS3_HOST_SECONDS = "90"
$env:STICKS3_BRIDGE_JSON = Join-Path $tmp "no.json"
$log = Join-Path $tmp "recv_timeout.log"
Remove-Item $log -ErrorAction SilentlyContinue
$p = Start-Process -FilePath $exe -RedirectStandardOutput $log -RedirectStandardError "$log.err" -PassThru -WindowStyle Hidden
Start-Sleep -Milliseconds 1200

function RawGet([int]$timeoutMs) {
  $result = -1
  $c = $null
  try {
    $c = New-Object System.Net.Sockets.TcpClient
    $c.Connect("127.0.0.1", $Port)
    $c.ReceiveTimeout = $timeoutMs
    $s = $c.GetStream()
    $req = "GET /v1/retry/status HTTP/1.1`r`nHost: 127.0.0.1`r`nAuthorization: Bearer $Token`r`nConnection: close`r`n`r`n"
    $b = [Text.Encoding]::ASCII.GetBytes($req)
    $s.Write($b, 0, $b.Length); $s.Flush()
    $buf = New-Object byte[] 1024
    $n = $s.Read($buf, 0, $buf.Length)
    if ($n -le 0) {
      $result = -1
    } else {
      $txt = [Text.Encoding]::ASCII.GetString($buf, 0, $n)
      if ($txt -match "HTTP/1.1 200") { $result = 200 }
      elseif ($txt -match "HTTP/1.1 (\d+)") { $result = [int]$matches[1] }
      else { $result = -2 }
    }
  } catch {
    $result = -1
  }
  if ($c) { try { $c.Close() } catch {} }
  $result
}

# 1) 常开的停顿连接（连上，从不发数据，也不关闭）
$stall = New-Object System.Net.Sockets.TcpClient
$stall.Connect("127.0.0.1", $Port)
Write-Host "stalled client connected (will stay open)"

# 2) 轮询直到恢复或超预算
$sw = [System.Diagnostics.Stopwatch]::StartNew()
$recoveredMs = -1
while ($sw.Elapsed.TotalSeconds -lt $RecoverBudgetSec) {
  $code = RawGet 2500
  if ($code -eq 200) { $recoveredMs = [int]$sw.Elapsed.TotalMilliseconds; break }
  Start-Sleep -Milliseconds 300
}

Write-Host "recovered_after_ms=$recoveredMs  (budget=${RecoverBudgetSec}s)"
Write-Host "stall_held_open_sec=$([int]$sw.Elapsed.TotalSeconds)  (stalled client kept open throughout)"
Write-Host "== accept/recv log =="
if (Test-Path $log) { Select-String -Path $log -Pattern "control accept|control endpoint|recv" | ForEach-Object { Write-Host ("  " + $_.Line) } }

# 恢复发生在停顿连接未关闭期间 → 即证明端点不再被停顿连接永久拖死
$ok = ($recoveredMs -ge 0 -and $recoveredMs -lt ($RecoverBudgetSec * 1000))
try { $stall.Close() } catch {}
try { if (-not $p.HasExited) { $p.Kill() } } catch {}
Write-Host ("VERDICT=" + $(if ($ok) { "PASS" } else { "FAIL" }))
if ($ok) { exit 0 } else { exit 1 }
