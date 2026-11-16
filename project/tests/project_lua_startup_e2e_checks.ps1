param([Parameter(Mandatory=$true)][string]$EngineDirectory)
$ErrorActionPreference='Stop'
function Require($value,[string]$message){if(!$value){throw $message}}
function Read-LiveLog([string]$path){
 $stream=[IO.File]::Open($path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite)
 $reader=[IO.StreamReader]::new($stream)
 try{return $reader.ReadToEnd()}finally{$reader.Dispose()}
}
Add-Type -TypeDefinition @'
using System;using System.Text;using System.Runtime.InteropServices;
public static class LuaStartupWindow {
 delegate bool Callback(IntPtr w,IntPtr s);
 [DllImport("user32.dll")] static extern bool EnumWindows(Callback c,IntPtr s);
 [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr w,out uint p);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetClassNameW(IntPtr w,StringBuilder n,int c);
 [DllImport("user32.dll")] static extern bool PostMessageW(IntPtr w,uint m,IntPtr a,IntPtr b);
 public static IntPtr Find(uint pid){IntPtr found=IntPtr.Zero;EnumWindows((w,s)=>{uint p;GetWindowThreadProcessId(w,out p);if(p!=pid)return true;var n=new StringBuilder(128);GetClassNameW(w,n,n.Capacity);if(n.ToString()!="WindowClass")return true;found=w;return false;},IntPtr.Zero);return found;}
 public static bool Close(IntPtr w){return PostMessageW(w,0x10,IntPtr.Zero,IntPtr.Zero);}
}
'@
$root=[IO.Path]::GetFullPath($EngineDirectory)
$fixture=Join-Path ([IO.Path]::GetTempPath()) ('kubo-lua-startup-'+[guid]::NewGuid().ToString('N'))
$logRoot=Join-Path $PSScriptRoot '../../generated/cpu-checks'
New-Item -ItemType Directory -Path $logRoot -Force|Out-Null
$logPrefix=Split-Path -Leaf $fixture
$project=Join-Path $fixture (-join ([char[]]@(0x65e5,0x672c,0x8a9e)))
$manifest=Join-Path $project 'Game.kubo';$process=$null;$index=0
function Set-FixtureLimits($bytes,$states,$instructions){
 $value=@{name='LuaStartup';assetDirectory='Assets';startupScene='entry.scene';luaLimits=@{memoryBytes=$bytes;states=$states;frameInstructions=$instructions;extension='preserved'}}|ConvertTo-Json -Depth 5
 [IO.File]::WriteAllText($manifest,$value,[Text.UTF8Encoding]::new($true))
}
function Check-Launch([string]$exe,[string]$projectFile,[string]$expected,[string]$fault,[switch]$AutoProject){
 $script:index++;$out=Join-Path $logRoot ("$logPrefix-run-$script:index.out.log");$err=Join-Path $logRoot ("$logPrefix-run-$script:index.err.log")
 $launchArguments=if($AutoProject){'--diagnostics'}else{'--project "'+$projectFile+'" --diagnostics'}
 $script:process=Start-Process -FilePath $exe -ArgumentList $launchArguments -WorkingDirectory (Join-Path $fixture 'Unrelated') -WindowStyle Hidden -PassThru -RedirectStandardOutput $out -RedirectStandardError $err
 $handle=$script:process.Handle;$window=[IntPtr]::Zero;$watch=[Diagnostics.Stopwatch]::StartNew()
 try{
  while($watch.ElapsedMilliseconds -lt 20000){
   if($script:process.HasExited){throw 'Owned application exited before diagnostics/window verification'}
   $window=[LuaStartupWindow]::Find($script:process.Id)
   $text=if(Test-Path -LiteralPath $out){Read-LiveLog $out}else{''}
   if($window -ne [IntPtr]::Zero -and $text.Contains($expected) -and (!$fault -or $text.Contains($fault))){break}
   Start-Sleep -Milliseconds 100
  }
  Require ($window -ne [IntPtr]::Zero -and $text.Contains($expected) -and (!$fault -or $text.Contains($fault))) "Owned diagnostic timeout: $text"
  if($fault){Start-Sleep -Milliseconds 500}
  Require ([LuaStartupWindow]::Close($window)) 'Owned native close failed'
  Require ($script:process.WaitForExit(15000)) 'Owned native close timed out'
  $script:process.WaitForExit();Require ($script:process.ExitCode -eq 0) 'Owned application failed shutdown'
  $text=[IO.File]::ReadAllText($out)
  Require ($text.Contains($expected)) 'Actual applied limits missing'
  if($fault){
   $faultLines=@($text -split "`r?`n" | Where-Object {$_.Contains($fault)})
   Require ($faultLines.Count -gt 0) 'Fault missing'
   foreach($group in @($faultLines | Group-Object)){Require ($group.Count -eq 1) 'Identical entity fault repeated across frames'}
  }
  Require ([IO.File]::ReadAllText($err).Length -eq 0) 'Application stderr not empty'
 }finally{if($script:process -and !$script:process.HasExited){Stop-Process -Id $script:process.Id;$script:process.WaitForExit()};if($script:process){$script:process.Dispose();$script:process=$null}}
}
try{
 New-Item -ItemType Directory -Path (Join-Path $project 'Assets'),(Join-Path $fixture 'Unrelated') -Force|Out-Null
 [IO.File]::WriteAllText((Join-Path $project 'entry.scene'),'{"version":1,"entities":[{"id":1,"parent":0,"name":"First","active":true,"transform":{"position":[0,0,0],"rotation":[0,0,0],"scale":[1,1,1]},"model":"","script":{"asset":"Assets/test.lua","enabled":true}},{"id":2,"parent":0,"name":"Second","active":true,"transform":{"position":[0,0,0],"rotation":[0,0,0],"scale":[1,1,1]},"model":"","script":{"asset":"Assets/test.lua","enabled":true}}]}')
 [IO.File]::WriteAllText((Join-Path $project 'Assets/test.lua'),'function on_update(e,dt) end')
 Set-FixtureLimits 4194304 1 5000
 $digest=(Get-FileHash -LiteralPath $manifest).Hash
 Check-Launch (Join-Path $root 'KuboEditor.exe') $manifest 'KUBO_LUA_LIMITS memoryBytes=4194304 states=1 frameInstructions=5000' ''
 Require ((Get-FileHash -LiteralPath $manifest).Hash -eq $digest) 'Editor startup rewrote manifest'
 Set-FixtureLimits 3145728 2 7000
 Check-Launch (Join-Path $root 'KuboEditor.exe') $manifest 'KUBO_LUA_LIMITS memoryBytes=3145728 states=2 frameInstructions=7000' ''
 Set-FixtureLimits 4194304 1 5000
 Check-Launch (Join-Path $root 'KuboPlayer.exe') $manifest 'KUBO_LUA_LIMITS memoryBytes=4194304 states=1 frameInstructions=5000' 'KUBO_LUA_FAULT entity=2 error=Lua runtime VM budget exceeded'
 $export=Join-Path $fixture 'Export'
 & (Join-Path $root 'tools/package_project.ps1') -ProjectFile $manifest -OutputDirectory $export -RuntimeExecutable (Join-Path $root 'KuboPlayer.exe')
 Require ((Get-FileHash -LiteralPath $manifest).Hash -eq (Get-FileHash -LiteralPath (Join-Path $export 'Game.kubo')).Hash) 'Export changed saved limits/BOM/extension'
 Check-Launch (Join-Path $export 'KuboPlayer.exe') '' 'KUBO_LUA_LIMITS memoryBytes=4194304 states=1 frameInstructions=5000' 'KUBO_LUA_FAULT entity=2 error=Lua runtime VM budget exceeded' -AutoProject
 Set-FixtureLimits 4194304 8 100
 [IO.File]::WriteAllText((Join-Path $project 'Assets/test.lua'),'function on_update(e,dt) e:translate(7,0,0);local n=0;for i=1,10000 do n=n+1 end end')
 Check-Launch (Join-Path $root 'KuboPlayer.exe') $manifest 'KUBO_LUA_LIMITS memoryBytes=4194304 states=8 frameInstructions=100' 'Lua frame instruction budget exceeded'
 Write-Output 'PASS: real Application Editor configured limits/restart, Player VM/frame quota refusal, bounded diagnostics and no-project exported Player with exact manifest preservation (not physical UI/pixel/crash/clean-machine QA)'
}finally{
 if($process -and !$process.HasExited){Stop-Process -Id $process.Id;$process.WaitForExit()}
 $resolved=[IO.Path]::GetFullPath($fixture);if(!$resolved.StartsWith([IO.Path]::GetFullPath([IO.Path]::GetTempPath()),[StringComparison]::OrdinalIgnoreCase) -or !(Split-Path -Leaf $resolved).StartsWith('kubo-lua-startup-')){throw 'Unsafe fixture cleanup'}
 if(Test-Path -LiteralPath $resolved){Remove-Item -LiteralPath $resolved -Recurse -Force}
}
