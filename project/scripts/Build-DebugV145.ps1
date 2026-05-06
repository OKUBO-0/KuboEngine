param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    [string]$Platform = "x64"
)

$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot
$solutionPath = Join-Path $projectRoot "KuboEngine.sln"
$msbuildPath = "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe"

if (-not (Test-Path $msbuildPath)) {
    throw "v145 MSBuild was not found: $msbuildPath"
}

& $msbuildPath $solutionPath "/p:Configuration=$Configuration" "/p:Platform=$Platform"

if ($LASTEXITCODE -ne 0) {
    throw "MSBuild failed with exit code $LASTEXITCODE"
}
