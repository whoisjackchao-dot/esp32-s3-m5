$ErrorActionPreference = "Continue"
$out = "C:\Temp"
Set-Location "C:\Users\home\esp32-s3-m5\host\src"
Get-Process launcher,state_host,tray -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep 2
Remove-Item "$out\launcher_backoff.txt" -Force -ErrorAction SilentlyContinue
Remove-Item Env:STICKS3_HOST_SECONDS -ErrorAction SilentlyContinue

$p = Start-Process -FilePath ".\launcher.exe" -RedirectStandardOutput "$out\launcher_backoff.txt" -PassThru
Start-Sleep 5

for ($i = 0; $i -lt 7; $i++) {
    $deadline = (Get-Date).AddSeconds(40)
    $k = $null
    while ((Get-Date) -lt $deadline) {
        $k = Get-Process state_host -ErrorAction SilentlyContinue
        if ($k) { break }
        Start-Sleep 1
    }
    if ($k) { $k | Stop-Process -Force }
    Start-Sleep 1
}

Stop-Process -Id $p.Id -Force
Start-Sleep 3
$left = Get-Process launcher,state_host,tray -ErrorAction SilentlyContinue
if ($left) { Write-Output ("LEFTOVER=" + (($left | ForEach-Object { $_.Name }) -join ",")) } else { Write-Output "cleanup ok" }
Write-Output "--- launcher log (restart lines) ---"
Get-Content "$out\launcher_backoff.txt" -ErrorAction SilentlyContinue |
  Select-String -Pattern "restart #" | Select-Object -First 10
