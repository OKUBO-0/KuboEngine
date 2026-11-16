[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$ProjectFile,[ValidateSet('Debug','Release')][string]$Configuration='Debug')
$ErrorActionPreference='Stop'
$existingPrefabManifest=(Resolve-Path -LiteralPath $ProjectFile).Path
$existingPrefabProject=Split-Path $existingPrefabManifest -Parent
$existingPrefabRepo=Split-Path $PSScriptRoot -Parent
$existingPrefabRuntime=[IO.Path]::GetFullPath((Join-Path $existingPrefabRepo "../generated/outputs/$Configuration/KuboPlayer.exe"))
$existingPrefabRuntimeHash=(Get-FileHash -LiteralPath $existingPrefabRuntime).Hash
$existingPrefabHashes=@{}
foreach($file in Get-ChildItem -LiteralPath $existingPrefabProject -Recurse -File){$existingPrefabHashes[$file.FullName]=(Get-FileHash -LiteralPath $file.FullName).Hash}
$existingPrefabLogs=[IO.Path]::GetFullPath((Join-Path $existingPrefabRepo ('../generated/prefab-existing-player/'+[guid]::NewGuid().ToString('N'))))
New-Item -ItemType Directory -Path $existingPrefabLogs | Out-Null
Write-Output "PREFAB_EXISTING_PLAYER_LOGS=$existingPrefabLogs"
Add-Type -TypeDefinition @'
using System;using System.Text;using System.Runtime.InteropServices;
public static class ExistingPrefabWindow {
 delegate bool Callback(IntPtr w,IntPtr s);
 [DllImport("user32.dll")]static extern bool EnumWindows(Callback c,IntPtr s);
 [DllImport("user32.dll")]static extern uint GetWindowThreadProcessId(IntPtr w,out uint p);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern int GetClassNameW(IntPtr w,StringBuilder n,int c);
 [DllImport("user32.dll")]static extern bool PostMessageW(IntPtr w,uint m,IntPtr a,IntPtr b);
 public static IntPtr Find(uint id){IntPtr result=IntPtr.Zero;EnumWindows((w,s)=>{uint owner;GetWindowThreadProcessId(w,out owner);if(owner==id){var n=new StringBuilder(128);GetClassNameW(w,n,n.Capacity);if(n.ToString()=="WindowClass"){result=w;return false;}}return true;},IntPtr.Zero);return result;}
 public static bool Close(IntPtr w){return PostMessageW(w,0x0010,IntPtr.Zero,IntPtr.Zero);}
}
'@
function Read-ExistingPrefabLog([string]$Path){$stream=[IO.File]::Open($Path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite);$reader=[IO.StreamReader]::new($stream,[Text.Encoding]::UTF8);try{return $reader.ReadToEnd()}finally{$reader.Dispose()}}
$existingPrefabOut=Join-Path $existingPrefabLogs 'native.log';$existingPrefabErr=Join-Path $existingPrefabLogs 'native.stderr.log';$existingPrefabChild=$null
try{
 $existingPrefabChild=Start-Process -FilePath $existingPrefabRuntime -ArgumentList @('--diagnostics','--project',('"'+$existingPrefabManifest+'"')) -WorkingDirectory $existingPrefabLogs -WindowStyle Hidden -PassThru -RedirectStandardOutput $existingPrefabOut -RedirectStandardError $existingPrefabErr
 $held=$existingPrefabChild.Handle;$watch=[Diagnostics.Stopwatch]::StartNew();$seen=$false
 while($watch.ElapsedMilliseconds -lt 50000 -and !$existingPrefabChild.HasExited){if((Test-Path -LiteralPath $existingPrefabOut) -and (Read-ExistingPrefabLog $existingPrefabOut).Contains('KUBO_PREFAB_EXPORT_OK')){$seen=$true;break};Start-Sleep -Milliseconds 200}
 if(!$seen){throw 'Existing-project Prefab probe marker absent'}
 $window=[ExistingPrefabWindow]::Find($existingPrefabChild.Id)
 if($window -eq [IntPtr]::Zero -or ![ExistingPrefabWindow]::Close($window) -or !$existingPrefabChild.WaitForExit(15000)){throw 'Owned existing-project Player native close failed'}
 $existingPrefabChild.WaitForExit();$exit=$existingPrefabChild.ExitCode;if($exit -ne 0){throw "Owned Player exit=$exit"}
 $faults=@([IO.File]::ReadAllText($existingPrefabOut) -split "`r?`n" | Where-Object {$_ -like 'KUBO_LUA_FAULT*'})
 if($faults.Count -ne 2 -or @($faults | Where-Object {$_ -like 'KUBO_LUA_FAULT entity=4 *KUBO_PREFAB_EXPORT_OK*'}).Count -ne 1 -or @($faults | Where-Object {$_ -like 'KUBO_LUA_FAULT entity=2 *'}).Count -ne 1){throw 'Unexpected existing-project Lua fault set'}
 if([IO.File]::ReadAllText($existingPrefabErr).Length){throw 'Unexpected native stderr'}
 $faults | Write-Output
}finally{if($existingPrefabChild){if(!$existingPrefabChild.HasExited){Stop-Process -Id $existingPrefabChild.Id;$existingPrefabChild.WaitForExit()};$existingPrefabChild.Dispose()}}
foreach($file in $existingPrefabHashes.Keys){if((Get-FileHash -LiteralPath $file).Hash -ne $existingPrefabHashes[$file]){throw "Existing project bytes changed: $file"}}
if((Get-FileHash -LiteralPath $existingPrefabRuntime).Hash -ne $existingPrefabRuntimeHash){throw 'Production Player bytes changed'}
[ordered]@{complete=$true;configuration=$Configuration;nativeExitCode=$exit;runtimeSha256=$existingPrefabRuntimeHash;explicitProjectArgument=$true;newExportCreated=$false;projectFilesChecked=$existingPrefabHashes.Count;gpuInfoQueueCollected=$false;fullDistributionCoverage=$false} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $existingPrefabLogs 'complete.json') -Encoding UTF8
Write-Output "PASS: actual $Configuration Player existing Prefab project/probe+peer/native close0/read-only existing-file hashes; not a new or relocated Debug export"
