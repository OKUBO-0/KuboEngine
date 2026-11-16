[CmdletBinding()]
param([switch]$InitializationFailures,
      [switch]$Production,
      [switch]$IncludePersistentShutdown,
      [string]$MSBuild='C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe')
$ErrorActionPreference='Stop'
if($IncludePersistentShutdown -and (!$Production -or $InitializationFailures)){throw 'Persistent gate requires actual production Editor callback mode'}
$coordinatorProject=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path.TrimEnd('\')
$coordinatorFixture=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot ('..\..\generated\editor-run-coordinator\'+[guid]::NewGuid().ToString('N'))))
$coordinatorPinned=Join-Path $coordinatorFixture 'pinned';New-Item -ItemType Directory -Path $coordinatorPinned -Force | Out-Null
$coordinatorChild=$null
try{
 $coordinatorBefore=@{};foreach($configuration in @('Debug','Release')){foreach($binary in Get-ChildItem (Join-Path $coordinatorProject "..\generated\outputs\$configuration") -File | Where-Object {$_.Extension -in @('.exe','.lib','.dll')}){$coordinatorBefore[$binary.FullName]=(Get-FileHash -LiteralPath $binary.FullName).Hash}}
 $coordinatorTokens=$null;$coordinatorErrors=$null;$coordinatorAst=[Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot 'framework_run_candidate_checks.ps1'),[ref]$coordinatorTokens,[ref]$coordinatorErrors)
 if($coordinatorErrors.Count){throw 'Coordinator source generator syntax invalid'}
 $coordinatorBodies=@($coordinatorAst.FindAll({param($node) $node -is [Management.Automation.Language.StringConstantExpressionAst] -and $node.Value.StartsWith('void Framework::Run()') -and $node.Value.Contains('RunWithRetainedShutdownCandidate')},$true))
 if($coordinatorBodies.Count -ne 1){throw 'Expected one reviewed private coordinator source body'}
 $coordinatorSource=Get-Content -LiteralPath (Join-Path $coordinatorProject 'engine\base\core\Framework.cpp') -Raw
 $coordinatorSource=$coordinatorSource.Replace("`r`n","`n")
 $coordinatorOld="void Framework::Run()`n{`n`tInitialize();`n`twhile (true) {`n`t`tUpdate();`n`t`tif (IsEndRequest()) {`n`t`t`tbreak;`n`t`t}`n`t`tDraw();`n`t}`n`tFinalize();`n}"
 if(!$Production){
  if(([regex]::Matches($coordinatorSource,[regex]::Escape($coordinatorOld))).Count -ne 1){throw 'Historical prototype requires the old Run source; use -Production for adopted Run'}
  $coordinatorSource=$coordinatorSource.Replace($coordinatorOld,$coordinatorBodies[0].Value.Replace("`r`n","`n"))
  $coordinatorSource="#include `"framework_run_coordinator_candidate.h`"`n#include `"Framework.h`"`nextern void FixtureRunFatal(Engine::Base::Framework*,const std::exception_ptr&,const std::exception_ptr&,unsigned) noexcept;`n"+$coordinatorSource
 }
 if($InitializationFailures){
  $coordinatorGraphicsAnchor='dxCommon->Initialize(winApp.get());'
  if(([regex]::Matches($coordinatorSource,[regex]::Escape($coordinatorGraphicsAnchor))).Count -ne 1){throw 'Review graphics-ready observation boundary'}
  $coordinatorSource="#include `"Framework.h`"`nextern void FixtureEditorGraphicsReady(Engine::Base::DirectXCommon*);`n"+$coordinatorSource.Replace($coordinatorGraphicsAnchor,$coordinatorGraphicsAnchor+"`n`tFixtureEditorGraphicsReady(dxCommon.get());")
  $coordinatorUiSource=(Get-Content -LiteralPath (Join-Path $coordinatorProject 'engine\base\render\ImGuiManager.cpp') -Raw).Replace("`r`n","`n")
  $coordinatorUiAnchors=@("imGuiContext_ = ImGui::CreateContext();`n`tImGui::SetCurrentContext(imGuiContext_);","imPlotContext_ = ImPlot::CreateContext();`n`tImPlot::SetCurrentContext(imPlotContext_);","nodeEditorContext_ = ax::NodeEditor::CreateEditor(&nodeEditorConfig);`n`tax::NodeEditor::SetCurrentEditor(nodeEditorContext_);",'if (!ImGui_ImplWin32_Init(winApp_->GetHwnd())) throw std::runtime_error("ImGui Win32 backend initialization failed");','srvDescriptorSize_ = GetDirectXCommon()->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);','if (!ImGui_ImplDX12_Init(&initInfo)) throw std::runtime_error("ImGui DX12 backend initialization failed");','ready_ = true;')
  for($coordinatorStage=0;$coordinatorStage -lt $coordinatorUiAnchors.Count;$coordinatorStage++){
   $coordinatorUiAnchor=$coordinatorUiAnchors[$coordinatorStage]
   if(([regex]::Matches($coordinatorUiSource,[regex]::Escape($coordinatorUiAnchor))).Count -ne 1){throw "Review UI acquisition boundary $coordinatorStage"}
   $coordinatorUiSource=$coordinatorUiSource.Replace($coordinatorUiAnchor,$coordinatorUiAnchor+"`n`tFixtureEditorUiAcquired($($coordinatorStage+1));")
  }
  $coordinatorUiSource="extern void FixtureEditorUiAcquired(unsigned);`n"+$coordinatorUiSource
  [IO.File]::WriteAllText((Join-Path $coordinatorPinned 'ImGuiManager.cpp'),$coordinatorUiSource,[Text.UTF8Encoding]::new($false))
 }
 [IO.File]::WriteAllText((Join-Path $coordinatorPinned 'Framework.cpp'),$coordinatorSource,[Text.UTF8Encoding]::new($false))
 $coordinatorHeader=Get-Content -LiteralPath (Join-Path $coordinatorProject 'engine\base\core\Framework.h') -Raw;$coordinatorAnchor="`tbool finalized_ = false;"
 if(([regex]::Matches($coordinatorHeader,[regex]::Escape($coordinatorAnchor))).Count -ne 1){throw 'Review coordinator admission layout'}
 if(!$Production){$coordinatorHeader=$coordinatorHeader.Replace($coordinatorAnchor,($coordinatorAnchor+"`r`n`tbool runActiveCandidate_=false,runStartedCandidate_=false;"))}
 [IO.File]::WriteAllText((Join-Path $coordinatorPinned 'Framework.h'),$coordinatorHeader,[Text.UTF8Encoding]::new($false))
 $coordinatorMain=if($InitializationFailures){'editor_run_initialization_checks.cpp'}else{'editor_run_coordinator_checks.cpp'}
 Copy-Item -LiteralPath (Join-Path $PSScriptRoot $coordinatorMain) -Destination (Join-Path $coordinatorPinned 'editor_run_coordinator_checks.cpp')
 foreach($file in @('framework_run_failure_candidate.h','framework_run_coordinator_candidate.h')){Copy-Item -LiteralPath (Join-Path $PSScriptRoot $file) -Destination (Join-Path $coordinatorPinned $file)}
 if($Production){
  $coordinatorMainText="#define KUBO_PRODUCTION_RUN_CHECKS`n"+(Get-Content -LiteralPath (Join-Path $coordinatorPinned 'editor_run_coordinator_checks.cpp') -Raw).Replace('runActiveCandidate_','runActive_')
  [IO.File]::WriteAllText((Join-Path $coordinatorPinned 'editor_run_coordinator_checks.cpp'),$coordinatorMainText,[Text.UTF8Encoding]::new($false))
  [IO.File]::WriteAllText((Join-Path $coordinatorPinned 'framework_run_failure_candidate.h'),"#pragma once`n#include `"engine/base/core/RunFailure.h`"`nusing FrameworkRunFailure=Engine::Base::RunFailure;`n",[Text.UTF8Encoding]::new($false))
 }
 Get-ChildItem -LiteralPath $coordinatorPinned -File | ForEach-Object {Get-FileHash -LiteralPath $_.FullName} | Format-List | Out-File -LiteralPath (Join-Path $coordinatorFixture 'pinned-hashes.log') -Encoding utf8
 $coordinatorTargets=(Resolve-Path (Join-Path $PSScriptRoot 'editor_run_coordinator_checks.targets')).Path
 & $MSBuild (Join-Path $coordinatorProject 'KuboEngine.vcxproj') /m:1 /p:Configuration=Debug /p:Platform=x64 "/p:SolutionDir=$coordinatorProject\" "/p:KuboCoordinatorRoot=$coordinatorFixture\" "/p:ForceImportBeforeCppTargets=$coordinatorTargets" /verbosity:minimal *> (Join-Path $coordinatorFixture 'build.log')
 if($LASTEXITCODE -ne 0){Get-Content (Join-Path $coordinatorFixture 'build.log') -Tail 25;throw 'Actual Editor coordinator build failed'}
 $coordinatorReads=@(Get-ChildItem (Join-Path $coordinatorFixture 'obj\KuboEngine') -Filter CL.read.1.tlog -Recurse -File)
 if(!$coordinatorReads.Count){throw 'Missing compiler header-universe evidence'}
 $coordinatorOriginalHeader=(Join-Path $coordinatorProject 'engine\base\core\Framework.h')
 foreach($read in $coordinatorReads){if(Get-Content -LiteralPath $read.FullName | Where-Object {$_.Equals($coordinatorOriginalHeader,[StringComparison]::OrdinalIgnoreCase)}){throw 'Mixed original/private Framework header universe'}}
 $coordinatorOut=Join-Path $coordinatorFixture 'native.log';$coordinatorErr=Join-Path $coordinatorFixture 'native.stderr.log'
 $coordinatorChild=Start-Process -FilePath (Join-Path $coordinatorFixture 'bin\KuboEngine.exe') -WorkingDirectory $coordinatorProject -WindowStyle Hidden -PassThru -RedirectStandardOutput $coordinatorOut -RedirectStandardError $coordinatorErr
 $coordinatorHandle=$coordinatorChild.Handle;if(!$coordinatorChild.WaitForExit(50000)){throw 'Owned actual Editor coordinator exceeded50s bound'}
 $coordinatorChild.WaitForExit();$coordinatorExit=$coordinatorChild.ExitCode;Get-Content $coordinatorOut;Get-Content $coordinatorErr
 $coordinatorMarker=if($InitializationFailures){'EDITOR_RUN_INITIALIZATION cases=20 ui_boundaries=7 scene_read_fault=1 cold_foreign_preserved=1 original_identity=1 terminal_before_throw=1 gpu_errors=0 native_exit=0'}else{'EDITOR_RUN_COORDINATOR destroyed=32 run_refusals=128 finalize_reentries=32 terminal_before_throw=1 original_pair=1 complete_host_alive=1 gpu_errors=0 native_exit=0'}
 if($coordinatorExit -ne 0 -or !(Select-String -LiteralPath $coordinatorOut -Pattern $coordinatorMarker -SimpleMatch -Quiet)){throw "Actual Editor coordinator gate failed: $coordinatorExit"}
 if($Production -and !$InitializationFailures -and !(Select-String -LiteralPath $coordinatorOut -Pattern 'PRODUCTION_RUN_GLOBAL competing_refusals=128 foreign_thread_refused=1 admission_released=1' -SimpleMatch -Quiet)){throw 'Missing production global Run admission marker'}
 if($IncludePersistentShutdown){
  $coordinatorChild.Dispose();$coordinatorChild=$null
  $coordinatorPersistentOut=Join-Path $coordinatorFixture 'persistent.log';$coordinatorPersistentErr=Join-Path $coordinatorFixture 'persistent.stderr.log'
  $coordinatorChild=Start-Process -FilePath (Join-Path $coordinatorFixture 'bin\KuboEngine.exe') -ArgumentList 'persistent' -WorkingDirectory $coordinatorProject -WindowStyle Hidden -PassThru -RedirectStandardOutput $coordinatorPersistentOut -RedirectStandardError $coordinatorPersistentErr
  $coordinatorHandle=$coordinatorChild.Handle;if(!$coordinatorChild.WaitForExit(20000)){throw 'Owned persistent Editor child exceeded20s bound'}
  $coordinatorChild.WaitForExit();$coordinatorPersistentExit=$coordinatorChild.ExitCode
  Get-Content $coordinatorPersistentOut;Get-Content $coordinatorPersistentErr
  if($coordinatorPersistentExit -ne 70 -or !(Select-String -LiteralPath $coordinatorPersistentOut -Pattern 'PRODUCTION_RUN_PERSISTENT attempts=3 pending=11 complete_host_alive=1 unwind=0 gpu_errors=0' -SimpleMatch -Quiet)){throw "Persistent production Editor gate exit=$coordinatorPersistentExit"}
  foreach($coordinatorDiagnostic in @('KuboEngine operation error: non-standard C++ exception','KuboEngine first shutdown error: owned persistent shutdown refusal 1','KuboEngine shutdown incomplete after 3 attempts; exiting without host unwind')){if(!(Select-String -LiteralPath $coordinatorPersistentErr -Pattern $coordinatorDiagnostic -SimpleMatch -Quiet)){throw 'Persistent root lost first error diagnostics'}}
  Write-Output 'PASS: joined production persistent exit70; both errors reported; full Editor alive before fatal exit'
 }
 foreach($binary in $coordinatorBefore.Keys){if((Get-FileHash -LiteralPath $binary).Hash -ne $coordinatorBefore[$binary]){throw "Private Editor build modified production binary: $binary"}}
 Write-Output "PASS: actual Editor coordinated Run terminal cleanup; production-source=$Production initialization-failures=$InitializationFailures; normal binaries unchanged; not shipped Application/SDK ABI proof"
}finally{
 if($coordinatorChild){if(!$coordinatorChild.HasExited){Stop-Process -Id $coordinatorChild.Id;$coordinatorChild.WaitForExit()};$coordinatorChild.Dispose()}
 Write-Output "Retained actual Editor coordinator source/build/native logs: $coordinatorFixture"
}
