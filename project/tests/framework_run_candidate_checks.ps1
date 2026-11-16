[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$SdkDirectory,
      [switch]$Candidate,
      [switch]$Coordinated,
      [switch]$ProductionRun,
      [switch]$ProductionFenceFaults,
      [switch]$Gpu,
      [switch]$Submitted,
      [switch]$Player,
      [switch]$PlayerPartial,
      [switch]$ProductionPlayer,
      [switch]$PlayerWait,
      [switch]$PlayerDispatch,
      [string]$MSBuild='C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe')
$ErrorActionPreference='Stop'
if($ProductionFenceFaults -and (!$ProductionRun -or !$Submitted)){throw 'Fence fault matrix requires -ProductionRun -Submitted'}
if($ProductionRun){
 if($Candidate -or $PlayerPartial -or $PlayerWait -or $PlayerDispatch -or ($Gpu -and !$Submitted -and !$ProductionPlayer) -or ($Player -and !$ProductionPlayer)){throw 'Production Run supports CPU, submitted-GPU recovery or actual production Player callback probe'}
 $Coordinated=$true
}
if($Coordinated -and ($Candidate -or ($Gpu -and !$Submitted -and !$ProductionPlayer) -or ($Player -and !$ProductionPlayer) -or $PlayerPartial -or $PlayerWait -or $PlayerDispatch -or ($Submitted -and $ProductionPlayer))){throw 'Coordinated gate supports CPU, submitted-GPU or actual production-Player source probes'}
if($PlayerDispatch){
 if($Candidate -or $Submitted -or $PlayerPartial -or $PlayerWait){throw 'Player dispatch admission requires the production callback probe'}
 $ProductionPlayer=$true
}
if($PlayerWait){
 if($Candidate -or $Submitted -or $PlayerPartial){throw 'Player wait admission requires the production callback probe'}
 $ProductionPlayer=$true
}
if($ProductionPlayer -and $Candidate){throw 'Production Player and private candidate variants are distinct'}
if($ProductionPlayer){$Player=$true}
if($PlayerPartial){$Player=$true}
if($Submitted -and $Player){throw 'Choose submitted or real Player source probe, not both'}
if($Submitted -or $Player){$Gpu=$true}
$runProject=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).TrimEnd('\')
$runSdk=(Resolve-Path -LiteralPath $SdkDirectory).Path.TrimEnd('\')
$runContracts=@('engine\base\core\Framework.h','engine\script\ScriptRuntime.h','engine\base\directx\DirectXCommon.h')
if($ProductionRun){$runContracts+=@('engine\base\core\RunFailure.h','engine\base\core\RunDiagnostics.h')}
foreach($runContract in $runContracts){
 if((Get-FileHash -LiteralPath (Join-Path $runProject $runContract)).Hash -ne
    (Get-FileHash -LiteralPath (Join-Path $runSdk ('include\'+$runContract))).Hash){throw "Use a matching SDK: $runContract"}
}
[xml]$runProps=Get-Content -LiteralPath (Join-Path $runSdk 'KuboRuntime.Includes.props') -Raw
$runTool=$runProps.SelectSingleNode('//*[local-name()="KuboSdkVCToolsVersion"]')
if(!$runTool){throw 'Missing compiler contract'}
$runFixture=Join-Path ([IO.Path]::GetTempPath()) ('kubo-framework-run-candidate-'+[guid]::NewGuid().ToString('N'))
if($ProductionRun){$runFixture=[IO.Path]::GetFullPath((Join-Path $runProject ('..\generated\framework-run-source\'+[guid]::NewGuid().ToString('N'))))}
New-Item -ItemType Directory -Path $runFixture | Out-Null
$runChild=$null
try {
 Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'framework_services_fixture.vcxproj') -Destination (Join-Path $runFixture 'framework_services_fixture.vcxproj')
 $runFixtureSource=if($Coordinated -and !$Submitted -and !$Player){'framework_run_coordinator_fixture.cpp'}elseif($PlayerPartial){'runtime_game_partial_candidate_fixture.cpp'}elseif($Player){'runtime_game_shutdown_candidate_fixture.cpp'}elseif($Submitted){'framework_run_submitted_candidate_fixture.cpp'}elseif($Gpu){'framework_run_gpu_candidate_fixture.cpp'}else{'framework_run_candidate_fixture.cpp'}
 Copy-Item -LiteralPath (Join-Path $PSScriptRoot $runFixtureSource) -Destination (Join-Path $runFixture 'framework_services_fixture.cpp')
 Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'framework_run_failure_candidate.h') -Destination (Join-Path $runFixture 'framework_run_failure_candidate.h')
 Copy-Item -LiteralPath (Join-Path $runProject 'engine\base\core\Framework.h') -Destination (Join-Path $runFixture 'Framework.h')
 $runSource=Join-Path $runProject 'engine\base\core\Framework.cpp'
 $runBody=Get-Content -LiteralPath $runSource -Raw
 $runExpected=@'
void Framework::Run()
{
	Initialize();
	while (true) {
		Update();
		if (IsEndRequest()) {
			break;
		}
		Draw();
	}
	Finalize();
}
'@ -replace '\t',"`t"
 $runExpected=$runExpected.Replace("`r`n","`n");$runBody=$runBody.Replace("`r`n","`n")
 if(!$ProductionRun -and ([regex]::Matches($runBody,[regex]::Escape($runExpected))).Count -ne 1){throw 'Historical Run anchor changed; use -ProductionRun for adopted Run'}
 if($Candidate){
  $runReplacement=@'
void Framework::Run()
{
    std::exception_ptr operation,cleanup;
    try {
        Initialize();
        while(true) {
            Update();
            if(IsEndRequest())break;
            Draw();
        }
    } catch(...) {operation=std::current_exception();}
    try {Finalize();} catch(...) {cleanup=std::current_exception();}
    if(operation && cleanup)throw FrameworkRunFailure(operation,cleanup);
    if(cleanup)std::rethrow_exception(cleanup);
    if(operation)std::rethrow_exception(operation);
}
'@
  $runBody=$runBody.Replace($runExpected,$runReplacement.Replace("`r`n","`n"))
 }
 $runBody="#include `"framework_run_failure_candidate.h`"`r`n"+$runBody
 if($Coordinated -and !$ProductionRun){
  Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'framework_run_coordinator_candidate.h') -Destination (Join-Path $runFixture 'framework_run_coordinator_candidate.h')
  $runHeaderPath=Join-Path $runFixture 'Framework.h';$runHeaderBody=Get-Content -LiteralPath $runHeaderPath -Raw
  $runAdmissionAnchor="`tbool finalized_ = false;"
  if(([regex]::Matches($runHeaderBody,[regex]::Escape($runAdmissionAnchor))).Count -ne 1){throw 'Run coordinator private admission anchor changed'}
  $runHeaderBody=$runHeaderBody.Replace($runAdmissionAnchor,($runAdmissionAnchor+"`r`n`tbool runActiveCandidate_=false,runStartedCandidate_=false;"))
  [IO.File]::WriteAllText($runHeaderPath,$runHeaderBody,[Text.UTF8Encoding]::new($false))
  $runCoordinatedBody=@'
void Framework::Run()
{
    if(runActiveCandidate_ || runStartedCandidate_ || initializationStarted_ || finalizing_ || finalized_)
        throw std::logic_error("Framework Run is single-use and cannot reenter an initialized host");
    runActiveCandidate_=true;runStartedCandidate_=true;
    struct RunAdmission {bool& flag;~RunAdmission(){flag=false;}} admission{runActiveCandidate_};
    RunWithRetainedShutdownCandidate([this]{
        Initialize();while(true){Update();if(IsEndRequest())break;Draw();}
    },[this]{
        Finalize();
        if((initializationStarted_ && !finalized_) || services_.PendingCount()!=0 ||
           (dxCommon && dxCommon->GetCommandState()!=DirectXCommon::CommandState::Retired) ||
           (winApp && IsWindow(winApp->GetHwnd())))
            throw std::logic_error("Framework Finalize returned without terminal base retirement");
#ifndef KUBO_RUNTIME_ONLY
        if(imGuiManager)throw std::logic_error("Framework Finalize returned with owned UI context");
#endif
    },[this](const std::exception_ptr& operation,const std::exception_ptr& cleanup,unsigned attempts) noexcept {
        FixtureRunFatal(this,operation,cleanup,attempts);
    });
}
'@
  $runBody=$runBody.Replace($runExpected,$runCoordinatedBody.Replace("`r`n","`n"))
  $runBody="#include `"framework_run_coordinator_candidate.h`"`n#include `"Framework.h`"`nextern void FixtureRunFatal(Engine::Base::Framework*,const std::exception_ptr&,const std::exception_ptr&,unsigned) noexcept;`n"+$runBody
 }
 if($ProductionRun){
  [IO.File]::WriteAllText((Join-Path $runFixture 'framework_run_failure_candidate.h'),"#pragma once`n#include `"engine/base/core/RunFailure.h`"`nusing FrameworkRunFailure=Engine::Base::RunFailure;`n",[Text.UTF8Encoding]::new($false))
  $runProductionFixture=(Get-Content -LiteralPath (Join-Path $runFixture 'framework_services_fixture.cpp') -Raw).Replace('runActiveCandidate_','runActive_')
  [IO.File]::WriteAllText((Join-Path $runFixture 'framework_services_fixture.cpp'),"#define KUBO_PRODUCTION_RUN_CHECKS`n"+$runProductionFixture,[Text.UTF8Encoding]::new($false))
 }
 if($PlayerPartial){
  $runPrivateHeader=Get-Content -LiteralPath (Join-Path $runFixture 'Framework.h') -Raw
  $runReturn="`t`treturn manager;"
  if(([regex]::Matches($runPrivateHeader,[regex]::Escape($runReturn))).Count -ne 1){throw 'Partial Player service registration anchor changed'}
  $runPrivateHeader=$runPrivateHeader.Replace($runReturn,"`t`tFixturePartialBefore(static_cast<size_t>(service));`r`n"+$runReturn)
  $runPrivateHeader="#include <cstddef>`r`nextern void FixturePartialBefore(std::size_t);`r`n"+$runPrivateHeader
  [IO.File]::WriteAllText((Join-Path $runFixture 'Framework.h'),$runPrivateHeader,[Text.UTF8Encoding]::new($false))
  $runCoreAnchors=@('winApp = std::make_unique<Engine::Base::WinApp>();','dxCommon = std::make_shared<Engine::Base::DirectXCommon>();','srvManager = std::make_unique<Engine::Base::SrvManager>();','offscreenRenderManager = std::make_unique<Engine::Base::OffscreenRenderManager>();')
  for($runCoreIndex=0;$runCoreIndex -lt $runCoreAnchors.Count;$runCoreIndex++){
   $runCoreAnchor=$runCoreAnchors[$runCoreIndex]
   if(([regex]::Matches($runBody,[regex]::Escape($runCoreAnchor))).Count -ne 1){throw 'Partial Player core admission changed'}
   $runBody=$runBody.Replace($runCoreAnchor,($runCoreAnchor+"`n`tFixturePartialCore($runCoreIndex);"))
  }
  foreach($runCoreReady in @(@{Before='winApp->Initialize();';After='FixturePartialWindow(winApp.get());'},@{Before='dxCommon->Initialize(winApp.get());';After='FixturePartialGraphics(dxCommon.get());'})){
   if(([regex]::Matches($runBody,[regex]::Escape($runCoreReady.Before))).Count -ne 1){throw 'Partial Player owner ready anchor changed'}
   $runBody=$runBody.Replace($runCoreReady.Before,($runCoreReady.Before+"`n`t"+$runCoreReady.After))
  }
  $runStagePattern='(?m)^(\tAcquireService\(Service::(\w+),[^\n]*;)$'
  if(([regex]::Matches($runBody,$runStagePattern)).Count -ne 11){throw 'Partial Player expected eleven service stages'}
  $runBody=[regex]::Replace($runBody,$runStagePattern,{param($match) $match.Groups[1].Value+"`n`tFixturePartialAfter(static_cast<size_t>(Service::"+$match.Groups[2].Value+"));"})
  $runBody=$runBody.Replace('#include "Framework.h"',"#include `"Framework.h`"`nextern void FixturePartialCore(std::size_t);`nextern void FixturePartialAfter(std::size_t);`nextern void FixturePartialWindow(Engine::Base::WinApp*);`nextern void FixturePartialGraphics(Engine::Base::DirectXCommon*);")
 }
 [IO.File]::WriteAllText((Join-Path $runFixture 'pinned_Framework.cpp'),$runBody,[Text.UTF8Encoding]::new($false))
 if($Player){
  $runPlayerSource=Join-Path $runProject 'engine\app\RuntimeGame.cpp'
  $runPlayerHeader=Join-Path $runProject 'engine\app\RuntimeGame.h'
  if((Get-FileHash -LiteralPath $runPlayerHeader).Hash -ne (Get-FileHash -LiteralPath (Join-Path $runSdk 'include\engine\app\RuntimeGame.h')).Hash){throw 'Use the matching Player SDK header'}
  $runPlayerHeaderBody=(Get-Content -LiteralPath $runPlayerHeader -Raw).Replace("`r`n","`n")
  $runPlayerClass="class RuntimeGame : public Base::Framework {`npublic:"
  if(([regex]::Matches($runPlayerHeaderBody,[regex]::Escape($runPlayerClass))).Count -ne 1){throw 'Player friend admission changed'}
  $runPlayerHeaderBody=$runPlayerHeaderBody.Replace($runPlayerClass,($runPlayerClass+"`n    friend struct RuntimeCandidateAccess;"))
  $runPlayerBody=(Get-Content -LiteralPath $runPlayerSource -Raw).Replace("`r`n","`n")
  $runPlayerFinalize=@'
void RuntimeGame::Finalize()
{
    scripts_.Shutdown(world_);
    diagnosticLuaErrors_.clear();
    scriptAudio_.Clear();
    entityAudio_.Clear();
    Base::Framework::Finalize();
}
'@
  $runPlayerFinalize=$runPlayerFinalize.Replace("`r`n","`n")
  $runPlayerAdoptedFinalize=@'
void RuntimeGame::Finalize()
{
    if(runtimeFinalizing_)return;
    if(scripts_.IsDispatchActive())throw std::logic_error("Cannot finalize Player during script dispatch");
    runtimeFinalizing_=true;
    struct Guard {bool& flag;~Guard(){flag=false;}} guard{runtimeFinalizing_};
    // Keep the complete host/core alive and stop before callbacks on a wait
    // failure. Retire recorded commands before hooks can release their owners.
    if(dxCommon)dxCommon->PrepareForShutdown();
    scripts_.Shutdown(world_);
    diagnosticLuaErrors_.clear();
    scriptAudio_.Clear();
    entityAudio_.Clear();
    Base::Framework::Finalize();
}
'@
  $runPlayerAdoptedFinalize=$runPlayerAdoptedFinalize.Replace("`r`n","`n")
  $runPlayerOldCount=([regex]::Matches($runPlayerBody,[regex]::Escape($runPlayerFinalize))).Count
  $runPlayerNewCount=([regex]::Matches($runPlayerBody,[regex]::Escape($runPlayerAdoptedFinalize))).Count
  if($runPlayerOldCount+$runPlayerNewCount -ne 1){throw 'Player Finalize anchor changed'}
  if($ProductionPlayer){if($runPlayerNewCount -ne 1){throw 'Production guarded Player contract not present'}}
  elseif($runPlayerNewCount){
   # Explicit historical comparator in the OWNED copy, never a production revert.
   $runPlayerBody=$runPlayerBody.Replace($runPlayerAdoptedFinalize,$runPlayerFinalize)
  }
  if($Candidate){
   $runPlayerField='    std::unordered_map<Scene::EntityId,std::string> diagnosticLuaErrors_;'
   if(([regex]::Matches($runPlayerHeaderBody,[regex]::Escape($runPlayerField))).Count -ne 1){throw 'Player private candidate field anchor changed'}
   $runPlayerHeaderBody=$runPlayerHeaderBody.Replace($runPlayerField,($runPlayerField+"`n    bool runtimeFinalizingCandidate_=false;"))
   $runPlayerNewFinalize=@'
void RuntimeGame::Finalize()
{
    if(runtimeFinalizingCandidate_)return;
    runtimeFinalizingCandidate_=true;
    struct Guard {bool& flag;~Guard(){flag=false;}} guard{runtimeFinalizingCandidate_};
    if(dxCommon)dxCommon->PrepareForShutdown();
    scripts_.Shutdown(world_);
    diagnosticLuaErrors_.clear();
    scriptAudio_.Clear();
    entityAudio_.Clear();
    Base::Framework::Finalize();
}
'@
   $runPlayerBody=$runPlayerBody.Replace($runPlayerFinalize,$runPlayerNewFinalize.Replace("`r`n","`n"))
  }
  [IO.File]::WriteAllText((Join-Path $runFixture 'RuntimeGame.h'),$runPlayerHeaderBody,[Text.UTF8Encoding]::new($false))
  [IO.File]::WriteAllText((Join-Path $runFixture 'pinned_RuntimeGame.cpp'),$runPlayerBody,[Text.UTF8Encoding]::new($false))
  $runPlayerProject=Join-Path $runFixture 'framework_services_fixture.vcxproj'
  $runPlayerXml=Get-Content -LiteralPath $runPlayerProject -Raw
  $runPlayerCompile='<ClCompile Include="pinned_Framework.cpp" />'
  if(([regex]::Matches($runPlayerXml,[regex]::Escape($runPlayerCompile))).Count -ne 1){throw 'Player private project anchor changed'}
  $runPlayerXml=$runPlayerXml.Replace($runPlayerCompile,($runPlayerCompile+'<ClCompile Include="pinned_RuntimeGame.cpp" />'))
  [IO.File]::WriteAllText($runPlayerProject,$runPlayerXml,[Text.UTF8Encoding]::new($false))
  @($runPlayerSource,$runPlayerHeader,(Join-Path $runFixture 'pinned_RuntimeGame.cpp'),(Join-Path $runFixture 'RuntimeGame.h')) | ForEach-Object {Get-FileHash -LiteralPath $_} | Format-List | Out-File -LiteralPath (Join-Path $runFixture 'player-input-hashes.log') -Encoding utf8
 }
 if($Submitted -or $PlayerWait){
  $runGraphicsSource=Join-Path $runProject 'engine\base\directx\DirectXCommon.cpp'
  $runGraphicsHeader=Join-Path $runProject 'engine\base\directx\DirectXCommon.h'
  Copy-Item -LiteralPath $runGraphicsHeader -Destination (Join-Path $runFixture 'DirectXCommon.h')
  $runGraphicsBody=(Get-Content -LiteralPath $runGraphicsSource -Raw).Replace("`r`n","`n")
  if($PlayerWait){
   $runWaitAnchor="void DirectXCommon::WaitForAllFrames()`n{"
   if(([regex]::Matches($runGraphicsBody,[regex]::Escape($runWaitAnchor))).Count -ne 1){throw 'Player wait admission anchor changed'}
   $runGraphicsBody=$runGraphicsBody.Replace($runWaitAnchor,($runWaitAnchor+"`n`tFixturePlayerWaitAdmission();"))
   $runGraphicsBody="extern void FixturePlayerWaitAdmission();`n"+$runGraphicsBody
  }else{
  $runPresent="`tCloseAndExecuteCommandList();`n`tThrowIfFailed(`n`t`tswapChain->Present"
  $runSignal='commandQueue->Signal(fence.Get(), candidateValue)'
  if(([regex]::Matches($runGraphicsBody,[regex]::Escape($runPresent))).Count -ne 1 -or
     ([regex]::Matches($runGraphicsBody,[regex]::Escape($runSignal))).Count -ne 1){throw 'Submitted Run injection anchors changed'}
  $runGraphicsBody=$runGraphicsBody.Replace($runPresent,"`tCloseAndExecuteCommandList();`n`tFixtureBeforePresent();`n`tThrowIfFailed(`n`t`tswapChain->Present")
  $runGraphicsBody=$runGraphicsBody.Replace($runSignal,'FixtureQueueSignal(commandQueue.Get(), fence.Get(), candidateValue)')
  $runGraphicsBody="#include <d3d12.h>`nextern void FixtureBeforePresent();`nextern HRESULT FixtureQueueSignal(ID3D12CommandQueue*, ID3D12Fence*, UINT64);`n"+$runGraphicsBody
  if($Coordinated){
   $runFenceWait=if($ProductionRun){'const DWORD waitResult = WaitForSingleObject(fenceEvent, remainingWaitMs);'}else{'const DWORD waitResult = WaitForSingleObject(fenceEvent, INFINITE);'}
   if(([regex]::Matches($runGraphicsBody,[regex]::Escape($runFenceWait))).Count -ne 1){throw 'Coordinated finite-wait source anchor changed'}
   if($ProductionRun){
    # Keep the production deadline, only count/inject results at the API seam.
    $runGraphicsBody=$runGraphicsBody.Replace($runFenceWait,'const DWORD waitResult = FixtureProductionFenceWait(fenceEvent, remainingWaitMs);')
    $runCompletionAnchor='const auto completed = fence->GetCompletedValue();'
    if(([regex]::Matches($runGraphicsBody,[regex]::Escape($runCompletionAnchor))).Count -ne 1){throw 'Review completed-fence sentinel hook'}
    $runGraphicsBody=$runGraphicsBody.Replace($runCompletionAnchor,'const auto completed = FixtureProductionFenceCompleted(fence.Get());')
    $runGraphicsBody="#include <Windows.h>`n#include <d3d12.h>`nextern DWORD FixtureProductionFenceWait(HANDLE,DWORD) noexcept;`nextern UINT64 FixtureProductionFenceCompleted(ID3D12Fence*) noexcept;`n"+$runGraphicsBody
   }else{
    $runGraphicsBody=$runGraphicsBody.Replace($runFenceWait,('const DWORD waitResult = FixtureFenceWait(fenceEvent);'+"`n`tif(waitResult==WAIT_TIMEOUT)ThrowIfFailed(HRESULT_FROM_WIN32(ERROR_TIMEOUT), `"ID3D12Fence wait timeout`");"))
    $runGraphicsBody="#include <Windows.h>`nextern DWORD FixtureFenceWait(HANDLE) noexcept;`n"+$runGraphicsBody
   }
  }
  }
  [IO.File]::WriteAllText((Join-Path $runFixture 'pinned_DirectXCommon.cpp'),$runGraphicsBody,[Text.UTF8Encoding]::new($false))
  $runPrivateProject=Join-Path $runFixture 'framework_services_fixture.vcxproj'
  $runPrivateXml=Get-Content -LiteralPath $runPrivateProject -Raw
  $runCompileAnchor='<ClCompile Include="pinned_Framework.cpp" />'
  if(([regex]::Matches($runPrivateXml,[regex]::Escape($runCompileAnchor))).Count -ne 1){throw 'Submitted private project anchor changed'}
  $runPrivateXml=$runPrivateXml.Replace($runCompileAnchor,($runCompileAnchor+'<ClCompile Include="pinned_DirectXCommon.cpp" />'))
  [IO.File]::WriteAllText($runPrivateProject,$runPrivateXml,[Text.UTF8Encoding]::new($false))
  @($runGraphicsSource,$runGraphicsHeader,(Join-Path $runFixture 'pinned_DirectXCommon.cpp')) | ForEach-Object {Get-FileHash -LiteralPath $_} | Format-List | Out-File -LiteralPath (Join-Path $runFixture 'graphics-input-hashes.log') -Encoding utf8
 }
 Copy-Item -LiteralPath (Join-Path $runProject '..\generated\outputs\Debug\imgui.lib') -Destination (Join-Path $runFixture 'pinned-imgui.lib')
 @($runSource,(Join-Path $runFixture 'pinned_Framework.cpp'),(Join-Path $runFixture 'Framework.h')) | ForEach-Object {Get-FileHash -LiteralPath $_} | Format-List | Out-File -LiteralPath (Join-Path $runFixture 'input-hashes.log') -Encoding utf8
 $runBuildLog=Join-Path $runFixture 'build-Debug.log'
 & $MSBuild (Join-Path $runFixture 'framework_services_fixture.vcxproj') /m:1 /p:Configuration=Debug /p:Platform=x64 "/p:VCToolsVersion=$($runTool.InnerText)" "/p:KuboProjectRoot=$runProject/" "/p:KuboSdkRoot=$runSdk/" /v:minimal *> $runBuildLog
 if($LASTEXITCODE -ne 0){Get-Content -LiteralPath $runBuildLog -Tail 30;throw 'Run candidate build failed'}
 $runBin=Join-Path $runFixture 'bin'
 if($Gpu){Copy-Item -LiteralPath (Join-Path $runSdk 'runtime\Resources') -Destination (Join-Path $runBin 'Resources') -Recurse}
 foreach($runDll in @('dxcompiler.dll','dxil.dll')){Copy-Item -LiteralPath (Join-Path $runProject "..\generated\outputs\Debug\$runDll") -Destination (Join-Path $runBin $runDll)}
 $runRedist=Join-Path ([IO.Path]::GetDirectoryName($MSBuild)) '..\..\..\..\VC\Redist\MSVC'
 $runPrefix=($runTool.InnerText.Split('.')[0..1] -join '.')+'.'
 $runOpenMp=@(Get-ChildItem -LiteralPath $runRedist -Filter vcomp140d.dll -Recurse -File | Where-Object {$_.FullName -match '\\x64\\Microsoft\.VC143\.DebugOpenMP\\' -and $_.FullName -notmatch '\\onecore\\' -and $_.FullName.Contains(('\MSVC\'+$runPrefix))} | Sort-Object FullName -Descending | Select-Object -First 1)
 if($runOpenMp.Count -ne 1){throw 'Missing matching installed Debug x64 OpenMP runtime'}
 Copy-Item -LiteralPath $runOpenMp[0].FullName -Destination (Join-Path $runBin vcomp140d.dll)
 $runVariant=if($Coordinated){'coordinated'}elseif($ProductionPlayer){'production'}elseif($Candidate){'candidate'}else{'before'}
 $runOut=Join-Path $runFixture 'native.log';$runErr=Join-Path $runFixture 'native.stderr.log'
 $runArguments=$runVariant
 if($Gpu){$runOwnedProject=Join-Path $runFixture 'projects';New-Item -ItemType Directory -Path $runOwnedProject | Out-Null;$runArguments+=' "'+$runOwnedProject+'"'}
 if($PlayerWait){$runArguments+=' wait'}
 if($PlayerDispatch){$runArguments+=' dispatch'}
 $runChild=Start-Process -FilePath (Join-Path $runBin FrameworkServicesChecks.exe) -ArgumentList $runArguments -WorkingDirectory $runProject -WindowStyle Hidden -PassThru -RedirectStandardOutput $runOut -RedirectStandardError $runErr
 $runHandle=$runChild.Handle
 $runBound=if($Gpu){50000}else{20000}
 if(!$runChild.WaitForExit($runBound)){throw 'Owned Run candidate child exceeded bound'}
 $runChild.WaitForExit();$runExit=$runChild.ExitCode;$runChild.Dispose();$runChild=$null
 Get-Content -LiteralPath $runOut;Get-Content -LiteralPath $runErr
 $runExpectedMarker=if($Coordinated -and !$Submitted -and !$Player){'FRAMEWORK_RUN_COORDINATOR cases=15 '}elseif($PlayerPartial){"RUNTIME_GAME_PARTIAL_CANDIDATE variant=$runVariant cases=28 "}elseif($Player){"RUNTIME_GAME_SHUTDOWN_CANDIDATE variant=$runVariant destroyed=32 "}elseif($Submitted){"FRAMEWORK_RUN_SUBMITTED_CANDIDATE variant=$runVariant cases=3 "}elseif($Gpu){"FRAMEWORK_RUN_GPU_CANDIDATE variant=$runVariant cases=10 "}else{"FRAMEWORK_RUN_CANDIDATE variant=$runVariant cases=15 "}
 if($runExit -ne 0 -or !(Select-String -LiteralPath $runOut -Pattern $runExpectedMarker -SimpleMatch -Quiet)){throw "Run candidate $runVariant failed: $runExit"}
 if($Coordinated -and !$ProductionRun -and !$Player){$fatalStages=if($Submitted){@(3)}else{@(0..4)};foreach($fatalStage in $fatalStages){
  $fatalOut=Join-Path $runFixture "fatal-$fatalStage.log";$fatalErr=Join-Path $runFixture "fatal-$fatalStage.stderr.log"
  $fatalArguments="fatal $fatalStage";if($Submitted){$fatalArguments+=' "'+$runOwnedProject+'"'}
  $runChild=Start-Process -FilePath (Join-Path $runBin FrameworkServicesChecks.exe) -ArgumentList $fatalArguments -WorkingDirectory $runProject -WindowStyle Hidden -PassThru -RedirectStandardOutput $fatalOut -RedirectStandardError $fatalErr
  $fatalHandle=$runChild.Handle;if(!$runChild.WaitForExit(20000)){throw 'Owned coordinator fatal child exceeded bound'}
  $runChild.WaitForExit();$fatalExit=$runChild.ExitCode;$runChild.Dispose();$runChild=$null
  Get-Content -LiteralPath $fatalOut;Get-Content -LiteralPath $fatalErr
  if($fatalExit -ne 73 -or !(Select-String -LiteralPath $fatalOut -Pattern "RUN_COORDINATOR_FATAL stage=$fatalStage attempts=3 original_identity=1 complete_host_alive=1 unwind=0 native_exit=73" -SimpleMatch -Quiet)){throw "Coordinator persistent failure stage $fatalStage failed: $fatalExit"}
  if($Submitted -and !(Select-String -LiteralPath $fatalOut -Pattern 'FRAMEWORK_RUN_SUBMITTED_FATAL submitted=1 unfenced=1 pending=11 buffers_live=1 gpu_errors=0 native_exit=73' -SimpleMatch -Quiet)){throw 'Missing complete submitted-host persistent-failure retention marker'}
 }}
 if($Coordinated -and !$ProductionRun -and $Submitted){
  $waitOut=Join-Path $runFixture 'fatal-wait.log';$waitErr=Join-Path $runFixture 'fatal-wait.stderr.log'
  $runChild=Start-Process -FilePath (Join-Path $runBin FrameworkServicesChecks.exe) -ArgumentList ('wait-timeout 3 "'+$runOwnedProject+'"') -WorkingDirectory $runProject -WindowStyle Hidden -PassThru -RedirectStandardOutput $waitOut -RedirectStandardError $waitErr
  $waitHandle=$runChild.Handle;if(!$runChild.WaitForExit(20000)){throw 'Owned coordinator wait-timeout child exceeded bound'}
  $runChild.WaitForExit();$waitExit=$runChild.ExitCode;$runChild.Dispose();$runChild=$null
  Get-Content -LiteralPath $waitOut;Get-Content -LiteralPath $waitErr
  if($waitExit -ne 73 -or !(Select-String -LiteralPath $waitOut -Pattern 'FRAMEWORK_RUN_WAIT_FATAL submitted=1 unfenced=0 pending=11 buffers_live=1 waits=3 timeouts=3 signals=1 gpu_errors=0 native_exit=73' -SimpleMatch -Quiet)){throw "Coordinated fenced incomplete-work wait gate failed: $waitExit"}
  $omittedOut=Join-Path $runFixture 'fatal-omitted.log';$omittedErr=Join-Path $runFixture 'fatal-omitted.stderr.log'
  $runChild=Start-Process -FilePath (Join-Path $runBin FrameworkServicesChecks.exe) -ArgumentList ('omitted-finalize 3 "'+$runOwnedProject+'"') -WorkingDirectory $runProject -WindowStyle Hidden -PassThru -RedirectStandardOutput $omittedOut -RedirectStandardError $omittedErr
  $omittedHandle=$runChild.Handle;if(!$runChild.WaitForExit(20000)){throw 'Owned omitted-finalize child exceeded bound'}
  $runChild.WaitForExit();$omittedExit=$runChild.ExitCode;$runChild.Dispose();$runChild=$null
  Get-Content -LiteralPath $omittedOut;Get-Content -LiteralPath $omittedErr
  if($omittedExit -ne 73 -or !(Select-String -LiteralPath $omittedOut -Pattern 'FRAMEWORK_RUN_OMITTED_FATAL returned_without_retirement=1 attempts=3 pending=11 buffers_live=1 signals=0 gpu_errors=0 native_exit=73' -SimpleMatch -Quiet)){throw "Coordinator trusted incomplete virtual Finalize return: $omittedExit"}
 }
 if($PlayerWait -and !(Select-String -LiteralPath $runOut -Pattern 'RUNTIME_GAME_WAIT_ADMISSION refused=1 hooks_before_retry=0 retry_destroyed=32 native_exit=0' -SimpleMatch -Quiet)){throw 'Missing Player wait refusal/retry gate'}
 if($PlayerDispatch -and !(Select-String -LiteralPath $runOut -Pattern 'RUNTIME_GAME_DISPATCH_ADMISSION refused=96 owners_preserved=1 native_exit=0' -SimpleMatch -Quiet)){throw 'Missing Player active-dispatch admission gate'}
 if($ProductionFenceFaults){
  foreach($fenceMode in @('fatal','wait-timeout','removed-before','removed-after','wait-failed','unexpected-wait','omitted-finalize','stale-wake')){
   $fenceOut=Join-Path $runFixture "production-$fenceMode.log";$fenceErr=Join-Path $runFixture "production-$fenceMode.stderr.log"
   $fenceArguments=if($fenceMode -eq 'stale-wake'){'stale-wake "'+$runOwnedProject+'"'}else{$fenceMode+' 3 "'+$runOwnedProject+'"'}
   $runChild=Start-Process -FilePath (Join-Path $runBin FrameworkServicesChecks.exe) -ArgumentList $fenceArguments -WorkingDirectory $runProject -WindowStyle Hidden -PassThru -RedirectStandardOutput $fenceOut -RedirectStandardError $fenceErr
   $fenceHandle=$runChild.Handle;if(!$runChild.WaitForExit(25000)){throw "Owned production fence child $fenceMode exceeded25s bound"}
   $runChild.WaitForExit();$fenceExit=$runChild.ExitCode;$runChild.Dispose();$runChild=$null
   Get-Content $fenceOut;Get-Content $fenceErr
   if($fenceMode -eq 'stale-wake'){
    if($fenceExit -ne 0 -or !(Select-String -LiteralPath $fenceOut -Pattern 'PRODUCTION_FENCE_STALE cases=3 remaining_budget=1 completed_copy=1 gpu_errors=0 native_exit=0' -SimpleMatch -Quiet)){throw 'Production stale wake authorized early completion or renewed its budget'}
   }else{
    if($fenceExit -ne 70 -or !(Select-String -LiteralPath $fenceOut -Pattern "PRODUCTION_FENCE_FATAL mode=$fenceMode attempts=3 pending=11 buffers_live=1" -SimpleMatch -Quiet) -or !(Select-String -LiteralPath $fenceOut -Pattern 'complete_host_alive=1 unwind=0 gpu_errors=0' -SimpleMatch -Quiet)){throw "Production persistent fence mode $fenceMode exit=$fenceExit"}
    $fenceDiagnostic=switch($fenceMode){
     'fatal' {'ID3D12CommandQueue::Signal failed with HRESULT 0x80004005'}
     'wait-timeout' {'ID3D12Fence wait timeout failed with HRESULT 0x800705B4'}
     'removed-before' {'ID3D12Fence device removed failed with HRESULT 0x887A0005'}
     'removed-after' {'ID3D12Fence device removed failed with HRESULT 0x887A0005'}
     'wait-failed' {'WaitForSingleObject fence failed with HRESULT 0x80004005'}
     'unexpected-wait' {'Unexpected GPU fence wait result failed with HRESULT 0x8000FFFF'}
     'omitted-finalize' {'Framework Finalize returned without terminal retirement'}
    }
    if(!(Select-String -LiteralPath $fenceErr -Pattern ('KuboEngine first shutdown error: '+$fenceDiagnostic) -SimpleMatch -Quiet) -or !(Select-String -LiteralPath $fenceErr -Pattern 'KuboEngine operation error: non-standard C++ exception' -SimpleMatch -Quiet)){throw "Production root lost original diagnostics in $fenceMode"}
   }
   Write-Output "PRODUCTION_FENCE_CHECK mode=$fenceMode joined_exit=$fenceExit"
  }
 }
 Write-Output "PASS: $runVariant actual Framework source Run; production-run=$ProductionRun GPU=$Gpu Submitted=$Submitted Player=$Player; not SDK-only or shipped Application lifetime proof"
} finally {
 if($runChild){if(!$runChild.HasExited){Stop-Process -Id $runChild.Id;$runChild.WaitForExit()};$runChild.Dispose()}
 Write-Output "Retained Run candidate inputs/logs: $runFixture"
}
