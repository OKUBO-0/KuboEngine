param([Parameter(Mandatory=$true)][string]$EngineDirectory,[Parameter(Mandatory=$true)][string]$NewPackageDirectory,[Parameter(Mandatory=$true)][string]$NativeSdkDirectory)
$ErrorActionPreference='Stop'
function Need($value,[string]$message){if(!$value){throw $message}}
$engine=(Resolve-Path -LiteralPath $EngineDirectory).Path;$package=(Resolve-Path -LiteralPath $NewPackageDirectory).Path
$fixture=Join-Path ([IO.Path]::GetTempPath()) ('kubo-install-workflow-'+[guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Path $fixture | Out-Null
$store=Join-Path $fixture 'Managed Engines';$manifestPath=Join-Path $package 'engine-package.json'
$old=Get-Content -LiteralPath (Join-Path $engine 'engine-package.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$new=Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
Need ($old.engineVersion -ne $new.engineVersion) 'Workflow needs two distinct synthetic package declarations'
$digest=(Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash.ToLowerInvariant()
$start=New-Object Diagnostics.ProcessStartInfo;$start.FileName=Join-Path $engine 'KuboLauncher.exe';$start.UseShellExecute=$false;$start.CreateNoWindow=$true;$start.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden;$start.WorkingDirectory=$fixture
$start.EnvironmentVariables['APPDATA']=Join-Path $fixture 'AppData';$start.EnvironmentVariables['LOCALAPPDATA']=Join-Path $fixture 'LocalAppData'
$start.Arguments=(@('--install-package',$manifestPath,'--store',$store,'--accept-local-package','--no-launch') | ForEach-Object {'"'+[regex]::Replace([regex]::Replace($_,'(\\*)"','$1$1\"'),'(\\+)$','$1$1')+'"'}) -join ' '
$child=New-Object Diagnostics.Process;$child.StartInfo=$start
try{Need ($child.Start()) 'Owned install did not start';$handle=$child.Handle;Need ($child.WaitForExit(30000)) 'Owned install observation timeout';Need ($child.ExitCode -eq 0) 'Owned workflow install failed'}finally{if(!$child.HasExited){$child.Kill();$child.WaitForExit()};$child.Dispose()}
$installed=Join-Path $store ('package-'+$digest)
& (Join-Path $PSScriptRoot 'portable_engine_e2e_checks.ps1') -EngineDirectory $engine -NativeSdkDirectory $NativeSdkDirectory -SelectedEditor (Join-Path $installed 'KuboEditor.exe') -ExpectedEngineVersion $new.engineVersion -RollbackToBundled -ExpectedBundledEngineVersion $old.engineVersion -KeepFixture
foreach($entry in $new.files){Need ((Get-FileHash -LiteralPath (Join-Path $installed $entry.path) -Algorithm SHA256).Hash.ToLowerInvariant() -eq $entry.sha256) 'Workflow changed installed package'}
foreach($entry in $old.files){Need ((Get-FileHash -LiteralPath (Join-Path $engine $entry.path) -Algorithm SHA256).Hash.ToLowerInvariant() -eq $entry.sha256) 'Workflow changed original package'}
Need (!(Test-Path -LiteralPath (Join-Path $fixture 'AppData/KuboEngine/engine_catalog.json'))) 'Install silently selected an engine in its isolated profile'
Write-Output "PASS: actual original Hub external installed declaration $($new.engineVersion) launch/restart, production combo rollback to $($old.engineVersion) and restart, package bytes preserved; declarations are synthetic, not ABI/signature/physical registration proof"
Write-Output "WORKFLOW_FIXTURE=$fixture"
