[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$SdkDirectory,[string]$MSBuild='C:/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/amd64/MSBuild.exe')
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'sdk_link_inputs.ps1')
function Need($ok,[string]$message){if(!$ok){throw $message}}
function Write-Json([string]$path,$value){[IO.File]::WriteAllText($path,($value|ConvertTo-Json -Depth 32),[Text.UTF8Encoding]::new($false))}
$sdk=(Resolve-Path -LiteralPath $SdkDirectory).Path.TrimEnd('\');$manifestPath=Join-Path $sdk 'sdk-package.json';$digest=(Get-FileHash -LiteralPath $manifestPath).Hash;$manifest=Get-Content -LiteralPath $manifestPath -Raw -Encoding utf8|ConvertFrom-Json
Need ($manifest.format -eq 'kubo-runtime-sdk-1') 'Wrong Particle authoring SDK format'
foreach($relative in @('include/engine/scene/entity/ParticleComponents.h','include/engine/scene/entity/RenderParticleFrame.h')){Need (Test-Path -LiteralPath (Join-Path $sdk $relative) -PathType Leaf) 'Matched Particle authoring headers required'}
function Check-Sdk {
 Need ((Get-FileHash -LiteralPath $manifestPath).Hash -eq $digest) 'Particle authoring SDK manifest changed'
 foreach($entry in $manifest.files){Need ((Get-FileHash -LiteralPath (Join-Path $sdk $entry.path)).Hash.ToLowerInvariant() -eq $entry.sha256) ('Particle authoring SDK payload changed '+$entry.path)}
 $actual=@(Get-ChildItem -LiteralPath $sdk -Recurse -File -Force|ForEach-Object {$_.FullName.Substring($sdk.Length+1).Replace('\','/')})
 Need (@(Compare-Object @((@($manifest.files.path)+@('sdk-package.json'))|Sort-Object) @($actual|Sort-Object)).Count -eq 0) 'Particle authoring SDK inventory changed'
}
Check-Sdk
$label=-join([char[]]@(0x7c92,0x5b50,0x7de8,0x96c6));$fixture=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot ('../../generated/sdk-particle-authoring/'+[guid]::NewGuid().ToString('N')+'-'+$label)))
Need (!(Test-Path -LiteralPath $fixture)) 'Refuse existing SDK Particle authoring fixture';New-Item -ItemType Directory -Path $fixture,(Join-Path $fixture 'Unrelated cwd')|Out-Null
Write-Output ('SDK_PARTICLE_AUTHORING_LOGS_BASE64='+[Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($fixture)))
$sourceFiles=@('sdk_particle_authoring_fixture.vcxproj','sdk_particle_authoring_main.cpp','player_particle_fixture.cpp','player_particle_fixture.h')
$inputs=@(foreach($file in $sourceFiles){$source=Join-Path $PSScriptRoot $file;Copy-Item -LiteralPath $source -Destination $fixture;[ordered]@{name=$file;sha256=(Get-FileHash -LiteralPath $source).Hash}});Write-Json (Join-Path $fixture 'fixture-inputs.json') $inputs
[xml]$includes=Get-Content -LiteralPath (Join-Path $sdk 'KuboRuntime.Includes.props') -Raw;$toolchain=$includes.SelectSingleNode('//*[local-name()="KuboSdkVCToolsVersion"]').InnerText
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).TrimEnd('\')+'\'
function Native([string]$exe,[string]$mode,[string]$tag){
 $child=$null;$out=Join-Path $fixture "$tag.log";$err=Join-Path $fixture "$tag.stderr.log"
 try{
  $launch=@{FilePath=$exe;WorkingDirectory=(Join-Path $fixture 'Unrelated cwd');WindowStyle='Hidden';PassThru=$true;RedirectStandardOutput=$out;RedirectStandardError=$err};if($mode -eq '--particle-export'){$launch.ArgumentList=$mode}
  $child=Start-Process @launch;$handle=$child.Handle;Write-Json (Join-Path $fixture "$tag.process.json") ([ordered]@{processId=$child.Id;startTimeUtc=$child.StartTime.ToUniversalTime().ToString('o');exe=$exe;mode=$mode})
  $timedOut=!$child.WaitForExit(30000);if($timedOut){$child.Kill()};$child.WaitForExit();$exit=$child.ExitCode;Write-Json (Join-Path $fixture "$tag.terminal.json") ([ordered]@{processId=$child.Id;exitCode=$exit;timedOut=$timedOut;completedAtUtc=[DateTime]::UtcNow.ToString('o')})
  Need (!$timedOut -and $null -ne $exit -and $exit -eq 0) "SDK Particle authoring native failed: $tag"
  $lines=@(Get-Content -LiteralPath $out);Need (@($lines|Where-Object {$_ -eq ('RUNNER_MODE='+$mode)}).Count -eq 1 -and @($lines|Where-Object {$_ -eq 'PASS: SDK-only authored Particle RuntimeGame startup fixture'}).Count -eq 1) 'SDK Particle authoring requested mode/final marker missing'
  $pixels=@($lines|Where-Object {$_ -match '^PLAYER_PARTICLE_PIXEL '});Need ($pixels.Count -eq 8) 'SDK Particle authoring frame records missing'
  $deployed=[int]($mode -eq '--particle-export');for($i=0;$i -lt 8;++$i){Need ($pixels[$i] -eq "PLAYER_PARTICLE_PIXEL deployed=$deployed frame=$i rgb=255,215,246") 'SDK Particle authoring startup pixel sequence wrong'}
  Need (@($lines|Where-Object {$_ -like 'PASS: actual RuntimeGame Particle*GPU errors0/shutdown*'}).Count -eq 1) 'SDK Particle authoring RuntimeGame lifecycle/GPU assertion missing'
  Write-Output "SDK_PARTICLE_AUTHORING_NATIVE_PASS=$tag PID=$($child.Id)"
 }finally{if($child){if(!$child.HasExited){$child.Kill();$child.WaitForExit()};$child.Dispose()}}
}
foreach($configuration in @('Debug','Release')){
 foreach($driveRoot in @([IO.Path]::GetPathRoot($fixture),[IO.Path]::GetPathRoot([IO.Path]::GetTempPath()))|Select-Object -Unique){Need (([IO.DriveInfo]::new($driveRoot)).AvailableFreeSpace -ge 4GB) 'SDK Particle authoring requires 4 GiB free'}
 $project=Join-Path $fixture 'sdk_particle_authoring_fixture.vcxproj';$evaluation=& $MSBuild $project "/p:Configuration=$configuration" /p:Platform=x64 "/p:VCToolsVersion=$toolchain" "/p:KuboSdkRoot=$sdk/" /getProperty:ConfigurationType,TLogLocation /getItem:ClCompile,ProjectReference|Out-String;Need ($LASTEXITCODE -eq 0) 'SDK Particle authoring evaluation failed';$metadata=$evaluation|ConvertFrom-Json
 Need ($metadata.Properties.ConfigurationType -eq 'Application' -and @($metadata.Items.ProjectReference).Count -eq 0 -and @($metadata.Items.ClCompile).Count -eq 2) 'SDK Particle authoring must compile exactly two copied sources without ProjectReferences'
 foreach($compile in $metadata.Items.ClCompile){Need ($compile.FullPath.StartsWith($fixture+'\',[StringComparison]::OrdinalIgnoreCase) -and [IO.Path]::GetFileName($compile.FullPath) -in @('sdk_particle_authoring_main.cpp','player_particle_fixture.cpp')) 'SDK Particle authoring compilation escaped copied fixture'}
 $tlogs=[IO.Path]::GetFullPath($metadata.Properties.TLogLocation).TrimEnd('\');Need ($tlogs -eq (Join-Path $fixture "obj/$configuration/Tracking")) 'SDK Particle authoring tracking escaped fixture';Write-Json (Join-Path $fixture "$configuration-evaluation.json") $metadata
 & $MSBuild $project /m:1 "/p:Configuration=$configuration" /p:Platform=x64 "/p:VCToolsVersion=$toolchain" "/p:KuboSdkRoot=$sdk/" /v:minimal *> (Join-Path $fixture "$configuration-build.log");Need ($LASTEXITCODE -eq 0) "SDK Particle authoring build failed: $configuration"
 $reads=Get-Content -LiteralPath (Join-Path $tlogs 'CL.read.1.tlog');foreach($read in $reads){Need (!$read.TrimStart('^').StartsWith($repo,[StringComparison]::OrdinalIgnoreCase)) 'SDK Particle authoring compiler read original project source/header'}
 foreach($relative in @('engine/scene/entity/ParticleComponents.h','engine/scene/entity/EntityScene.h','engine/app/RuntimeGame.h')){$header=Join-Path $sdk ('include/'+$relative);Need (@($reads|Where-Object {$_.Equals($header,[StringComparison]::OrdinalIgnoreCase)}).Count -ge 1) ('Particle SDK header not compiled: '+$relative)}
 Need (Test-SdkRuntimeLinkInputs (Get-Content -LiteralPath (Join-Path $tlogs 'link.command.1.tlog') -Raw)) 'Particle authoring SDK missing Runtime or linked Editor'
 Need (Test-SdkResolvedArchiveInputs (Get-Content -LiteralPath (Join-Path $tlogs 'link.read.1.tlog')) (Join-Path $sdk "lib/$configuration")) 'Particle authoring archives did not resolve from selected SDK'
 $exe=Join-Path $fixture "bin/$configuration/SdkParticleAuthoring.exe";Native $exe '--particles' "$configuration-native"
 $game=Join-Path $fixture "$configuration-Project";New-Item -ItemType Directory -Path (Join-Path $game 'Assets')|Out-Null;$imageName=[string][char]0x767d+'.png';$hint='Assets/'+$imageName;$image=Join-Path $game $hint
 Copy-Item -LiteralPath (Join-Path $sdk 'runtime/Resources/textures/common/white.png') -Destination $image;Write-Json ($image+'.kubo-meta.json') @{version=1;guid=('a'*32)}
 $particle=@{asset=$hint;guid=('a'*32);burst=1;rate=0;lifetime=60;size=1;velocity=@(0,0,0);spread=@(0,0,0);startColor=@(1,0,0,1);endColor=@(1,0,0,1)};$entity=@{id=1;parent=0;name='Particle';active=$true;model='';transform=@{position=@(0,0,0);rotation=@(0,0,0);scale=@(1,1,1)};particle=$particle}
 $camera=@{id=2;parent=0;name='Camera';active=$true;model='';transform=@{position=@(0,0,-5);rotation=@(0,0,0);scale=@(1,1,1)};camera=@{enabled=$true;primary=$true;fovY=.45;nearClip=.1;farClip=100;projection='orthographic';orthographicHeight=4}}
 Write-Json (Join-Path $game 'Game.kubo') @{name='SDK authored Particle';startupScene='entry.scene';assetDirectory='Assets';gameOutput=@{width=320;height=180}};Write-Json (Join-Path $game 'entry.scene') @{version=2;entities=@($entity,$camera)};Write-Json (Join-Path $game 'Assets/emitter.prefab') @{format='kubo-prefab-1';root=1;world=@{version=2;entities=@($entity)}};Write-Json (Join-Path $game 'Assets/emitter.prefab.kubo-meta.json') @{version=1;guid=('b'*32)}
 $authored=@(Get-ChildItem -LiteralPath $game -Recurse -File|ForEach-Object {[ordered]@{path=$_.FullName.Substring($game.Length+1);sha256=(Get-FileHash -LiteralPath $_.FullName).Hash}});$export=Join-Path $fixture "$configuration-Export"
 & (Join-Path $sdk 'tools/package_project.ps1') -ProjectFile (Join-Path $game 'Game.kubo') -RuntimeExecutable $exe -OutputDirectory $export -Configuration $configuration *> (Join-Path $fixture "$configuration-package.log")
 foreach($needle in @('Startup Particle dependencies verified: 1 assigned references, 1 texture files, 0 unassigned drafts','Prefab Particle dependencies verified: 1 assigned references, 1 texture files summed per template, 0 unassigned drafts')){Need (@(Select-String -LiteralPath (Join-Path $fixture "$configuration-package.log") -SimpleMatch $needle).Count -eq 2) 'Particle authoring SDK source/candidate closure missing'}
 $renamed=Join-Path $export 'RenamedAuthoredParticles.exe';Copy-Item -LiteralPath (Join-Path $export 'KuboPlayer.exe') -Destination $renamed;Need ((Get-FileHash -LiteralPath $exe).Hash -eq (Get-FileHash -LiteralPath $renamed).Hash) 'Renamed SDK Particle executable changed'
 foreach($filter in @('*.cpp','*.h','*.lib','KuboEditor.exe','KuboMcpServer.exe')){Need (@(Get-ChildItem -LiteralPath $export -File -Recurse -Filter $filter).Count -eq 0) 'Particle authoring SDK export leaked source/Editor/archive'}
 Native $renamed '--particle-export' "$configuration-export"
 foreach($entry in $authored){Need ((Get-FileHash -LiteralPath (Join-Path $game $entry.path)).Hash -eq $entry.sha256 -and (Get-FileHash -LiteralPath (Join-Path $export $entry.path)).Hash -eq $entry.sha256) 'Particle SDK project/Scene/Prefab/texture/metadata changed'}
 Copy-Item -LiteralPath $tlogs -Destination (Join-Path $fixture "$configuration-tlogs") -Recurse;Check-Sdk
}
foreach($entry in $inputs){Need ((Get-FileHash -LiteralPath (Join-Path $fixture $entry.name)).Hash -eq $entry.sha256 -and (Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $entry.name)).Hash -eq $entry.sha256) 'SDK Particle authoring copied/source fixture changed'}
Write-Json (Join-Path $fixture 'complete.json') ([ordered]@{complete=$true;sdk=$sdk;sdkManifestSha256=$digest;configurations=@('Debug','Release');nativeRuns=2;renamedExportRuns=2;frameworkSessions=4;particleFrameRecords=32;authoredParticleSceneStartup=$true;sourceAndCandidateParticleClosure=$true;originalSourceHeadersRead=$false;editorLinkInputs=$false;resolvedSdkArchives=$true;sdkHashesAndInventoryUnchanged=$true;fixtureSourceUnchanged=$true;fullCpuCoverage=$false})
Write-Output 'PASS: SDK-only authored Particle World2 RuntimeGame startup, Debug+Release native/renamed export, frames32/four sessions/GPU errors0, exact selected SDK headers+archives and unchanged SDK; Scene/Prefab source+candidate Texture closure'
