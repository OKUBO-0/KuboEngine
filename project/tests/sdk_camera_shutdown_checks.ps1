[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$SdkDirectory,
      [string]$MSBuild='C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe')
$ErrorActionPreference='Stop'
$cameraSdk=(Resolve-Path -LiteralPath $SdkDirectory).Path.TrimEnd('\')
[xml]$cameraProps=Get-Content -LiteralPath (Join-Path $cameraSdk 'KuboRuntime.Includes.props') -Raw
$cameraTool=$cameraProps.SelectSingleNode('//*[local-name()="KuboSdkVCToolsVersion"]')
if(!$cameraTool -or [string]::IsNullOrWhiteSpace($cameraTool.InnerText)){throw 'Missing SDK compiler contract'}
$cameraFixture=Join-Path ([IO.Path]::GetTempPath()) ('kubo-sdk-camera-'+[guid]::NewGuid().ToString('N'))
$cameraChild=$null
New-Item -ItemType Directory -Path $cameraFixture | Out-Null
try {
 foreach($file in @('camera_shutdown_checks.cpp','sdk_camera_shutdown_fixture.vcxproj')){
  Copy-Item -LiteralPath (Join-Path $PSScriptRoot $file) -Destination (Join-Path $cameraFixture $file)
 }
 foreach($configuration in @('Debug','Release')){
  $buildLog=Join-Path $cameraFixture "build-$configuration.log"
  & $MSBuild (Join-Path $cameraFixture 'sdk_camera_shutdown_fixture.vcxproj') /m:1 "/p:Configuration=$configuration" /p:Platform=x64 "/p:VCToolsVersion=$($cameraTool.InnerText)" "/p:KuboSdkRoot=$cameraSdk/" /v:minimal *> $buildLog
  if($LASTEXITCODE -ne 0){Get-Content -LiteralPath $buildLog -Tail 25;throw "SDK camera $configuration build failed"}
  $originalEngine=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\engine')).TrimEnd('\')+'\'
  $originalTests=[IO.Path]::GetFullPath($PSScriptRoot).TrimEnd('\')+'\'
  $reads=@(Get-ChildItem -LiteralPath (Join-Path $cameraFixture "obj\$configuration") -Filter 'CL.read.1.tlog' -File -Recurse)
  if(!$reads.Count){throw 'Missing compiler read records'}
  foreach($read in $reads){foreach($line in Get-Content -LiteralPath $read.FullName){
   if($line.StartsWith($originalEngine,[StringComparison]::OrdinalIgnoreCase) -or $line.StartsWith($originalTests,[StringComparison]::OrdinalIgnoreCase)){throw 'SDK camera fixture read original source/header'}
  }}
  $outLog=Join-Path $cameraFixture "run-$configuration.log"
  $errLog=Join-Path $cameraFixture "run-$configuration.stderr.log"
  $cameraChild=Start-Process -FilePath (Join-Path $cameraFixture "bin\$configuration\SdkCameraShutdownChecks.exe") -WorkingDirectory $cameraFixture -WindowStyle Hidden -PassThru -RedirectStandardOutput $outLog -RedirectStandardError $errLog
  $cameraHandle=$cameraChild.Handle
  if(!$cameraChild.WaitForExit(50000)){throw "SDK camera $configuration native timeout"}
  $cameraChild.WaitForExit()
  $caseExit=$cameraChild.ExitCode;$cameraChild.Dispose();$cameraChild=$null
  $caseText=Get-Content -LiteralPath $outLog -Raw
  Get-Content -LiteralPath $outLog;Get-Content -LiteralPath $errLog
  if($caseExit -ne 0 -or $caseText -notmatch '(?m)^PASS: camera identity detach' -or $caseText -notmatch 'CAMERA_SHUTDOWN_FAULT consumed=0'){throw "SDK camera $configuration failed exit=$caseExit"}
  Write-Output "SDK_CAMERA_CHECK configuration=$configuration exit=$caseExit"
 }
 Write-Output 'PASS: SDK-only Debug/Release camera shutdown contract; no original source/header reads, window or GPU initialization'
}finally {
 if($cameraChild){if(!$cameraChild.HasExited){Stop-Process -Id $cameraChild.Id;$cameraChild.WaitForExit()};$cameraChild.Dispose()}
 Write-Output "Retained SDK camera fixture/logs: $cameraFixture"
}
