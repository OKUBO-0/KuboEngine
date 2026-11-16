$ErrorActionPreference='Stop'
function Require($value,[string]$why){if(!$value){throw $why}}
$coverageFixture=[IO.Path]::GetFullPath((Join-Path ([IO.Path]::GetTempPath()) ('kubo-cpu-coverage-'+[guid]::NewGuid().ToString('N'))))
$coveragePassed=$false
try {
    New-Item -ItemType Directory -Path $coverageFixture -ErrorAction Stop | Out-Null
    $coverageRunner=Join-Path $coverageFixture 'runner.ps1';$coverageLog=Join-Path $coverageFixture 'run.log';$coverageMarker=Join-Path $coverageFixture 'must-not-execute'
    $coverageSource='$checks=@(@{Name=''alpha'';Sources=@(''a.cpp'')},@{Name=''beta'';Sources=@(''b.cpp'')},@{Name=''gamma'';Sources=@(''g.cpp'')})'+[Environment]::NewLine+'throw ''Runner must not execute during inventory'''
    [IO.File]::WriteAllText($coverageRunner,$coverageSource)
    $coverageAudit=Join-Path $PSScriptRoot 'check_cpu_run_coverage.ps1'
    function Audit([string]$bytes,[hashtable]$options){[IO.File]::WriteAllText($coverageLog,$bytes);& $coverageAudit -RunnerPath $coverageRunner -LogPath $coverageLog @options}
    function Refuse([string]$bytes,[hashtable]$options){$denied=$false;try{Audit $bytes $options | Out-Null}catch{$denied=$true};if(!$denied){throw ('Invalid coverage admitted; options='+($options.Keys -join ',')+'; log='+$bytes)}}
    $full="CPU_CHECK_START=alpha`nCPU_CHECK_PASS=alpha`nCPU_CHECK_START=beta`nCPU_CHECK_PASS=beta`nCPU_CHECK_START=gamma`nCPU_CHECK_PASS=gamma`nCPU_CHECK_SUMMARY=3 REGISTERED=3`n"
    $result=Audit $full @{RequireComplete=$true;TerminalExitCode=0};Require ($result.Complete -and $result.FullCoverage -and $result.Passed -eq 3 -and $result.Status -eq 'complete') 'Full coverage rejected'
    $result=Audit $full @{};Require (!$result.Complete -and $result.Status -eq 'coverage_complete_exit_unverified') 'Summary alone proves terminal success'
    Refuse $full @{RequireComplete=$true};Refuse $full @{RequireComplete=$true;TerminalExitCode=1}
    $partial="CPU_CHECK_START=alpha`nCPU_CHECK_PASS=alpha`nCPU_CHECK_START=beta`n"
    $result=Audit $partial @{};Require (!$result.Complete -and $result.Passed -eq 1 -and $result.Active -eq 'beta') 'Live partial progress misclassified'
    Refuse $partial @{RequireComplete=$true;TerminalExitCode=0}
    [IO.File]::WriteAllText($coverageLog,$partial+'CPU_CHECK_STA')
    $liveWriter=[IO.FileStream]::new($coverageLog,[IO.FileMode]::Open,[IO.FileAccess]::Write,[IO.FileShare]::Read)
    try{$result=& $coverageAudit -RunnerPath $coverageRunner -LogPath $coverageLog;Require (!$result.Complete -and $result.Passed -eq 1 -and $result.Active -eq 'beta') 'Live writer sharing/partial marker handling failed'}finally{$liveWriter.Dispose()}
    foreach($bad in @(
        $full.Replace('CPU_CHECK_PASS=beta','CPU_CHECK_PASS=alpha'),
        $full.Replace('CPU_CHECK_START=beta','CPU_CHECK_START=gamma'),
        $full.Replace('CPU_CHECK_SUMMARY=3','CPU_CHECK_SUMMARY=2'),
        $full.Replace('REGISTERED=3','REGISTERED=4'),
        ($full+"CPU_CHECK_PASS=gamma`n"),
        "CPU_CHECK_PASS=alpha`n",
        "CPU_CHECK_START=unknown`n",
        "CPU_CHECK_START=alpha`nCPU_CHECK_START=alpha`n",
        "CPU_CHECK_PASS=Alpha`n",
        "CPU_CHECK_broken`n"
    )){Refuse $bad @{RequireComplete=$true;TerminalExitCode=0}}
    $single="CPU_CHECK_START=beta`nCPU_CHECK_PASS=beta`nCPU_CHECK_SUMMARY=1 REGISTERED=3`n"
    $result=Audit $single @{OnlySuite='beta';RequireComplete=$true;TerminalExitCode=0};Require ($result.Complete -and !$result.FullCoverage -and $result.Expected -eq 1 -and $result.Registered -eq 3) 'Focused coverage mode failed'
    Refuse $single @{RequireComplete=$true;TerminalExitCode=0}
    Refuse $full @{OnlySuite='missing';RequireComplete=$true;TerminalExitCode=0}
    [IO.File]::WriteAllText($coverageRunner,$coverageSource.Replace("Name='beta'","Name='alpha'"));Refuse $full @{RequireComplete=$true;TerminalExitCode=0}
    [IO.File]::WriteAllText($coverageRunner,'$checks=@(@{Name=(Get-Date);Sources=@(''a.cpp'')})');Refuse $full @{RequireComplete=$true;TerminalExitCode=0}
    [IO.File]::WriteAllText($coverageRunner,'$checks=@(@{Name=''alpha''})' + [Environment]::NewLine + '$checks=@(@{Name=''beta''})');Refuse $full @{RequireComplete=$true;TerminalExitCode=0}
    Require (!(Test-Path -LiteralPath $coverageMarker)) 'Audit executed a runner'
    $coveragePassed=$true
    Write-Output 'PASS: synthetic CPU coverage sequence/duplicate/name/summary/partial/exit/focused/AST cases; no compiler or engine suite executed'
}finally{
    if($coveragePassed){
        $coverageTemp=[IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')+'\'
        if(!$coverageFixture.StartsWith($coverageTemp,[StringComparison]::OrdinalIgnoreCase) -or ![IO.Path]::GetFileName($coverageFixture).StartsWith('kubo-cpu-coverage-',[StringComparison]::Ordinal)){throw 'Unsafe owned fixture cleanup path'}
        Remove-Item -LiteralPath $coverageFixture -Recurse -Force
    }else{Write-Warning "Retained coverage fixture: $coverageFixture"}
}
