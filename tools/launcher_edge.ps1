$ErrorActionPreference = "Continue"
$out = "C:\Temp"
Set-Location "C:\Users\home\esp32-s3-m5\host\src"
Get-Process launcher,state_host,tray -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep 2
$logs = "$env:LOCALAPPDATA\sticks3\logs"
Remove-Item "$out\launcher_edge.txt" -Force -ErrorAction SilentlyContinue

$listener = New-Object System.Net.Sockets.TcpListener([System.Net.IPAddress]::Loopback, 45910)
$listener.Start()
Write-Output ("port45910_occupied=" + $listener.Server.IsBound)

$env:STICKS3_HOST_SECONDS = "40"
$p = Start-Process -FilePath ".\launcher.exe" -RedirectStandardOutput "$out\launcher_edge.txt" -PassThru
Start-Sleep 12
$sh = Get-Process state_host -ErrorAction SilentlyContinue
Write-Output ("state_host_alive_with_port_busy=" + ($null -ne $sh))

$t1 = Get-Process tray -ErrorAction SilentlyContinue
Write-Output ("tray_before=" + (($t1 | ForEach-Object { $_.Id }) -join ","))
$t1 | Stop-Process -Force
Start-Sleep 7
$t2 = Get-Process tray -ErrorAction SilentlyContinue
Write-Output ("tray_after=" + (($t2 | ForEach-Object { $_.Id }) -join ","))
Write-Output ("tray_restarted=" + ($null -ne $t2))

Stop-Process -Id $p.Id -Force
Start-Sleep 3
$listener.Stop()
$left = Get-Process launcher,state_host,tray -ErrorAction SilentlyContinue
if ($left) { Write-Output ("LEFTOVER=" + (($left | ForEach-Object { $_.Name }) -join ",")) } else { Write-Output "cleanup ok" }

Write-Output "--- launcher ---"
Get-Content "$out\launcher_edge.txt" -ErrorAction SilentlyContinue
$newest = Get-ChildItem $logs -Filter "state_host-*.log" -ErrorAction SilentlyContinue |
  Sort-Object LastWriteTime -Descending | Select-Object -First 1
Write-Output ("--- state_host log: " + $newest.Name + " ---")
Get-Content $newest.FullName -ErrorAction SilentlyContinue
