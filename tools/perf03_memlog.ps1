param([int]$Seconds=7200,[string]$Out="mem.csv")
"t,ws_kb" | Set-Content -Path $Out
$sw=[Diagnostics.Stopwatch]::StartNew()
while($sw.Elapsed.TotalSeconds -lt $Seconds){
  $p = Get-Process state_host -ErrorAction SilentlyContinue | Select-Object -First 1
  if($p){ Add-Content -Path $Out -Value ("{0},{1}" -f [int]$sw.Elapsed.TotalSeconds, [int]($p.WorkingSet64/1KB)) }
  Start-Sleep -Seconds 30
}
