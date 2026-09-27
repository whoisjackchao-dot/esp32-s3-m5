# q04_run.ps1 — Q04 真实 ASR 跑批：冻结语料 15 条 wav → hypotheses.json
# Key 用隐藏输入，仅本进程环境变量，不入日志/命令行（SEC-03）。
# 用法：powershell -ExecutionPolicy Bypass -File tools\q04_run.ps1
param(
  [string]$Frozen = "C:\Users\home\esp32-s3-m5\tools\Q04语料-冻结-20260927.json",
  [string]$Out    = "C:\Users\home\esp32-s3-m5\tools\hypotheses-20260927.json"
)
$ErrorActionPreference = "Stop"
$exe = "C:\Users\home\esp32-s3-m5\host\src\asr_smoke.exe"
if (-not (Test-Path -LiteralPath $exe)) { throw "asr_smoke.exe 不存在：$exe" }
$doc = Get-Content -Raw -Encoding UTF8 $Frozen | ConvertFrom-Json
$s = Read-Host '请输入智谱 API Key（隐藏输入，仅本进程）' -AsSecureString
$env:STICKS3_ASR_KEY = [Runtime.InteropServices.Marshal]::PtrToStringBSTR(
    [Runtime.InteropServices.Marshal]::SecureStringToBSTR($s))
$hyps = New-Object System.Collections.ArrayList
try {
  $n = 0
  foreach ($smp in $doc.samples) {
    $n++
    Write-Host ("[{0}/{1}] {2}" -f $n, $doc.samples.Count, $smp.id)
    $raw = (& $exe $smp.wav 2>&1 | Out-String)
    $text = ""
    foreach ($line in ($raw -split "`n")) { if ($line -match '^text:\s?(.*)$') { $text = $matches[1].Trim() } }
    [void]$hyps.Add([pscustomobject]@{ id = $smp.id; hypothesis = $text })
    Write-Host ("    text: {0}" -f $text)
  }
} finally {
  Remove-Item Env:STICKS3_ASR_KEY -ErrorAction SilentlyContinue
}
$json = $hyps | ConvertTo-Json -Depth 4
[IO.File]::WriteAllText($Out, $json, (New-Object Text.UTF8Encoding($false)))
Write-Host "wrote $Out ($($hyps.Count) 条)"
