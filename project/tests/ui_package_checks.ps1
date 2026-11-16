[CmdletBinding()]
param([ValidateSet('Debug','Release')][string]$Configuration='Release')
$ErrorActionPreference='Stop'
$fixture=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot ('../../generated/ui-package/'+[guid]::NewGuid().ToString('N'))))
$source=Join-Path $fixture 'Source';$runtime=Join-Path $fixture 'Engine';$packager=Join-Path $PSScriptRoot '../tools/package_project.ps1'
function Need($ok,[string]$message){if(!$ok){throw $message}}
function Write-UI([string]$path,[string]$text){[IO.File]::WriteAllText($path,$text,[Text.UTF8Encoding]::new($false))}
foreach($folder in @('Source/Content','Source/Uncopied','Engine/Resources/Shaders','Engine/Resources/textures/skybox','Engine/Resources/textures/common','Engine/ThirdPartyLicenses')){New-Item -ItemType Directory -Force -Path (Join-Path $fixture $folder)|Out-Null}
foreach($file in @('KuboPlayer.exe','dxcompiler.dll','dxil.dll','Resources/Shaders/test.hlsl','Resources/textures/skybox/test.dds','Resources/textures/common/white.png','ThirdPartyLicenses/Lua-5.4.9.txt')){Write-UI (Join-Path $runtime $file) 'fixture runtime, not executable'}
$manifest=Join-Path $source 'Game.kubo';$scene=Join-Path $source 'entry.scene';$imageName=([string][char]0x767d)+'.png';$image=Join-Path $source ('Content/'+$imageName);$imageMeta=$image+'.kubo-meta.json';$prefab=Join-Path $source 'Content/ui.prefab'
$guid='aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa';$scriptGuid='cccccccccccccccccccccccccccccccc'
Write-UI $manifest '{"name":"UI package","assetDirectory":"Content","startupScene":"entry.scene","engineVersion":"0.1.0"}'
Write-UI $image 'identity fixture only, not image decoding';Write-UI $imageMeta ('{"version":1,"guid":"'+$guid+'"}')
Write-UI (Join-Path $source 'Content/script.lua') 'return {}';Write-UI (Join-Path $source 'Content/script.lua.kubo-meta.json') ('{"version":1,"guid":"'+$scriptGuid+'"}');Write-UI (Join-Path $source 'Uncopied/outside.png') 'uncopied image'
$entity=@{id=1;parent=0;name='UI';active=$false;model='';transform=@{position=@(0,0,0);rotation=@(0,0,0);scale=@(1,1,1)};canvas=@{};sprite=@{enabled=$false;asset='Content/stale.png';guid=$guid}}
$world=@{version=1;entities=@($entity)};$goodScene=$world|ConvertTo-Json -Depth 12;$goodPrefab=@{format='kubo-prefab-1';root=1;world=$world}|ConvertTo-Json -Depth 14
Write-UI $scene $goodScene;Write-UI $prefab $goodPrefab;Write-UI ($prefab+'.kubo-meta.json') '{"version":1,"guid":"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"}'
$imageBytes=[IO.File]::ReadAllText($image);$identity=[IO.File]::ReadAllText($imageMeta)
function Export-UI([string]$name){& $packager -ProjectFile $manifest -Configuration $Configuration -OutputDirectory (Join-Path $fixture $name) -RuntimeExecutable (Join-Path $runtime 'KuboPlayer.exe')}
Export-UI 'Good'
$published=Join-Path $fixture 'Good';$publishedHash=(Get-FileHash (Join-Path $published 'package.json')).Hash
foreach($relative in @('entry.scene','Content/ui.prefab',('Content/'+$imageName),('Content/'+$imageName+'.kubo-meta.json'))){Need ((Get-FileHash (Join-Path $source $relative)).Hash -eq (Get-FileHash (Join-Path $published $relative)).Hash) ('Export UI copy changed: '+$relative)}
. (Join-Path $PSScriptRoot '../tools/package_publication.ps1')
function Reject-UI([string]$name){
    $destination=Join-Path $fixture $name;New-Item -ItemType Directory -Path $destination|Out-Null;$before=[Kubo.PackagePublicationNative]::Identity($destination);$refused=$false
    try{Export-UI $name}catch{$refused=$true;Write-Output "EXPECTED_UI_REFUSAL=$($_.Exception.Message)"}
    Need ($refused -and [Kubo.PackagePublicationNative]::Identity($destination) -ceq $before -and @(Get-ChildItem -LiteralPath $destination -Force).Count -eq 0) ('UI preflight published/changed empty destination: '+$name)
    Need (!(Test-Path -LiteralPath (Get-KuboPackageJournalPath $destination))) 'UI preflight started publication'
}
foreach($case in @('missing-guid','wrong-type','invalid-sprite','invalid-canvas','uncopied')){
    $changed=$goodScene|ConvertFrom-Json
    if($case -eq 'missing-guid'){$changed.entities[0].sprite.guid='ffffffffffffffff'}
    if($case -eq 'wrong-type'){$changed.entities[0].sprite.guid=$scriptGuid}
    if($case -eq 'invalid-sprite'){$changed.entities[0].sprite|Add-Member -NotePropertyName size -NotePropertyValue @(-1,100)}
    if($case -eq 'invalid-canvas'){$changed.entities[0].canvas|Add-Member -NotePropertyName referenceSize -NotePropertyValue @(0,720)}
    if($case -eq 'uncopied'){$changed.entities[0].sprite.PSObject.Properties.Remove('guid');$changed.entities[0].sprite.asset='Uncopied/outside.png'}
    Write-UI $scene ($changed|ConvertTo-Json -Depth 14);Reject-UI ('Scene-'+$case);Write-UI $scene $goodScene
}
# Legacy startup still requires every exported Prefab's assigned images.
Write-UI $scene '{}';$changed=$goodPrefab|ConvertFrom-Json;$changed.world.entities[0].sprite.guid='ffffffffffffffff';Write-UI $prefab ($changed|ConvertTo-Json -Depth 16);Reject-UI 'Prefab-missing-guid';Write-UI $prefab $goodPrefab;Export-UI 'LegacyStartup';Write-UI $scene $goodScene
# Test-only copy shim changes only the staged identity. Real second-pass native
# validation must refuse while preserving the existing public destination.
$signals=[Collections.Generic.List[string]]::new()
function Copy-Item {
    param([string]$LiteralPath,[string]$Destination,[switch]$Recurse,[switch]$Force)
    Microsoft.PowerShell.Management\Copy-Item @PSBoundParameters
    if($LiteralPath -ceq $imageMeta){$candidate=Join-Path $Destination ([IO.Path]::GetFileName($imageMeta));Write-UI $candidate '{"version":1,"guid":"eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"}';$signals.Add($candidate)}
}
$candidateOutput=Join-Path $fixture 'CandidateIdentity';New-Item -ItemType Directory -Path $candidateOutput|Out-Null;$before=[Kubo.PackagePublicationNative]::Identity($candidateOutput);$refused=$false
try{Export-UI 'CandidateIdentity'}catch{$refused=$true;Write-Output "EXPECTED_UI_CANDIDATE_REFUSAL=$($_.Exception.Message)"}finally{Remove-Item Function:Copy-Item}
Need ($signals.Count -eq 1 -and $refused -and [Kubo.PackagePublicationNative]::Identity($candidateOutput) -ceq $before -and @(Get-ChildItem -LiteralPath $candidateOutput -Force).Count -eq 0) 'Changed copied image identity reached publication'
Need ([IO.File]::ReadAllText($scene) -ceq $goodScene -and [IO.File]::ReadAllText($prefab) -ceq $goodPrefab -and [IO.File]::ReadAllText($image) -ceq $imageBytes -and [IO.File]::ReadAllText($imageMeta) -ceq $identity) 'Export UI validation changed authored sources'
Need ((Get-FileHash (Join-Path $published 'package.json')).Hash -eq $publishedHash) 'Prior UI package was changed'
Write-Output "UI_PACKAGE_FIXTURE=$fixture"
Write-Output 'PASS: actual packager/native format4 Scene+Prefab Sprite dependencies, disabled assigned GUID+Unicode image copies, legacy startup, wrong-type/missing/invalid/uncopied preflight and copied-candidate identity refusal, source/publication isolation (fixture runtime, not Player execution)'
