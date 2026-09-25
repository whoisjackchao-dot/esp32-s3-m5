# ASR 真实端点冒烟运行器（Key 隐藏输入→进程环境变量→用完即清）
# 用法：powershell -ExecutionPolicy Bypass -File tools\asr_smoke_run.ps1 -AudioPath C:\Temp\test_asr.wav
param([Parameter(Mandatory = $true)][string]$AudioPath)

$exe = Join-Path (Split-Path -Parent $PSScriptRoot) "host\src\asr_smoke.exe"
if (-not (Test-Path -LiteralPath $exe)) { throw "smoke exe 不存在：$exe（先 build_asr_smoke.cmd）" }
if (-not (Test-Path -LiteralPath $AudioPath)) { throw "音频不存在：$AudioPath" }

$s = Read-Host '请输入智谱API Key（隐藏输入）' -AsSecureString
$env:STICKS3_ASR_KEY = [Runtime.InteropServices.Marshal]::PtrToStringBSTR(
    [Runtime.InteropServices.Marshal]::SecureStringToBSTR($s))
try {
    & $exe $AudioPath
    exit $LASTEXITCODE
} finally {
    Remove-Item Env:STICKS3_ASR_KEY -ErrorAction SilentlyContinue
}
