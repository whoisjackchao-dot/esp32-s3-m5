$ErrorActionPreference = "Continue"
$src = "C:\Users\home\esp32-s3-m5\host\src"
$out = "C:\Temp"
Set-Location $src
Get-Process launcher,state_host,tray -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep 2
Remove-Item "$out\launcher_1.txt" -Force -ErrorAction SilentlyContinue
Remove-Item "$env:LOCALAPPDATA\sticks3\host.json" -Force -ErrorAction SilentlyContinue
$logs = "$env:LOCALAPPDATA\sticks3\logs"
New-Item -ItemType Directory -Path $logs -Force | Out-Null
Remove-Item "$logs\state_host.1.log" -Force -ErrorAction SilentlyContinue
$big = "$logs\state_host-20991231-235959.log"
Set-Content -Path $big -Value ("x" * 1258291) -NoNewline
Write-Output ("pre_log_size=" + (Get-Item $big).Length)

$env:STICKS3_HOST_SECONDS = "90"
$p1 = Start-Process -FilePath ".\launcher.exe" -RedirectStandardOutput "$out\launcher_1.txt" -PassThru
Start-Sleep 6
Write-Output ("P1 alive=" + (-not $p1.HasExited))
$k1 = Get-Process state_host -ErrorAction SilentlyContinue
Write-Output ("before_kill=" + (($k1 | ForEach-Object { $_.Id }) -join ","))

$k1 | Stop-Process -Force
Start-Sleep 7
$k2 = Get-Process state_host -ErrorAction SilentlyContinue
Write-Output ("after_kill=" + (($k2 | ForEach-Object { $_.Id }) -join ","))
Write-Output ("restarted=" + ($null -ne $k2))

Write-Output ("rotated=" + (Test-Path "$logs\state_host.1.log"))

Stop-Process -Id $p1.Id -Force
Start-Sleep 3
$left = Get-Process launcher,state_host,tray -ErrorAction SilentlyContinue
if ($left) { Write-Output ("LEFTOVER=" + (($left | ForEach-Object { $_.Name }) -join ",")) } else { Write-Output "cleanup ok" }
Write-Output "--- launcher_1.txt ---"
Get-Content "$out\launcher_1.txt" -ErrorAction SilentlyContinue
Write-Output "--- logs ---"
Get-ChildItem $logs | Sort-Object Name | Select-Object Name,Length | Format-Table -AutoSize | Out-String
