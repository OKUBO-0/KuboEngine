[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$SdkDirectory,
      [switch]$RequireRunReport,
      [string]$MSBuild='C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe')
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'sdk_link_inputs.ps1')
$fenceSdk=(Resolve-Path -LiteralPath $SdkDirectory).Path.TrimEnd('\')
$fenceRepo=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path.TrimEnd('\')
$fenceFixture=[IO.Path]::GetFullPath((Join-Path $fenceRepo ('..\generated\sdk-fence-consumers\'+[guid]::NewGuid().ToString('N'))))
$fenceChild=$null
New-Item -ItemType Directory -Path $fenceFixture | Out-Null
try{
 $fenceManifest=Get-Content -LiteralPath (Join-Path $fenceSdk 'sdk-package.json') -Raw -Encoding UTF8 | ConvertFrom-Json
 foreach($entry in $fenceManifest.files){if((Get-FileHash -LiteralPath (Join-Path $fenceSdk $entry.path)).Hash.ToLowerInvariant() -ne $entry.sha256){throw 'SDK digest mismatch before consumer build'}}
 foreach($file in @('sdk_fence_checks.cpp','sdk_fence_checks.vcxproj')){Copy-Item -LiteralPath (Join-Path $PSScriptRoot $file) -Destination (Join-Path $fenceFixture $file)}
 [xml]$fenceProps=Get-Content -LiteralPath (Join-Path $fenceSdk 'KuboRuntime.Includes.props') -Raw
 $fenceTool=$fenceProps.SelectSingleNode('//*[local-name()="KuboSdkVCToolsVersion"]');if(!$fenceTool){throw 'SDK compiler metadata absent'}
 & $MSBuild (Join-Path $fenceFixture 'sdk_fence_checks.vcxproj') /m:1 /p:Configuration=Debug /p:Platform=x64 "/p:KuboSdkRoot=$fenceSdk\" "/p:VCToolsVersion=$($fenceTool.InnerText)" /verbosity:minimal *> (Join-Path $fenceFixture 'build.log')
 if($LASTEXITCODE -ne 0){Get-Content (Join-Path $fenceFixture 'build.log') -Tail 25;throw 'SDK-only fence consumer build failed'}
 $fenceReads=@(Get-ChildItem (Join-Path $fenceFixture 'obj') -Filter CL.read.1.tlog -Recurse -File);if(!$fenceReads.Count){throw 'Missing compiler read evidence'}
 foreach($read in $fenceReads){foreach($line in Get-Content -LiteralPath $read.FullName){if($line.StartsWith($fenceRepo+'\engine\',[StringComparison]::OrdinalIgnoreCase) -or $line.StartsWith($fenceRepo+'\tests\',[StringComparison]::OrdinalIgnoreCase)){throw 'SDK consumer read original engine/test source'}}}
 $fenceLinks=@(Get-ChildItem (Join-Path $fenceFixture 'obj') -Filter link.command.1.tlog -Recurse -File);if(!$fenceLinks.Count -or !(Test-SdkRuntimeLinkInputs ((Get-Content $fenceLinks[0].FullName -Raw)))){throw 'SDK consumer linker has non-runtime inputs'}
 $fenceRedist=Join-Path ([IO.Path]::GetDirectoryName($MSBuild)) '..\..\..\..\VC\Redist\MSVC';$fencePrefix=($fenceTool.InnerText.Split('.')[0..1] -join '.')+'.'
 $fenceOpenMp=@(Get-ChildItem -LiteralPath $fenceRedist -Filter vcomp140d.dll -Recurse -File | Where-Object {$_.FullName -match '\\x64\\Microsoft\.VC143\.DebugOpenMP\\' -and $_.FullName -notmatch '\\onecore\\' -and $_.FullName.Contains(('\MSVC\'+$fencePrefix))} | Sort-Object FullName -Descending | Select-Object -First 1)
 if($fenceOpenMp.Count -ne 1){throw 'Matching installed Debug OpenMP runtime absent'}
 Copy-Item -LiteralPath $fenceOpenMp[0].FullName -Destination (Join-Path $fenceFixture 'bin\vcomp140d.dll')
 $fenceWorking=Join-Path $fenceFixture 'unrelated-cwd';New-Item -ItemType Directory -Path $fenceWorking | Out-Null
 foreach($mode in @('recovery','persistent')){
  $fenceOut=Join-Path $fenceFixture "$mode.log";$fenceErr=Join-Path $fenceFixture "$mode.stderr.log"
  $fenceChild=Start-Process -FilePath (Join-Path $fenceFixture 'bin\SdkFenceChecks.exe') -ArgumentList ($mode+' "'+(Join-Path $fenceFixture "project-$mode")+'"') -WorkingDirectory $fenceWorking -WindowStyle Hidden -PassThru -RedirectStandardOutput $fenceOut -RedirectStandardError $fenceErr
  $fenceHandle=$fenceChild.Handle;if(!$fenceChild.WaitForExit(35000)){throw "Owned SDK-only $mode child exceeded35s bound"}
  $fenceChild.WaitForExit();$fenceExit=$fenceChild.ExitCode;$fenceChild.Dispose();$fenceChild=$null
  Get-Content -LiteralPath $fenceOut -Encoding UTF8;Get-Content -LiteralPath $fenceErr -Encoding UTF8
  if($mode -eq 'recovery'){
   if($fenceExit -ne 0 -or !(Select-String -LiteralPath $fenceOut -Pattern 'SDK_FENCE_RECOVERY original_identity=1 terminal_before_throw=1 copied_bytes=64 exact_once=1 gpu_errors=0 native_exit=0' -SimpleMatch -Quiet)){throw "SDK-only recovery failed: $fenceExit"}
  }else{
   if($fenceExit -ne 70 -or !(Select-String -LiteralPath $fenceOut -Pattern 'SDK_FENCE_FATAL attempts=3 real_timeouts=3 pending=11 buffers_live=1 complete_host_alive=1 unwind=0 gpu_errors=0' -SimpleMatch -Quiet) -or !(Select-String -LiteralPath $fenceErr -Pattern 'KuboEngine first shutdown error: ID3D12Fence wait timeout failed with HRESULT 0x800705B4' -SimpleMatch -Quiet) -or !(Select-String -LiteralPath $fenceErr -Pattern 'KuboEngine operation error:' -SimpleMatch -Quiet)){throw "SDK-only real deadline/full-host retention failed: $fenceExit"}
  }
  if($RequireRunReport){
   $reports=@(Get-ChildItem -LiteralPath (Join-Path $fenceFixture "project-$mode") -Filter '.kubo-run-error-*.log' -File)
   if($reports.Count -ne 1){throw "Expected one project-owned run report for $mode"}
   $reportText=Get-Content -LiteralPath $reports[0].FullName -Raw -Encoding UTF8
   $reportComplete=if($mode -eq 'persistent'){'0'}else{'1'}
   $reportAttempts=if($mode -eq 'persistent'){'3'}else{'1'}
   if(!$reportText.Contains("shutdown_complete=$reportComplete`nshutdown_attempts=$reportAttempts") -or
      !$reportText.Contains("[operation error]`nnon-standard C++ exception`n") -or
      !$reportText.EndsWith("END KuboEngine run report`n")){throw "Run report metadata/operation/end marker failed: $mode"}
   if($mode -eq 'persistent' -and !$reportText.Contains('ID3D12Fence wait timeout failed with HRESULT 0x800705B4')){throw 'Persistent report lost original cleanup diagnostic'}
   if($mode -eq 'recovery' -and !$reportText.Contains("[first shutdown error]`nnone`n")){throw 'Recovery report fabricated cleanup error'}
   if(@(Get-ChildItem -LiteralPath $fenceWorking -Filter '.kubo-run-error-*.log' -File).Count){throw 'Run report was written to unrelated cwd'}
   Write-Output "SDK_RUN_REPORT mode=$mode paired_diagnostics=1 captured_project=1 complete=$reportComplete attempts=$reportAttempts"
  }
  Write-Output "SDK_FENCE_CHECK mode=$mode joined_exit=$fenceExit"
 }
 foreach($entry in $fenceManifest.files){if((Get-FileHash -LiteralPath (Join-Path $fenceSdk $entry.path)).Hash.ToLowerInvariant() -ne $entry.sha256){throw 'Consumer modified SDK package'}}
 Write-Output 'PASS: SDK-only Debug prebuilt finite wait/recovery/persistent no-unwind; compiler/link/immutable package evidence; not device-removal injection or clean-machine proof'
}finally{
 if($fenceChild){if(!$fenceChild.HasExited){Stop-Process -Id $fenceChild.Id;$fenceChild.WaitForExit()};$fenceChild.Dispose()}
 Write-Output "Retained SDK-only fence consumer: $fenceFixture"
}
