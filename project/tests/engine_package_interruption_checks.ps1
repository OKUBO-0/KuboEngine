param([Parameter(Mandatory=$true)][string]$EngineDirectory,[Parameter(Mandatory=$true)][string]$ProbeExecutable)
$ErrorActionPreference='Stop'
function Need($value,[string]$message){if(!$value){throw $message}}
$engine=(Resolve-Path -LiteralPath $EngineDirectory).Path;$probePath=(Resolve-Path -LiteralPath $ProbeExecutable).Path;$launcher=Join-Path $engine 'KuboLauncher.exe'
$fixture=Join-Path ([IO.Path]::GetTempPath()) ('kubo-install-interrupt-'+[guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Path $fixture | Out-Null
$utf8=New-Object Text.UTF8Encoding($false)
function Json([string]$path,$value){[IO.File]::WriteAllText($path,($value | ConvertTo-Json -Depth 8 -Compress),$utf8)}
function Source([string]$name,[string]$version,[bool]$large){
 $root=Join-Path $fixture $name;New-Item -ItemType Directory -Path $root | Out-Null
 $files=@(foreach($role in @('KuboLauncher.exe','KuboEditor.exe','KuboPlayer.exe')){[IO.File]::WriteAllText((Join-Path $root $role),'abc',$utf8);[ordered]@{path=$role;sha256='ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad'}})
 if($large){$blob=Join-Path $root 'Payload.bin';$stream=[IO.File]::Open($blob,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None);try{$stream.SetLength(64MB);$stream.Flush($true)}finally{$stream.Dispose()};$files+=@([ordered]@{path='Payload.bin';sha256=(Get-FileHash -LiteralPath $blob -Algorithm SHA256).Hash.ToLowerInvariant()})}
 Json (Join-Path $root 'engine-package.json') ([ordered]@{format='kubo-portable-engine-2';engineVersion=$version;platform='Windows-x64';launcher='KuboLauncher.exe';editor='KuboEditor.exe';player='KuboPlayer.exe';configuration='Release';files=$files})
 return $root
}
function Run-Launcher([string[]]$arguments,[int[]]$allowed,[string]$tag){
 $start=New-Object Diagnostics.ProcessStartInfo;$start.FileName=$launcher;$start.UseShellExecute=$false;$start.CreateNoWindow=$true;$start.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden;$start.WorkingDirectory=$fixture
 $start.EnvironmentVariables['APPDATA']=Join-Path $fixture 'AppData';$start.EnvironmentVariables['LOCALAPPDATA']=Join-Path $fixture 'LocalAppData'
 $start.Arguments=($arguments | ForEach-Object {'"'+[regex]::Replace([regex]::Replace($_,'(\\*)"','$1$1\"'),'(\\+)$','$1$1')+'"'}) -join ' '
 $child=New-Object Diagnostics.Process;$child.StartInfo=$start
 try{Need ($child.Start()) 'Owned Launcher did not start';$handle=$child.Handle;Need ($child.WaitForExit(45000)) 'Owned Launcher observation timeout';$exit=$child.ExitCode;Need ($allowed -contains $exit) "Unexpected $tag exit=$exit";Write-Host "INTERRUPTION_CASE=$tag CHILD_PID=$($child.Id) EXIT=$exit";return $exit}finally{if(!$child.HasExited){$child.Kill();$child.WaitForExit()};$child.Dispose()}
}
$old=Source 'OldSource' '0.0.1' $false;$new=Source 'NewSource' '0.0.2' $true
$oldManifest=Join-Path $old 'engine-package.json';$newManifest=Join-Path $new 'engine-package.json'
$oldHash=(Get-FileHash -LiteralPath $oldManifest -Algorithm SHA256).Hash.ToLowerInvariant();$newHash=(Get-FileHash -LiteralPath $newManifest -Algorithm SHA256).Hash.ToLowerInvariant()
$catalog=Join-Path $fixture 'AppData/KuboEngine/engine_catalog.json';New-Item -ItemType Directory -Path (Split-Path $catalog -Parent) | Out-Null
Json $catalog @('kubo-engine-catalog-1',(Join-Path $engine 'KuboEditor.exe'),(Join-Path $engine 'KuboEditor.exe'))
$catalogHash=(Get-FileHash -LiteralPath $catalog -Algorithm SHA256).Hash
$interrupted=$false;$probeReceipts=@()
for($attempt=1;$attempt -le 3 -and !$interrupted;$attempt++){
 $store=Join-Path $fixture "InterruptedStore-$attempt"
 $null=Run-Launcher @('--install-package',$oldManifest,'--store',$store,'--accept-local-package','--no-launch') @(0) 'seed-old-version'
 $oldInstalled=Join-Path $store ('package-'+$oldHash)
 $line=(@($launcher,$newManifest,$store,$newHash) | ForEach-Object {'"'+[regex]::Replace([regex]::Replace($_,'(\\*)"','$1$1\"'),'(\\+)$','$1$1')+'"'}) -join ' '
 $out=Join-Path $fixture "probe-$attempt.log";$err=Join-Path $fixture "probe-$attempt.stderr.log";$probe=$null
 try{
  $probe=Start-Process -FilePath $probePath -ArgumentList $line -WindowStyle Hidden -PassThru -RedirectStandardOutput $out -RedirectStandardError $err;$handle=$probe.Handle
  Need ($probe.WaitForExit(45000)) 'Owned interruption probe observation timeout'
  $probe.WaitForExit();$probeExit=$probe.ExitCode;Get-Content -LiteralPath $out
  Need ($probeExit -in @(0,3)) "Interruption probe failed exit=$probeExit; retain $err"
  $probeReceipts+=@([ordered]@{probePid=$probe.Id;exitCode=$probeExit;store=$store;log=$out})
 }finally{
  if($probe){if(!$probe.HasExited){
   # Only exact children of this still-owned helper, at our trusted executable,
   # with this unique fixture in argv are candidates for timeout cleanup.
   foreach($owned in @(Get-CimInstance Win32_Process -Filter "ParentProcessId=$($probe.Id)" | Where-Object {$_.ExecutablePath -eq $launcher -and $_.CommandLine.Contains($fixture)})){
    $process=[Diagnostics.Process]::GetProcessById($owned.ProcessId);try{$ownedHandle=$process.Handle;if(!$process.HasExited){$process.Kill();$process.WaitForExit()}}finally{$process.Dispose()}
   }
   $probe.Kill();$probe.WaitForExit()
  };$probe.Dispose()}
 }
 if($probeExit -eq 3){Write-Output 'Completed before suspension; independent terminal was observed. Try a fresh owned store, not a restart of live work.';continue}
 $interrupted=$true;$lines=Get-Content -LiteralPath $out
 $match=@($lines | Where-Object {$_ -match '^INTERRUPTED_CANDIDATE_TOKEN='});Need ($match.Count -eq 1) 'Missing held-child interruption receipt'
 Need ($match[0] -match '^INTERRUPTED_CANDIDATE_TOKEN=([0-9a-f]{32}) CHILD_PID=([0-9]+) CHILD_EXIT=3763404801 PRIMARY_THREAD_ID=([0-9]+) PREVIOUS_SUSPEND_COUNT=0$') 'Unexpected child terminal or suspension ownership'
 $token=$Matches[1];$killedPid=[int]$Matches[2]
 $candidate=Join-Path $store ('.pending-'+$token);$journal=Join-Path $store ('pending-'+$token+'.json');$final=Join-Path $store ('package-'+$newHash)
 Need ((Test-Path -LiteralPath $candidate) -and (Test-Path -LiteralPath $journal) -and !(Test-Path -LiteralPath $final)) 'Interrupted child lost journal/candidate or published unexpectedly'
 $journalBefore=(Get-FileHash -LiteralPath $journal -Algorithm SHA256).Hash
 $verification=Run-Launcher @('--verify-package',(Join-Path $candidate 'engine-package.json'),'--no-launch') @(0,1) 'inspect-interrupted-candidate'
 if($verification -eq 1){
  $null=Run-Launcher @('--recover-installation',$token,'--store',$store,'--accept-local-package','--no-launch') @(1) 'partial-refusal-after-real-kill'
  Need ((Test-Path -LiteralPath $candidate) -and (Get-FileHash -LiteralPath $journal -Algorithm SHA256).Hash -eq $journalBefore -and !(Test-Path -LiteralPath $final)) 'Partial recovery removed/rewrote pending state'
  $null=Run-Launcher @('--install-package',$newManifest,'--store',$store,'--accept-local-package','--no-launch') @(0) 'explicit-retry-from-intact-source'
  Need ((Test-Path -LiteralPath $candidate) -and (Test-Path -LiteralPath $journal)) 'Retry deleted previous interrupted diagnostic state'
  $recovery='partial-refused-explicit-source-retry'
 }else{
  $null=Run-Launcher @('--recover-installation',$token,'--store',$store,'--accept-local-package','--no-launch') @(0) 'complete-candidate-after-real-kill'
  Need (!(Test-Path -LiteralPath $candidate) -and !(Test-Path -LiteralPath $journal)) 'Ready recovery left live candidate/journal';$recovery='ready-reverified-and-published'
 }
 foreach($source in @($old,$new)){
  $declaration=Get-Content -LiteralPath (Join-Path $source 'engine-package.json') -Raw -Encoding UTF8 | ConvertFrom-Json
  $installed=if($source -eq $old){$oldInstalled}else{$final}
  foreach($entry in $declaration.files){foreach($root in @($source,$installed)){Need ((Get-FileHash -LiteralPath (Join-Path $root $entry.path) -Algorithm SHA256).Hash.ToLowerInvariant() -eq $entry.sha256) 'Interruption/recovery changed old/source/final payload'}}
 }
 Need ((Get-FileHash -LiteralPath $catalog -Algorithm SHA256).Hash -eq $catalogHash) 'Interruption/recovery changed selected-engine catalog'
 [ordered]@{format='kubo-real-install-interruption-1';killedChildPid=$killedPid;expectedChildExit=3763404801;recovery=$recovery;token=$token;store=$store;sourceManifestSha256=$newHash;oldManifestSha256=$oldHash;catalogSha256=$catalogHash;probes=$probeReceipts;powerLossTested=$false;syntheticExecutablesNeverLaunched=$true} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $fixture 'verified.json') -Encoding UTF8
}
Need $interrupted 'No actual private-candidate interruption was observed'
Write-Output "PASS: real held owned installer primary-thread interruption, $recovery, old/source/final hashes and selected catalog retained (not power-loss, hostile-writer, or fake executable launch proof)"
Write-Output "INTERRUPTION_FIXTURE=$fixture"
