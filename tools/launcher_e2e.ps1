$ErrorActionPreference = "Continue"
$out = "C:\Temp"
Set-Location "C:\Users\home\esp32-s3-m5\host\src"
Get-Process launcher,state_host,tray -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep 2
$logs = "$env:LOCALAPPDATA\sticks3\logs"
Remove-Item "$out\launcher_e2e.txt" -Force -ErrorAction SilentlyContinue
$env:STICKS3_HOST_SECONDS = "45"

$p = Start-Process -FilePath ".\launcher.exe" -RedirectStandardOutput "$out\launcher_e2e.txt" -PassThru
Start-Sleep 18
Write-Output ("alive=" + (-not $p.HasExited))
$kids = Get-Process state_host,tray -ErrorAction SilentlyContinue
Write-Output ("children=" + (($kids | ForEach-Object { $_.Name }) -join ","))
Stop-Process -Id $p.Id -Force
Start-Sleep 3
$left = Get-Process launcher,state_host,tray -ErrorAction SilentlyContinue
if ($left) { Write-Output ("LEFTOVER=" + (($left | ForEach-Object { $_.Name }) -join ",")) } else { Write-Output "cleanup ok" }

Write-Output "--- launcher ---"
Get-Content "$out\launcher_e2e.txt" -ErrorAction SilentlyContinue
$newest = Get-ChildItem $logs -Filter "state_host-*.log" -ErrorAction SilentlyContinue |
  Sort-Object LastWriteTime -Descending | Select-Object -First 1
Write-Output ("--- state_host log: " + $newest.Name + " ---")
Get-Content $newest.FullName -ErrorAction SilentlyContinue
