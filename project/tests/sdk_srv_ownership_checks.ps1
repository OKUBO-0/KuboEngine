[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$SdkDirectory,
      [switch]$ExpectBefore,
      [switch]$KeepFixture,
      [string]$MSBuild='C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe')
$ErrorActionPreference='Stop'
$srvSdk=(Resolve-Path -LiteralPath $SdkDirectory).Path.TrimEnd('\')
[xml]$srvSdkProps=Get-Content -LiteralPath (Join-Path $srvSdk 'KuboRuntime.Includes.props') -Raw
$srvToolNode=$srvSdkProps.SelectSingleNode('//*[local-name()="KuboSdkVCToolsVersion"]')
if(!$srvToolNode -or [string]::IsNullOrWhiteSpace($srvToolNode.InnerText)){throw 'Missing SDK compiler contract'}
$srvTemp=[IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')
$srvFixture=Join-Path $srvTemp ('kubo-sdk-srv-'+[guid]::NewGuid().ToString('N'))
$srvComplete=$false;$srvChild=$null
New-Item -ItemType Directory -Path $srvFixture | Out-Null
try {
 foreach($file in @('sdk_srv_ownership_fixture.cpp','sdk_srv_ownership_fixture.vcxproj')){
  Copy-Item -LiteralPath (Join-Path $PSScriptRoot $file) -Destination (Join-Path $srvFixture $file)
 }
 foreach($configuration in @('Debug','Release')){
  $buildLog=Join-Path $srvFixture "build-$configuration.log"
  & $MSBuild (Join-Path $srvFixture 'sdk_srv_ownership_fixture.vcxproj') /m:1 "/p:Configuration=$configuration" /p:Platform=x64 "/p:VCToolsVersion=$($srvToolNode.InnerText)" "/p:KuboSdkRoot=$srvSdk/" /v:minimal *> $buildLog
  if($LASTEXITCODE -ne 0){Get-Content -LiteralPath $buildLog -Tail 30;throw "SDK SRV fixture $configuration build failed"}
  $bin=Join-Path $srvFixture 'bin';$obj=Join-Path $srvFixture 'obj'
  $modes=@('object','label','reclaim')
  if($configuration -eq 'Release'){$bin=Join-Path $bin 'Release';$obj=Join-Path $obj 'Release';$modes=@('object')}
  $sourceEngine=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\engine')).TrimEnd('\')+'\'
  $reads=@(Get-ChildItem -LiteralPath $obj -Filter 'CL.read.1.tlog' -File -Recurse)
  if(!$reads.Count){throw 'Compiler read records are required'}
  foreach($read in $reads){foreach($line in Get-Content -LiteralPath $read.FullName){
   if($line.StartsWith($sourceEngine,[StringComparison]::OrdinalIgnoreCase)){throw 'SDK-only fixture read original engine source/header'}
  }}
  foreach($mode in $modes){
   $arguments=@('--expect-fixed')
   if($mode -eq 'label'){$arguments=@('--label-allocation','--expect-fixed')}
   if($mode -eq 'reclaim'){$arguments=@('--reclaim-allocation','--expect-fixed')}
   $outLog=Join-Path $srvFixture "$configuration-$mode.log";$errLog=Join-Path $srvFixture "$configuration-$mode.stderr.log"
   $srvChild=Start-Process -FilePath (Join-Path $bin 'SdkSrvOwnershipChecks.exe') -ArgumentList $arguments -WorkingDirectory $srvTemp -WindowStyle Hidden -PassThru -RedirectStandardOutput $outLog -RedirectStandardError $errLog
   # Retain the native handle before exit so Windows PowerShell exposes ExitCode.
   $srvHandle=$srvChild.Handle
   if(!$srvChild.WaitForExit(50000)){throw "Owned SDK SRV $configuration/$mode timed out"}
   $srvChild.WaitForExit()
   $caseExit=$srvChild.ExitCode;$srvChild.Dispose();$srvChild=$null
   $caseText=Get-Content -LiteralPath $outLog -Raw
   Get-Content -LiteralPath $outLog;Get-Content -LiteralPath $errLog
   $expected=if($ExpectBefore){1}else{0}
   if($caseExit -ne $expected){throw "SDK SRV $configuration/$mode exit=$caseExit expected=$expected"}
   if($ExpectBefore){
    $marker=switch($mode){'object'{'unreturned=15'};'label'{'SRV_LABEL_FAULT coherent=0'};'reclaim'{'remaining=2049 max=2048 first=0 second=0 used=2'}}
    if(!$caseText.Contains($marker)){throw 'Expected-before refusal must reproduce the specific ownership failure'}
   }elseif($caseText -notmatch '(?m)^PASS:'){throw 'Successful SRV fixture must report its checked invariant'}
   Write-Output "SDK_SRV_CHECK configuration=$configuration mode=$mode exit=$caseExit expected=$expected"
  }
 }
 $srvComplete=$true
 Write-Output "PASS: SDK-only SRV ownership contract, Debug object/label/reclaim and Release object; expected-before=$ExpectBefore"
}finally {
 if($srvChild){if(!$srvChild.HasExited){Stop-Process -Id $srvChild.Id;$srvChild.WaitForExit()};$srvChild.Dispose()}
 $resolvedFixture=[IO.Path]::GetFullPath($srvFixture).TrimEnd('\')
 if($srvComplete -and !$KeepFixture){
  if(!([IO.Path]::GetDirectoryName($resolvedFixture).Equals($srvTemp,[StringComparison]::OrdinalIgnoreCase)) -or [IO.Path]::GetFileName($resolvedFixture) -notmatch '^kubo-sdk-srv-[a-f0-9]{32}$'){throw 'Refuse unsafe fixture cleanup'}
  Remove-Item -LiteralPath $resolvedFixture -Recurse -Force
 }else{Write-Output "Retained SDK SRV fixture/logs: $resolvedFixture"}
}
