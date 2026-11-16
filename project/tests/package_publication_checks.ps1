[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$publicationSource=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\tools\package_publication.ps1'))
$packager=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\tools\package_project.ps1'))
. $publicationSource
$fixture=Join-Path ([IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\generated\cpu-checks'))) ('package-publication-'+[guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($fixture) | Out-Null
Write-Output "Retained publication fixtures: $fixture"
function Require($ok,[string]$why) {if(!$ok) {throw $why}}
function Fill-Candidate($context) {
    [IO.File]::WriteAllText((Join-Path $context.Stage 'package.json'),'{"format":"kubo-runtime-package-1"}')
    [IO.File]::WriteAllText((Join-Path $context.Stage 'KuboPlayer.exe'),'complete candidate')
}
foreach($existing in @($false,$true)) {
    $output=Join-Path $fixture ('Successful-'+$existing)
    if($existing) {[IO.Directory]::CreateDirectory($output) | Out-Null}
    $context=Start-KuboPackagePublication $output
    try {Fill-Candidate $context;Complete-KuboPackagePublication $context} finally {Close-KuboPackagePublication $context}
    Require ((Test-Path -LiteralPath (Join-Path $output 'package.json')) -and !(Test-Path -LiteralPath (Get-KuboPackageJournalPath $output))) 'Successful publication did not commit/discard journal'
}
# Ordinary assembly failure must leave new/empty public output untouched.
$project=Join-Path $fixture 'Project';$runtime=Join-Path $fixture 'Engine'
foreach($folder in @($project,(Join-Path $project 'Assets'),(Join-Path $runtime 'Resources\Shaders'),(Join-Path $runtime 'Resources\textures\skybox'),(Join-Path $runtime 'Resources\textures\common'),(Join-Path $runtime 'ThirdPartyLicenses'))) {[IO.Directory]::CreateDirectory($folder) | Out-Null}
[IO.File]::WriteAllText((Join-Path $project 'Game.kubo'),'{"name":"Fixture","startupScene":"entry.scene","assetDirectory":"Assets"}')
[IO.File]::WriteAllText((Join-Path $project 'entry.scene'),'{"version":1,"entities":[]}')
foreach($file in @('KuboPlayer.exe','dxcompiler.dll','dxil.dll','Resources\Shaders\test.hlsl','Resources\textures\skybox\test.dds','Resources\textures\common\white.png','ThirdPartyLicenses\Lua-5.4.9.txt')) {[IO.File]::WriteAllText((Join-Path $runtime $file),'fixture runtime')}
$locked=Join-Path $project 'Assets\locked.txt';[IO.File]::WriteAllText($locked,'preserve locked source')
$held=[IO.File]::Open($locked,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
try {
    foreach($existing in @($false,$true)) {
        $output=Join-Path $fixture ('CopyFailure-'+$existing)
        if($existing) {[IO.Directory]::CreateDirectory($output) | Out-Null}
        $before=[Kubo.PackagePublicationNative]::Identity($output);$failed=$false
        try {& $packager -ProjectFile (Join-Path $project 'Game.kubo') -RuntimeExecutable (Join-Path $runtime 'KuboPlayer.exe') -OutputDirectory $output} catch {$failed=$true}
        Require ($failed -and [Kubo.PackagePublicationNative]::Identity($output) -ceq $before) 'Copy failure altered public output identity'
        if($existing) {Require (@(Get-ChildItem -LiteralPath $output -Force).Count -eq 0) 'Copy failure leaked partial public files'} else {Require (!(Test-Path -LiteralPath $output)) 'Copy failure published new output'}
        Require (Test-Path -LiteralPath (Get-KuboPackageJournalPath $output)) 'Interrupted candidate has no recovery record'
        $noticed=$false;try {Recover-KuboPackagePublication $output | Out-Null} catch {$noticed=$_.Exception.Message.Contains('Interrupted package retained')}
        Require $noticed 'Assembly recovery did not archive/retain the failed candidate'
    }
} finally {$held.Dispose()}
# Metadata must be part of the candidate, with a matching runtime digest.
$nativeRecord=@{format='kubo-native-game-build-1';configuration='Release';runtimeSha256=(Get-FileHash -LiteralPath (Join-Path $runtime 'KuboPlayer.exe') -Algorithm SHA256).Hash.ToLowerInvariant();nativeProjectName='Fixture.vcxproj'}
$nativeOutput=Join-Path $fixture 'NativeRecord'
& $packager -ProjectFile (Join-Path $project 'Game.kubo') -RuntimeExecutable (Join-Path $runtime 'KuboPlayer.exe') -OutputDirectory $nativeOutput -NativeBuildRecord $nativeRecord
Require ([IO.File]::ReadAllText((Join-Path $nativeOutput 'native-build.json')).Contains($nativeRecord.runtimeSha256)) 'Native metadata was not published with the runtime'
$nativeRecord.runtimeSha256='0'*64;$invalidOutput=Join-Path $fixture 'InvalidNativeRecord';$blocked=$false
try {& $packager -ProjectFile (Join-Path $project 'Game.kubo') -RuntimeExecutable (Join-Path $runtime 'KuboPlayer.exe') -OutputDirectory $invalidOutput -NativeBuildRecord $nativeRecord} catch {$blocked=$true}
Require ($blocked -and !(Test-Path -LiteralPath $invalidOutput)) 'Invalid native digest reached public output'
# Startup files cannot overwrite package-owned files.
$manifestFile=Join-Path $project 'Game.kubo';$manifestBytes=[IO.File]::ReadAllBytes($manifestFile)
try {
    [IO.File]::WriteAllText((Join-Path $project 'package.json'),'{"version":1,"entities":[]}')
    [IO.File]::WriteAllText($manifestFile,'{"name":"Fixture","startupScene":"package.json","assetDirectory":"Assets"}')
    $collisionOutput=Join-Path $fixture 'ReservedCollision';$blocked=$false
    try {& $packager -ProjectFile $manifestFile -RuntimeExecutable (Join-Path $runtime 'KuboPlayer.exe') -OutputDirectory $collisionOutput} catch {$blocked=$true}
    Require ($blocked -and !(Test-Path -LiteralPath $collisionOutput)) 'Reserved startup collision was published'
} finally {[IO.File]::WriteAllBytes($manifestFile,$manifestBytes)}
# A competing output created after retiring the old empty folder is never replaced.
$output=Join-Path $fixture 'Competitor';[IO.Directory]::CreateDirectory($output) | Out-Null
$context=Start-KuboPackagePublication $output;Fill-Candidate $context;$failed=$false
try {
    Complete-KuboPackagePublication $context {param($c) [IO.Directory]::CreateDirectory($c.Output) | Out-Null;[IO.File]::WriteAllText((Join-Path $c.Output 'user.txt'),'competitor data')}
} catch {$failed=$true} finally {Close-KuboPackagePublication $context}
Require ($failed -and [IO.File]::ReadAllText((Join-Path $output 'user.txt')) -eq 'competitor data' -and (Test-Path -LiteralPath $context.Backup) -and (Test-Path -LiteralPath $context.Stage)) 'Competing output or original backup was lost'
$failed=$false;try {Recover-KuboPackagePublication $output | Out-Null} catch {$failed=$true};Require $failed 'Conflicting output was adopted'
# The same journal cannot be recovered by a second active writer.
$context=Start-KuboPackagePublication (Join-Path $fixture 'Active');$blocked=$false
try {try {Recover-KuboPackagePublication $context.Output | Out-Null} catch {$blocked=$true};Require $blocked 'Live journal was not held exclusively'} finally {Close-KuboPackagePublication $context}
# Untrusted recovery records cannot redirect siblings or coerce array/string fields.
foreach($case in @('traversal','array','identity','utf8','deep')) {
    $context=Start-KuboPackagePublication (Join-Path $fixture ('Malformed-'+$case));Fill-Candidate $context
    $record=$context.Record | ConvertTo-Json | ConvertFrom-Json
    Close-KuboPackagePublication $context
    $journalPath=Get-KuboPackageJournalPath $context.Output
    switch($case) {
        'traversal' {$record.stage='../outside'}
        'array' {$record.root=@($record.root)}
        'identity' {$record.stageId='00000000:0000000000000000'}
    }
    if($case -eq 'utf8') {[IO.File]::WriteAllBytes($journalPath,[byte[]]@(0xff,0xfe,0xff))}
    elseif($case -eq 'deep') {[IO.File]::WriteAllText($journalPath,(('['*32)+'0'+(']'*32)))}
    else {[IO.File]::WriteAllText($journalPath,($record | ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))}
    $blocked=$false;try {Recover-KuboPackagePublication $context.Output | Out-Null} catch {$blocked=$true}
    Require ($blocked -and (Test-Path -LiteralPath $journalPath) -and [IO.File]::ReadAllText((Join-Path $context.Stage 'KuboPlayer.exe')) -eq 'complete candidate') 'Invalid recovery record changed candidate or was silently adopted'
}
# Real killed children at each commit boundary; callbacks are verification seams.
$childScript=Join-Path $fixture 'child.ps1'
[IO.File]::WriteAllText($childScript,@'
param($Helper,$Output,$Marker,$Phase)
$ErrorActionPreference='Stop'
. $Helper
$c=Start-KuboPackagePublication $Output
[IO.File]::WriteAllText((Join-Path $c.Stage 'package.json'),'{"format":"kubo-runtime-package-1"}')
[IO.File]::WriteAllText((Join-Path $c.Stage 'KuboPlayer.exe'),'complete candidate')
$pause={param($context) [IO.File]::WriteAllText($Marker,'ready');while($true) {Start-Sleep -Milliseconds 100}}
if($Phase -eq 'retired') {Complete-KuboPackagePublication $c -BeforeCommit $pause}
else {Complete-KuboPackagePublication $c -AfterCommit $pause}
'@)
foreach($phase in @('retired','published')) {
    $output=Join-Path $fixture ('Crash-'+$phase);[IO.Directory]::CreateDirectory($output) | Out-Null
    $original=[Kubo.PackagePublicationNative]::Identity($output);$marker=Join-Path $fixture ($phase+'.ready');$child=$null
    try {
        $child=Start-Process -FilePath (Join-Path ([Environment]::SystemDirectory) 'WindowsPowerShell\v1.0\powershell.exe') -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-File',('"'+$childScript+'"'),'-Helper',('"'+$publicationSource+'"'),'-Output',('"'+$output+'"'),'-Marker',('"'+$marker+'"'),'-Phase',$phase) -WindowStyle Hidden -PassThru
        $childHandle=$child.Handle
        for($attempt=0;$attempt -lt 200 -and !(Test-Path -LiteralPath $marker);$attempt++) {if($child.HasExited) {throw 'Boundary child exited before readiness'};Start-Sleep -Milliseconds 100}
        Require (Test-Path -LiteralPath $marker) 'Boundary child did not reach commit seam'
        Stop-Process -Id $child.Id;Require ($child.WaitForExit(10000)) 'Owned boundary child remains live'
        if($phase -eq 'retired') {
            Require (!(Test-Path -LiteralPath $output)) 'Retired boundary already published output'
            $notice=$false;try {Recover-KuboPackagePublication $output | Out-Null} catch {$notice=$_.Exception.Message.Contains('Interrupted package retained')}
            Require ($notice -and [Kubo.PackagePublicationNative]::Identity($output) -ceq $original -and @(Get-ChildItem -LiteralPath $output -Force).Count -eq 0) 'Crash recovery did not restore exact original empty folder'
            $retry=Start-KuboPackagePublication $output;try {Fill-Candidate $retry;Complete-KuboPackagePublication $retry} finally {Close-KuboPackagePublication $retry}
        } else {
            [IO.File]::WriteAllText((Join-Path $output 'user-after-publication.txt'),'preserve user addition')
            Require (Recover-KuboPackagePublication $output) 'Already committed package was not recovered'
            Require ([IO.File]::ReadAllText((Join-Path $output 'user-after-publication.txt')) -eq 'preserve user addition') 'Adoption regenerated or removed user data'
        }
    } finally {if($child) {if(!$child.HasExited) {Stop-Process -Id $child.Id;$child.WaitForExit()};$child.Dispose()}}
}
Write-Output 'PASS: staged new/empty publication, copy-failure isolation, exclusive journals, competitor preservation and actual killed-child before/after commit recovery/retry without deleting retained data'
