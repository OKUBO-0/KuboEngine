[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$LogDirectory,[string]$Only)
$ErrorActionPreference='Stop'
if(!(Get-Command cl.exe -ErrorAction SilentlyContinue)){throw 'Run from the pinned x64 Developer PowerShell environment'}
$recordedRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$recordedLogs=[IO.Path]::GetFullPath($LogDirectory).TrimEnd('\')
if(Test-Path -LiteralPath $recordedLogs){throw 'Choose a new log directory; preserve previous run evidence'}
if($recordedLogs.Equals($recordedRoot,[StringComparison]::OrdinalIgnoreCase) -or $recordedRoot.StartsWith($recordedLogs+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Log directory cannot contain the project root'}
$recordedRunner=Join-Path $PSScriptRoot 'run_cpu_checks.ps1'
$recordedBefore=(Get-FileHash -LiteralPath $recordedRunner -Algorithm SHA256).Hash
$recordedHost=(Get-Process -Id $PID).Path
$recordedArguments=@('-NoProfile','-ExecutionPolicy','Bypass','-File',$recordedRunner)
if($Only){$recordedArguments+=@('-Only',$Only)}
$recordedCommandLine=($recordedArguments | ForEach-Object {'"'+[regex]::Replace([regex]::Replace([string]$_,'(\\*)"','$1$1\"'),'(\\+)$','$1$1')+'"'}) -join ' '
New-Item -ItemType Directory -Path $recordedLogs | Out-Null
$recordedOut=Join-Path $recordedLogs 'stdout.log';$recordedErr=Join-Path $recordedLogs 'stderr.log'
$recordedChild=$null;$recordedExit=$null
$recordedModulePath=[Environment]::GetEnvironmentVariable('PSModulePath','Process')
try{
    try{
        [Environment]::SetEnvironmentVariable('PSModulePath',((Join-Path (Split-Path $recordedHost) 'Modules')+[IO.Path]::PathSeparator+$recordedModulePath),'Process')
        $recordedChild=Start-Process -FilePath $recordedHost -ArgumentList $recordedCommandLine -WorkingDirectory $recordedRoot -WindowStyle Hidden -PassThru -RedirectStandardOutput $recordedOut -RedirectStandardError $recordedErr
        $recordedHandle=$recordedChild.Handle
    }finally{[Environment]::SetEnvironmentVariable('PSModulePath',$recordedModulePath,'Process')}
    # These are generated verification artifacts, not source or package writes.
    [ordered]@{format='kubo-cpu-process-1';processId=$recordedChild.Id;startTimeUtc=$recordedChild.StartTime.ToUniversalTime().ToString('o');hostExecutable=$recordedHost;runner=$recordedRunner;runnerSha256=$recordedBefore;only=$Only} |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $recordedLogs 'process.json') -Encoding UTF8
    Write-Output "CPU_RECORDED_LOGS=$recordedLogs CHILD=$($recordedChild.Id)"
    while(!$recordedChild.WaitForExit(1000)){} # Observation timeout is not termination.
    $recordedChild.WaitForExit();$recordedExit=$recordedChild.ExitCode
    if($null -eq $recordedExit){throw 'Missing independently observed CPU terminal exit'}
    $recordedAfter=(Get-FileHash -LiteralPath $recordedRunner -Algorithm SHA256).Hash
    [ordered]@{format='kubo-cpu-terminal-1';processId=$recordedChild.Id;exitCode=$recordedExit;completedAtUtc=[DateTime]::UtcNow.ToString('o');runnerSha256Before=$recordedBefore;runnerSha256After=$recordedAfter;registryUnchanged=($recordedBefore -eq $recordedAfter);only=$Only} |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $recordedLogs 'terminal.json') -Encoding UTF8
    Write-Output "CPU_RECORDED_EXIT=$recordedExit"
    if($recordedExit -ne 0){throw "CPU child failed exit=$recordedExit; retain stdout/stderr and terminal receipt"}
    if($recordedAfter -ne $recordedBefore){throw 'CPU registry changed during recorded verification'}
    $recordedCoverageArguments=@{LogPath=$recordedOut;TerminalExitCode=$recordedExit;RequireComplete=$true}
    if($Only){$recordedCoverageArguments.OnlySuite=$Only}
    $recordedCoverage=& (Join-Path $PSScriptRoot 'check_cpu_run_coverage.ps1') @recordedCoverageArguments
    $recordedCoverage | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $recordedLogs 'coverage.json') -Encoding UTF8
    $recordedCoverage | Format-List
}finally{
    if($recordedChild){if(!$recordedChild.HasExited){$recordedChild.Kill();$recordedChild.WaitForExit()};$recordedChild.Dispose()}
}
