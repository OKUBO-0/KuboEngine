[CmdletBinding()]
param(
    [ValidateRange(1,100)][int]$Iterations=30,
    [string]$Runner
)
$ErrorActionPreference='Stop'
$repeatWorking=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if([string]::IsNullOrWhiteSpace($Runner)) {$Runner=Join-Path $PSScriptRoot '..\..\generated\cpu-checks\export_job_checks.exe'}
$repeatRunner=[IO.Path]::GetFullPath($Runner)
if(!(Test-Path -LiteralPath $repeatRunner -PathType Leaf)) {throw 'Build export_job_checks with run_cpu_checks.ps1 first'}
$repeatLogs=Join-Path ([IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\generated\cpu-checks'))) ('export-repetitions-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $repeatLogs | Out-Null
$repeatRecords=@()
$repeatDigest=(Get-FileHash -LiteralPath $repeatRunner -Algorithm SHA256).Hash.ToLowerInvariant()
function Save-RepeatReport {
    [ordered]@{format='kubo-export-repetitions-1';runner=$repeatRunner;sha256=$repeatDigest;requested=$Iterations;completed=$repeatRecords.Count;runs=$repeatRecords} |
        ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $repeatLogs 'summary.json') -Encoding UTF8
}
Save-RepeatReport
Write-Output "Retained repetition logs: $repeatLogs"
for($repeatIndex=1;$repeatIndex -le $Iterations;$repeatIndex++) {
    $repeatOut=Join-Path $repeatLogs ("run-$repeatIndex.out.log")
    $repeatErr=Join-Path $repeatLogs ("run-$repeatIndex.err.log")
    $repeatProcess=$null
    try {
        Write-Output "Export cancellation repetition $repeatIndex/$Iterations"
        $repeatProcess=Start-Process -FilePath $repeatRunner -WorkingDirectory $repeatWorking -WindowStyle Hidden -PassThru -RedirectStandardOutput $repeatOut -RedirectStandardError $repeatErr
        $repeatHandle=$repeatProcess.Handle
        if($repeatHandle -eq [IntPtr]::Zero) {throw 'Runner process handle unavailable'}
        $repeatClock=[Diagnostics.Stopwatch]::StartNew();$repeatNextNotice=30
        while(!$repeatProcess.WaitForExit(1000)) {
            if($repeatClock.Elapsed.TotalSeconds -ge $repeatNextNotice) {
                Write-Output "Runner PID $($repeatProcess.Id) is still live; waiting on the same handle (logs: $repeatOut, $repeatErr)"
                $repeatNextNotice+=30
            }
        }
        $repeatProcess.WaitForExit();$repeatClock.Stop()
        [string]$repeatText=Get-Content -LiteralPath $repeatOut -Raw -Encoding UTF8
        [string]$repeatErrors=Get-Content -LiteralPath $repeatErr -Raw -Encoding UTF8
        if($repeatText) {Write-Output $repeatText.TrimEnd()}
        if($repeatErrors) {Write-Output $repeatErrors.TrimEnd()}
        $repeatPassed=$repeatProcess.ExitCode -eq 0 -and
            $repeatText.Contains('PASS: native/prebuilt cancel waits for child/grandchild job exit, preserves unrelated process/partial output and permits clean restart') -and
            $repeatText.Contains('PASS: async export, Unicode/metacharacter paths, log capture, concurrent rejection and failure isolation')
        $repeatRecords+=@{iteration=$repeatIndex;pid=$repeatProcess.Id;exitCode=$repeatProcess.ExitCode;seconds=$repeatClock.Elapsed.TotalSeconds;passed=$repeatPassed;stdout=$repeatOut;stderr=$repeatErr}
        Save-RepeatReport
        if(!$repeatPassed) {throw "Export repetition failed: $repeatIndex; retained report/logs: $repeatLogs"}
        if((Get-FileHash -LiteralPath $repeatRunner -Algorithm SHA256).Hash.ToLowerInvariant() -ne $repeatDigest) {throw 'Runner changed during repetition; do not rebuild a live verification executable'}
    } finally {
        # No name-based kill, no observation-timeout restart. A live runner is
        # waited above; disposing its handle is not cancellation of its exports.
        if($repeatProcess) {$repeatProcess.Dispose()}
    }
}
Write-Output "PASS: $Iterations serial export repetitions; both native/prebuilt cancellation and restart markers, exit codes and unchanged runner digest; logs: $repeatLogs"
