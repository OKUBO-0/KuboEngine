[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$SdkDirectory,[string]$MSBuild='C:/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/amd64/MSBuild.exe')
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'sdk_link_inputs.ps1')
function Need($ok,[string]$message){if(!$ok){throw $message}}
$particleSdk=(Resolve-Path -LiteralPath $SdkDirectory).Path.TrimEnd('\')
$sdkManifestPath=Join-Path $particleSdk 'sdk-package.json';$sdkDigest=(Get-FileHash $sdkManifestPath).Hash
$sdkManifest=Get-Content $sdkManifestPath -Raw -Encoding utf8|ConvertFrom-Json
Need ($sdkManifest.format -eq 'kubo-runtime-sdk-1') 'Wrong SDK format'
function Check-Sdk {
 Need ((Get-FileHash $sdkManifestPath).Hash -eq $sdkDigest) 'SDK manifest changed'
 foreach($entry in $sdkManifest.files){Need ((Get-FileHash -LiteralPath (Join-Path $particleSdk $entry.path)).Hash.ToLowerInvariant() -eq $entry.sha256) ('SDK payload changed '+$entry.path)}
 $actual=@(Get-ChildItem -LiteralPath $particleSdk -Recurse -File -Force|ForEach-Object {$_.FullName.Substring($particleSdk.Length+1).Replace('\','/')})
 Need (@(Compare-Object @((@($sdkManifest.files.path)+@('sdk-package.json'))|Sort-Object) @($actual|Sort-Object)).Count -eq 0) 'SDK exact inventory changed'
}
Check-Sdk
$label=-join([char[]]@(0x7c92,0x5b50,0x691c,0x8a3c))
$fixture=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot ('../../generated/sdk-particle-groups/'+[guid]::NewGuid().ToString('N')+'-'+$label)))
Need (!(Test-Path $fixture)) 'Refuse existing SDK Particle fixture'
New-Item -ItemType Directory -Path $fixture,(Join-Path $fixture 'Unrelated cwd')|Out-Null
Write-Output "SDK_PARTICLE_LOGS=$fixture"
# ASCII locator survives a Windows PowerShell child's console encoding while
# the actual project/compile paths remain Unicode.
Write-Output ('SDK_PARTICLE_LOGS_BASE64='+[Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($fixture)))
$sourceFiles=@('sdk_particle_group_fixture.vcxproj','sdk_particle_group_main.cpp','particle_group_fixture.cpp','particle_group_fixture.h')
$fixtureInputs=@(foreach($file in $sourceFiles){$source=Join-Path $PSScriptRoot $file;Copy-Item -LiteralPath $source -Destination $fixture;[ordered]@{name=$file;sha256=(Get-FileHash -LiteralPath $source).Hash}})
$fixtureInputs|ConvertTo-Json|Set-Content (Join-Path $fixture 'fixture-inputs.json') -Encoding utf8
[xml]$includes=Get-Content (Join-Path $particleSdk 'KuboRuntime.Includes.props') -Raw
$toolchain=$includes.SelectSingleNode('//*[local-name()="KuboSdkVCToolsVersion"]').InnerText
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).TrimEnd('\')+'\'
function Native([string]$exe,[string]$tag){
 $child=$null;$out=Join-Path $fixture "$tag.log";$err=Join-Path $fixture "$tag.stderr.log"
 try{
  $child=Start-Process -FilePath $exe -WorkingDirectory (Join-Path $fixture 'Unrelated cwd') -WindowStyle Hidden -PassThru -RedirectStandardOutput $out -RedirectStandardError $err;$handle=$child.Handle
  [ordered]@{processId=$child.Id;startTimeUtc=$child.StartTime.ToUniversalTime().ToString('o');executable=$exe}|ConvertTo-Json|Set-Content (Join-Path $fixture "$tag.process.json") -Encoding utf8
  Need ($handle -ne [IntPtr]::Zero -and $child.WaitForExit(30000)) 'SDK Particle native timeout (30s)'
  $child.WaitForExit();$exit=$child.ExitCode
  [ordered]@{processId=$child.Id;exitCode=$exit;completedAtUtc=[DateTime]::UtcNow.ToString('o')}|ConvertTo-Json|Set-Content (Join-Path $fixture "$tag.terminal.json") -Encoding utf8
  $lines=Get-Content $out;Get-Content $out -Tail 3;Get-Content $err -Tail 6
  Need ($exit -eq 0 -and @($lines|Where-Object {$_ -eq 'PASS: SDK-only Particle group ownership/multi-view fixture'}).Count -eq 1) 'SDK Particle native failed'
  $pixels=@($lines|Where-Object {$_ -match '^PARTICLE_GROUP_PIXEL '});Need ($pixels.Count -eq 32) 'SDK Particle32 pixels missing'
  foreach($session in 0,1){foreach($view in 0,1){$selected=@($pixels|Where-Object {$_ -match ('^PARTICLE_GROUP_PIXEL session='+$session+' stage=\d+ frame=[01] view='+$view+' ')});Need ($selected.Count -eq 8) 'SDK Particle session/view count';$indices=@()
   for($stage=0;$stage -lt 8;++$stage){Need ($selected[$stage] -match ('^PARTICLE_GROUP_PIXEL session='+$session+' stage='+$stage+' frame=([01]) view='+$view+' visible=([01]) rgb=(\d+,\d+,\d+)$')) 'SDK Particle malformed pixel';$index=[int]$Matches[1];$visible=[int]$Matches[2];$rgb=$Matches[3];$shouldDraw=$stage -ne 6 -and !($stage -eq 2 -and $view -eq 1);$color=if(!$shouldDraw){'196,215,246'}elseif($stage -ge 3 -and $stage -le 5){'196,255,246'}else{'255,215,246'};Need ($visible -eq [int]$shouldDraw -and $rgb -eq $color) 'SDK Particle pixel sequence';$indices+=$index}
   Need (@($indices|Where-Object {$_ -eq 0}).Count -eq 4 -and @($indices|Where-Object {$_ -eq 1}).Count -eq 4) 'SDK Particle swapchain coverage'
  };Need (@($lines|Where-Object {$_ -eq "PARTICLE_GROUP_FINAL session=$session retired=1 stages=8 indices=3"}).Count -eq 1) 'SDK Particle session not retired'}
  Need (@($lines|Where-Object {$_ -eq 'PASS: actual Particle owned groups/candidate failure+retry/legacy isolation/move+Reset/stale slots+sessions, independent Game320x180 Scene480x270 pixels32, pending vertex retention/completed collection, clear+reemit, multiple swapchain indices and GPU errors0/shutdown (not Entity authoring/Lua/MCP)'}).Count -eq 1) 'SDK Particle final assertion/GPU shutdown missing'
 }finally{if($child){if(!$child.HasExited){$child.Kill();$child.WaitForExit()};$child.Dispose()}}
}
foreach($configuration in @('Debug','Release')){
 Need (([IO.DriveInfo]::new([IO.Path]::GetPathRoot($fixture))).AvailableFreeSpace -ge 4GB -and ([IO.DriveInfo]::new([IO.Path]::GetPathRoot([IO.Path]::GetTempPath()))).AvailableFreeSpace -ge 4GB) 'SDK Particle verification requires 4 GiB free'
 $evaluation=& $MSBuild (Join-Path $fixture 'sdk_particle_group_fixture.vcxproj') "/p:Configuration=$configuration" /p:Platform=x64 "/p:VCToolsVersion=$toolchain" "/p:KuboSdkRoot=$particleSdk/" /getProperty:ConfigurationType,TLogLocation /getItem:ClCompile,ProjectReference | Out-String
 Need ($LASTEXITCODE -eq 0) 'SDK Particle project evaluation failed'
 $metadata=$evaluation|ConvertFrom-Json
 Need ($metadata.Properties.ConfigurationType -eq 'Application' -and @($metadata.Items.ProjectReference).Count -eq 0 -and @($metadata.Items.ClCompile).Count -eq 2) 'SDK Particle must be an application with exactly two fixture sources and no ProjectReferences'
 foreach($compile in $metadata.Items.ClCompile){Need ($compile.FullPath.StartsWith($fixture+'\',[StringComparison]::OrdinalIgnoreCase) -and [IO.Path]::GetFileName($compile.FullPath) -in @('sdk_particle_group_main.cpp','particle_group_fixture.cpp')) 'SDK Particle compilation escaped fixture sources'}
 $tlogs=[IO.Path]::GetFullPath($metadata.Properties.TLogLocation).TrimEnd('\');Need ($tlogs -eq (Join-Path $fixture "obj/$configuration/Tracking")) 'SDK Particle tracking escaped its declared directory'
 $metadata|ConvertTo-Json -Depth 12|Set-Content (Join-Path $fixture "$configuration-evaluation.json") -Encoding utf8
 & $MSBuild (Join-Path $fixture 'sdk_particle_group_fixture.vcxproj') /m:1 "/p:Configuration=$configuration" /p:Platform=x64 "/p:VCToolsVersion=$toolchain" "/p:KuboSdkRoot=$particleSdk/" /v:minimal *> (Join-Path $fixture "$configuration-build.log")
 if($LASTEXITCODE -ne 0){Get-Content (Join-Path $fixture "$configuration-build.log") -Tail 22;throw 'SDK Particle build failed'}
 $reads=Get-Content (Join-Path $tlogs 'CL.read.1.tlog')
 foreach($read in $reads){Need (!$read.TrimStart('^').StartsWith($repo,[StringComparison]::OrdinalIgnoreCase)) 'SDK Particle compiler read original source/header'}
 $particleHeader=Join-Path $particleSdk 'include/engine/particle/core/ParticleManager.h'
 Need (@($reads|Where-Object {$_.Equals($particleHeader,[StringComparison]::OrdinalIgnoreCase)}).Count -ge 1) 'SDK Particle header was not compiled from selected SDK'
 Need (Test-SdkRuntimeLinkInputs (Get-Content (Join-Path $tlogs 'link.command.1.tlog') -Raw)) 'SDK Particle Editor link inputs or missing Runtime'
 Need (Test-SdkResolvedArchiveInputs (Get-Content (Join-Path $tlogs 'link.read.1.tlog')) (Join-Path $particleSdk "lib/$configuration")) 'SDK Particle libraries did not resolve from selected SDK'
 $exe=Join-Path $fixture "bin/$configuration/SdkParticleGroups.exe";Native $exe "$configuration-native"
 $game=Join-Path $fixture "$configuration Game";New-Item -ItemType Directory -Path (Join-Path $game 'Assets')|Out-Null
 [IO.File]::WriteAllText((Join-Path $game 'Game.kubo'),'{"name":"SDK Particle deployment","assetDirectory":"Assets","startupScene":"entry.scene"}',[Text.UTF8Encoding]::new($false))
 [IO.File]::WriteAllText((Join-Path $game 'entry.scene'),'{"version":1,"entities":[]}',[Text.UTF8Encoding]::new($false))
 $authored=@(foreach($name in @('Game.kubo','entry.scene')){[ordered]@{name=$name;sha256=(Get-FileHash (Join-Path $game $name)).Hash}})
 $export=Join-Path $fixture "$configuration-Export"
 & (Join-Path $particleSdk 'tools/package_project.ps1') -ProjectFile (Join-Path $game 'Game.kubo') -RuntimeExecutable $exe -OutputDirectory $export -Configuration $configuration
 $renamed=Join-Path $export 'RenamedParticleGroups.exe';Copy-Item -LiteralPath (Join-Path $export 'KuboPlayer.exe') -Destination $renamed
 Need ((Get-FileHash $renamed).Hash -eq (Get-FileHash $exe).Hash) 'Renamed Particle executable changed'
 foreach($name in @('*.cpp','*.h','*.lib','KuboEditor.exe','KuboMcpServer.exe')){Need (@(Get-ChildItem -LiteralPath $export -Recurse -File -Filter $name).Count -eq 0) 'Particle export leaked source/archives/Editor'}
 Native $renamed "$configuration-export"
 foreach($entry in $authored){Need ((Get-FileHash (Join-Path $game $entry.name)).Hash -eq $entry.sha256 -and (Get-FileHash (Join-Path $export $entry.name)).Hash -eq $entry.sha256) 'Particle deployment fixture source/copy changed'}
 Copy-Item -LiteralPath $tlogs -Destination (Join-Path $fixture "$configuration-tlogs") -Recurse
 Check-Sdk
}
foreach($entry in $fixtureInputs){Need ((Get-FileHash (Join-Path $fixture $entry.name)).Hash -eq $entry.sha256 -and (Get-FileHash (Join-Path $PSScriptRoot $entry.name)).Hash -eq $entry.sha256) 'SDK Particle fixture input changed'}
[ordered]@{complete=$true;sdk=$particleSdk;sdkManifestSha256=$sdkDigest;configurations=@('Debug','Release');nativeRuns=2;renamedExportRuns=2;frameworkSessions=8;particleViewRecords=128;originalSourceHeadersRead=$false;editorLinkInputs=$false;resolvedSdkArchives=$true;sdkHashesAndInventoryUnchanged=$true;fixtureSourceUnchanged=$true;runtimeGeneratedParticleFixture=$true;authoredParticleSceneStartup=$false;fullCpuCoverage=$false}|ConvertTo-Json|Set-Content (Join-Path $fixture 'complete.json') -Encoding utf8
Write-Output 'PASS: SDK-only Particle groups Debug+Release native/renamed export, view128/eight sessions/GPU errors0, exact selected SDK headers+archives and unchanged SDK; not authored Particle Scene startup'
