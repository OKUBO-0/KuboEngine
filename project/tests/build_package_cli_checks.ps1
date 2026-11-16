[CmdletBinding()]
param([string]$MSBuild,[switch]$Positive)
$ErrorActionPreference='Stop'
function Require($value,[string]$message){if(!$value){throw $message}}
$packageCliRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$packageCliFixture=[IO.Path]::GetFullPath((Join-Path $packageCliRoot ('../generated/build-package-cli/'+[guid]::NewGuid().ToString('N'))))
$packageCliSource=Join-Path $packageCliFixture 'Source'
New-Item -ItemType Directory -Path (Join-Path $packageCliSource 'Assets') | Out-Null
$packageCliManifest=Join-Path $packageCliSource 'entry.kubo'
[IO.File]::WriteAllText($packageCliManifest,'{"name":"CLI package fixture","engineVersion":"0.1.0","startupScene":"entry.scene","assetDirectory":"Assets"}',[Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText((Join-Path $packageCliSource 'entry.scene'),'{"version":1,"entities":[]}',[Text.UTF8Encoding]::new($false))
$packageCliScript=Join-Path $packageCliRoot 'tools/build_package.ps1'
$packageCliHost=(Get-Process -Id $PID).Path
Write-Output "BUILD_PACKAGE_CLI_LOGS=$packageCliFixture"
$packageCliOutput=Join-Path $packageCliFixture 'MustNotPublish'
$packageCliLog=Join-Path $packageCliFixture 'expected-failure.log'
$packageCliArguments=@('-NoProfile','-ExecutionPolicy','Bypass','-File',$packageCliScript,'-ProjectFile',$packageCliManifest,'-OutputDirectory',$packageCliOutput,'-Configuration','Debug','-VCToolsVersion','14.44.0')
if($MSBuild){$packageCliArguments+=@('-MSBuild',$MSBuild)}
$packageCliErrorPreference=$ErrorActionPreference
try {
 # Windows PowerShell turns redirected native stderr into NativeCommandError.
 # Observe this deliberately failing child through its terminal exit/log, not
 # the parent's Stop preference. The child build_package still fails closed.
 $ErrorActionPreference='Continue'
 & $packageCliHost @packageCliArguments *> $packageCliLog
 $packageCliFailure=$LASTEXITCODE
} finally {$ErrorActionPreference=$packageCliErrorPreference}
Require ($packageCliFailure -ne 0) 'Missing compiler unexpectedly built a package'
$packageCliFailureText=Get-Content -LiteralPath $packageCliLog -Raw
Require ($packageCliFailureText.Contains('KUBO_ENGINE_BUILD_EXIT=1 CONFIG=Debug')) 'Failure not observed through real engine build helper'
Require (!(Test-Path -LiteralPath $packageCliOutput) -and !$packageCliFailureText.Contains('Kubo runtime package created:')) 'Failed build published a stale prebuilt Player'
Write-Output "PASS: integrated build_package missing-compiler failure=$packageCliFailure, no stale Player publication"
if($Positive) {
 $packageCliOutput=Join-Path $packageCliFixture 'ReleaseExport'
 $packageCliPositiveLog=Join-Path $packageCliFixture 'release.log'
 $packageCliArguments=@('-NoProfile','-ExecutionPolicy','Bypass','-File',$packageCliScript,'-ProjectFile',$packageCliManifest,'-OutputDirectory',$packageCliOutput,'-Configuration','Release')
 if($MSBuild){$packageCliArguments+=@('-MSBuild',$MSBuild)}
 & $packageCliHost @packageCliArguments *> $packageCliPositiveLog
 $packageCliExit=$LASTEXITCODE
 if($packageCliExit -ne 0){Get-Content $packageCliPositiveLog -Tail 12;throw 'Integrated Release build/package failed'}
 $packageCliPositiveText=Get-Content -LiteralPath $packageCliPositiveLog -Raw
 Require ($packageCliPositiveText.Contains('KUBO_ENGINE_BUILD_EXIT=0 CONFIG=Release') -and $packageCliPositiveText.Contains('Kubo runtime package created:')) 'Integrated workflow skipped build/package'
 Require ((Get-FileHash -LiteralPath (Join-Path $packageCliOutput 'KuboPlayer.exe')).Hash -eq (Get-FileHash -LiteralPath (Join-Path $packageCliRoot '../generated/outputs/Release/KuboPlayer.exe')).Hash) 'Export did not use current Player'
 Require ([IO.File]::ReadAllText((Join-Path $packageCliOutput 'entry.kubo')) -ceq [IO.File]::ReadAllText($packageCliManifest)) 'Export changed project manifest'
 Write-Output 'PASS: integrated engine Release build -> exact current Player package, owned empty Scene fixture; not exported Player execution/clean machine proof'
}
Write-Output "BUILD_PACKAGE_CLI_FIXTURE_RETAINED=$packageCliFixture"
