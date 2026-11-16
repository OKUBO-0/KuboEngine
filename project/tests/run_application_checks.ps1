[CmdletBinding()]
param([string]$BuildDirectory)
$ErrorActionPreference='Stop'
if([string]::IsNullOrWhiteSpace($BuildDirectory)) {$BuildDirectory=Join-Path $PSScriptRoot '..\..\generated\outputs\Debug'}
$applicationBuild=[IO.Path]::GetFullPath($BuildDirectory)
$applicationWorking=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$applicationLogs=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\generated\cpu-checks'))
foreach($required in @('EngineResourceChecks.exe','PlayerRuntimeChecks.exe')) {
    if(!(Test-Path -LiteralPath (Join-Path $applicationBuild $required) -PathType Leaf)) {throw "Build optional application runners first: $required"}
}
New-Item -ItemType Directory -Path $applicationLogs -Force | Out-Null
$applicationTag=[guid]::NewGuid().ToString('N')
$applicationCases=@()
foreach($argument in @('','--generic','--lua','--lua-failure','--lua-input','--lua-properties','--animator','--prefab','--lights','--ui','--lua-ui','--particles')) {$applicationCases+=@{exe='PlayerRuntimeChecks.exe';argument=$argument}}
foreach($argument in @('','--shaders','--gizmo-input','--generic-views','--render','--mcp-export-guard','--fixed-hooks','--git-index','--shutdown','--animator','--controller-save','--controller-save-as','--controller-recovery','--controller-owner-retry','--startup-close','--prefab','--lights','--render-inspector','--ui-inspector','--ui-move','--particles','--particle-authoring')) {$applicationCases+=@{exe='EngineResourceChecks.exe';argument=$argument}}
$applicationCases+=@{exe='EngineResourceChecks.exe';argument='--asset-thumbnails'}
$applicationCases+=@{exe='EngineResourceChecks.exe';argument='--scene-grid'}
$applicationCases+=@{exe='EngineResourceChecks.exe';argument='--selection-inspector'}
$applicationCases+=@{exe='EngineResourceChecks.exe';argument='--editor-input-focus'}
$applicationCases+=@{exe='EngineResourceChecks.exe';argument='--editor-input-focus-oversized'}
$applicationCases+=@{exe='EngineResourceChecks.exe';argument='--editor-endurance'}
$applicationCases+=@{exe='EngineResourceChecks.exe';argument='--editor-toolbar'}
$applicationCases+=@{exe='EngineResourceChecks.exe';argument='--scene-asset-placement'}
$applicationCases+=@{exe='EngineResourceChecks.exe';argument='--scene-transition'}
$applicationCases+=@{exe='PlayerRuntimeChecks.exe';argument='--scene-transition'}
$applicationIndex=0
foreach($case in $applicationCases) {
    $applicationIndex++
    $stdout=Join-Path $applicationLogs ("application-$applicationTag-$applicationIndex.out.log")
    $stderr=Join-Path $applicationLogs ("application-$applicationTag-$applicationIndex.err.log")
    $launch=@{FilePath=(Join-Path $applicationBuild $case.exe);WorkingDirectory=$applicationWorking;WindowStyle='Hidden';PassThru=$true;RedirectStandardOutput=$stdout;RedirectStandardError=$stderr}
    if($case.argument) {$launch.ArgumentList=$case.argument}
    # The composite document scenario and sustained rendering check need
    # longer bounds on the checked host.
    $applicationTimeout=switch($case.argument){
        '--generic-views' {60000}
        '--editor-endurance' {120000}
        default {30000}
    }
    Write-Output "Running $($case.exe) '$($case.argument)' timeoutMs=$applicationTimeout"
    $applicationWatch=[Diagnostics.Stopwatch]::StartNew()
    $ownedApplication=$null
    try {
        $ownedApplication=Start-Process @launch;$ownedHandle=$ownedApplication.Handle
        if(!$ownedApplication.WaitForExit($applicationTimeout)) {throw "Owned runner timed out: $($case.exe) $($case.argument), bound $applicationTimeout ms; logs: $stdout, $stderr"}
        $ownedApplication.WaitForExit()
        Get-Content -LiteralPath $stdout -Encoding UTF8;Get-Content -LiteralPath $stderr -Encoding UTF8
        if($ownedApplication.ExitCode -ne 0) {throw "Runner failed: $($case.exe) $($case.argument), exit $($ownedApplication.ExitCode); logs: $stdout, $stderr"}
        if(@(Get-Content -LiteralPath $stdout -Encoding UTF8) -notcontains ('RUNNER_MODE='+$case.argument)) {throw "Requested runner mode was not executed: $($case.exe) '$($case.argument)'"}
        Write-Output "RUNNER_ELAPSED_MS=$($applicationWatch.ElapsedMilliseconds) MODE=$($case.argument)"
    } finally {
        if($ownedApplication) {
            if(!$ownedApplication.HasExited) {Stop-Process -Id $ownedApplication.Id -Force;$ownedApplication.WaitForExit()}
            $ownedApplication.Dispose()
        }
    }
}
Write-Output "PASS: all $($applicationCases.Count) application runner modes, owned hidden processes and retained per-run logs"
