# ============================================================================
# StickS3 x OpenCode - M3 人工闭环确认：OpenCode 手动发送 + 目视确认
# ----------------------------------------------------------------------------
# 用例：E2E-01p（E2E-01 的人工确认子步）｜需求：R11｜节点：M3 故事4
# 目的：闭合交付节点 M3 出口判据的最后一条 —— “结果出现在正确输入框，
#       且待用户手动发送”。把“我发了”变成有前后锚点、可审计的归档证据。
#
# 桥接能力边界（contracts/M1-桥接契约冻结.md + plugin/src/bridge/server.ts 实测）：
#   GET  /v1/health              -> {protocolVersion, pluginVersion, tuiEpoch, alive}
#   GET  /v1/binding             -> BindingTarget {epoch, boundSessionID,
#                                   currentRouteSessionID, promptVisible,
#                                   promptDisabled, dialogBlocking}
#   POST /v1/draft/append        -> {status:"applied"|...}   （唯一写操作）
#   GET  /v1/operations/{opID}   -> {status, reason?}
#   POST /v1/operations/{opID}/cancel
#   GET  /v1/state/stream        -> SSE（M1 仅一条快照）
#
#   桥接【不提供】submit / 草稿读回 / 消息计数（契约 §6，server.ts 实测）。
#   因此“是否已发送、落点是否正确、输入框是否清空”只能由人工目视裁决；
#   本脚本负责：采集前后锚点 + 结构化引导人工确认 + 生成证据文档。
#   脚本全程不调用任何 submit/审批接口（这正是要被验证的安全边界）。
#
# 用法（Windows PowerShell，仓库根目录）：
#   powershell -ExecutionPolicy Bypass -File tools\m3_manual_send_verify.ps1
#       -> 默认：注入一条哨兵文本到当前草稿，人工目视确认后手动发送（无需设备/ASR）
#   powershell ... -Existing
#       -> 不注入；确认并发送“已经由 voice_slice.ps1 填入的那段草稿”
#   powershell ... -Text "自定义文本"
#   powershell ... -ExpectSession ses_xxx
#   powershell ... -NoEvidence
#
# 退出码：0=PASS闭环成立；1=有未达标项；3=前置/桥接不可用
# ============================================================================
param(
    [switch]$Existing,
    [string]$Text = "",
    [string]$ExpectSession = "",
    [switch]$NoEvidence,
    [string]$EvidenceDir = ""
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $EvidenceDir) { $EvidenceDir = Join-Path $repoRoot "evidence\M3" }

function Write-Section([string]$t) {
    Write-Host ""
    Write-Host "=== $t ===" -ForegroundColor Cyan
}

# ------------------------------------------------------------- 1. 读 bridge.json
Write-Section "1/6 定位桥接"
$cands = @(
    (Join-Path $env:USERPROFILE ".local\share\sticks3-opencode\bridge.json"),
    "\\wsl.localhost\Ubuntu\home\home\.local\share\sticks3-opencode\bridge.json",
    "\\wsl$\Ubuntu\home\home\.local\share\sticks3-opencode\bridge.json"
)
$desc = $cands | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $desc) {
    Write-Host "[FAIL] 找不到 bridge.json —— StickS3 插件未运行？" -ForegroundColor Red
    $cands | ForEach-Object { Write-Host "         $_" }
    exit 3
}
Write-Host "[OK] 描述文件: $desc"
$j = Get-Content -LiteralPath $desc -Raw | ConvertFrom-Json
if (-not $j.url -or -not $j.token -or -not $j.tuiEpoch) {
    Write-Host "[FAIL] bridge.json 字段不完整（需要 url/token/tuiEpoch）" -ForegroundColor Red
    exit 3
}
$hdr = @{ Authorization = "Bearer $($j.token)" }

# 统一桥接调用：返回 @{status; raw; json}，非 2xx 也取到 body（便于按 status 字段判定）
function Invoke-BridgeJson {
    param([string]$Method, [string]$Path, [byte[]]$BodyBytes = $null)
    $uri = "$($j.url)$Path"
    $status = 0; $text = ""
    try {
        $reqArgs = @{
            Method = $Method; Uri = $uri; Headers = $hdr
            TimeoutSec = 10; UseBasicParsing = $true
        }
        if ($BodyBytes) {
            $reqArgs.ContentType = "application/json"
            $reqArgs.Body = $BodyBytes
        }
        $r = Invoke-WebRequest @reqArgs
        $status = [int]$r.StatusCode
        $text = $r.Content
    } catch {
        $resp = $_.Exception.Response
        if ($resp) {
            $status = [int]$resp.StatusCode
            try {
                $sr = New-Object System.IO.StreamReader($resp.GetResponseStream())
                $text = $sr.ReadToEnd()
            } catch { $text = "" }
        }
        if (-not $text) { $text = "{""__transport_error"":""$($_.Exception.Message)""}" }
    }
    $obj = $null
    if ($text) { try { $obj = $text | ConvertFrom-Json } catch { $obj = $null } }
    [pscustomobject]@{ status = $status; raw = $text; json = $obj }
}

