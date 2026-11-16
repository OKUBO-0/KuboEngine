[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$SourceProject,[ValidateSet('Debug','Release')][string]$Configuration='Release')
$ErrorActionPreference='Stop'
if((Get-PSDrive C).Free -lt 4GB){throw 'Prefab Player export requires at least 4 GiB free'}
$prefabPlayerRepo=Split-Path $PSScriptRoot -Parent
$prefabPlayerSource=(Resolve-Path -LiteralPath $SourceProject).Path.TrimEnd('\')
$prefabPlayerRuntime=[IO.Path]::GetFullPath((Join-Path $prefabPlayerRepo "../generated/outputs/$Configuration/KuboPlayer.exe"))
$prefabPlayerRuntimeHash=(Get-FileHash -LiteralPath $prefabPlayerRuntime).Hash
$prefabPlayerLogs=[IO.Path]::GetFullPath((Join-Path $prefabPlayerRepo ('../generated/prefab-export-player/'+[guid]::NewGuid().ToString('N'))))
$prefabPlayerCopy=Join-Path $prefabPlayerLogs 'Source';$prefabPlayerOutput=Join-Path $prefabPlayerLogs 'Export';$prefabPlayerCwd=Join-Path $prefabPlayerLogs 'Unrelated'
New-Item -ItemType Directory -Path $prefabPlayerCopy,$prefabPlayerCwd | Out-Null
Write-Output "PREFAB_EXPORT_PLAYER_LOGS=$prefabPlayerLogs"
$prefabPlayerOriginalHashes=@{}
foreach($file in Get-ChildItem -LiteralPath $prefabPlayerSource -Recurse -File){$prefabPlayerOriginalHashes[$file.FullName]=(Get-FileHash -LiteralPath $file.FullName).Hash}
foreach($name in @('Game.kubo','entry.scene','Assets')){Copy-Item -LiteralPath (Join-Path $prefabPlayerSource $name) -Destination $prefabPlayerCopy -Recurse}
$prefabPlayerInner=Join-Path $prefabPlayerCopy 'Assets/inner.lua'
# An intentional success diagnostic after assertions, not a silent healthy run.
# The hook error is reported by the production --diagnostics path; it must be
# the only root error, alongside the intentionally failing peer.
[IO.File]::AppendAllText($prefabPlayerInner,@'
local update_before_probe=on_update
local observed_updates=0
function on_update(e,dt)
 update_before_probe(e,dt);observed_updates=observed_updates+1
 if observed_updates<10 then return end
 assert(e:id()==4 and scene.entity_count()==5)
 assert(scene.parent(e:id())==1 and scene.parent(5)==4)
 local x,y,z=e:position();assert(x>0 and z>0)
 local sx,sy,sz=scene.scale(e:id());assert(sz==1.25)
 local fx=scene.position(2);assert(fx==0)
 local ready,playing,time=scene.animator_state(e:id())
 assert(ready and playing and time>0 and audio.available())
 error('KUBO_PREFAB_EXPORT_OK',0)
end
'@,[Text.UTF8Encoding]::new($false))
& (Join-Path $prefabPlayerRepo 'tools/package_project.ps1') -ProjectFile (Join-Path $prefabPlayerCopy 'Game.kubo') -RuntimeExecutable $prefabPlayerRuntime -OutputDirectory $prefabPlayerOutput -Configuration $Configuration
if((Get-FileHash -LiteralPath (Join-Path $prefabPlayerOutput 'KuboPlayer.exe')).Hash -ne $prefabPlayerRuntimeHash){throw 'Export is not the normal Player binary'}
foreach($file in Get-ChildItem -LiteralPath $prefabPlayerCopy -Recurse -File){$relative=$file.FullName.Substring($prefabPlayerCopy.Length+1);if((Get-FileHash -LiteralPath $file.FullName).Hash -ne (Get-FileHash -LiteralPath (Join-Path $prefabPlayerOutput $relative)).Hash){throw "Copied project changed: $relative"}}
foreach($excluded in @('KuboEditor.exe','KuboRuntime.lib','tools')){if(Test-Path -LiteralPath (Join-Path $prefabPlayerOutput $excluded)){throw "Generic Player export leaked $excluded"}}
# Both exact paths are freshly owned children. Retire copied source Assets,
# retaining it for diagnosis instead of allowing source-path fallback.
$retireFrom=[IO.Path]::GetFullPath((Join-Path $prefabPlayerCopy 'Assets'));$retireTo=[IO.Path]::GetFullPath((Join-Path $prefabPlayerLogs 'RetiredAssets'))
foreach($path in @($retireFrom,$retireTo)){if(!$path.StartsWith($prefabPlayerLogs+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe private fixture asset retirement'}}
if(((Get-Item -LiteralPath $retireFrom).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){throw 'Linked private fixture'}
Move-Item -LiteralPath $retireFrom -Destination $retireTo
Add-Type -TypeDefinition @'
using System;using System.Text;using System.Runtime.InteropServices;
public static class PrefabExportWindow {
 delegate bool Callback(IntPtr w,IntPtr s);
 [DllImport("user32.dll")]static extern bool EnumWindows(Callback c,IntPtr s);
 [DllImport("user32.dll")]static extern uint GetWindowThreadProcessId(IntPtr w,out uint p);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern int GetClassNameW(IntPtr w,StringBuilder n,int c);
 [DllImport("user32.dll")]static extern bool PostMessageW(IntPtr w,uint m,IntPtr a,IntPtr b);
 public static IntPtr Find(uint id){IntPtr result=IntPtr.Zero;EnumWindows((w,s)=>{uint owner;GetWindowThreadProcessId(w,out owner);if(owner==id){var n=new StringBuilder(128);GetClassNameW(w,n,n.Capacity);if(n.ToString()=="WindowClass"){result=w;return false;}}return true;},IntPtr.Zero);return result;}
 public static bool Close(IntPtr w){return PostMessageW(w,0x0010,IntPtr.Zero,IntPtr.Zero);}
}
'@
$prefabPlayerRenamed=Join-Path $prefabPlayerOutput 'RenamedGenericGame.exe';Copy-Item -LiteralPath (Join-Path $prefabPlayerOutput 'KuboPlayer.exe') -Destination $prefabPlayerRenamed
$nativeLog=Join-Path $prefabPlayerLogs 'native.log';$nativeErr=Join-Path $prefabPlayerLogs 'native.stderr.log';$owned=$null
function Read-PrefabPlayerLog([string]$Path){$stream=[IO.File]::Open($Path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite);$reader=[IO.StreamReader]::new($stream,[Text.Encoding]::UTF8);try{return $reader.ReadToEnd()}finally{$reader.Dispose()}}
try{
 $owned=Start-Process -FilePath $prefabPlayerRenamed -ArgumentList '--diagnostics' -WorkingDirectory $prefabPlayerCwd -WindowStyle Hidden -PassThru -RedirectStandardOutput $nativeLog -RedirectStandardError $nativeErr
 $held=$owned.Handle;$watch=[Diagnostics.Stopwatch]::StartNew();$seen=$false
 while($watch.ElapsedMilliseconds -lt 50000 -and !$owned.HasExited){if((Test-Path -LiteralPath $nativeLog) -and (Read-PrefabPlayerLog $nativeLog).Contains('KUBO_PREFAB_EXPORT_OK')){$seen=$true;break};Start-Sleep -Milliseconds 200}
 if(!$seen){throw "Generic Player Prefab probe marker missing: $nativeLog"}
 $window=[PrefabExportWindow]::Find($owned.Id);if($window -eq [IntPtr]::Zero -or ![PrefabExportWindow]::Close($window)){throw 'Owned generic Player native close failed'}
 if(!$owned.WaitForExit(15000)){throw 'Owned generic Player close timed out'};$owned.WaitForExit();$code=$owned.ExitCode
 if($code -ne 0){throw "Generic Player exit=$code"}
 $faults=@([IO.File]::ReadAllText($nativeLog) -split "`r?`n" | Where-Object {$_ -like 'KUBO_LUA_FAULT*'})
 if($faults.Count -ne 2 -or @($faults | Where-Object {$_ -like 'KUBO_LUA_FAULT entity=4 *KUBO_PREFAB_EXPORT_OK*'}).Count -ne 1 -or @($faults | Where-Object {$_ -like 'KUBO_LUA_FAULT entity=2 *'}).Count -ne 1){throw 'Unexpected generic Player Lua fault set'}
 if([IO.File]::ReadAllText($nativeErr).Length -ne 0){throw 'Unexpected generic Player stderr'}
 if(Test-Path -LiteralPath (Join-Path $prefabPlayerOutput 'Library/Mcp/editor-session.json')){throw 'Generic Player enabled Editor MCP'}
 $faults | Write-Output
}finally{if($owned){if(!$owned.HasExited){Stop-Process -Id $owned.Id;$owned.WaitForExit()};$owned.Dispose()}}
foreach($path in $prefabPlayerOriginalHashes.Keys){if((Get-FileHash -LiteralPath $path).Hash -ne $prefabPlayerOriginalHashes[$path]){throw 'Original SDK consumer project changed'}}
if((Get-FileHash -LiteralPath $prefabPlayerRuntime).Hash -ne $prefabPlayerRuntimeHash){throw 'Original runtime changed'}
[ordered]@{complete=$true;configuration=$Configuration;nativeExitCode=$code;runtimeSha256=$prefabPlayerRuntimeHash;normalPlayer=$true;renamedWithoutProjectArgument=$true;retiredCopiedSource=$true;deliberateProbeFault=$true;unexpectedLuaFaults=0;audioVoicesExplicitlyObserved=$false;gpuInfoQueueCollected=$false} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $prefabPlayerLogs 'complete.json') -Encoding UTF8
Write-Output "PASS: normal $Configuration Player prefab export/Lua Create+Update+Fixed/Animator+Physics/peer isolation/renamed executable/native close0; deliberate probe diagnostic, not Audio voice/GPU/pixel/clean-machine verification"
