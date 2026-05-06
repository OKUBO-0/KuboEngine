param(
    [int]$Seconds = 0
)

$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot
$exePath = Join-Path $projectRoot "..\generated\outputs\Debug\KuboEngine.exe"
$exePath = [System.IO.Path]::GetFullPath($exePath)

if (-not (Test-Path $exePath)) {
    throw "Debug executable was not found: $exePath"
}

$process = Start-Process -FilePath $exePath -PassThru

if ($Seconds -gt 0) {
    Start-Sleep -Seconds $Seconds
    if (-not $process.HasExited) {
        Stop-Process -Id $process.Id
    }
}