# ------------------------------------------------------------- 2. 前置锚点
Write-Section "2/6 发送前锚点（/v1/health + /v1/binding）"
$health = Invoke-BridgeJson -Method "GET" -Path "/v1/health"
if ($health.status -ne 200) {
    Write-Host "[FAIL] /v1/health -> HTTP $($health.status)：$($health.raw)" -ForegroundColor Red
    exit 3
}
Write-Host ("[OK] health: alive={0} pluginVersion={1} tuiEpoch={2}" -f `
    $health.json.alive, $health.json.pluginVersion, $health.json.tuiEpoch) -ForegroundColor Green

$bind = Invoke-BridgeJson -Method "GET" -Path "/v1/binding"
if ($bind.status -ne 200 -or -not $bind.json) {
    Write-Host "[FAIL] /v1/binding -> HTTP $($bind.status)：$($bind.raw)" -ForegroundColor Red
    exit 3
}
$canAppend = [bool]($bind.json.promptVisible -and -not $bind.json.promptDisabled -and -not $bind.json.dialogBlocking)
$liveSession = if ($bind.json.currentRouteSessionID) { $bind.json.currentRouteSessionID } else { $bind.json.boundSessionID }
$session = if ($ExpectSession) { $ExpectSession } else { $liveSession }
if (-not $session) {
    Write-Host "[FAIL] 无可用会话（currentRouteSessionID/boundSessionID 均为空）——请切到会话视图" -ForegroundColor Red
    exit 3
}
Write-Host ("[OK] binding: currentRouteSessionID={0} canAppend={1}" -f $bind.json.currentRouteSessionID, $canAppend) -ForegroundColor Green
if (-not $canAppend) {
    Write-Host "[FAIL] canAppend=false —— 输入框当前不可追加（需会话视图、无阻塞对话框）" -ForegroundColor Red
    exit 3
}
if ($ExpectSession -and $liveSession -and ($ExpectSession -ne $liveSession)) {
    Write-Host ("[WARN] 预期会话({0}) != 实时路由会话({1})；请先切到预期会话" -f $ExpectSession, $liveSession) -ForegroundColor Yellow
}

# ------------------------------------------------------------- 3. 注入哨兵（可选）
$opID = $null
$apResp = $null
$injected = $false
Write-Section "3/6 准备草稿"
if ($Existing) {
    Write-Host "[INFO] -Existing：不注入。请确认输入框里是你刚由 voice_slice 填入的草稿。" -ForegroundColor Yellow
} else {
    if (-not $Text) {
        $Text = "[M3-CLOSE-CHECK {0}] Visual-confirm this text is in the CORRECT session input box, then press Enter manually. (bridge has no submit)" -f (Get-Date -Format "yyyy-MM-dd HH:mm:ss")
    }
    $opID = "op_m3close_{0}_{1}" -f (Get-Date -Format "yyyyMMddHHmmss"), (Get-Random -Minimum 1000 -Maximum 9999)
    $bodyObj = [ordered]@{
        protocolVersion       = 1
        tuiEpoch              = $j.tuiEpoch
        sessionID             = $session
        operationID           = $opID
        transactionGeneration = 1
        text                  = $Text
    }
    $bodyJson = $bodyObj | ConvertTo-Json -Compress
    $bodyBytes = [System.Text.Encoding]::UTF8.GetBytes($bodyJson)
    Write-Host "[INFO] 注入哨兵文本（BR-04 /v1/draft/append）..." -ForegroundColor Yellow
    $apResp = Invoke-BridgeJson -Method "POST" -Path "/v1/draft/append" -BodyBytes $bodyBytes
    if ($apResp.status -ne 200 -or $apResp.json.status -ne "applied") {
        Write-Host "[FAIL] 追加未 applied：HTTP $($apResp.status) $($apResp.raw)" -ForegroundColor Red
        exit 3
    }
    $injected = $true
    Write-Host "[OK] status=applied，operationID=$opID（未提交）" -ForegroundColor Green
}

# 前快照（含 operation 回执查询，TTL 300s 内有效）
$opQueryPre = $null
if ($opID) { $opQueryPre = Invoke-BridgeJson -Method "GET" -Path "/v1/operations/$opID" }
$preSnapshot = [ordered]@{
    capturedAt            = (Get-Date -Format "o")
    bridgeUrl             = $j.url
    tuiEpoch              = $j.tuiEpoch
    pluginVersion         = $health.json.pluginVersion
    liveSession           = $liveSession
    expectedSession       = $session
    canAppend             = $canAppend
    binding               = $bind.json
    injected              = $injected
    injectedText          = if ($injected) { $Text } else { $null }
    operationID           = $opID
    appendResponse        = if ($apResp) { $apResp.json } else { $null }
    operationQueryPreSend = if ($opQueryPre) { $opQueryPre.json } else { $null }
}

# ------------------------------------------------------------- 4. 人工目视确认 + 手动发送
Write-Section "4/6 人工目视确认 + 手动发送（这一步只能由你完成）"
Write-Host "请在 OpenCode 中依次确认：" -ForegroundColor White
Write-Host ("  (A) 当前会话是预期会话: {0}" -f $session) -ForegroundColor White
if ($injected) {
    Write-Host "  (B) 输入框里出现了刚才注入的哨兵文本（含 [M3-CLOSE-CHECK ...]）" -ForegroundColor White
} else {
    Write-Host "  (B) 输入框里的草稿就是你刚填入的那段转写内容" -ForegroundColor White
}
Write-Host "  (C) 输入框仍是草稿，尚未被提交（没有新增的用户消息）" -ForegroundColor White
Write-Host ""
Write-Host "注意：本脚本没有 send/submit 能力（桥接契约 §6 明确不提供）。发送必须由你按 Enter 完成。" -ForegroundColor Yellow
$null = Read-Host "目视确认完成后，回到本窗口按 Enter 继续"

$null = Read-Host "现在切到 OpenCode，按 Enter 手动发送；发送完成后回到本窗口按 Enter"

# ------------------------------------------------------------- 5. 发送后锚点
Write-Section "5/6 发送后锚点"
$health2 = Invoke-BridgeJson -Method "GET" -Path "/v1/health"
$bind2 = Invoke-BridgeJson -Method "GET" -Path "/v1/binding"
$opQueryPost = $null
if ($opID) { $opQueryPost = Invoke-BridgeJson -Method "GET" -Path "/v1/operations/$opID" }
$postSnapshot = [ordered]@{
    capturedAt             = (Get-Date -Format "o")
    health                 = $health2.json
    binding                = $bind2.json
    operationQueryPostSend = if ($opQueryPost) { $opQueryPost.json } else { $null }
}

# 结构化人工裁决（桥接无可观测信号，只能问人）
Write-Host ""
$q1 = (Read-Host "1) 草稿是否出现在【预期会话 $session】？(y/n)").Trim().ToLower()
$q2 = (Read-Host "2) 你是否【手动】按 Enter 发送了？（不是脚本发送）(y/n)").Trim().ToLower()
$q3 = (Read-Host "3) 发送后输入框是否已清空？(y/n)").Trim().ToLower()
$q4 = (Read-Host "4) 是否出现重复消息或自动二次发送？(y/n，期望 n)").Trim().ToLower()

# ------------------------------------------------------------- 判定
$checks = @(
    [pscustomobject]@{ name = "落点=预期会话"; expect = "y"; got = $q1; ok = ($q1 -eq "y") },
    [pscustomobject]@{ name = "人工手动发送"; expect = "y"; got = $q2; ok = ($q2 -eq "y") },
    [pscustomobject]@{ name = "发送后输入框清空"; expect = "y"; got = $q3; ok = ($q3 -eq "y") },
    [pscustomobject]@{ name = "无重复/自动二次发送"; expect = "n"; got = $q4; ok = ($q4 -eq "n") }
)
$anchors = @()
$anchors += [pscustomobject]@{ name = "bridge alive(后)"; expect = "True"; got = "$($health2.json.alive)"; ok = ($health2.json.alive -eq $true) }
$anchors += [pscustomobject]@{ name = "epoch 未旋转"; expect = $health.json.tuiEpoch; got = "$($health2.json.tuiEpoch)"; ok = ("$($health2.json.tuiEpoch)" -eq "$($health.json.tuiEpoch)") }
if ($opID -and $opQueryPost) {
    $anchors += [pscustomobject]@{ name = "append 回执(applied)"; expect = "applied"; got = "$($opQueryPost.json.status)"; ok = ("$($opQueryPost.json.status)" -eq "applied") }
}
$bad = @($checks + $anchors | Where-Object { -not $_.ok })
$allOk = ($bad.Count -eq 0)

Write-Section "6/6 判定"
$checks + $anchors | ForEach-Object {
    $mark = if ($_.ok) { "[PASS]" } else { "[FAIL]" }
    $color = if ($_.ok) { "Green" } else { "Red" }
    Write-Host ("{0} {1}: expect={2} got={3}" -f $mark, $_.name, $_.expect, $_.got) -ForegroundColor $color
}
if ($allOk) {
    Write-Host ""
    Write-Host "PASS：M3 用户价值闭环成立（草稿落点正确 + 由人手动发送 + 未自动提交）。" -ForegroundColor Green
} else {
    Write-Host ""
    Write-Host "FAIL/需复核：存在未达标项，不得据此宣布 M3 闭环成立。" -ForegroundColor Red
}

# ------------------------------------------------------------- 证据归档
if (-not $NoEvidence) {
    if (-not (Test-Path -LiteralPath $EvidenceDir)) { New-Item -ItemType Directory -Force -Path $EvidenceDir | Out-Null }
    $stamp = Get-Date -Format "yyyy-MM-dd-HHmmss"
    $evFile = Join-Path $EvidenceDir "$stamp-M3人工闭环确认.md"
    $verdict = if ($allOk) { "PASS" } else { "FAIL" }
    $verdictLine = if ($allOk) {
        "M3 用户价值闭环成立：草稿出现在预期会话输入框，且由人手动发送；桥接未提供、脚本未调用任何自动发送接口。"
    } else {
        "存在未达标项（见第 6 节）；不得据此宣布闭环成立。"
    }
    $preJson = ($preSnapshot | ConvertTo-Json -Depth 8)
    $postJson = ($postSnapshot | ConvertTo-Json -Depth 8)
    $checkRows = (($checks + $anchors) | ForEach-Object {
            "| {0} | {1} | {2} | {3} |" -f $_.name, $_.expect, $_.got, $(if ($_.ok) { "PASS" } else { "FAIL" })
        }) -join "`n"
    $modeLine = if ($Existing) { "确认已有草稿（未注入）" } else { "注入哨兵文本后确认" }
    $md = @"
# M3 人工闭环确认：OpenCode 手动发送并目视确认（$stamp）

> 用例：E2E-01p（E2E-01 的人工确认子步）｜需求：R11｜节点：M3 故事4｜类型：H（人工，不可自动裁决）
> 契约版本：M1 桥接契约冻结（2026-09-20）；实现依据：plugin/src/bridge/server.ts 实测
> 状态：**$verdict** —— $verdictLine

## 1. 运行方式

- 命令：``powershell -ExecutionPolicy Bypass -File tools\m3_manual_send_verify.ps1``
- 模式：$modeLine
- 预期会话：$session ｜ 实时路由会话：$liveSession ｜ canAppend：$canAppend

## 2. 发送前快照（/v1/health + /v1/binding + /v1/operations）

```json
$preJson
```

## 3. 人工步骤

1. 在 OpenCode 目视确认：会话 = 预期会话；输入框 = 目标文本；草稿未被提交。
2. 用户按 Enter 手动发送（脚本无 send/submit 能力）。

## 4. 发送后快照（/v1/health + /v1/binding + /v1/operations）

```json
$postJson
```

## 5. 判定

| 检查 | 期望 | 实际 | 结果 |
| --- | --- | --- | --- |
$checkRows

## 6. 诚实边界（本记录不能证明什么）

- 桥接不提供 submit / 草稿读回 / 消息计数（M1 契约 §6；server.ts 实测仅 6 条只读或追加端点）。
- 第 3 节人工声明不可自动核验；本节前后快照只能证明 epoch 未旋转、追加回执为 applied，**不能**证明消息已提交。
- E2E-01 的托盘、物理松键、设备状态屏部分仍未覆盖（见 M3 故事4 纵切证据 §7）。

## 7. 追踪

- 需求：R11（转写只进正确草稿末尾、不自动发送）
- 用例：E2E-01p（E2E-01 成功路径的人工确认子步）
- 节点：M3 故事4 / 交付节点 M3 出口判据（“结果出现在正确输入框且待用户手动发送”）

## 8. 清理

- 未修改项目代码；未 git commit；captures/ 不入库。
"@
    Set-Content -LiteralPath $evFile -Value $md -Encoding UTF8
    Write-Host ""
    Write-Host "[OK] 证据已写入: $evFile" -ForegroundColor Green
}

# ------------------------------------------------------------- 下一步
Write-Host ""
Write-Host "下一步（需你决定）：" -ForegroundColor Cyan
Write-Host "  1) 若 PASS：可勾掉 StickS3_OpenCode_文档索引.md 的「待用户决定」第一条（M3 闭环确认）。"
Write-Host "  2) M3 出关口仍剩：30 条语料冻结 + Q03 时延（5/15/30s 各 20 次）+ Q04 准确率（30 条 >=27）。"
Write-Host "  3) 之后进入 M4（状态屏三页 / 托盘 / USB-电池策略）。"

if ($allOk) { exit 0 } else { exit 1 }
