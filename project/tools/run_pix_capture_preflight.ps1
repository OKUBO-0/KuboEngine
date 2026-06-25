param(
    [string]$OutputPath = "generated/outputs/pix_capture_preflight.txt"
)

$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot
$absoluteOutput = Join-Path $projectRoot $OutputPath
$outputDirectory = Split-Path -Parent $absoluteOutput
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null

$pixCandidates = @(
    "$env:ProgramFiles\Microsoft PIX\WinPixEventRuntime",
    "$env:ProgramFiles\Microsoft PIX",
    "${env:ProgramFiles(x86)}\Microsoft PIX"
) | Where-Object { $_ -and (Test-Path $_) }

$lines = [System.Collections.Generic.List[string]]::new()
$lines.Add("status=READY")
$lines.Add("projectRoot=$projectRoot")
$lines.Add("pixInstalled=$([bool]$pixCandidates.Count)")
foreach ($candidate in $pixCandidates) {
    $lines.Add("pixPath=$candidate")
}
$lines.Add("cr012=Capture runtime dynamic texture load; compare frame latency and queue idle around TextureManager::LoadTexture/UploadTextureResource.")
$lines.Add("cr026=Capture shadow pass timing; inspect caster silhouette, shimmering during camera motion, and submitted/cull telemetry.")
$lines.Add("stressCommand=.\tools\run_scene_transition_stress.ps1 -Cycles 3 -TimeoutSeconds 120")
$lines.Add("report=generated/outputs/scene_transition_stress.txt")

Set-Content -Path $absoluteOutput -Value $lines -Encoding UTF8
Get-Content $absoluteOutput
