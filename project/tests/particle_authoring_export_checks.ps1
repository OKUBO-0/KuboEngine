[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$LogDirectory)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'));$logs=[IO.Path]::GetFullPath($LogDirectory)
if(Test-Path -LiteralPath $logs){throw 'Preserve previous Particle export verification evidence'}
if(([IO.DriveInfo]::new([IO.Path]::GetPathRoot($logs))).AvailableFreeSpace -lt 8GB){throw 'Particle build/export verification requires 8 GiB free'}
New-Item -ItemType Directory -Path $logs|Out-Null
$msbuild='C:/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/amd64/MSBuild.exe'
function Need($ok,[string]$message){if(!$ok){throw $message}}
function Write-Json([string]$path,$value){[IO.File]::WriteAllText($path,($value|ConvertTo-Json -Depth 32),[Text.UTF8Encoding]::new($false))}
function Native([string]$exe,[string]$mode,[string]$tag){
 $child=$null;$out=Join-Path $logs "$tag.log";$err=Join-Path $logs "$tag.stderr.log"
 try{
  $child=Start-Process -FilePath $exe -ArgumentList $mode -WindowStyle Hidden -WorkingDirectory $root -PassThru -RedirectStandardOutput $out -RedirectStandardError $err;$handle=$child.Handle
  Write-Json (Join-Path $logs "$tag.process.json") ([ordered]@{processId=$child.Id;startTimeUtc=$child.StartTime.ToUniversalTime().ToString('o');exe=$exe;mode=$mode})
  $timedOut=!$child.WaitForExit(30000);if($timedOut){$child.Kill()};$child.WaitForExit();$exit=$child.ExitCode
  Write-Json (Join-Path $logs "$tag.terminal.json") ([ordered]@{processId=$child.Id;exitCode=$exit;timedOut=$timedOut;completedAtUtc=[DateTime]::UtcNow.ToString('o')})
  Need (!$timedOut -and $null -ne $exit -and $exit -eq 0) "Particle native failed: $tag; retain logs"
  Need (@(Get-Content -LiteralPath $out) -contains ('RUNNER_MODE='+$mode)) 'Requested Particle runner mode missing'
  Need (@(Select-String -LiteralPath $out -Pattern '^PLAYER_PARTICLE_PIXEL ').Count -eq 8) 'Missing eight Player Particle frame records'
  Write-Output "PARTICLE_NATIVE_PASS=$tag PID=$($child.Id)"
 }finally{if($child){if(!$child.HasExited){$child.Kill();$child.WaitForExit()};$child.Dispose()}}
}
foreach($configuration in @('Debug','Release')){
 & $msbuild (Join-Path $root 'KuboPlayer.vcxproj') /m:1 "/p:Configuration=$configuration" /p:Platform=x64 /p:VCToolsVersion=14.44.35207 "/p:SolutionDir=$root/" "/p:ForceImportBeforeCppTargets=$root/tests/player_runtime_checks.targets" /v:minimal /nologo *> (Join-Path $logs "$configuration-build.log")
 Need ($LASTEXITCODE -eq 0) "Particle Player build failed: $configuration"
 $exe=[IO.Path]::GetFullPath((Join-Path $root "../generated/outputs/$configuration/PlayerRuntimeChecks.exe"))
 Native $exe '--particles' "$configuration-source"
 $project=Join-Path $logs "$configuration-Project";New-Item -ItemType Directory -Path (Join-Path $project 'Assets')|Out-Null
 $imageName=[string][char]0x767d+'.png';$hint='Assets/'+$imageName;$image=Join-Path $project $hint
 Copy-Item -LiteralPath (Join-Path $root 'Resources/textures/common/white.png') -Destination $image
 Write-Json ($image+'.kubo-meta.json') @{version=1;guid=('a'*32)}
 $particle=@{enabled=$true;asset=$hint;guid=('a'*32);burst=1;rate=0;lifetime=60;size=1;velocity=@(0,0,0);spread=@(0,0,0);startColor=@(1,0,0,1);endColor=@(1,0,0,1)}
 $entity=@{id=1;parent=0;name='Particle';active=$true;model='';transform=@{position=@(0,0,0);rotation=@(0,0,0);scale=@(1,1,1)};particle=$particle}
 $camera=@{id=2;parent=0;name='Camera';active=$true;model='';transform=@{position=@(0,0,-5);rotation=@(0,0,0);scale=@(1,1,1)};camera=@{enabled=$true;primary=$true;fovY=.45;nearClip=.1;farClip=100;projection='orthographic';orthographicHeight=4}}
 $scene=@{version=2;entities=@($entity,$camera)};$prefab=@{format='kubo-prefab-1';root=1;world=@{version=2;entities=@($entity)}}
 Write-Json (Join-Path $project 'Game.kubo') @{name='Particle exported startup';assetDirectory='Assets';startupScene='entry.scene';gameOutput=@{width=320;height=180}}
 Write-Json (Join-Path $project 'entry.scene') $scene;Write-Json (Join-Path $project 'Assets/emitter.prefab') $prefab;Write-Json (Join-Path $project 'Assets/emitter.prefab.kubo-meta.json') @{version=1;guid=('b'*32)}
 $files=@(Get-ChildItem -LiteralPath $project -File -Recurse|ForEach-Object {[ordered]@{relative=$_.FullName.Substring($project.Length+1);sha256=(Get-FileHash -LiteralPath $_.FullName).Hash}})
 $export=Join-Path $logs "$configuration-Export"
 & (Join-Path $root 'tools/package_project.ps1') -ProjectFile (Join-Path $project 'Game.kubo') -RuntimeExecutable $exe -OutputDirectory $export -Configuration $configuration *> (Join-Path $logs "$configuration-package.log")
 $renamed=Join-Path $export 'ParticlePlayer.exe';Copy-Item -LiteralPath (Join-Path $export 'KuboPlayer.exe') -Destination $renamed
 Need ((Get-FileHash -LiteralPath $renamed).Hash -eq (Get-FileHash -LiteralPath $exe).Hash) 'Renamed Player bytes changed'
 foreach($item in $files){Need ((Get-FileHash -LiteralPath (Join-Path $project $item.relative)).Hash -eq $item.sha256) 'Packaging changed Particle authored source';Need ((Get-FileHash -LiteralPath (Join-Path $export $item.relative)).Hash -eq $item.sha256) 'Copied Particle project bytes changed'}
 foreach($filter in @('*.cpp','*.h','*.lib','KuboEditor.exe','KuboMcpServer.exe')){Need (@(Get-ChildItem -LiteralPath $export -Recurse -File -Filter $filter).Count -eq 0) 'Particle export leaked source/Editor/archive'}
 Native $renamed '--particle-export' "$configuration-export"
 foreach($item in $files){Need ((Get-FileHash -LiteralPath (Join-Path $export $item.relative)).Hash -eq $item.sha256) 'Exported Particle runtime changed authored source'}
 foreach($scope in @('startup','prefab')){
  $particle.guid='f'*32
  if($scope -eq 'startup'){Write-Json (Join-Path $project 'entry.scene') $scene}else{Write-Json (Join-Path $project 'Assets/emitter.prefab') $prefab}
  $badOutput=Join-Path $logs "$configuration-Bad-$scope";$refused=$false
  try{& (Join-Path $root 'tools/package_project.ps1') -ProjectFile (Join-Path $project 'Game.kubo') -RuntimeExecutable $exe -OutputDirectory $badOutput -Configuration $configuration *> (Join-Path $logs "$configuration-bad-$scope.log")}catch{$refused=$true;$_|Out-String|Set-Content -LiteralPath (Join-Path $logs "$configuration-bad-$scope.error.log")}
  Need ($refused -and !(Test-Path -LiteralPath $badOutput)) "Missing Particle GUID was exported: $scope"
  $particle.guid='a'*32;Write-Json (Join-Path $project 'entry.scene') $scene;Write-Json (Join-Path $project 'Assets/emitter.prefab') $prefab
 }
 foreach($item in $files){Need ((Get-FileHash -LiteralPath (Join-Path $project $item.relative)).Hash -eq $item.sha256) 'Particle negative-case fixture restoration changed source'}
 Write-Output "PARTICLE_AUTHORING_EXPORT_PASS=$configuration"
}
Write-Output 'PASS: source RuntimeGame and actual renamed exports load Particle World2 startup/Prefab Texture closure, Debug+Release four native runs32 pixel records, source/candidate validation and missing GUID refusal; no SDK consumer/Lua/MCP claim'
