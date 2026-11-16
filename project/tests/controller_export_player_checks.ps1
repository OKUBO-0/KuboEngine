[CmdletBinding()]
param([string]$RuntimeExecutable,[string]$EngineDirectory,[switch]$IncludeZeroDuration)
$ErrorActionPreference='Stop'
$controllerPlayerProject=Split-Path $PSScriptRoot -Parent
$controllerPlayerPackageScript=Join-Path $controllerPlayerProject 'tools/package_project.ps1'
$controllerPlayerEngineHashes=@{}
if($EngineDirectory){
 $controllerPlayerEngine=(Resolve-Path -LiteralPath $EngineDirectory).Path.TrimEnd('\')
 $RuntimeExecutable=Join-Path $controllerPlayerEngine 'KuboPlayer.exe'
 $controllerPlayerPackageScript=Join-Path $controllerPlayerEngine 'tools/package_project.ps1'
 Get-ChildItem -LiteralPath $controllerPlayerEngine -Recurse -File | ForEach-Object {$controllerPlayerEngineHashes[$_.FullName]=(Get-FileHash -LiteralPath $_.FullName).Hash}
}
if(!$RuntimeExecutable){$RuntimeExecutable=Join-Path $controllerPlayerProject '../generated/outputs/Release/KuboPlayer.exe'}
$controllerPlayerRuntime=(Resolve-Path -LiteralPath $RuntimeExecutable).Path
$controllerPlayerRuntimeHash=(Get-FileHash -LiteralPath $controllerPlayerRuntime).Hash
$controllerPlayerRuntimeRoot=Split-Path $controllerPlayerRuntime -Parent
$controllerPlayerFixture=[IO.Path]::GetFullPath((Join-Path $controllerPlayerProject ('../generated/controller-export-player/'+[guid]::NewGuid().ToString('N'))))
New-Item -ItemType Directory -Path $controllerPlayerFixture | Out-Null
Write-Output "CONTROLLER_EXPORT_PLAYER_LOGS=$controllerPlayerFixture"
function Require($value,[string]$why){if(!$value){throw $why}}
function Write-ControllerPlayer([string]$path,[string]$text){[IO.File]::WriteAllText($path,$text,[Text.UTF8Encoding]::new($false))}
function Read-ControllerPlayerLog([string]$path){
 $stream=[IO.File]::Open($path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite)
 $reader=[IO.StreamReader]::new($stream,[Text.Encoding]::UTF8)
 try{return $reader.ReadToEnd()}finally{$reader.Dispose()}
}
Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class ControllerExportPlayerWindow {
 delegate bool Callback(IntPtr w,IntPtr s);
 [DllImport("user32.dll")] static extern bool EnumWindows(Callback c,IntPtr s);
 [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr w,out uint p);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetClassNameW(IntPtr w,StringBuilder n,int c);
 [DllImport("user32.dll")] static extern bool PostMessageW(IntPtr w,uint m,IntPtr a,IntPtr b);
 public static IntPtr Find(uint pid){IntPtr result=IntPtr.Zero;EnumWindows((w,s)=>{uint p;GetWindowThreadProcessId(w,out p);if(p!=pid)return true;var n=new StringBuilder(128);GetClassNameW(w,n,n.Capacity);if(n.ToString()=="WindowClass"){result=w;return false;}return true;},IntPtr.Zero);return result;}
 public static bool Close(IntPtr w){return PostMessageW(w,0x10,IntPtr.Zero,IntPtr.Zero);}
}
'@
$controllerPlayerHealthy=@'
local stage,previous,previousDt,fixed=0,0,0,0
local function queued(e,command,r,p,t)
 assert(scene[command](e:id()),'Controller command admission')
 local a,b,c=scene.animator_state(e:id());assert(a==r and b==p and c==t,'Controller predicted pending state')
end
function on_create(e)
 assert(not scene.animator_state(e:id()),'Create Controller already bound')
 assert(scene.controller_state(e:id())==nil,'Create Controller fabricated state')
 assert(scene.controller_set_float(e:id(),'speed',1))
 assert(scene.controller_set_bool(e:id(),'enabled',true))
 assert(scene.controller_trigger(e:id(),'go'))
 assert(scene.animator_restart(e:id()))
end
function on_fixed_update(e,dt) fixed=fixed+1 end
function on_update(e,dt)
 local r,p,t=scene.animator_state(e:id())
 if not r then assert(stage==0,'Controller binding lost');previousDt=dt;return end
 assert(scene.controller_state(e:id())=='Run','Controller query did not sample actual transition')
 assert(scene.position(assert(scene.find('Failed Controller peer')))==0,'Failed peer scalar leaked')
 stage=stage+1
 assert(scene.controller_state(e:id(),1)==((stage==1 or stage>=5) and 'Idle' or 'Cutscene'),'Layer force state/failed-peer isolation lost')
 local lt=scene.controller_time(e:id(),1);local lp=scene.controller_progress(e:id(),1);local ln=scene.controller_normalized_time(e:id(),1)
 assert(lt>=0 and lp>=0 and ln>=0 and ln<=1,'Layer actual numeric reader unavailable')
 assert(scene.controller_layer_weight(e:id(),1)==(stage==1 and 0.25 or 0.75),'Runtime layer weight lost')
 if stage==1 then
  -- Typed queued parameters/trigger admit Run speed4 from entry speed2. Multiplier .5
  -- makes the real clock 2*dt, unlike legacy clip .5 or entry-only speed1.
  assert(p and t>0 and math.abs(t-previousDt*2)<0.00001,'Controller state speed/transition not applied')
  assert(scene.controller_set_layer_weight(e:id(),1,0.75));assert(scene.controller_play_state(e:id(),'Cutscene',0.04,1))
  assert(scene.controller_state(e:id(),1)=='Idle' and scene.controller_layer_weight(e:id(),1)==0.25,'Pending layer force/weight predicted')
  queued(e,'animator_pause',r,p,t)
 elseif stage==2 then
  assert(not p and t==previous,'Controller Pause not held');queued(e,'animator_stop',r,p,t)
 elseif stage==3 then
  assert(not p and t==0,'Controller Stop did not reset clock');queued(e,'animator_play',r,p,t)
 elseif stage==4 then
  assert(p and math.abs(t-previousDt*2)<0.00001,'Controller Play lost state speed');queued(e,'animator_restart',r,p,t)
  assert(scene.controller_play_state(e:id(),'Idle',0,1));assert(scene.controller_state(e:id(),1)=='Cutscene','Pending reentry predicted')
 elseif stage==5 then
  assert(p and math.abs(t-previousDt*2)<0.00001,'Controller Restart lost active state/rate');queued(e,'animator_pause',r,p,t)
 else
  assert(not p and t==previous,'Controller final Pause advanced')
  if fixed>0 then error('KUBO_CONTROLLER_EXPORT_OK fixed='..fixed,0) end
 end
 previous=t;previousDt=dt
end
'@
$controllerPlayerUnavailable=@'
local updates,fixed=0,0
function on_create(e) assert(not scene.animator_state(e:id()));assert(scene.animator_restart(e:id())) end
function on_fixed_update(e,dt) fixed=fixed+1 end
function on_update(e,dt)
 local r,p,t=scene.animator_state(e:id());assert(not r and not p and t==nil,'Invalid Controller fell back to legacy clip')
 assert(scene.controller_state(e:id())==nil,'Invalid Controller fabricated state')
 assert(scene.animator_play(e:id()));updates=updates+1
 if updates>=8 and fixed>0 then error('KUBO_CONTROLLER_EXPORT_UNAVAILABLE_OK fixed='..fixed,0) end
end
'@
$controllerPlayerZero=@'
local stage=0
local fixed=0
function on_create(e)
 assert(scene.controller_set_float(e:id(),'speed',1))
 assert(scene.controller_set_bool(e:id(),'enabled',true))
 assert(scene.controller_trigger(e:id(),'go'))
end
function on_fixed_update(e,dt) fixed=fixed+1 end
function on_update(e,dt)
 local r,p,t=scene.animator_state(e:id())
 if not r then assert(stage==0);return end
 local state=scene.controller_state(e:id())
 assert(t==0,'Zero duration manufactured seconds')
 assert(scene.position(assert(scene.find('Failed Controller peer')))==0,'Failed peer scalar leaked')
 if stage==0 then
  assert(state=='Idle' and not p,'Paused zero duration completed')
  if fixed>0 then assert(scene.animator_play(e:id()));stage=1 end
 elseif stage==1 then
  assert(p,'Zero duration Play refused')
  if state=='Run' then assert(scene.animator_pause(e:id()));stage=2 else assert(state=='Idle') end
 elseif stage==2 then
  assert(state=='Run' and not p,'Zero duration Pause/exit state lost')
  error('KUBO_CONTROLLER_EXPORT_ZERO_OK fixed='..fixed,0)
 end
end
'@
$controllerPlayerModes=@('healthy','missing-clip')
if($IncludeZeroDuration){$controllerPlayerModes+=@('zero-clamp','zero-loop')}
foreach($controllerPlayerMode in $controllerPlayerModes) {
 $controllerPlayerCase=Join-Path $controllerPlayerFixture $controllerPlayerMode
 $controllerPlayerSource=Join-Path $controllerPlayerCase 'Source'
 $controllerPlayerAssets=Join-Path $controllerPlayerSource 'Assets'
 $controllerPlayerUnrelated=Join-Path $controllerPlayerCase 'Unrelated'
 $controllerPlayerOutput=Join-Path $controllerPlayerCase 'Export'
 New-Item -ItemType Directory -Path $controllerPlayerAssets,$controllerPlayerUnrelated | Out-Null
 Copy-Item -LiteralPath (Join-Path $controllerPlayerProject 'Resources/models/walk') -Destination (Join-Path $controllerPlayerAssets 'models') -Recurse
 Copy-Item -LiteralPath (Join-Path $controllerPlayerRuntimeRoot 'Resources/textures/common/white.png') -Destination (Join-Path $controllerPlayerAssets 'models/white.png')
 if($controllerPlayerMode.StartsWith('zero-')){
  # A private glTF fixture: keep geometry/rig/buffer layout, reduce only
  # animation channels to one key at t=0. Never mutate the repository model.
  $zeroModelFile=Join-Path $controllerPlayerAssets 'models/walk.gltf'
  $zeroModel=Get-Content -LiteralPath $zeroModelFile -Raw -Encoding UTF8 | ConvertFrom-Json
  Require ($zeroModel.buffers.Count -eq 1 -and $zeroModel.buffers[0].uri -eq 'walk.bin') 'Unexpected zero-duration fixture buffer'
  $zeroBufferFile=Join-Path $controllerPlayerAssets 'models/walk.bin'
  $zeroBuffer=[IO.File]::ReadAllBytes($zeroBufferFile)
  foreach($zeroAnimation in $zeroModel.animations){foreach($zeroSampler in $zeroAnimation.samplers){
   Require (!$zeroSampler.interpolation -or $zeroSampler.interpolation -in @('LINEAR','STEP')) 'Unexpected zero-duration interpolation'
   $zeroInput=$zeroModel.accessors[$zeroSampler.input];$zeroOutput=$zeroModel.accessors[$zeroSampler.output]
   Require ($zeroInput.componentType -eq 5126 -and $zeroInput.type -eq 'SCALAR') 'Unexpected time accessor'
   $zeroOffset=[int]$zeroModel.bufferViews[$zeroInput.bufferView].byteOffset+[int]$zeroInput.byteOffset
   Require ($zeroOffset -ge 0 -and $zeroOffset+4 -le $zeroBuffer.Length) 'Time accessor escapes fixture buffer'
   [Array]::Copy([BitConverter]::GetBytes([single]0),0,$zeroBuffer,$zeroOffset,4)
   $zeroInput.count=1;$zeroInput.min=@(0);$zeroInput.max=@(0);$zeroOutput.count=1
   $zeroOutput.PSObject.Properties.Remove('min');$zeroOutput.PSObject.Properties.Remove('max')
  }}
  [IO.File]::WriteAllBytes($zeroBufferFile,$zeroBuffer)
  Write-ControllerPlayer $zeroModelFile ($zeroModel | ConvertTo-Json -Depth 64)
 }
 $controllerPlayerManifest=Join-Path $controllerPlayerSource 'entry.kubo'
 Write-ControllerPlayer $controllerPlayerManifest '{"name":"Controller Export Player","startupScene":"entry.scene","assetDirectory":"Assets"}'
 $controllerPlayerGuid='cccccccccccccccccccccccccccccccc'
 $controllerPlayerClip=if($controllerPlayerMode -eq 'missing-clip'){'missing-controller-model-clip'}else{'default'}
 $controllerPlayerDefinition=@{format='kubo-animator-controller-1';parameters=@(@{name='speed';type='float';default=0},@{name='enabled';type='bool';default=$false},@{name='go';type='trigger';default=$false});layers=@(@{name='Base';mode='override';weight=1;mask=@();entry='Idle';states=@(@{name='Idle';clip='default';speed=2;loop=$false},@{name='Run';clip=$controllerPlayerClip;speed=4;loop=$false});transitions=@(@{from='Idle';to='Run';blendSeconds=0.05;exitTime=$null;reenter=$false;conditions=@(@{parameter='speed';op='gt';value=0.5},@{parameter='enabled';op='eq';value=$true},@{parameter='go';op='set';value=$true})},@{from='Run';to='Idle';blendSeconds=0.05;exitTime=$null;reenter=$false;conditions=@(@{parameter='enabled';op='eq';value=$false})})})}
 if($controllerPlayerMode.StartsWith('zero-')){
  $controllerPlayerDefinition.layers[0].states[0].loop=($controllerPlayerMode -eq 'zero-loop')
  $controllerPlayerDefinition.layers[0].transitions[0].exitTime=0.75
 }
 if($controllerPlayerMode -eq 'healthy'){
  $controllerPlayerDefinition.layers+=@{name='Export overlay';mode='override';weight=0.25;mask=@();entry='Idle';states=@(@{name='Idle';clip='default';speed=1;loop=$true},@{name='Cutscene';clip='default';speed=2;loop=$true});transitions=@()}
 }
 Write-ControllerPlayer (Join-Path $controllerPlayerAssets 'locomotion.animfsm') ($controllerPlayerDefinition | ConvertTo-Json -Depth 12)
 Write-ControllerPlayer (Join-Path $controllerPlayerAssets 'locomotion.animfsm.kubo-meta.json') ('{"version":1,"guid":"'+$controllerPlayerGuid+'"}')
 $controllerPlayerScene=@{version=1;entities=@(@{id=1;name='Export Controller';active=$true;parent=0;model='Assets/models/walk.gltf';animator=@{clip='default';controllerGuid=$controllerPlayerGuid;speed=0.5;loop=$true;playOnAwake=$false};script=@{asset='Assets/probe.lua';enabled=$true};transform=@{position=@(0,0,0);rotation=@(0,0,0);scale=@(1,1,1)}})}
 $controllerPlayerScene.entities+=@{id=2;name='Failed Controller peer';active=$true;parent=0;model='';script=@{asset='Assets/fault.lua';enabled=$true};transform=@{position=@(0,0,0);rotation=@(0,0,0);scale=@(1,1,1)}}
 Write-ControllerPlayer (Join-Path $controllerPlayerAssets 'fault.lua') "function on_update(e,dt) local id=assert(scene.find('Export Controller'));assert(scene.controller_set_bool(id,'enabled',false));assert(scene.controller_play_state(id,'Cutscene',0,1));e:translate(99,0,0);error('KUBO_CONTROLLER_PEER_DISCARD',0) end"
 Write-ControllerPlayer (Join-Path $controllerPlayerSource 'entry.scene') ($controllerPlayerScene | ConvertTo-Json -Depth 12)
 Write-ControllerPlayer (Join-Path $controllerPlayerAssets 'probe.lua') $(if($controllerPlayerMode.StartsWith('zero-')){$controllerPlayerZero}elseif($controllerPlayerMode -eq 'healthy'){$controllerPlayerHealthy}else{$controllerPlayerUnavailable})
 & $controllerPlayerPackageScript -ProjectFile $controllerPlayerManifest -OutputDirectory $controllerPlayerOutput -RuntimeExecutable $controllerPlayerRuntime -Configuration Release
 Require ((Get-FileHash -LiteralPath (Join-Path $controllerPlayerOutput 'KuboPlayer.exe')).Hash -eq $controllerPlayerRuntimeHash) 'Packaged Player bytes changed'
 foreach($controllerPlayerInput in Get-ChildItem -LiteralPath $controllerPlayerSource -Recurse -File) {
  $relative=$controllerPlayerInput.FullName.Substring($controllerPlayerSource.Length+1)
  Require ((Get-FileHash -LiteralPath (Join-Path $controllerPlayerOutput $relative)).Hash -eq (Get-FileHash -LiteralPath $controllerPlayerInput.FullName).Hash) "Copied Controller fixture differs: $relative"
 }
 $controllerPlayerRetired=Join-Path $controllerPlayerCase 'RetiredAssets'
 foreach($controllerPlayerMovePath in @($controllerPlayerAssets,$controllerPlayerRetired)) {
  Require ([IO.Path]::GetFullPath($controllerPlayerMovePath).StartsWith($controllerPlayerFixture+'\',[StringComparison]::OrdinalIgnoreCase)) 'Move escapes owned fixture'
 }
 Require (((Get-Item -LiteralPath $controllerPlayerAssets).Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) 'Owned assets linked'
 Move-Item -LiteralPath $controllerPlayerAssets -Destination $controllerPlayerRetired
 Require (!(Test-Path -LiteralPath $controllerPlayerAssets)) 'Original assets still reachable'
 # Explicit --project must remain authoritative even with a second manifest.
 Write-ControllerPlayer (Join-Path $controllerPlayerOutput 'Other.kubo') '{"name":"Not selected","assetDirectory":"OtherAssets","startupScene":"other.scene"}'
 $controllerPlayerLog=Join-Path $controllerPlayerCase 'native.log'
 $controllerPlayerError=Join-Path $controllerPlayerCase 'native.stderr.log'
 $controllerPlayerMarker=if($controllerPlayerMode.StartsWith('zero-')){'KUBO_CONTROLLER_EXPORT_ZERO_OK fixed='}elseif($controllerPlayerMode -eq 'healthy'){'KUBO_CONTROLLER_EXPORT_OK fixed='}else{'KUBO_CONTROLLER_EXPORT_UNAVAILABLE_OK fixed='}
 $controllerPlayerChild=$null
 try {
  $controllerPlayerSelected=Join-Path $controllerPlayerOutput 'entry.kubo'
  $controllerPlayerChild=Start-Process -FilePath (Join-Path $controllerPlayerOutput 'KuboPlayer.exe') -ArgumentList @('--diagnostics','--project',('"'+$controllerPlayerSelected+'"')) -WorkingDirectory $controllerPlayerUnrelated -WindowStyle Hidden -PassThru -RedirectStandardOutput $controllerPlayerLog -RedirectStandardError $controllerPlayerError
  $controllerPlayerHandle=$controllerPlayerChild.Handle
  $controllerPlayerWatch=[Diagnostics.Stopwatch]::StartNew();$controllerPlayerObserved=$false
  while($controllerPlayerWatch.ElapsedMilliseconds -lt 50000 -and !$controllerPlayerChild.HasExited) {
   if((Test-Path -LiteralPath $controllerPlayerLog) -and (Read-ControllerPlayerLog $controllerPlayerLog).Contains($controllerPlayerMarker)){$controllerPlayerObserved=$true;break}
   Start-Sleep -Milliseconds 200
  }
  Require $controllerPlayerObserved "Export Controller marker absent: $controllerPlayerMode; inspect $controllerPlayerLog"
  $controllerPlayerWindow=[ControllerExportPlayerWindow]::Find($controllerPlayerChild.Id)
  Require ($controllerPlayerWindow -ne [IntPtr]::Zero -and [ControllerExportPlayerWindow]::Close($controllerPlayerWindow)) 'Owned native close failed'
  Require ($controllerPlayerChild.WaitForExit(15000)) 'Owned native close timed out'
  $controllerPlayerChild.WaitForExit();Require ($controllerPlayerChild.ExitCode -eq 0) 'Owned native exit not0'
  $controllerPlayerFaults=@([IO.File]::ReadAllText($controllerPlayerLog) -split "`r?`n" | Where-Object {$_ -like 'KUBO_LUA_FAULT*'})
  $controllerPlayerSuccessFaults=@($controllerPlayerFaults | Where-Object {$_.Contains($controllerPlayerMarker)})
  $controllerPlayerPeerFaults=@($controllerPlayerFaults | Where-Object {$_.Contains('KUBO_CONTROLLER_PEER_DISCARD')})
  Require ($controllerPlayerFaults.Count -eq 2 -and $controllerPlayerSuccessFaults.Count -eq 1 -and $controllerPlayerPeerFaults.Count -eq 1) 'Unexpected Lua fault besides deliberate success/failed-peer markers'
  Require ([IO.File]::ReadAllText($controllerPlayerError).Length -eq 0) 'Unexpected native stderr'
  Write-Output $controllerPlayerFaults
  Write-Output "CONTROLLER_EXPORT_PLAYER_MODE=$controllerPlayerMode NATIVE_EXIT=$($controllerPlayerChild.ExitCode)"
 }finally{if($controllerPlayerChild){if(!$controllerPlayerChild.HasExited){Stop-Process -Id $controllerPlayerChild.Id;$controllerPlayerChild.WaitForExit()};$controllerPlayerChild.Dispose()}}
}
Require ((Get-FileHash -LiteralPath $controllerPlayerRuntime).Hash -eq $controllerPlayerRuntimeHash) 'Source runtime bytes changed'
if($EngineDirectory){
 $controllerPlayerEngineAfter=@(Get-ChildItem -LiteralPath $controllerPlayerEngine -Recurse -File)
 Require ($controllerPlayerEngineAfter.Count -eq $controllerPlayerEngineHashes.Count) 'Portable inventory changed'
 foreach($controllerPlayerEngineFile in $controllerPlayerEngineAfter){Require ($controllerPlayerEngineHashes.ContainsKey($controllerPlayerEngineFile.FullName) -and (Get-FileHash -LiteralPath $controllerPlayerEngineFile.FullName).Hash -eq $controllerPlayerEngineHashes[$controllerPlayerEngineFile.FullName]) 'Portable bytes changed'}
 Write-Output "CONTROLLER_EXPORT_PORTABLE_DIGESTS_UNCHANGED=$($controllerPlayerEngineAfter.Count)"
}
Write-Output "CONTROLLER_EXPORT_PLAYER_CASES=$($controllerPlayerModes.Count) ZERO_DURATION_ENABLED=$($IncludeZeroDuration.IsPresent)"
Write-Output 'PASS: real shipped Player Controller Lua float/bool/trigger -> conditional state transition, failed-peer parameter+scalar isolation, GUID/query/state-speed/queued playback/fixed and missing-clip refusal; retired source assets/unrelated cwd/native close0, deliberate diagnostic markers, not GPU/pixel/clean-machine/full API coverage'
