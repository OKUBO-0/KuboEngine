[CmdletBinding()]
param([switch]$ExpectBefore,
      [string]$BuildDirectory,
      [string]$MSBuild='C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe')
$ErrorActionPreference='Stop'
$uiProject=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).TrimEnd('\')
if([string]::IsNullOrWhiteSpace($BuildDirectory)){$BuildDirectory=Join-Path $uiProject '..\generated\outputs'}
$uiBuild=(Resolve-Path -LiteralPath $BuildDirectory).Path
$uiFixture=Join-Path ([IO.Path]::GetTempPath()) ('kubo-imgui-shutdown-'+[guid]::NewGuid().ToString('N'))
$uiChild=$null
New-Item -ItemType Directory -Path $uiFixture | Out-Null
try {
 foreach($file in @('imgui_shutdown_fixture.cpp','imgui_shutdown_fixture.vcxproj')){
  Copy-Item -LiteralPath (Join-Path $PSScriptRoot $file) -Destination (Join-Path $uiFixture $file)
 }
 Copy-Item -LiteralPath (Join-Path $uiProject 'externals\imgui\imgui_impl_dx12.cpp') -Destination (Join-Path $uiFixture 'pinned_imgui_impl_dx12.cpp')
 Copy-Item -LiteralPath (Join-Path $uiProject 'externals\imgui_node_editor\imgui_node_editor_api.cpp') -Destination (Join-Path $uiFixture 'pinned_node_editor_api.cpp')
 foreach($configuration in @('Debug','Release')){
  $libDirectory=Join-Path $uiFixture "lib\$configuration"
  New-Item -ItemType Directory -Path $libDirectory | Out-Null
  Copy-Item -LiteralPath (Join-Path $uiBuild "$configuration\imgui.lib") -Destination (Join-Path $libDirectory 'imgui.lib')
  $uiBuildLog=Join-Path $uiFixture "build-$configuration.log"
  & $MSBuild (Join-Path $uiFixture 'imgui_shutdown_fixture.vcxproj') /m:1 "/p:Configuration=$configuration" /p:Platform=x64 "/p:KuboProjectRoot=$uiProject/" /v:minimal *> $uiBuildLog
  if($LASTEXITCODE -ne 0){Get-Content -LiteralPath $uiBuildLog -Tail 20;throw "UI shutdown $configuration build failed"}
  $uiReads=@(Get-ChildItem -LiteralPath (Join-Path $uiFixture "obj\$configuration") -Filter 'CL.read.1.tlog' -File -Recurse)
  if(!$uiReads.Count){throw 'UI fixture compiler read records missing'}
  foreach($read in $uiReads){foreach($line in Get-Content -LiteralPath $read.FullName){
   foreach($prefix in @(($uiProject+'\engine\'),([IO.Path]::GetFullPath($PSScriptRoot).TrimEnd('\')+'\'))){
    if($line.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)){throw 'Private UI fixture read original engine/test source'}
   }
  }}
  $uiOut=Join-Path $uiFixture "run-$configuration.log";$uiErr=Join-Path $uiFixture "run-$configuration.stderr.log"
  $uiChild=Start-Process -FilePath (Join-Path $uiFixture "bin\$configuration\ImGuiShutdownChecks.exe") -WorkingDirectory $uiFixture -WindowStyle Hidden -PassThru -RedirectStandardOutput $uiOut -RedirectStandardError $uiErr
  $uiHandle=$uiChild.Handle
  if(!$uiChild.WaitForExit(50000)){throw "Owned UI shutdown $configuration timeout"}
  $uiChild.WaitForExit()
  $caseExit=$uiChild.ExitCode;$uiChild.Dispose();$uiChild=$null
  $caseText=Get-Content -LiteralPath $uiOut -Raw
  Get-Content -LiteralPath $uiOut;Get-Content -LiteralPath $uiErr
  $expected=if($ExpectBefore){1}else{0}
  if($caseExit -ne $expected -or !$caseText.Contains('IMGUI_FIXTURE_RECOVERED normal_clean=1 outstanding_arrays=0 current_null=1')){throw "UI shutdown $configuration exit=$caseExit expected=$expected or failed recovery"}
  if($ExpectBefore){
   if(!$caseText.Contains('IMGUI_VIEWPORT_CHECK position=2 fault=1 attempts=2 retained_arrays=1') -or !$caseText.Contains('NODE_EDITOR_CHECK current_cleared=0 unrelated_preserved=1 final_cleared=0')){throw 'Before fixture did not reproduce both specific faults'}
  }else{
   foreach($position in @(1,2)){if(!$caseText.Contains("IMGUI_VIEWPORT_CHECK position=$position fault=1 attempts=$position retained_arrays=0")){throw 'Missing repaired allocation-position marker'}}
   if(!$caseText.Contains('NODE_EDITOR_CHECK current_cleared=1 unrelated_preserved=1 final_cleared=1') -or $caseText -notmatch '(?m)^PASS:'){throw 'Missing repaired context/terminal markers'}
  }
  Write-Output "IMGUI_SHUTDOWN_CHECK configuration=$configuration exit=$caseExit expected=$expected"
 }
 Write-Output "PASS: UI shutdown contract controls Debug/Release; expected-before=$ExpectBefore (not manager/backend Init/GPU/physical UI QA)"
}finally{
 if($uiChild){if(!$uiChild.HasExited){Stop-Process -Id $uiChild.Id;$uiChild.WaitForExit()};$uiChild.Dispose()}
 Write-Output "Retained UI shutdown fixture/logs: $uiFixture"
}
