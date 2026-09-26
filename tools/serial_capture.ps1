param([string]$Com="COM5",[string]$Out="serial.log",[int]$Seconds=40)
$ErrorActionPreference="SilentlyContinue"
$p = New-Object System.IO.Ports.SerialPort $Com,115200,"None",8,"One"
$p.ReadTimeout=1000
$p.Open()
$sw=[Diagnostics.Stopwatch]::StartNew()
while($sw.Elapsed.TotalSeconds -lt $Seconds){
  try{ $l=$p.ReadLine(); Add-Content -Path $Out -Value $l }catch{}
}
$p.Close()
