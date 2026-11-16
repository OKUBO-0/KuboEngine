[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$packager=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\tools\package_project.ps1'))
$fixture=Join-Path ([IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\generated\cpu-checks'))) ('asset-reference-package-'+[guid]::NewGuid().ToString('N'))
$project=Join-Path $fixture 'Project';$runtime=Join-Path $fixture 'Engine'
function Require($ok,[string]$why) {if(!$ok) {throw $why}}
foreach($folder in @($project,(Join-Path $project 'Content'),(Join-Path $runtime 'Resources\Shaders'),(Join-Path $runtime 'Resources\textures\skybox'),(Join-Path $runtime 'Resources\textures\common'),(Join-Path $runtime 'ThirdPartyLicenses'))) {[IO.Directory]::CreateDirectory($folder) | Out-Null}
foreach($file in @('KuboPlayer.exe','dxcompiler.dll','dxil.dll','Resources\Shaders\test.hlsl','Resources\textures\skybox\test.dds','Resources\textures\common\white.png','ThirdPartyLicenses\Lua-5.4.9.txt')) {[IO.File]::WriteAllText((Join-Path $runtime $file),'fixture runtime')}
$projectFile=Join-Path $project 'Game.kubo';$sceneFile=Join-Path $project 'entry.scene'
[IO.File]::WriteAllText($projectFile,'{"name":"Fixture","startupScene":"entry.scene","assetDirectory":"Content"}')
$modelName=(-join ([char[]]@(0x30e2,0x30c7,0x30eb)))+'.obj'
$modelRelative='Content/'+$modelName;$modelFile=Join-Path $project $modelRelative
$modelGuid='1234567890abcdef1234567890abcdef';$scriptGuid='fedcba9876543210'
[IO.File]::WriteAllText($modelFile,"v 0 0 0`nv 1 0 0`nv 0 1 0`nf 1 2 3`n")
[IO.File]::WriteAllText(($modelFile+'.kubo-meta.json'),('{"version":1,"guid":"'+$modelGuid+'"}'))
$scriptFile=Join-Path $project 'Content/actual.lua'
[IO.File]::WriteAllText($scriptFile,'function on_update(entity, dt) end')
[IO.File]::WriteAllText(($scriptFile+'.kubo-meta.json'),('{"version":1,"guid":"'+$scriptGuid+'"}'))
function Write-Scene($entity) {[IO.File]::WriteAllText($sceneFile,(@{version=1;entities=@($entity)} | ConvertTo-Json -Depth 8))}
function Export-To([string]$name) {& $packager -ProjectFile $projectFile -RuntimeExecutable (Join-Path $runtime 'KuboPlayer.exe') -OutputDirectory (Join-Path $fixture $name)}
function Reject-Reference($entity,[string]$name,[string]$pattern) {
    Write-Scene $entity
    $output=Join-Path $fixture $name
    # A pre-existing empty destination must remain empty with its identity intact.
    [IO.Directory]::CreateDirectory($output) | Out-Null
    . (Join-Path $PSScriptRoot '..\tools\package_publication.ps1')
    $before=[Kubo.PackagePublicationNative]::Identity($output);$message=''
    try {Export-To $name} catch {$message=$_.Exception.Message}
    Require ($message -match $pattern) "Unexpected rejection for ${name}: $message"
    Require ([Kubo.PackagePublicationNative]::Identity($output) -ceq $before) "Output identity changed: $name"
    Require (@(Get-ChildItem -LiteralPath $output -Force).Count -eq 0) "Partial output leaked: $name"
    Require (!(Test-Path -LiteralPath (Get-KuboPackageJournalPath $output))) "Preflight started a publication: $name"
}
Write-Scene @{model=$modelRelative;script=@{asset='native-callback';enabled=$true}}
Export-To 'PathModelNativeCallback'
Require (Test-Path -LiteralPath (Join-Path $fixture ('PathModelNativeCallback/'+$modelRelative))) 'Unicode Model omitted'
# GUID identity wins over a stale path, including GUID-only Script authoring.
$valid=@{model='Content/stale.obj';modelGuid=$modelGuid;script=@{asset='Content/stale.lua';guid=$scriptGuid;enabled=$false}}
Write-Scene $valid;Export-To 'GuidReferences'
Write-Scene @{model='';script=@{guid=$scriptGuid}};Export-To 'GuidOnlyScript'
Write-Scene @{model=''};Export-To 'UnassignedModel'
Reject-Reference @{model='Content/missing.obj'} 'MissingModel' 'Model source missing'
Reject-Reference @{model='Content/stale.obj';modelGuid=('0'*32)} 'MissingModelGuid' 'Model GUID source missing'
Reject-Reference @{model=$modelRelative;modelGuid='BAD'} 'InvalidModelGuid' 'Model GUID must'
Reject-Reference @{model=$modelRelative;modelGuid=$scriptGuid} 'WrongModelType' 'incorrect asset type'
Reject-Reference @{model='';script=@{asset='Content/actual.lua';guid=$modelGuid}} 'WrongScriptType' 'incorrect asset type'
Reject-Reference @{model='';script=@{guid=('0'*16)}} 'MissingScriptGuid' 'Script GUID source missing'
Reject-Reference @{model='';script=@{guid=$scriptGuid;asset=123}} 'InvalidScriptHint' 'Script asset must'
foreach($path in @('../outside.obj','Content/sub/../file.obj','C:relative.obj','Content:stream.obj')) {
    Reject-Reference @{model=$path} ('InvalidPath-'+[guid]::NewGuid().ToString('N')) 'traversal|Invalid project path|relative paths'
}
[IO.File]::WriteAllText((Join-Path $project 'outside.obj'),'outside copied roots')
Reject-Reference @{model='outside.obj'} 'UncopiedModel' 'copied asset directory'
[IO.File]::WriteAllText($scriptFile,('x'*(1024*1024+1)))
Reject-Reference @{model='';script=@{guid=$scriptGuid}} 'OversizedScript' 'Script exceeds 1 MiB'
[IO.File]::WriteAllText($scriptFile,'function on_update(entity, dt) end')
# Root-array coercion must not turn a non-object Scene/identity into an object.
$identityFile=$modelFile+'.kubo-meta.json';$identityText=[IO.File]::ReadAllText($identityFile)
[IO.File]::WriteAllText($identityFile,('['+$identityText+']'))
Reject-Reference $valid 'ArrayIdentity' 'JSON root must be an object'
[IO.File]::WriteAllText($identityFile,$identityText)
[IO.File]::WriteAllText($sceneFile,'[{"version":1,"entities":[]}]')
$rejected=$false;try {Export-To 'ArrayScene'} catch {$rejected=$_.Exception.Message -match 'JSON root must be an object'}
Require ($rejected -and !(Test-Path -LiteralPath (Join-Path $fixture 'ArrayScene'))) 'Root array Scene published'
# Inject a copy-time candidate identity change, not a source change. This exercises
# the real packager's second validation pass, without a production test switch.
Write-Scene $valid
$candidateMutation=@{Done=$false}
function Copy-Item {
    [CmdletBinding()]
    param([string]$LiteralPath,[string]$Destination,[switch]$Force,[switch]$Recurse)
    Microsoft.PowerShell.Management\Copy-Item @PSBoundParameters
    if($LiteralPath -ceq $sceneFile) {
        $stage=Split-Path -Parent $Destination
        Require ((Split-Path -Parent $stage) -ceq $fixture) 'Candidate mutation escaped test fixture'
        Require ((Split-Path -Leaf $stage).StartsWith('.kubo-package-stage-')) 'Candidate mutation is not a stage'
        [IO.File]::WriteAllText((Join-Path $stage ($modelRelative+'.kubo-meta.json')),('{"version":1,"guid":"'+('0'*32)+'"}'))
        $candidateMutation.Done=$true
    }
}
try {
    $rejected=$false;$candidateError='';try {Export-To 'ChangedCandidate'} catch {$candidateError=$_.Exception.Message;$rejected=$candidateError -match 'Model GUID source missing'}
    Require ($candidateMutation.Done -and $rejected -and !(Test-Path -LiteralPath (Join-Path $fixture 'ChangedCandidate'))) "Candidate validation test failed (mutated=$($candidateMutation.Done)): $candidateError"
    Require ([IO.File]::ReadAllText($identityFile) -ceq $identityText) 'Candidate test altered source identity'
} finally {Remove-Item Function:\Copy-Item}
Write-Output "PASS: Unicode Model and GUID Script package references, native callback compatibility, copied-candidate revalidation and empty-output preservation; retained fixtures: $fixture"
