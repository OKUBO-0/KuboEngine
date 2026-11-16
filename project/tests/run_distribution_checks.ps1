[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$EngineDirectory,
      [Parameter(Mandatory=$true)][string]$SdkDirectory,
      [string]$LogDirectory,
      [switch]$WithRender,
      [switch]$WithUI,
      [switch]$WithParticles,
      [ValidateRange(1,1024)][double]$MinimumFreeGiB=4)
$ErrorActionPreference='Stop'
$distributionEngine=(Resolve-Path -LiteralPath $EngineDirectory).Path.TrimEnd('\')
$distributionSdk=(Resolve-Path -LiteralPath $SdkDirectory).Path.TrimEnd('\')
if([string]::IsNullOrWhiteSpace($LogDirectory)){
 $LogDirectory=Join-Path $PSScriptRoot ('..\..\generated\distribution-runs\'+[guid]::NewGuid().ToString('N'))
}
$distributionLogs=[IO.Path]::GetFullPath($LogDirectory).TrimEnd('\')
foreach($package in @($distributionEngine,$distributionSdk)){
 if($distributionLogs.Equals($package,[StringComparison]::OrdinalIgnoreCase) -or $distributionLogs.StartsWith($package+'\',[StringComparison]::OrdinalIgnoreCase)){
  throw 'Verification logs must not modify immutable package directories'
 }
}
function Confirm-DistributionSpace {
 foreach($root in @([IO.Path]::GetPathRoot($distributionLogs),[IO.Path]::GetPathRoot([IO.Path]::GetFullPath([IO.Path]::GetTempPath()))) | Select-Object -Unique){
  $drive=[IO.DriveInfo]::new($root)
  if(!$drive.IsReady -or $drive.AvailableFreeSpace -lt ($MinimumFreeGiB*1GB)){
   throw "Insufficient free space on $root; require at least $MinimumFreeGiB GiB before starting distribution consumers"
  }
 }
}
Confirm-DistributionSpace
foreach($manifest in @((Join-Path $distributionEngine 'engine-package.json'),(Join-Path $distributionSdk 'sdk-package.json'))){
 if(!(Test-Path -LiteralPath $manifest -PathType Leaf)){throw "Missing package manifest: $manifest"}
}
if((Test-Path -LiteralPath $distributionLogs) -and @(Get-ChildItem -LiteralPath $distributionLogs -Force).Count){throw 'Choose a new empty verification log directory'}
$distributionHosts=@('powershell.exe','pwsh.exe')
$distributionHostPaths=@{}
foreach($hostName in $distributionHosts){
 # PATH may contain several installations. Preserve command precedence, but
 # never concatenate multiple executable paths into a single invocation.
 $distributionCommands=@(Get-Command $hostName -CommandType Application -ErrorAction Stop)
 $distributionHostPaths[$hostName]=$distributionCommands[0].Source
 if(!(Test-Path -LiteralPath $distributionHostPaths[$hostName] -PathType Leaf)){throw "Missing resolved host executable: $hostName"}
}
New-Item -ItemType Directory -Path $distributionLogs -Force | Out-Null
$distributionCases=@(
 @{Name='sdk';Script='runtime_sdk_checks.ps1';Arguments=@('-SdkDirectory',$distributionSdk,'-NativeBuildExport','-PhysicsHierarchy')},
 @{Name='sdk-prefab';Script='sdk_prefab_checks.ps1';Arguments=@('-SdkDirectory',$distributionSdk)},
 @{Name='srv-contract';Script='sdk_srv_ownership_checks.ps1';Arguments=@('-SdkDirectory',$distributionSdk,'-KeepFixture')},
 @{Name='portable';Script='portable_engine_e2e_checks.ps1';Arguments=@('-EngineDirectory',$distributionEngine,'-NativeSdkDirectory',$distributionSdk)}
)
if($WithRender){$distributionCases[1].Arguments+=@('-WithRender')}
if($WithUI){$distributionCases[1].Arguments+=@('-WithUI')}
if($WithParticles){$distributionCases[1].Arguments+=@('-WithParticles')}
foreach($protocol in @('2025-11-25','2024-11-05')){
 $distributionMcpArguments=@('-EngineDirectory',$distributionEngine,'-ProtocolVersion',$protocol,'-WithCamera','-WithModel','-WithScript','-WithAudio','-UnicodePaths','-OrthographicCamera')
 # Preserve old-package six-component replay; new authored-light packages opt in.
 if($WithRender){$distributionMcpArguments+=@('-WithRender')}
 # MCP covers live IPC/startup/persistence; sdk-prefab separately reads UI pixels.
 if($WithUI){$distributionMcpArguments+=@('-WithUI')}
 $distributionCases+=@{Name="mcp-$protocol";Script='editor_mcp_e2e_checks.ps1';Arguments=$distributionMcpArguments}
}
foreach($script in @('distribution_snapshot_checks','distribution_snapshot_fixture_checks')){
 $distributionCases+=@{Name=$script;Script="$script.ps1";Arguments=@('-EngineDirectory',$distributionEngine,'-SdkDirectory',$distributionSdk)}
}
$distributionPassed=0
foreach($hostName in $distributionHosts){foreach($case in $distributionCases){
 Confirm-DistributionSpace
 $caseScript=Join-Path $PSScriptRoot $case.Script
 if(!(Test-Path -LiteralPath $caseScript -PathType Leaf)){throw "Missing verification script: $caseScript"}
 $caseLog=Join-Path $distributionLogs "$($case.Name)-$hostName.log"
 $caseErrorLog=Join-Path $distributionLogs "$($case.Name)-$hostName.stderr.log"
 Write-Output "DISTRIBUTION_START=$($case.Name) HOST=$hostName"
 # Native stderr can contain intentional negative-control diagnostics. Do not
 # let PowerShell's NativeCommandError promotion interrupt a successful child.
 # Quote each Windows argv element (including trailing backslashes/quotes).
 $caseArguments=@('-NoProfile','-ExecutionPolicy','Bypass','-File',$caseScript)+@($case.Arguments)
 $caseCommandLine=($caseArguments | ForEach-Object {
  '"'+[regex]::Replace([regex]::Replace([string]$_,'(\\*)"','$1$1\"'),'(\\+)$','$1$1')+'"'
 }) -join ' '
 $caseChild=$null;$caseExit=$null
 try {
  # Start-Process inherits the raw PSModulePath, unlike a direct native
  # PowerShell invocation. A PS7 parent can otherwise make Windows PowerShell
  # load PS7's Utility manifest, which does not expose its Get-FileHash function.
  # Prioritize the target's built-in modules only during process creation;
  # restore this runner's process environment even when creation fails.
  $caseOriginalModulePath=[Environment]::GetEnvironmentVariable('PSModulePath','Process')
  $caseBuiltinModules=Join-Path ([IO.Path]::GetDirectoryName($distributionHostPaths[$hostName])) 'Modules'
  if(!(Test-Path -LiteralPath (Join-Path $caseBuiltinModules 'Microsoft.PowerShell.Utility') -PathType Container)){throw "Missing target host built-in Utility module: $hostName"}
  try {
   [Environment]::SetEnvironmentVariable('PSModulePath',($caseBuiltinModules+[IO.Path]::PathSeparator+$caseOriginalModulePath),'Process')
   $caseChild=Start-Process -FilePath $distributionHostPaths[$hostName] -ArgumentList $caseCommandLine -WorkingDirectory $PSScriptRoot -WindowStyle Hidden -PassThru -RedirectStandardOutput $caseLog -RedirectStandardError $caseErrorLog
  } finally {
   [Environment]::SetEnvironmentVariable('PSModulePath',$caseOriginalModulePath,'Process')
  }
  $caseHandle=$caseChild.Handle
  while(!$caseChild.WaitForExit(1000)) { } # Observation timeout is not failure.
  $caseChild.WaitForExit()
  $caseExit=$caseChild.ExitCode
 } finally {
  if($caseChild){if(!$caseChild.HasExited){Stop-Process -Id $caseChild.Id;$caseChild.WaitForExit()};$caseChild.Dispose()}
 }
 if($null -eq $caseExit){throw "Missing independent terminal exit: $($case.Name) $hostName"}
 Get-Content -LiteralPath $caseLog -Tail 8
 Get-Content -LiteralPath $caseErrorLog -Tail 8
 if($caseExit -ne 0){throw "Distribution consumer failed: $($case.Name) $hostName exit=$caseExit; log=$caseLog"}
 $distributionPassed++
 Write-Output "DISTRIBUTION_PASS=$($case.Name) HOST=$hostName EXIT=$caseExit"
}}
if($distributionPassed -ne ($distributionCases.Count*$distributionHosts.Count)){throw 'Incomplete distribution verification'}
Write-Output "DISTRIBUTION_SUMMARY=$distributionPassed EXPECTED=$($distributionCases.Count*$distributionHosts.Count)"
Write-Output "DISTRIBUTION_RENDER_COMPONENTS=$([bool]$WithRender)"
Write-Output "DISTRIBUTION_MCP_UI_COMPONENTS=$([bool]$WithUI)"
Write-Output "DISTRIBUTION_PARTICLE_GROUPS=$([bool]$WithParticles)"
Write-Output "DISTRIBUTION_PARTICLE_AUTHORING=$([bool]$WithParticles)"
Write-Output "Retained distribution logs: $distributionLogs"
