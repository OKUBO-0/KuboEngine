param([Parameter(Mandatory=$true)][string]$EngineDirectory)
$ErrorActionPreference='Stop'
function Require($value,[string]$message){if(!$value){throw $message}}
Add-Type -TypeDefinition @'
using System;using System.Text;using System.Runtime.InteropServices;
public static class PackageHubWindow {
 delegate bool Callback(IntPtr w,IntPtr state);
 [DllImport("user32.dll")]static extern bool EnumWindows(Callback callback,IntPtr state);
 [DllImport("user32.dll")]static extern uint GetWindowThreadProcessId(IntPtr w,out uint pid);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern int GetClassNameW(IntPtr w,StringBuilder text,int length);
 [DllImport("user32.dll")]static extern IntPtr GetDlgItem(IntPtr w,int id);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern IntPtr SendMessageTimeoutW(IntPtr w,uint msg,IntPtr a,StringBuilder b,uint flags,uint timeout,out IntPtr result);
 [DllImport("user32.dll")]static extern bool PostMessageW(IntPtr w,uint msg,IntPtr a,IntPtr b);
 [DllImport("user32.dll",EntryPoint="GetWindowLongPtrW")]static extern IntPtr GetWindowLongPtrW(IntPtr w,int index);
 public static IntPtr Find(uint pid){IntPtr found=IntPtr.Zero;EnumWindows((w,s)=>{uint owner;GetWindowThreadProcessId(w,out owner);if(owner!=pid)return true;var name=new StringBuilder(128);GetClassNameW(w,name,name.Capacity);if(name.ToString()!="KuboLauncherWindow")return true;found=w;return false;},IntPtr.Zero);return found;}
 public static string Details(IntPtr window){var text=new StringBuilder(2048);IntPtr result;if(SendMessageTimeoutW(GetDlgItem(window,113),0xD,new IntPtr(text.Capacity),text,2,1000,out result)==IntPtr.Zero)throw new Exception("Owned result control did not respond");return text.ToString();}
 public static bool ReadOnly(IntPtr window){var control=GetDlgItem(window,113);return control!=IntPtr.Zero && (GetWindowLongPtrW(control,-16).ToInt64()&0x800)!=0;}
 public static bool Close(IntPtr window){return PostMessageW(window,0x10,IntPtr.Zero,IntPtr.Zero);}
}
'@
$engine=(Resolve-Path -LiteralPath $EngineDirectory).Path
$launcher=Join-Path $engine 'KuboLauncher.exe';$manifest=Join-Path $engine 'engine-package.json'
$fixture=Join-Path ([IO.Path]::GetTempPath()) ('kubo-hub-package-e2e-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fixture | Out-Null
$before=@(Get-ChildItem -LiteralPath $engine -File -Recurse -Force | ForEach-Object {[pscustomobject]@{path=$_.FullName;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash}})
function Start-Owned([string[]]$arguments,[string]$tag){
 $start=New-Object Diagnostics.ProcessStartInfo;$start.FileName=$launcher;$start.UseShellExecute=$false;$start.CreateNoWindow=$true
 $start.WorkingDirectory=$fixture;$start.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden;$start.EnvironmentVariables['APPDATA']=Join-Path $fixture 'AppData'
 $start.Arguments=($arguments | ForEach-Object {'"'+[regex]::Replace([regex]::Replace($_,'(\\*)"','$1$1\"'),'(\\+)$','$1$1')+'"'}) -join ' '
 $child=New-Object Diagnostics.Process;$child.StartInfo=$start
 Require ($child.Start()) 'Cannot start owned Launcher';$handle=$child.Handle
 Write-Host "PACKAGE_HUB_START=$tag PID=$($child.Id)";return $child
}
function Stop-Owned($child){if($child){if(!$child.HasExited){$child.Kill();$child.WaitForExit()};$child.Dispose()}}
$child=$null
try{
 $child=Start-Owned @('--verify-package',$manifest,'--no-launch') 'cli-valid'
 Require ($child.WaitForExit(30000)) 'CLI verification observation timed out';Require ($child.ExitCode -eq 0) 'CLI refused current package';$child.Dispose();$child=$null
 $child=Start-Owned @('--verify-package',(Join-Path $fixture 'missing.json'),'--no-launch') 'cli-invalid'
 Require ($child.WaitForExit(10000)) 'CLI refusal observation timed out';Require ($child.ExitCode -eq 1) 'CLI accepted missing package';$child.Dispose();$child=$null
 $child=Start-Owned @('--verify-package',$manifest) 'cli-usage'
 Require ($child.WaitForExit(10000)) 'CLI usage observation timed out';Require ($child.ExitCode -eq 2) 'CLI accepted ambiguous usage';$child.Dispose();$child=$null
 $child=Start-Owned @('--inspect-package',$manifest,'--no-launch') 'hub'
 $deadline=[DateTime]::UtcNow.AddSeconds(30);$window=[IntPtr]::Zero;$text=''
 while([DateTime]::UtcNow -lt $deadline){
  Require (!$child.HasExited) 'Hub exited before verification result'
  $window=[PackageHubWindow]::Find($child.Id)
  if($window -ne [IntPtr]::Zero){$text=[PackageHubWindow]::Details($window);if($text -like 'Manifest checksums match:*' -or $text -like 'Package not verified:*'){break}}
  Start-Sleep -Milliseconds 50
 }
 $declaration=Get-Content -LiteralPath $manifest -Raw -Encoding UTF8 | ConvertFrom-Json
 Require ($text.StartsWith("Manifest checksums match: $($declaration.files.Count) files")) "Actual Hub did not verify package: $text"
 Require ($text.Contains('NOT publisher/signature/ABI') -and $text.Contains('Not installed or selected') -and [PackageHubWindow]::ReadOnly($window)) 'Actual Hub overclaimed package trust or has editable result'
 Require ([PackageHubWindow]::Close($window)) 'Owned Hub close failed'
 Require ($child.WaitForExit(15000)) 'Owned Hub did not retire worker on close';Require ($child.ExitCode -eq 0) 'Owned Hub closed with failure';$child.Dispose();$child=$null
 foreach($entry in $before){Require ((Get-FileHash -LiteralPath $entry.path -Algorithm SHA256).Hash -eq $entry.sha256) 'Verification changed packaged file bytes'}
 Require (@(Get-ChildItem -LiteralPath $engine -File -Recurse -Force).Count -eq $before.Count) 'Verification changed package file inventory'
 Require (!(Test-Path -LiteralPath (Join-Path $fixture 'AppData/KuboEngine/engine_catalog.json')) -and !(Test-Path -LiteralPath (Join-Path $fixture 'AppData/KuboEngine/recent_projects.json'))) 'Inspection wrote catalog or recent history'
 Write-Output "PASS: actual packaged Launcher CLI valid/invalid/usage, async Hub all $($declaration.files.Count) payloads, read-only trust notice, owned close and package/catalog/history preservation (not physical dialog/pixel/signature/install proof)"
 Write-Output "Retained verification fixture: $fixture"
}finally{Stop-Owned $child}
