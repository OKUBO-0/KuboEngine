[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$LogPath,
    [string]$RunnerPath,
    [string]$OnlySuite,
    [Nullable[int]]$TerminalExitCode,
    [switch]$RequireComplete
)
$ErrorActionPreference='Stop'
if([string]::IsNullOrWhiteSpace($RunnerPath)){$RunnerPath=Join-Path $PSScriptRoot 'run_cpu_checks.ps1'}
# Parse declarations, never dot-source/execute the compiler runner to inventory it.
$coverageTokens=$null;$coverageErrors=$null
$coverageAst=[Management.Automation.Language.Parser]::ParseFile([IO.Path]::GetFullPath($RunnerPath),[ref]$coverageTokens,[ref]$coverageErrors)
if($coverageErrors.Count){throw 'CPU runner syntax errors prevent coverage inventory'}
$coverageAssignments=@($coverageAst.FindAll({param($node)
    $node -is [Management.Automation.Language.AssignmentStatementAst] -and
    $node.Left -is [Management.Automation.Language.VariableExpressionAst] -and
    $node.Left.VariablePath.UserPath -eq 'checks'
},$true))
if($coverageAssignments.Count -ne 1){throw 'Require exactly one static checks declaration'}
$coverageTables=@($coverageAssignments[0].Right.FindAll({param($node) $node -is [Management.Automation.Language.HashtableAst]},$true))
$coverageNames=@();$coverageSet=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
foreach($table in $coverageTables){
    $pairs=@($table.KeyValuePairs | Where-Object {$_.Item1 -is [Management.Automation.Language.StringConstantExpressionAst] -and $_.Item1.Value -eq 'Name'})
    if($pairs.Count -ne 1 -or $pairs[0].Item2 -isnot [Management.Automation.Language.PipelineAst] -or $pairs[0].Item2.PipelineElements.Count -ne 1){throw 'Require one constant CPU suite Name per declaration'}
    $element=$pairs[0].Item2.PipelineElements[0]
    if($element -isnot [Management.Automation.Language.CommandExpressionAst] -or $element.Expression -isnot [Management.Automation.Language.StringConstantExpressionAst]){throw 'Dynamic CPU suite name cannot be inventoried safely'}
    $name=$element.Expression.Value
    if($name -cnotmatch '^[a-z0-9_]+$' -or !$coverageSet.Add($name)){throw 'Invalid or duplicate registered CPU suite name'}
    $coverageNames+=,$name
}
if(!$coverageNames.Count){throw 'CPU suite inventory is empty'}
$coverageExpected=$coverageNames
if($OnlySuite){if(!$coverageSet.Contains($OnlySuite)){throw 'OnlySuite is not an exact registered name'};$coverageExpected=@($OnlySuite)}
$coveragePassed=0;$coverageActive=$null;$coverageSummary=$false;$coverageStarted=0
$coverageHasExit=$PSBoundParameters.ContainsKey('TerminalExitCode') -and $null -ne $TerminalExitCode
$coverageStream=[IO.FileStream]::new([IO.Path]::GetFullPath($LogPath),[IO.FileMode]::Open,[IO.FileAccess]::Read,([IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete))
try {
    if($coverageStream.Length -gt 64*1024*1024){throw 'CPU coverage log exceeds 64 MiB'}
    $coverageReader=[IO.StreamReader]::new($coverageStream,[Text.Encoding]::UTF8,$true,4096,$true)
    try{$coverageText=$coverageReader.ReadToEnd()}finally{$coverageReader.Dispose()}
}finally{$coverageStream.Dispose()}
# A live writer may be halfway through an ASCII marker. Report only completed
# lines until the caller supplies an independently observed terminal result.
if(!$coverageHasExit -and $coverageText.Length -and !$coverageText.EndsWith("`n")){
    $coverageLastNewline=$coverageText.LastIndexOf("`n");$coverageText=$coverageText.Substring(0,$coverageLastNewline+1)
}
foreach($line in ($coverageText -split "`r?`n")){
    if($line -cmatch '^CPU_CHECK_START=([a-z0-9_]+)$'){
        $name=$Matches[1]
        if($coverageSummary -or $coverageActive -or $coveragePassed -ge $coverageExpected.Count -or $name -cne $coverageExpected[$coveragePassed]){throw 'Unexpected, repeated or out-of-order CPU suite start'}
        $coverageActive=$name;$coverageStarted++
    }elseif($line -cmatch '^CPU_CHECK_PASS=([a-z0-9_]+)$'){
        if($coverageSummary -or !$coverageActive -or $Matches[1] -cne $coverageActive){throw 'CPU pass has no matching active suite'}
        $coveragePassed++;$coverageActive=$null
    }elseif($line -cmatch '^CPU_CHECK_SUMMARY=([0-9]+) REGISTERED=([0-9]+)$'){
        if($coverageSummary -or $coverageActive -or [int]$Matches[1] -ne $coveragePassed -or [int]$Matches[2] -ne $coverageNames.Count -or $coveragePassed -ne $coverageExpected.Count){throw 'CPU summary does not cover the declared run'}
        $coverageSummary=$true
    }elseif($line.StartsWith('CPU_CHECK_',[StringComparison]::Ordinal)){throw 'Malformed CPU coverage marker'}
}
$coverageComplete=$coverageSummary -and $coverageHasExit -and $TerminalExitCode -eq 0
if($RequireComplete -and !$coverageComplete){throw 'Full declared coverage and independently observed terminal exit 0 are both required'}
$coverageStatus='running_or_incomplete'
if($coverageSummary){$coverageStatus='coverage_complete_exit_unverified'}
if($coverageHasExit){
    if($TerminalExitCode -ne 0){$coverageStatus='terminal_failed'}
    elseif($coverageComplete){$coverageStatus='complete'}
    else{$coverageStatus='terminal_incomplete'}
}
[pscustomobject]@{
    Status=$coverageStatus;Complete=$coverageComplete;FullCoverage=($coverageComplete -and !$OnlySuite);Registered=$coverageNames.Count
    Expected=$coverageExpected.Count;Started=$coverageStarted;Passed=$coveragePassed
    Remaining=($coverageExpected.Count-$coveragePassed);Active=$coverageActive;Summary=$coverageSummary;TerminalExitCode=$TerminalExitCode
}
