# M3 纵切流水线：BLE 采音(30s) → Opus 解码 WAV → ASR 真实调用 → 草稿追加
# 前置：OpenCode+插件桥接已运行；环境变量已设
#   STICKS3_BRIDGE_URL / TOKEN / TUI_EPOCH / SESSION_ID（M1 契约值）
# 用法：
#   powershell -ExecutionPolicy Bypass -File tools\voice_slice.ps1            # 全流程
#   powershell ... -AudioPath C:\path\x.wav                                   # 跳过采音（调试）
param([string]$AudioPath = "")
$ErrorActionPreference = "Stop"
$src = "C:\Temp\ESP32-S3M5\host\src"
$tools = "C:\Temp\ESP32-S3M5\tools"
$capDir = "C:\Temp\ESP32-S3M5\captures\slice"
$py = "C:\Users\shiwenc\AppData\Local\Programs\Python\Python312\python.exe"
New-Item -ItemType Directory -Force -Path $capDir | Out-Null

$need = 'STICKS3_BRIDGE_URL', 'STICKS3_BRIDGE_TOKEN', 'STICKS3_TUI_EPOCH', 'STICKS3_SESSION_ID'
$missing = $need | Where-Object { -not (Get-Item "Env:$_" -ErrorAction SilentlyContinue) }
if ($missing) { Write-Host "提示：桥接变量缺失（$($missing -join ', ')），本次仅做 ASR 识别、不追加草稿" -ForegroundColor Yellow }

$s = Read-Host '智谱API Key（隐藏输入）' -AsSecureString
$env:STICKS3_ASR_KEY = [Runtime.InteropServices.Marshal]::PtrToStringBSTR(
    [Runtime.InteropServices.Marshal]::SecureStringToBSTR($s))

try {
    if ($AudioPath -ne "") {
        $wav = $AudioPath
    } else {
        Write-Host "== 1/3 BLE 采音：READY 触发，请对设备连续说 30 秒 ==" -ForegroundColor Cyan
        $env:STICKS3_CAPTURE_DIR = $capDir
        & "$src\ble_client.exe"
        if ($LASTEXITCODE -ne 0) { throw "BLE 采音失败" }
        Remove-Item Env:STICKS3_CAPTURE_DIR -ErrorAction SilentlyContinue
        $cap = Get-ChildItem -Path $capDir -Filter "rec_*.s3pcap" |
            Sort-Object LastWriteTime -Descending | Select-Object -First 1
        if (-not $cap) { throw "未找到捕获文件" }
        $wav = $cap.FullName -replace '\.s3pcap$', '.wav'
        Write-Host "== 2/3 Opus 解码 → WAV ==" -ForegroundColor Cyan
        & $py "$tools\decode_capture.py" $cap.FullName $wav
        if ($LASTEXITCODE -ne 0) { throw "解码失败" }
    }
    Write-Host "== 3/3 ASR 真实调用 → 草稿追加 ==" -ForegroundColor Cyan
    & "$src\voice_append.exe" $wav
    exit $LASTEXITCODE
} finally {
    Remove-Item Env:STICKS3_ASR_KEY -ErrorAction SilentlyContinue
    Remove-Item Env:STICKS3_CAPTURE_DIR -ErrorAction SilentlyContinue
}
