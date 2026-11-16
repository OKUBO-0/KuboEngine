[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$SdkDirectory,
      [string]$MSBuild='C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe')
$ErrorActionPreference='Stop'
$ledgerProject=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).TrimEnd('\')
$ledgerSdk=(Resolve-Path -LiteralPath $SdkDirectory).Path.TrimEnd('\')
$ledgerSource=Join-Path $ledgerProject 'engine\base\directx'
$ledgerHeader=Join-Path $ledgerSource 'DirectXCommon.h'
$ledgerSdkHeader=Join-Path $ledgerSdk 'include\engine\base\directx\DirectXCommon.h'
if((Get-FileHash -LiteralPath $ledgerHeader).Hash -ne (Get-FileHash -LiteralPath $ledgerSdkHeader).Hash){throw 'Use an SDK with the current DirectXCommon header, not a pre-ledger snapshot'}
[xml]$ledgerProps=Get-Content -LiteralPath (Join-Path $ledgerSdk 'KuboRuntime.Includes.props') -Raw
$ledgerTool=$ledgerProps.SelectSingleNode('//*[local-name()="KuboSdkVCToolsVersion"]')
if(!$ledgerTool -or [string]::IsNullOrWhiteSpace($ledgerTool.InnerText)){throw 'Missing SDK compiler contract'}
$ledgerFixture=Join-Path ([IO.Path]::GetTempPath()) ('kubo-gpu-command-state-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $ledgerFixture | Out-Null
$ledgerChild=$null
try {
 foreach($ledgerFile in @('gpu_command_state_fixture.cpp','gpu_command_state_fixture.vcxproj')){
  Copy-Item -LiteralPath (Join-Path $PSScriptRoot $ledgerFile) -Destination (Join-Path $ledgerFixture $ledgerFile)
 }
 Copy-Item -LiteralPath $ledgerHeader -Destination (Join-Path $ledgerFixture 'DirectXCommon.h')
 $ledgerBody=Get-Content -LiteralPath (Join-Path $ledgerSource 'DirectXCommon.cpp') -Raw
 # Only the owned source copy gains six injection/observation hooks. Exact admission anchors
 # deliberately fail on source drift instead of silently testing the wrong site.
 $ledgerPresent="`tCloseAndExecuteCommandList();`r`n`tThrowIfFailed(`r`n`t`tswapChain->Present"
 $ledgerSignal='commandQueue->Signal(fence.Get(), candidateValue)'
 if(([regex]::Matches($ledgerBody,[regex]::Escape($ledgerPresent))).Count -ne 1 -or
    ([regex]::Matches($ledgerBody,[regex]::Escape($ledgerSignal))).Count -ne 1){throw 'Submission fixture injection anchors changed; review before updating'}
 $ledgerBody=$ledgerBody.Replace($ledgerPresent,"`tCloseAndExecuteCommandList();`r`n`tFixtureBeforePresent();`r`n`tThrowIfFailed(`r`n`t`tswapChain->Present")
 $ledgerBody=$ledgerBody.Replace($ledgerSignal,'FixtureQueueSignal(commandQueue.Get(), fence.Get(), candidateValue)')
 $stateHooks=@(
  @{Before='hr = commandList->Close();';After='hr = FixtureClose(commandList.Get());'},
  @{Before='commandQueue->ExecuteCommandLists(1, commandLists);';After='FixtureExecute(commandQueue.Get(), commandLists[0]);'},
  @{Before='hr = frameContext.commandAllocator->Reset();';After='hr = FixtureAllocatorReset(frameContext.commandAllocator.Get());'},
  @{Before='hr = commandList->Reset(frameContext.commandAllocator.Get(), nullptr);';After='hr = FixtureListReset(commandList.Get(), frameContext.commandAllocator.Get());'}
 )
 foreach($stateHook in $stateHooks){
  if(([regex]::Matches($ledgerBody,[regex]::Escape($stateHook.Before))).Count -ne 1){throw 'Command-state injection anchor changed; review source before updating fixture'}
  $ledgerBody=$ledgerBody.Replace($stateHook.Before,$stateHook.After)
 }
 $ledgerBody="extern HRESULT FixtureClose(ID3D12GraphicsCommandList*);`r`nextern void FixtureExecute(ID3D12CommandQueue*, ID3D12CommandList*);`r`nextern HRESULT FixtureAllocatorReset(ID3D12CommandAllocator*);`r`nextern HRESULT FixtureListReset(ID3D12GraphicsCommandList*, ID3D12CommandAllocator*);`r`n"+$ledgerBody
 $ledgerBody="#include <d3d12.h>`r`nextern void FixtureBeforePresent();`r`nextern HRESULT FixtureQueueSignal(ID3D12CommandQueue*, ID3D12Fence*, UINT64);`r`n"+$ledgerBody
 [IO.File]::WriteAllText((Join-Path $ledgerFixture 'pinned_DirectXCommon.cpp'),$ledgerBody,[Text.UTF8Encoding]::new($false))
 Copy-Item -LiteralPath (Join-Path $ledgerProject '..\generated\outputs\Debug\imgui.lib') -Destination (Join-Path $ledgerFixture 'pinned-imgui.lib')
 @($ledgerHeader,(Join-Path $ledgerSource 'DirectXCommon.cpp'),(Join-Path $ledgerFixture 'pinned_DirectXCommon.cpp'),(Join-Path $ledgerFixture 'pinned-imgui.lib')) |
  ForEach-Object {Get-FileHash -LiteralPath $_} | Format-List | Out-File -LiteralPath (Join-Path $ledgerFixture 'pinned-input-hashes.log') -Encoding utf8
 $ledgerBuildLog=Join-Path $ledgerFixture 'build-Debug.log'
 & $MSBuild (Join-Path $ledgerFixture 'gpu_command_state_fixture.vcxproj') /m:1 /p:Configuration=Debug /p:Platform=x64 "/p:VCToolsVersion=$($ledgerTool.InnerText)" "/p:KuboProjectRoot=$ledgerProject/" "/p:KuboSdkRoot=$ledgerSdk/" /v:minimal *> $ledgerBuildLog
 if($LASTEXITCODE -ne 0){Get-Content -LiteralPath $ledgerBuildLog -Tail 30;throw 'GPU submission fixture build failed'}
 $ledgerBin=Join-Path $ledgerFixture 'bin'
 foreach($ledgerDll in @('dxcompiler.dll','dxil.dll')){
  Copy-Item -LiteralPath (Join-Path $ledgerProject "..\generated\outputs\Debug\$ledgerDll") -Destination (Join-Path $ledgerBin $ledgerDll)
 }
 $ledgerOpenMp=Join-Path ([IO.Path]::GetDirectoryName($MSBuild)) '..\..\..\..\VC\Redist\MSVC'
 $ledgerToolPrefix=($ledgerTool.InnerText.Split('.')[0..1] -join '.')+'.'
 $ledgerOpenMpDll=@(Get-ChildItem -LiteralPath $ledgerOpenMp -Filter 'vcomp140d.dll' -File -Recurse | Where-Object {
  $_.FullName -match '\\x64\\Microsoft\.VC143\.DebugOpenMP\\' -and $_.FullName -notmatch '\\onecore\\' -and
  $_.FullName.Contains(('\MSVC\'+$ledgerToolPrefix))
 } | Sort-Object FullName -Descending | Select-Object -First 1)
 if($ledgerOpenMpDll.Count -ne 1){throw 'Debug x64 OpenMP runtime must be available in the installed compiler redist'}
 Copy-Item -LiteralPath $ledgerOpenMpDll[0].FullName -Destination (Join-Path $ledgerBin 'vcomp140d.dll')
 foreach($ledgerMode in @('cold','normal','recorded','close','present-recover','signal-recover','shutdown-retry','allocator-reset','list-reset')){
  $ledgerOut=Join-Path $ledgerFixture "$ledgerMode.log";$ledgerErr=Join-Path $ledgerFixture "$ledgerMode.stderr.log"
  $ledgerChild=Start-Process -FilePath (Join-Path $ledgerBin 'GpuSubmissionChecks.exe') -ArgumentList $ledgerMode -WorkingDirectory $ledgerProject -WindowStyle Hidden -PassThru -RedirectStandardOutput $ledgerOut -RedirectStandardError $ledgerErr
  $ledgerHandle=$ledgerChild.Handle
  if(!$ledgerChild.WaitForExit(50000)){throw "Owned GPU submission child timed out: $ledgerMode"}
  $ledgerChild.WaitForExit();$ledgerExit=$ledgerChild.ExitCode
  $ledgerChild.Dispose();$ledgerChild=$null
  Get-Content -LiteralPath $ledgerOut;Get-Content -LiteralPath $ledgerErr
  if($ledgerExit -ne 0){throw "GPU submission $ledgerMode exit=$ledgerExit"}
  $ledgerMarker=if($ledgerMode -eq 'cold'){'GPU_COMMAND_STATE cold=1'}else{"GPU_COMMAND_STATE mode=$ledgerMode "}
  if(!(Select-String -LiteralPath $ledgerOut -Pattern $ledgerMarker -SimpleMatch -Quiet)){throw "Missing checked recovery marker: $ledgerMode"}
  Write-Output "GPU_COMMAND_STATE_CHECK mode=$ledgerMode native_exit=$ledgerExit"
 }
 Write-Output 'PASS: nine Debug command-state controls (cold CPU-only, eight real-device); source probe, not SDK-only/device-removal proof'
} finally {
 if($ledgerChild){if(!$ledgerChild.HasExited){Stop-Process -Id $ledgerChild.Id;$ledgerChild.WaitForExit()};$ledgerChild.Dispose()}
 Write-Output "Retained GPU submission fixture/logs: $ledgerFixture"
}
