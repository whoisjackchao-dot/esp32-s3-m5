param(
    [string]$Port = "COM5",
    [string]$Out = "C:\Temp\ESP32-S3M5\captures\q03\records.jsonl",
    [int]$PerGroup = 20,
    [string[]]$Groups = @("5s", "10s", "30s")
)
$ErrorActionPreference = "Stop"
$py = "C:\Users\shiwenc\AppData\Local\Programs\Python\Python312\python.exe"

$sec = Read-Host "智谱 API Key（隐藏输入，本战役复用）" -AsSecureString
$env:STICKS3_ASR_KEY = [Runtime.InteropServices.Marshal]::PtrToStringBSTR(
    [Runtime.InteropServices.Marshal]::SecureStringToBSTR($sec))

$done = 0
$bridgeOk = $true
try {
    foreach ($g in $Groups) {
        for ($i = 1; $i -le $PerGroup; $i++) {
            try {
                . "$PSScriptRoot\bridge_env.ps1" | Out-Null
            } catch {
                $bridgeOk = $false
                Write-Host "[STOP] 桥接装载失败（OpenCode 可能已退出 / epoch 已旋转）：$($_.Exception.Message)" -ForegroundColor Red
                Write-Host "[HINT] 确认 OpenCode 停留在目标会话后重跑本脚本即可续跑（已完成 $done 次记录在案）。" -ForegroundColor Yellow
                break
            }
            Write-Host "=== 组 $g 第 $i/$PerGroup 次：出现提示后按住 BtnA 讲话（约 $g）再松开 ===" -ForegroundColor Cyan
            & $py "$PSScriptRoot\q03_run.py" --group $g --index $i --port $Port --out $Out
            $done += 1
        }
        if (-not $bridgeOk) { break }
    }
} finally {
    Remove-Item Env:STICKS3_ASR_KEY -ErrorAction SilentlyContinue
}

Write-Host "=== Q03 汇总判定（本次完成 $done 次）===" -ForegroundColor Cyan
& $py "$PSScriptRoot\q03_latency.py" $Out
