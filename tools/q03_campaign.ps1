param(
    [string]$Port = "COM5",
    [string]$Out = (Join-Path (Split-Path -Parent $PSScriptRoot) "captures\q03\records.jsonl"),
    [int]$MaxTry = 3,
    [hashtable]$Counts = @{ "5s" = 5; "10s" = 5; "30s" = 5 }
)
$ErrorActionPreference = "Stop"
$py = (Get-Command python).Source
$order = @("5s", "10s", "30s")

$sec = Read-Host "智谱 API Key（隐藏输入，本战役复用）" -AsSecureString
$env:STICKS3_ASR_KEY = [Runtime.InteropServices.Marshal]::PtrToStringBSTR(
    [Runtime.InteropServices.Marshal]::SecureStringToBSTR($sec))

$done = 0
$redone = 0
$bridgeOk = $true
try {
    foreach ($g in $order) {
        $n = [int]$Counts[$g]
        for ($i = 1; $i -le $n; $i++) {
            for ($try = 1; $try -le $MaxTry; $try++) {
                try {
                    . "$PSScriptRoot\bridge_env.ps1" | Out-Null
                } catch {
                    $bridgeOk = $false
                    Write-Host "[STOP] 桥接装载失败（OpenCode 可能已退出 / epoch 已旋转）：$($_.Exception.Message)" -ForegroundColor Red
                    Write-Host "[HINT] 确认 OpenCode 停留在目标会话后重跑本脚本即可续跑（记录在案）。" -ForegroundColor Yellow
                    break
                }
                if ($try -gt 1) {
                    Write-Host "[RETRY] 组 $g 第 $i 次 —— 第 $try 次尝试" -ForegroundColor Yellow
                }
                Write-Host "=== 组 $g 第 $i/$n 次：出现提示后按住 BtnA 讲话（目标约 $g，须达标）再松开 ===" -ForegroundColor Cyan
                & $py "$PSScriptRoot\q03_run.py" --group $g --index $i --port $Port --out $Out
                if ($LASTEXITCODE -eq 0) { break }
                $redone += 1
            }
            if (-not $bridgeOk) { break }
            $done += 1
        }
        if (-not $bridgeOk) { break }
    }
} finally {
    Remove-Item Env:STICKS3_ASR_KEY -ErrorAction SilentlyContinue
}

Write-Host "=== Q03 汇总判定（本次完成 $done 次；重试 $redone 次）===" -ForegroundColor Cyan
& $py "$PSScriptRoot\q03_latency.py" $Out
