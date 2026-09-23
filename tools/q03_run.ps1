param(
    [Parameter(Mandatory = $true)][ValidateSet("5s", "15s", "30s")][string]$Group,
    [int]$Index = 1,
    [string]$Port = "COM5",
    [double]$BeginTimeout = 120,
    [string]$Out = "C:\Temp\ESP32-S3M5\captures\q03\records.jsonl"
)
$ErrorActionPreference = "Stop"
. "$PSScriptRoot\bridge_env.ps1"
$py = "C:\Users\shiwenc\AppData\Local\Programs\Python\Python312\python.exe"
& $py "$PSScriptRoot\q03_run.py" --group $Group --index $Index --port $Port --begin-timeout $BeginTimeout --out $Out
exit $LASTEXITCODE
