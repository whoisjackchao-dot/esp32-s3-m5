param(
    [Parameter(Mandatory = $true)][ValidateSet("5s", "15s", "30s")][string]$Group,
    [int]$Index = 1,
    [string]$Port = "COM5",
    [double]$BeginTimeout = 120,
    [string]$Out = (Join-Path (Split-Path -Parent $PSScriptRoot) "captures\q03\records.jsonl")
)
$ErrorActionPreference = "Stop"
. "$PSScriptRoot\bridge_env.ps1"
$py = (Get-Command python).Source
& $py "$PSScriptRoot\q03_run.py" --group $Group --index $Index --port $Port --begin-timeout $BeginTimeout --out $Out
exit $LASTEXITCODE
