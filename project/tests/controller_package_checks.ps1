[CmdletBinding()]
param([string]$RuntimeExecutable)
$ErrorActionPreference='Stop'
$controllerPackageProject=Split-Path $PSScriptRoot -Parent
if(!$RuntimeExecutable){$RuntimeExecutable=Join-Path $controllerPackageProject '../generated/outputs/Release/KuboPlayer.exe'}
$controllerPackageFixture=[IO.Path]::GetFullPath((Join-Path $controllerPackageProject ('../generated/controller-package/'+[guid]::NewGuid().ToString('N'))))
New-Item -ItemType Directory -Path $controllerPackageFixture | Out-Null
Write-Output "CONTROLLER_PACKAGE_LOGS=$controllerPackageFixture"
function Write-ControllerFixture([string]$Path,[string]$Text){[IO.File]::WriteAllText($Path,$Text,[Text.UTF8Encoding]::new($false))}
$controllerPackageSource=Join-Path $controllerPackageFixture 'Source'
New-Item -ItemType Directory -Path (Join-Path $controllerPackageSource 'Assets') | Out-Null
$controllerPackageManifest=Join-Path $controllerPackageSource 'Game.kubo'
Write-ControllerFixture $controllerPackageManifest '{"name":"Controller Package","assetDirectory":"Assets","startupScene":"entry.scene"}'
$controllerPackageGuid='bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb'
Write-ControllerFixture (Join-Path $controllerPackageSource 'entry.scene') ('{"version":1,"entities":[{"id":1,"name":"Controller draft","parent":0,"active":true,"model":"","transform":{"position":[0,0,0],"rotation":[0,0,0],"scale":[1,1,1]},"animator":{"clip":"default","speed":1,"loop":true,"playOnAwake":false,"controllerGuid":"'+$controllerPackageGuid+'"}}]}')
$controllerPackageAsset=Join-Path $controllerPackageSource 'Assets/locomotion.animfsm'
$controllerPackageMetadata=$controllerPackageAsset+'.kubo-meta.json'
$controllerPackageGood='{"format":"kubo-animator-controller-1","parameters":[],"layers":[{"name":"Base","mode":"override","weight":1,"mask":[],"entry":"Idle","states":[{"name":"Idle","clip":"idle","speed":1,"loop":true}],"transitions":[]}]}'
Write-ControllerFixture $controllerPackageAsset $controllerPackageGood
Write-ControllerFixture $controllerPackageMetadata ('{"version":1,"guid":"'+$controllerPackageGuid+'"}')
$controllerPackageScript=Join-Path $controllerPackageProject 'tools/package_project.ps1'
$controllerPackageOutput=Join-Path $controllerPackageFixture 'Export'
& $controllerPackageScript -ProjectFile $controllerPackageManifest -OutputDirectory $controllerPackageOutput -RuntimeExecutable $RuntimeExecutable -Configuration Release
if(!(Test-Path -LiteralPath (Join-Path $controllerPackageOutput 'package.json'))){throw 'Controller package did not publish'}
foreach($relative in @('Assets/locomotion.animfsm','Assets/locomotion.animfsm.kubo-meta.json','Game.kubo','entry.scene')) {
 if((Get-FileHash -LiteralPath (Join-Path $controllerPackageSource $relative)).Hash -ne (Get-FileHash -LiteralPath (Join-Path $controllerPackageOutput $relative)).Hash){throw "Controller package digest mismatch: $relative"}
}
Write-ControllerFixture $controllerPackageAsset '{}'
$controllerPackageRejected=Join-Path $controllerPackageFixture 'Rejected'
$controllerPackageRefused=$false
try {& $controllerPackageScript -ProjectFile $controllerPackageManifest -OutputDirectory $controllerPackageRejected -RuntimeExecutable $RuntimeExecutable -Configuration Release}catch{$controllerPackageRefused=$true;Write-Output ('EXPECTED_CONTROLLER_REFUSAL='+$_.Exception.Message)}
if(!$controllerPackageRefused -or (Test-Path -LiteralPath (Join-Path $controllerPackageRejected 'package.json'))){throw 'Malformed Controller package was published'}
Write-ControllerFixture $controllerPackageAsset $controllerPackageGood
Write-ControllerFixture $controllerPackageMetadata '{"version":1,"guid":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"}'
$controllerPackageStaleRefused=$false
try {& $controllerPackageScript -ProjectFile $controllerPackageManifest -OutputDirectory (Join-Path $controllerPackageFixture 'Stale') -RuntimeExecutable $RuntimeExecutable -Configuration Release}catch{$controllerPackageStaleRefused=$true;Write-Output ('EXPECTED_STALE_GUID_REFUSAL='+$_.Exception.Message)}
if(!$controllerPackageStaleRefused){throw 'Stale Controller GUID package accepted'}
Write-Output 'PASS: actual package_project Controller request v2/source+copied candidate validation/bytes+metadata preservation/malformed+stale refusal; no shipped Player playback claim'
