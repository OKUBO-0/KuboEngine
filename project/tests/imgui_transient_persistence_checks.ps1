[CmdletBinding()]
param([string]$BuildDirectory,
      [string]$MSBuild='C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe')
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).TrimEnd('\')
if([string]::IsNullOrWhiteSpace($BuildDirectory)){$BuildDirectory=Join-Path $project '..\generated\outputs'}
$build=(Resolve-Path -LiteralPath $BuildDirectory).Path
$fixture=Join-Path ([IO.Path]::GetTempPath()) ('kubo-imgui-persistence-'+[guid]::NewGuid().ToString('N'))
$child=$null
New-Item -ItemType Directory -Path $fixture | Out-Null
try {
 foreach($file in @('imgui_transient_persistence_fixture.cpp','imgui_transient_persistence_fixture.vcxproj')){
  Copy-Item -LiteralPath (Join-Path $PSScriptRoot $file) -Destination (Join-Path $fixture $file)
 }
 Copy-Item -LiteralPath (Join-Path $project 'externals\imgui_node_editor\imgui_node_editor.cpp') -Destination (Join-Path $fixture 'pinned_node_editor.cpp')
 Copy-Item -LiteralPath (Join-Path $project 'externals\imgui_node_editor\imgui_node_editor_api.cpp') -Destination (Join-Path $fixture 'pinned_node_editor_api.cpp')
 foreach($configuration in @('Debug','Release')){
  $libs=Join-Path $fixture "lib\$configuration";New-Item -ItemType Directory -Path $libs | Out-Null
  Copy-Item -LiteralPath (Join-Path $build "$configuration\imgui.lib") -Destination (Join-Path $libs 'imgui.lib')
  $buildLog=Join-Path $fixture "build-$configuration.log"
  $global:LASTEXITCODE=$null
  & $MSBuild (Join-Path $fixture 'imgui_transient_persistence_fixture.vcxproj') /m:1 "/p:Configuration=$configuration" /p:Platform=x64 "/p:KuboProjectRoot=$project/" /v:minimal *> $buildLog
  if($null -eq $global:LASTEXITCODE -or $global:LASTEXITCODE -ne 0){Get-Content $buildLog -Tail 20;throw "Persistence $configuration build failed"}
  $reads=@(Get-ChildItem -LiteralPath (Join-Path $fixture "obj\$configuration") -Filter 'CL.read.1.tlog' -File -Recurse)
  if(!$reads.Count){throw 'Compiler read records missing'}
  foreach($read in $reads){foreach($line in Get-Content $read.FullName){
   foreach($prefix in @(($project+'\engine\'),([IO.Path]::GetFullPath($PSScriptRoot).TrimEnd('\')+'\'))){
    if($line.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)){throw 'Private persistence fixture read original engine/test source'}
   }
  }}
  foreach($mode in @('positive','fault','global','node','session')){
   $stdout=Join-Path $fixture "run-$configuration-$mode.log";$stderr=Join-Path $fixture "run-$configuration-$mode.stderr.log"
   $child=Start-Process -FilePath (Join-Path $fixture "bin\$configuration\ImGuiTransientPersistenceChecks.exe") -ArgumentList $mode -WorkingDirectory $fixture -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
   $ownedHandle=$child.Handle
   if(!$child.WaitForExit(50000)){throw "Owned persistence fixture timeout: $configuration/$mode"}
   $child.WaitForExit();$caseExit=$child.ExitCode;$child.Dispose();$child=$null
   $caseText=Get-Content -LiteralPath $stdout -Raw
   Get-Content $stdout;Get-Content $stderr
   if($caseExit -ne 0 -or !$caseText.Contains("TRANSIENT_PERSISTENCE mode=$mode consumed=0 current_retired=1") -or $caseText -notmatch '(?m)^PASS:'){throw "Persistence case failed: $configuration/$mode exit=$caseExit"}
   if($mode -eq 'global' -and !$caseText.Contains('global_delta=1')){throw 'Global persistence callback not preserved'}
   if($mode -eq 'node' -and $caseText -notmatch 'node_delta=[1-9][0-9]*'){throw 'Dirty node callback not preserved'}
   if($mode -eq 'session' -and !$caseText.Contains('begin_delta=1 end_delta=1')){throw 'Session callbacks not preserved'}
   Write-Output "IMGUI_PERSISTENCE_CHECK configuration=$configuration mode=$mode exit=0"
  }
 }
 Write-Output 'IMGUI_PERSISTENCE_SUMMARY=10 EXPECTED=10'
}finally{
 if($child){if(!$child.HasExited){Stop-Process -Id $child.Id;$child.WaitForExit()};$child.Dispose()}
 Write-Output "Retained persistence fixture/logs: $fixture"
}
