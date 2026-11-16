[CmdletBinding()]
param([ValidateSet('Debug','Release')][string]$Configuration='Release')
$ErrorActionPreference='Stop'
function Require($condition,[string]$message) {if(!$condition){throw $message}}
Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class StartupFallbackWindow {
 delegate bool Callback(IntPtr window,IntPtr state);
 [DllImport("user32.dll")] static extern bool EnumWindows(Callback callback,IntPtr state);
 [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window,out uint owner);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetClassNameW(IntPtr window,StringBuilder name,int count);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetWindowTextW(IntPtr window,StringBuilder text,int count);
 [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr window,Callback callback,IntPtr state);
 [DllImport("user32.dll")] static extern bool PostMessageW(IntPtr window,uint message,IntPtr wparam,IntPtr lparam);
 public static IntPtr Find(uint process) {
  IntPtr found=IntPtr.Zero;EnumWindows((window,state)=>{uint owner;GetWindowThreadProcessId(window,out owner);
   if(owner!=process)return true;var name=new StringBuilder(128);GetClassNameW(window,name,name.Capacity);
   if(name.ToString()!="WindowClass")return true;found=window;return false;},IntPtr.Zero);return found;
 }
 public static bool Close(IntPtr window) {return PostMessageW(window,0x10,IntPtr.Zero,IntPtr.Zero);}
 public static string Dialogs(uint process) {
  var output=new StringBuilder();EnumWindows((window,state)=>{uint owner;GetWindowThreadProcessId(window,out owner);if(owner!=process)return true;
   var name=new StringBuilder(128);GetClassNameW(window,name,name.Capacity);if(name.ToString()!="#32770")return true;
   var text=new StringBuilder(2048);GetWindowTextW(window,text,text.Capacity);output.AppendLine(text.ToString());
   EnumChildWindows(window,(child,s)=>{var value=new StringBuilder(2048);GetWindowTextW(child,value,value.Capacity);output.AppendLine(value.ToString());return true;},IntPtr.Zero);return true;},IntPtr.Zero);return output.ToString();
 }
}
'@
$fallbackFixture=Join-Path ([IO.Path]::GetTempPath()) ('kubo-startup-fallback-'+[guid]::NewGuid().ToString('N'))
$fallbackProcess=$null
try {
    $fallbackLabel=-join ([char[]]@(0x65e5,0x672c,0x8a9e))
    $fallbackProject=Join-Path $fallbackFixture ($fallbackLabel+' Project')
    $fallbackWorking=Join-Path $fallbackFixture 'Unrelated'
    New-Item -ItemType Directory -Path (Join-Path $fallbackProject 'Assets'),$fallbackWorking -Force | Out-Null
    $fallbackBuild=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\..\generated\outputs\$Configuration"))
    $fallbackManifest=Join-Path $fallbackProject 'Fallback.kubo'
    foreach($fallbackSetting in @('',($fallbackLabel+'/Missing.scene'))) {
        $fallbackText=@{name='Fallback';engineVersion='0.1.0';assetDirectory='Assets';startupScene=$fallbackSetting} | ConvertTo-Json
        [IO.File]::WriteAllText($fallbackManifest,$fallbackText,[Text.UTF8Encoding]::new($false))
        foreach($fallbackExecutable in @('KuboEditor.exe','KuboPlayer.exe')) {
            Write-Output "Checking $Configuration $fallbackExecutable startupScene='$fallbackSetting'"
            $fallbackProcess=Start-Process -FilePath (Join-Path $fallbackBuild $fallbackExecutable) -ArgumentList ('--project "'+$fallbackManifest+'"') -WorkingDirectory $fallbackWorking -WindowStyle Hidden -PassThru
            $fallbackHandle=$fallbackProcess.Handle;$fallbackWindow=[IntPtr]::Zero
            for($attempt=0;$attempt -lt 100;$attempt++) {
                if($fallbackProcess.HasExited){break}
                $fallbackWindow=[StartupFallbackWindow]::Find($fallbackProcess.Id)
                if($fallbackWindow -ne [IntPtr]::Zero){break}
                Start-Sleep -Milliseconds 100
            }
            Require ($fallbackWindow -ne [IntPtr]::Zero) "Missing engine window: $fallbackExecutable / '$fallbackSetting'"
            Start-Sleep -Milliseconds 1000
            $fallbackDialogs=[StartupFallbackWindow]::Dialogs($fallbackProcess.Id)
            Require ([string]::IsNullOrEmpty($fallbackDialogs)) "Owned fallback startup dialog: $fallbackDialogs"
            Require (!$fallbackProcess.HasExited) 'Fallback application exited during startup'
            Require ([StartupFallbackWindow]::Close($fallbackWindow)) 'Owned fallback native close failed'
            Require ($fallbackProcess.WaitForExit(15000)) "Owned fallback shutdown timed out: $fallbackExecutable startupScene='$fallbackSetting'"
            $fallbackProcess.WaitForExit()
            Require ($fallbackProcess.ExitCode -eq 0) "Fallback process failed: $fallbackExecutable"
            $fallbackProcess.Dispose();$fallbackProcess=$null
        }
    }
    Require (!(Test-Path -LiteralPath (Join-Path $fallbackWorking 'UserScenes'))) 'Fallback startup created Scene data in unrelated cwd'
    Require ([IO.File]::ReadAllText($fallbackManifest) -eq $fallbackText) 'Fallback startup modified its manifest'
    Write-Output "PASS: actual $Configuration Editor/Player empty and missing Unicode startup Scene fallback, unrelated cwd, native close and manifest isolation (not direct UI save or pixel QA)"
} finally {
    if($fallbackProcess) {
        if(!$fallbackProcess.HasExited){Stop-Process -Id $fallbackProcess.Id -Force}
        $fallbackProcess.WaitForExit();$fallbackProcess.Dispose()
    }
    $fallbackResolved=[IO.Path]::GetFullPath($fallbackFixture)
    $fallbackTempRoot=[IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')+'\'
    if(!$fallbackResolved.StartsWith($fallbackTempRoot,[StringComparison]::OrdinalIgnoreCase) -or !(Split-Path -Leaf $fallbackResolved).StartsWith('kubo-startup-fallback-')) {throw 'Unsafe owned startup fixture cleanup'}
    if(Test-Path -LiteralPath $fallbackResolved){Remove-Item -LiteralPath $fallbackResolved -Recurse -Force}
}
