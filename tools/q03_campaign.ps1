param(
    [string]$Port = "COM5",
    [string]$Out = "C:\Temp\ESP32-S3M5\captures\q03\records.jsonl",
    [int]$PerGroup = 20
)
$ErrorActionPreference = "Stop"
. "$PSScriptRoot\bridge_env.ps1"

$sec = Read-Host "智谱 API Key（隐藏输入，本战役复用）" -AsSecureString
$env:STICKS3_ASR_KEY = [Runtime.InteropServices.Marshal]::PtrToStringBSTR(
    [Runtime.InteropServices.Marshal]::SecureStringToBSTR($sec))

$py = "C:\Users\shiwenc\AppData\Local\Programs\Python\Python312\python.exe"
try {
    foreach ($g in @("5s", "15s", "30s")) {
        for ($i = 1; $i -le $PerGroup; $i++) {
            Write-Host "=== 组 $g 第 $i/$PerGroup 次：出现提示后按住 BtnA 讲话（约 $g）再松开 ===" -ForegroundColor Cyan
            & $py "$PSScriptRoot\q03_run.py" --group $g --index $i --port $Port --out $Out
        }
    }
} finally {
    Remove-Item Env:STICKS3_ASR_KEY -ErrorAction SilentlyContinue
}

Write-Host "=== Q03 汇总判定 ===" -ForegroundColor Cyan
& $py "$PSScriptRoot\q03_latency.py" $Out
