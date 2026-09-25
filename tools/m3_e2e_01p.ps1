# ============================================================================
# StickS3 x OpenCode - M3 闭环一键编排（E2E-01p 完整设备链路，路径 A）
# ----------------------------------------------------------------------------
# 用例：E2E-01p（E2E-01 人工确认子步）｜需求：R11｜节点：M3 故事4
# 串联：bridge_env(点源) -> voice_slice(采音30s/解码/ASR/草稿) -> manual_send_verify(-Existing)
#
# 前置条件（不满足会在对应步骤 FAIL）：
#   1. opencode 已重启且桥接插件已加载（bridge.json 存在、/v1/binding 可查）
#   2. opencode 停留在目标会话视图（脚本运行全程不要切换/新建会话）
#   3. ESP32 设备已上电、BLE 可连（固件应为 STEREO 修复版）
#   4. 智谱 ASR Key 可用（脚本中途隐藏输入，可能产生真实调用费用）
#
# 用法（Windows PowerShell，任意目录）：
#   powershell -ExecutionPolicy Bypass -File <仓库根>\tools\m3_e2e_01p.ps1
#
# 退出码：0=全链 PASS；1=某步失败（脚本内部会标注失败步骤）；3=桥接不可用
# ============================================================================

$ErrorActionPreference = "Stop"
$root  = Split-Path -Parent $PSScriptRoot
$tools = Join-Path $root "tools"

function Step([string]$t) {
    Write-Host ""
    Write-Host ("===== {0} =====" -f $t) -ForegroundColor Cyan
}

Write-Host "M3 闭环一键编排（E2E-01p，路径 A：设备语音全链）" -ForegroundColor White
Write-Host "开始前请确认：" -ForegroundColor Yellow
Write-Host "  - opencode 停留在目标会话（全程不要切换/新建会话）"
Write-Host "  - ESP32 已上电、可 BLE 连接"
Write-Host "  - 智谱 ASR Key 在手（中途隐藏输入；真实调用，可能计费）"

# ------------------------------------------------ 步骤 1/3：装载桥接环境变量
Step "1/3 装载桥接环境变量（bridge_env.ps1，点源）"
. (Join-Path $tools "bridge_env.ps1")
if (-not $env:STICKS3_BRIDGE_URL -or -not $env:STICKS3_SESSION_ID) {
    Write-Host "[FAIL] 桥接变量未装载成功（bridge.json 缺失或绑定不可用）" -ForegroundColor Red
    exit 3
}

# ------------------------------------------------ 步骤 2/3：设备采音 + ASR + 草稿
Step "2/3 设备采音30秒 → Opus解码 → 智谱ASR → 草稿追加（voice_slice.ps1）"
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $tools "voice_slice.ps1")
$slice = $LASTEXITCODE
if ($slice -ne 0) {
    Write-Host ""
    Write-Host ("[FAIL] voice_slice 退出码 {0} —— 闭环中止，未执行人工确认。" -f $slice) -ForegroundColor Red
    Write-Host "  - BLE/采音异常：检查设备上电与固件（STEREO 修复版）" -ForegroundColor Yellow
    Write-Host "  - 桥接变量缺失：本脚本步骤 1 是否 [OK]" -ForegroundColor Yellow
    Write-Host "  - ASR 失败：核对 Key / 网络（企业网络需 --ssl-revoke-best-effort 场景见手册 §7.2）" -ForegroundColor Yellow
    exit $slice
}
Write-Host "[OK] 草稿已填入 OpenCode 输入框（未提交）" -ForegroundColor Green

# ------------------------------------------------ 步骤 3/3：目视确认 + 手动发送
Step "3/3 目视确认 + 手动发送（m3_manual_send_verify.ps1 -Existing）"
Write-Host "接下来按脚本提示：确认草稿 → 手动发送 → 回答 4 个确认问题。" -ForegroundColor White
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $tools "m3_manual_send_verify.ps1") -Existing
$verify = $LASTEXITCODE

# ------------------------------------------------ 收尾
Step "收尾"
if ($verify -eq 0) {
    Write-Host "[PASS] E2E-01p 闭环成立：草稿落点正确 + 由人手动发送 + 未自动提交。" -ForegroundColor Green
    Write-Host "证据已由 verify 脚本归档至 evidence\M3\，请回到 OpenCode 会话告知结果，"
    Write-Host "由会话完成索引登记（勾掉待用户决定第一条）与本地 commit。"
} else {
    Write-Host ("[FAIL/需复核] verify 退出码 {0} —— 不得宣布 M3 闭环成立。" -f $verify) -ForegroundColor Red
    Write-Host "请把实际差异带回 OpenCode 会话处理。"
}
exit $verify
