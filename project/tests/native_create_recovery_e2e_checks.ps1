[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$EngineDirectory,[Parameter(Mandatory=$true)][string]$SdkDirectory,[string]$CrashRunner)
$ErrorActionPreference='Stop'
function Require($condition,$why) {if(!$condition) {throw $why}}
$recoveryEngine=[IO.Path]::GetFullPath($EngineDirectory)
$recoverySdk=[IO.Path]::GetFullPath($SdkDirectory)
if([string]::IsNullOrWhiteSpace($CrashRunner)) {$CrashRunner=Join-Path $PSScriptRoot '..\..\generated\cpu-checks\native_project_create_checks.exe'}
$recoveryCrash=[IO.Path]::GetFullPath($CrashRunner)
$recoveryFixture=Join-Path ([IO.Path]::GetTempPath()) ('kubo-native-recovery-'+[guid]::NewGuid().ToString('N')+'-'+(-join ([char[]]@(0x958b,0x767a,0x65e5,0x672c,0x8a9e))))
function Run-OwnedRecovery([string]$exe,[string[]]$arguments,[int]$expected) {
    $owned=$null
    try {
        $owned=Start-Process -FilePath $exe -ArgumentList $arguments -WorkingDirectory $recoveryFixture -WindowStyle Hidden -PassThru
        $handle=$owned.Handle
        Require ($handle -ne [IntPtr]::Zero -and $owned.WaitForExit(30000)) 'Owned recovery process timed out'
        $owned.WaitForExit();Require ($owned.ExitCode -eq $expected) "Owned recovery process exit $($owned.ExitCode), expected $expected"
    } finally {if($owned) {if(!$owned.HasExited) {Stop-Process -Id $owned.Id;$owned.WaitForExit()};$owned.Dispose()}}
}
try {
    New-Item -ItemType Directory -Path $recoveryFixture | Out-Null
    $recoveryManifest=Get-Content -LiteralPath (Join-Path $recoverySdk 'sdk-package.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    foreach($entry in $recoveryManifest.files) {Require ((Get-FileHash -LiteralPath (Join-Path $recoverySdk $entry.path)).Hash.ToLowerInvariant() -eq $entry.sha256) 'SDK digest mismatch before recovery'}
    $beforeRoot=Join-Path $recoveryFixture 'Before empty';New-Item -ItemType Directory -Path $beforeRoot | Out-Null
    $beforeProject=Join-Path $beforeRoot 'Game.kubo'
    Run-OwnedRecovery $recoveryCrash @('--crash-before',('"'+$beforeProject+'"'),('"'+$recoverySdk+'"')) 77
    Require (!(Test-Path -LiteralPath $beforeRoot) -and (Test-Path -LiteralPath ($beforeRoot+'.kubo-create-journal.json'))) 'Crash-before did not leave recorded empty-folder retirement'
    $beforeArgs=@('--create-native',('"'+$beforeProject+'"'),'--sdk',('"'+$recoverySdk+'"'),'--no-launch')
    Run-OwnedRecovery (Join-Path $recoveryEngine 'KuboLauncher.exe') $beforeArgs 1
    Require ((Test-Path -LiteralPath $beforeRoot -PathType Container) -and @(Get-ChildItem -LiteralPath $beforeRoot -Force).Count -eq 0) 'Production Launcher did not restore original empty folder'
    Require (!(Test-Path -LiteralPath ($beforeRoot+'.kubo-create-journal.json'))) 'Restored recovery record was not archived'
    Run-OwnedRecovery (Join-Path $recoveryEngine 'KuboLauncher.exe') $beforeArgs 0
    Require ((Test-Path -LiteralPath $beforeProject) -and (Test-Path -LiteralPath (Join-Path $beforeRoot 'Native\Starter.cpp'))) 'Production creation retry failed'
    $afterRoot=Join-Path $recoveryFixture 'After published';New-Item -ItemType Directory -Path $afterRoot | Out-Null
    $afterProject=Join-Path $afterRoot 'Game.kubo'
    Run-OwnedRecovery $recoveryCrash @('--crash-after',('"'+$afterProject+'"'),('"'+$recoverySdk+'"')) 78
    $marker=Join-Path $afterRoot 'user-added.txt';[IO.File]::WriteAllText($marker,'preserve user data after interruption')
    $sourceDigest=(Get-FileHash -LiteralPath (Join-Path $afterRoot 'Native\Starter.cpp')).Hash
    Run-OwnedRecovery (Join-Path $recoveryEngine 'KuboLauncher.exe') @('--create-native',('"'+$afterProject+'"'),'--sdk',('"'+$recoverySdk+'"'),'--no-launch') 0
    Require ((Get-FileHash -LiteralPath (Join-Path $afterRoot 'Native\Starter.cpp')).Hash -eq $sourceDigest -and [IO.File]::ReadAllText($marker) -eq 'preserve user data after interruption') 'Published recovery regenerated source or changed user data'
    Require (!(Test-Path -LiteralPath ($afterRoot+'.kubo-create-journal.json'))) 'Published recovery journal was not archived'
    Require (@(Get-ChildItem -LiteralPath $recoveryFixture -Directory -Filter '.kubo-native-stage-*').Count -ge 1) 'Recovery deleted interrupted stage data'
    Require (@(Get-ChildItem -LiteralPath $recoveryFixture -File -Filter '*.kubo-create-journal.json.recovered-*').Count -eq 2) 'Recovery did not retain both archived records'
    foreach($entry in $recoveryManifest.files) {Require ((Get-FileHash -LiteralPath (Join-Path $recoverySdk $entry.path)).Hash.ToLowerInvariant() -eq $entry.sha256) 'Recovery modified SDK content'}
    Write-Output 'PASS: production Launcher recovers actually terminated before/after publication children, restores empty folder/retries, adopts published project without source/user-data changes, retains stage/backup/records and preserves all SDK hashes'
} finally {
    $resolved=[IO.Path]::GetFullPath($recoveryFixture)
    $tempRoot=[IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')+'\'
    if(!$resolved.StartsWith($tempRoot,[StringComparison]::OrdinalIgnoreCase) -or !(Split-Path -Leaf $resolved).StartsWith('kubo-native-recovery-')) {throw 'Unsafe recovery fixture cleanup'}
    if(Test-Path -LiteralPath $resolved) {Remove-Item -LiteralPath $resolved -Recurse -Force}
}
