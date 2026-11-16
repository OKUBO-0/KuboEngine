[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$prefabPackageRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot ('../../generated/prefab-package/'+[guid]::NewGuid().ToString('N'))))
$prefabPackageSource=Join-Path $prefabPackageRoot 'Source'
$prefabPackageRuntime=Join-Path $prefabPackageRoot 'Engine'
$prefabPackager=Join-Path $PSScriptRoot '../tools/package_project.ps1'
function Write-PrefabPackage([string]$Path,[string]$Text){[IO.File]::WriteAllText($Path,$Text,[Text.UTF8Encoding]::new($false))}
function Require-PrefabPackage([bool]$Ok,[string]$Why){if(!$Ok){throw $Why}}
foreach($folder in @('Source/Assets','Engine/Resources/Shaders','Engine/Resources/textures/skybox','Engine/Resources/textures/common','Engine/ThirdPartyLicenses')){New-Item -ItemType Directory -Force -Path (Join-Path $prefabPackageRoot $folder) | Out-Null}
foreach($file in @('KuboPlayer.exe','dxcompiler.dll','dxil.dll','Resources/Shaders/test.hlsl','Resources/textures/skybox/test.dds','Resources/textures/common/white.png','ThirdPartyLicenses/Lua-5.4.9.txt')){Write-PrefabPackage (Join-Path $prefabPackageRuntime $file) 'fixture runtime only'}
$prefabPackageManifest=Join-Path $prefabPackageSource 'Game.kubo'
Write-PrefabPackage $prefabPackageManifest '{"name":"Prefab package","startupScene":"entry.scene","assetDirectory":"Assets"}'
# Legacy startup must not skip Prefab inspection.
Write-PrefabPackage (Join-Path $prefabPackageSource 'entry.scene') '{}'
$prefabPackageFile=Join-Path $prefabPackageSource 'Assets/enemy.prefab'
$prefabPackageGood='{"format":"kubo-prefab-1","root":1,"world":{"version":1,"entities":[{"id":1,"name":"Enemy","parent":0,"active":true,"model":"","transform":{"position":[0,0,0],"rotation":[0,0,0],"scale":[1,1,1]},"script":{"asset":"native.enemy","enabled":true}}]}}'
Write-PrefabPackage $prefabPackageFile $prefabPackageGood
Write-PrefabPackage ($prefabPackageFile+'.kubo-meta.json') '{"version":1,"guid":"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"}'
$prefabPackageOutput=Join-Path $prefabPackageRoot 'Export'
$prefabPackageRuntimeExe=Join-Path $prefabPackageRuntime 'KuboPlayer.exe'
& $prefabPackager -ProjectFile $prefabPackageManifest -OutputDirectory $prefabPackageOutput -RuntimeExecutable $prefabPackageRuntimeExe
Require-PrefabPackage (Test-Path -LiteralPath (Join-Path $prefabPackageOutput 'package.json')) 'Valid Prefab package not published'
Require-PrefabPackage ((Get-FileHash $prefabPackageFile).Hash -eq (Get-FileHash (Join-Path $prefabPackageOutput 'Assets/enemy.prefab')).Hash) 'Copied Prefab bytes changed'
$prefabPackagePublishedHash=(Get-FileHash (Join-Path $prefabPackageOutput 'package.json')).Hash
foreach($bad in @('{}','{"root":1,"\u0072oot":2}',$prefabPackageGood.Replace('"asset":"native.enemy"','"asset":"Assets/missing.lua","guid":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"'))){
    Write-PrefabPackage $prefabPackageFile $bad
    $rejected=Join-Path $prefabPackageRoot ([guid]::NewGuid().ToString('N'));$refused=$false
    try{& $prefabPackager -ProjectFile $prefabPackageManifest -OutputDirectory $rejected -RuntimeExecutable $prefabPackageRuntimeExe}catch{$refused=$true;Write-Output "EXPECTED_PREFAB_REFUSAL=$($_.Exception.Message)"}
    Require-PrefabPackage ($refused -and !(Test-Path -LiteralPath $rejected)) 'Invalid source reached publication'
}
Write-PrefabPackage $prefabPackageFile $prefabPackageGood
# Test-only command shim corrupts only the copied candidate, after its source passed.
$prefabCandidateSignals=[Collections.Generic.List[string]]::new()
function Copy-Item {
    param([string]$LiteralPath,[string]$Destination,[switch]$Recurse,[switch]$Force)
    Microsoft.PowerShell.Management\Copy-Item @PSBoundParameters
    if($LiteralPath -eq $prefabPackageFile){
        Write-PrefabPackage (Join-Path $Destination 'enemy.prefab') '{}'
        $prefabCandidateSignals.Add($Destination)
    }
}
$prefabCandidateOutput=Join-Path $prefabPackageRoot 'CorruptCandidate';New-Item -ItemType Directory -Path $prefabCandidateOutput | Out-Null
$prefabCandidateRefused=$false
try{& $prefabPackager -ProjectFile $prefabPackageManifest -OutputDirectory $prefabCandidateOutput -RuntimeExecutable $prefabPackageRuntimeExe}catch{$prefabCandidateRefused=$true;Write-Output "EXPECTED_CANDIDATE_REFUSAL=$($_.Exception.Message)"}finally{Remove-Item Function:Copy-Item}
Require-PrefabPackage ($prefabCandidateSignals.Count -eq 1 -and $prefabCandidateRefused -and @(Get-ChildItem -LiteralPath $prefabCandidateOutput -Force).Count -eq 0) 'Corrupt candidate reached public empty directory'
Require-PrefabPackage ([IO.File]::ReadAllText($prefabPackageFile) -ceq $prefabPackageGood) 'Candidate failure changed source'
Require-PrefabPackage ((Get-FileHash (Join-Path $prefabPackageOutput 'package.json')).Hash -eq $prefabPackagePublishedHash) 'Prior published output changed'
Write-Output "PREFAB_PACKAGE_FIXTURE=$prefabPackageRoot"
Write-Output 'PASS: actual package_project source and copied-candidate Prefab validation, legacy startup/native key compatibility, source and existing public output preserved; fixture runtime, not Player execution'
