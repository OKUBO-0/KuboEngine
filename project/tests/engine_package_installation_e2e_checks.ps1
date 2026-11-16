param([Parameter(Mandatory=$true)][string]$EngineDirectory,[Parameter(Mandatory=$true)][string]$NativeSdkDirectory)
$ErrorActionPreference='Stop'
function Need($value,[string]$message){if(!$value){throw $message}}
$engine=(Resolve-Path -LiteralPath $EngineDirectory).Path;$sdk=(Resolve-Path -LiteralPath $NativeSdkDirectory).Path
$fixture=Join-Path ([IO.Path]::GetTempPath()) ('kubo-install-e2e-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fixture | Out-Null
$store=Join-Path $fixture 'Managed Engines';$manifestPath=Join-Path $engine 'engine-package.json'
$digest=(Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash.ToLowerInvariant()
$manifest=Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
function Invoke-Owned([string[]]$arguments,[int]$expected,[string]$tag){
 $start=New-Object Diagnostics.ProcessStartInfo;$start.FileName=Join-Path $engine 'KuboLauncher.exe';$start.UseShellExecute=$false;$start.CreateNoWindow=$true;$start.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden
 $start.WorkingDirectory=$fixture;$start.EnvironmentVariables['APPDATA']=Join-Path $fixture 'AppData';$start.EnvironmentVariables['LOCALAPPDATA']=Join-Path $fixture 'LocalAppData'
 $start.Arguments=($arguments | ForEach-Object {'"'+[regex]::Replace([regex]::Replace($_,'(\\*)"','$1$1\"'),'(\\+)$','$1$1')+'"'}) -join ' '
 $child=New-Object Diagnostics.Process;$child.StartInfo=$start
 try{
  Need ($child.Start()) 'Owned Launcher did not start';$handle=$child.Handle
  Need ($child.WaitForExit(60000)) 'Owned installation observation timeout'
  Need ($child.ExitCode -eq $expected) "Owned installation $tag exit=$($child.ExitCode) expected=$expected"
  Write-Output "INSTALLATION_CLI_EXIT=$($child.ExitCode) CASE=$tag PID=$($child.Id)"
 }finally{if(!$child.HasExited){$child.Kill();$child.WaitForExit()};$child.Dispose()}
}
function Check-Package([string]$directory){
 Need ((Get-FileHash -LiteralPath (Join-Path $directory 'engine-package.json') -Algorithm SHA256).Hash.ToLowerInvariant() -eq $digest) 'Installed/source manifest changed'
 foreach($entry in $manifest.files){Need ((Get-FileHash -LiteralPath (Join-Path $directory $entry.path) -Algorithm SHA256).Hash.ToLowerInvariant() -eq $entry.sha256) 'Installed/source payload digest mismatch'}
 $actual=@(Get-ChildItem -LiteralPath $directory -File -Recurse -Force | ForEach-Object {$_.FullName.Substring($directory.Length+1).Replace('\','/')})
 $expected=@($manifest.files.path)+@('engine-package.json')
 Need (@(Compare-Object @($actual | Sort-Object) @($expected | Sort-Object)).Count -eq 0) 'Installed/source inventory mismatch'
}
$argsInstall=@('--install-package',$manifestPath,'--store',$store,'--accept-local-package','--no-launch')
Invoke-Owned $argsInstall 0 'install'
$installed=Join-Path $store ('package-'+$digest);Check-Package $installed;Check-Package $engine
Invoke-Owned $argsInstall 0 'idempotent'
Need (@(Get-ChildItem -LiteralPath $store -Directory -Filter 'package-*').Count -eq 1) 'Repeated install created another version'
Need (@(Get-ChildItem -LiteralPath $store -File -Filter 'pending-*.json').Count -eq 0) 'Successful install retained live journal unexpectedly'
Need (!(Test-Path -LiteralPath (Join-Path $fixture 'AppData/KuboEngine/engine_catalog.json'))) 'Install selected an engine implicitly'
Write-Output 'PASS: actual CLI install is source-preserving, non-overwrite and does not select or launch'
# Execute only the real built engine, not synthetic test payloads, from its new location.
& (Join-Path $PSScriptRoot 'portable_engine_e2e_checks.ps1') -EngineDirectory $installed -NativeSdkDirectory $sdk -ExpectedEngineVersion $manifest.engineVersion
Write-Output 'PASS: managed installed Launcher -> Editor / native template / export -> Player at relocated package path'
$token=[guid]::NewGuid().ToString('N');$candidate=Join-Path $store ('.pending-'+$token);$journal=Join-Path $store ('pending-'+$token+'.json')
[IO.File]::WriteAllText($journal,([ordered]@{format='kubo-engine-install-1';token=$token;manifestSha256=$digest} | ConvertTo-Json -Compress),(New-Object Text.UTF8Encoding($false)))
$moveSource=(Resolve-Path -LiteralPath $installed).Path;$moveDestination=[IO.Path]::GetFullPath($candidate)
Need ($moveSource.StartsWith($store+'\',[StringComparison]::OrdinalIgnoreCase) -and $moveDestination.StartsWith($store+'\',[StringComparison]::OrdinalIgnoreCase) -and $store.StartsWith($fixture+'\',[StringComparison]::OrdinalIgnoreCase)) 'Owned recovery fixture move escaped its store'
Need (!((Get-Item -LiteralPath $moveSource).Attributes -band [IO.FileAttributes]::ReparsePoint)) 'Recovery fixture source is a reparse point'
Move-Item -LiteralPath $moveSource -Destination $moveDestination
Invoke-Owned @('--recover-installation',$token,'--store',$store,'--accept-local-package','--no-launch') 0 'ready-candidate-recovery'
Need (!(Test-Path -LiteralPath $candidate) -and !(Test-Path -LiteralPath $journal)) 'Complete recovery did not retire its own candidate/journal';Check-Package $installed
# This is a constructed durable partial state, not proof of process-kill/power-loss behavior.
$partialToken=[guid]::NewGuid().ToString('N');$partial=Join-Path $store ('.pending-'+$partialToken);$partialJournal=Join-Path $store ('pending-'+$partialToken+'.json')
New-Item -ItemType Directory -Path $partial | Out-Null
[IO.File]::WriteAllText($partialJournal,([ordered]@{format='kubo-engine-install-1';token=$partialToken;manifestSha256=('f'*64)} | ConvertTo-Json -Compress),(New-Object Text.UTF8Encoding($false)))
Copy-Item -LiteralPath $manifestPath -Destination (Join-Path $partial 'engine-package.json')
Invoke-Owned @('--recover-installation',$partialToken,'--store',$store,'--accept-local-package','--no-launch') 1 'partial-candidate-refusal'
Need ((Test-Path -LiteralPath $partial) -and (Test-Path -LiteralPath $partialJournal)) 'Partial recovery deleted diagnostic candidate/journal'
$unmanaged=Join-Path $fixture 'Unmanaged';New-Item -ItemType Directory -Path $unmanaged | Out-Null
[IO.File]::WriteAllText((Join-Path $unmanaged 'user.txt'),'preserve')
Invoke-Owned @('--install-package',$manifestPath,'--store',$unmanaged,'--accept-local-package','--no-launch') 1 'unmanaged-refusal'
Need ([IO.File]::ReadAllText((Join-Path $unmanaged 'user.txt')) -ceq 'preserve') 'Unmanaged data changed'
Invoke-Owned @('--install-package',$manifestPath,'--store',(Join-Path $fixture 'NoConsent'),'--no-launch') 2 'missing-acceptance'
Need (!(Test-Path -LiteralPath (Join-Path $fixture 'NoConsent'))) 'Ambiguous CLI usage wrote destination'
Check-Package $installed;Check-Package $engine
Need (!(Test-Path -LiteralPath (Join-Path $fixture 'AppData/KuboEngine/engine_catalog.json'))) 'Recovery selected an engine implicitly'
Write-Output "PASS: actual install and explicit durable ready recovery, partial/unmanaged/usage refusals, $($manifest.files.Count) source+installed payloads unchanged; no catalog mutation (not signature/physical dialogs/power-loss proof)"
Write-Output "Retained installation fixture: $fixture"
