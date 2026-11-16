[CmdletBinding()]
param(
    [string]$ProjectFile = (Join-Path $PSScriptRoot '..\KuboEngine.kubo'),
    [string]$OutputDirectory,
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [string]$MSBuild,
    [ValidatePattern('^\d+\.\d+\.\d+$')][string]$VCToolsVersion='14.44.35207'
)

$ErrorActionPreference='Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$buildOptions=@{Target='All';Configuration=$Configuration;VCToolsVersion=$VCToolsVersion}
if($MSBuild){$buildOptions.MSBuild=$MSBuild}
& (Join-Path $PSScriptRoot 'build_engine.ps1') @buildOptions

$runtimePath = Join-Path (Split-Path -Parent $projectRoot) "generated\outputs\$Configuration\KuboPlayer.exe"
& (Join-Path $PSScriptRoot 'package_project.ps1') -ProjectFile $ProjectFile -OutputDirectory $OutputDirectory -Configuration $Configuration -RuntimeExecutable $runtimePath
