[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$SdkDirectory,
      [ValidateSet('Object','Scene')][string]$Probe='Object',
      [ValidateSet('Debug','Release')][string]$Configuration='Debug',
      [string]$MSBuild='C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe')
$ErrorActionPreference='Stop'
$objectProbeBase='animator_object_sdk_checks';$objectProbeExe='AnimatorObjectSdkChecks.exe';$objectProbeMarker='PASS: SDK-only real-device Object3D'
if($Probe -eq 'Scene'){$objectProbeBase='animator_scene_sdk_checks';$objectProbeExe='AnimatorSceneSdkChecks.exe';$objectProbeMarker='PASS: SDK-only production EntityScene Animator'}
$objectSdk=(Resolve-Path -LiteralPath $SdkDirectory).Path.TrimEnd('\')
$objectProject=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$objectFixture=[IO.Path]::GetFullPath((Join-Path $objectProject ('..\generated\animator-object-sdk\'+[guid]::NewGuid().ToString('N'))))
New-Item -ItemType Directory -Path $objectFixture | Out-Null
Write-Output "ANIMATOR_OBJECT_SDK_LOGS=$objectFixture"
$objectHashes=@{}
Get-ChildItem -LiteralPath $objectSdk -File -Recurse | ForEach-Object {$objectHashes[$_.FullName]=(Get-FileHash -LiteralPath $_.FullName).Hash}
[xml]$objectIncludes=Get-Content -LiteralPath (Join-Path $objectSdk 'KuboRuntime.Includes.props') -Raw -Encoding UTF8
$objectVersion=$objectIncludes.SelectSingleNode('//*[local-name()="KuboSdkVCToolsVersion"]').InnerText
$objectChild=$null
try {
 foreach($objectFile in @(($objectProbeBase+'.cpp'),($objectProbeBase+'.vcxproj'))){Copy-Item -LiteralPath (Join-Path $PSScriptRoot $objectFile) -Destination (Join-Path $objectFixture $objectFile)}
 if($Probe -eq 'Object'){Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'layered_object_fixture.h') -Destination $objectFixture}
 & $MSBuild (Join-Path $objectFixture ($objectProbeBase+'.vcxproj')) /m:1 "/p:Configuration=$Configuration" /p:Platform=x64 "/p:VCToolsVersion=$objectVersion" "/p:KuboSdkRoot=$objectSdk\" "/p:OutDir=$objectFixture\bin\" "/p:IntDir=$objectFixture\obj\" /verbosity:minimal *> (Join-Path $objectFixture 'build.log')
 if($LASTEXITCODE -ne 0){Get-Content (Join-Path $objectFixture 'build.log') -Tail 18;throw 'SDK Object animation build failed'}
 $objectReads=@(Get-ChildItem -LiteralPath (Join-Path $objectFixture 'obj') -Filter CL.read.1.tlog -Recurse)
 if($objectReads.Count -ne 1){throw 'Require one consumer header dependency record'}
 $objectSourceEngine=(Join-Path $objectProject 'engine').TrimEnd('\')+'\'
 if(@(Get-Content -LiteralPath $objectReads[0].FullName -Encoding Unicode | Where-Object { $_.StartsWith($objectSourceEngine,[StringComparison]::OrdinalIgnoreCase) }).Count){throw 'SDK consumer leaked original engine headers'}
 $objectModels=Join-Path $objectFixture 'bin\Resources\models'
 New-Item -ItemType Directory -Path $objectModels -Force | Out-Null
 Copy-Item -LiteralPath (Join-Path $objectProject 'Resources\models\walk') -Destination $objectModels -Recurse -Force
 $objectRedist=Join-Path ([IO.Path]::GetDirectoryName($MSBuild)) '..\..\..\..\VC\Redist\MSVC'
 $objectToolPrefix=($objectVersion.Split('.')[0..1] -join '.')+'.'
 $objectOpenMpName=if($Configuration -eq 'Debug'){'vcomp140d.dll'}else{'vcomp140.dll'}
 $objectOpenMpPattern=if($Configuration -eq 'Debug'){'\\x64\\Microsoft\.VC143\.DebugOpenMP\\'}else{'\\x64\\Microsoft\.VC143\.OpenMP\\'}
 $objectOpenMp=@(Get-ChildItem -LiteralPath $objectRedist -Filter $objectOpenMpName -File -Recurse | Where-Object {
  $_.FullName -match $objectOpenMpPattern -and $_.FullName -notmatch '\\onecore\\' -and $_.FullName.Contains(('\MSVC\'+$objectToolPrefix))
 } | Sort-Object FullName -Descending | Select-Object -First 1)
 if($objectOpenMp.Count -ne 1){throw "Installed matching $Configuration OpenMP runtime unavailable"}
 Copy-Item -LiteralPath $objectOpenMp[0].FullName -Destination (Join-Path $objectFixture ('bin\'+$objectOpenMpName))
 $objectChild=Start-Process -FilePath (Join-Path $objectFixture ('bin\'+$objectProbeExe)) -WorkingDirectory (Join-Path $objectFixture 'bin') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $objectFixture 'native.log') -RedirectStandardError (Join-Path $objectFixture 'native.stderr.log')
 $objectHandle=$objectChild.Handle
 if(!$objectChild.WaitForExit(50000)){throw 'Owned actual object child exceeded50s'}
 $objectChild.WaitForExit();$objectExit=$objectChild.ExitCode
 Get-Content -LiteralPath (Join-Path $objectFixture 'native.log') -Encoding UTF8
 Get-Content -LiteralPath (Join-Path $objectFixture 'native.stderr.log') -Encoding UTF8
 Write-Output "ANIMATOR_OBJECT_SDK_NATIVE_EXIT=$objectExit CONFIG=$Configuration PROBE=$Probe"
 if($objectExit -ne 0){throw 'Actual object SDK test failed'}
 if(!(Select-String -LiteralPath (Join-Path $objectFixture 'native.log') -SimpleMatch -Pattern $objectProbeMarker -Quiet)){throw 'Missing actual probe marker'}
 $objectControllerMarker=if($Probe -eq 'Scene'){'PASS: SDK-only EntityScene Controller GUID/Lua typed FIFO'}else{'PASS: SDK-only Object3D Controller GUID/revision/typed parameters'}
 if(!(Select-String -LiteralPath (Join-Path $objectFixture 'native.log') -SimpleMatch -Pattern $objectControllerMarker -Quiet)){throw 'Missing Controller SDK probe marker'}
 if($Probe -eq 'Object' -and !(Select-String -LiteralPath (Join-Path $objectFixture 'native.log') -SimpleMatch -Pattern 'PASS: actual Object3D four-layer ordered pose/independent clocks and legacy weight conversion clock/blend preservation' -Quiet)){throw 'Missing layered Object SDK probe marker'}
 if($Probe -eq 'Object' -and !(Select-String -LiteralPath (Join-Path $objectFixture 'native.log') -SimpleMatch -Pattern 'PASS: actual Object3D force-state' -Quiet)){throw 'Missing force-state Object SDK probe marker'}
 if($Probe -eq 'Scene' -and !(Select-String -LiteralPath (Join-Path $objectFixture 'native.log') -SimpleMatch -Pattern 'PASS: SDK-only Lua force-state actual Cutscene' -Quiet)){throw 'Missing force-state Scene SDK probe marker'}
 if($Probe -eq 'Scene' -and !(Select-String -LiteralPath (Join-Path $objectFixture 'native.log') -SimpleMatch -Pattern 'PASS: SDK-only selected manifest/legacy ambiguity/missing no-fallback/root-relative recovery' -Quiet)){throw 'Missing selected-manifest SDK probe marker'}
 if($Probe -eq 'Scene' -and !(Select-String -LiteralPath (Join-Path $objectFixture 'native.log') -SimpleMatch -Pattern 'PASS: SDK-only layered EntityScene Lua indexed state/time/progress/normalized/weight FIFO/no prediction/failed-hook isolation' -Quiet)){throw 'Missing layered Scene SDK probe marker'}
 if($Probe -eq 'Scene' -and !(Select-String -LiteralPath (Join-Path $objectFixture 'native.log') -SimpleMatch -Pattern 'PASS: SDK-only pre-init close policy/retained window owner/late configuration refusal' -Quiet)){throw 'Missing startup close ownership SDK marker'}
} finally {
 if($objectChild){if(!$objectChild.HasExited){Stop-Process -Id $objectChild.Id;$objectChild.WaitForExit()};$objectChild.Dispose()}
 $objectAfter=@(Get-ChildItem -LiteralPath $objectSdk -File -Recurse)
 if($objectAfter.Count -ne $objectHashes.Count){throw 'SDK inventory changed'}
 foreach($objectFile in $objectAfter){if(!$objectHashes.ContainsKey($objectFile.FullName) -or (Get-FileHash -LiteralPath $objectFile.FullName).Hash -ne $objectHashes[$objectFile.FullName]){throw 'SDK bytes changed'}}
 Write-Output "ANIMATOR_OBJECT_SDK_DIGESTS_UNCHANGED=$($objectHashes.Count)"
}
