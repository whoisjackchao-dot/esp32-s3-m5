# 桥接环境变量装载器：读插件写的 bridge.json → 查 /v1/binding 取 sessionID
# → 导出 STICKS3_BRIDGE_* 环境变量（不打印 token）
# 用法（必须点源，使变量留在当前会话）：
#   . "C:\Temp\ESP32-S3M5\tools\bridge_env.ps1"
$ErrorActionPreference = "Stop"

$cands = @(
    (Join-Path $env:USERPROFILE ".local\share\sticks3-opencode\bridge.json"),
    "\\wsl.localhost\Ubuntu\home\shiwenc\.local\share\sticks3-opencode\bridge.json",
    "\\wsl$\Ubuntu\home\shiwenc\.local\share\sticks3-opencode\bridge.json"
)
$desc = $cands | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $desc) {
    Write-Host "[FAIL] 找不到 bridge.json —— StickS3 插件未运行？" -ForegroundColor Red
    Write-Host "       已找过的位置：" ; $cands | ForEach-Object { Write-Host "         $_" }
    return
}
Write-Host "[OK] 描述文件: $desc"

$j = Get-Content -LiteralPath $desc -Raw | ConvertFrom-Json
if (-not $j.url -or -not $j.token -or -not $j.tuiEpoch) { throw "bridge.json 字段不完整" }

# 查绑定：取会话目标（token 不打印）
# 字段以插件实际实现为准（BindingTarget，见 plugin/src/core/types.ts）：
#   currentRouteSessionID = TUI 当前路由会话（append 目标；coordinator 校验 req.sessionID 必须等于它）
#   boundSessionID        = 插件绑定会话（与上者同源，作兜底）
#   canAppend ≡ promptVisible ∧ ¬promptDisabled ∧ ¬dialogBlocking（M1 契约 BR-02 语义，细化归 M4）
$hdr = @{ Authorization = "Bearer $($j.token)" }
$bind = Invoke-RestMethod -Uri "$($j.url)/v1/binding" -Headers $hdr -TimeoutSec 5
$sessionID = if ($bind.currentRouteSessionID) { $bind.currentRouteSessionID } else { $bind.boundSessionID }
$canAppend = [bool]($bind.promptVisible -and -not $bind.promptDisabled -and -not $bind.dialogBlocking)
if (-not $sessionID) { throw "绑定响应缺少可用会话（currentRouteSessionID/boundSessionID 均为空；promptVisible=$($bind.promptVisible)）" }

$env:STICKS3_BRIDGE_URL   = $j.url
$env:STICKS3_BRIDGE_TOKEN = $j.token
$env:STICKS3_TUI_EPOCH    = $j.tuiEpoch
$env:STICKS3_SESSION_ID   = $sessionID

Write-Host ("[OK] 桥接已装载：url={0} epoch={1} session={2} canAppend={3}（token 已隐藏）" -f `
    $j.url, $j.tuiEpoch, $sessionID, $canAppend) -ForegroundColor Green
if (-not $canAppend) { Write-Host "[WARN] 当前输入框不可追加（canAppend=false）——请切到会话视图且无阻塞对话框后重试" -ForegroundColor Yellow }
