[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$EngineDirectory,
      [Parameter(Mandatory=$true)][string]$SdkDirectory)
$ErrorActionPreference='Stop'
function Require($condition,[string]$message){if(!$condition){throw $message}}
$fixtureSdk=(Resolve-Path -LiteralPath $SdkDirectory).Path.TrimEnd('\')
$fixtureEngine=(Resolve-Path -LiteralPath $EngineDirectory).Path.TrimEnd('\')
$fixtureSource=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).TrimEnd('\')
$fixtureRoot=Join-Path ([IO.Path]::GetTempPath()) ('kubo-distribution-snapshot-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fixtureRoot | Out-Null
$fixtureHost=(Get-Process -Id $PID).Path
$fixtureOriginal=Get-Content -LiteralPath (Join-Path $fixtureSdk 'sdk-package.json') -Raw -Encoding UTF8 | ConvertFrom-Json
function New-SnapshotFixture([string]$label){
 $destination=Join-Path $fixtureRoot $label
 New-Item -ItemType Directory -Path $destination | Out-Null
 foreach($file in Get-ChildItem -LiteralPath $fixtureSdk -File -Recurse){
  $relative=$file.FullName.Substring($fixtureSdk.Length+1)
  $target=Join-Path $destination $relative
  New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
  # Hard-linked payloads are read-only inputs to snapshot_checks, never app inputs.
  # Every file this test edits is an independent copy, not a shared hard link.
  if($relative -in @('README.md','sdk-package.json')){Copy-Item -LiteralPath $file.FullName -Destination $target}
  else{New-Item -ItemType HardLink -Path $target -Target $file.FullName | Out-Null}
 }
 return $destination
}
function Check-SnapshotFixture([string]$directory,[string]$expectedError){
 $label=Split-Path -Leaf $directory
 $log=Join-Path $fixtureRoot ($label+'.log')
 # Windows PowerShell wraps expected native stderr in NativeCommandError.
 # Capture it without Stop terminating before we can assert the native exit.
 $fixturePreference=$ErrorActionPreference
 try{
  $ErrorActionPreference='Continue'
  & $fixtureHost -NoProfile -File (Join-Path $PSScriptRoot 'distribution_snapshot_checks.ps1') -EngineDirectory $fixtureEngine -SdkDirectory $directory *> $log
  $exit=$LASTEXITCODE
 } finally {$ErrorActionPreference=$fixturePreference}
 $output=Get-Content -LiteralPath $log -Raw
 if(!$expectedError){Require ($exit -eq 0) "Baseline snapshot fixture failed: $log"}
 else{Require ($exit -ne 0 -and $output.Contains($expectedError)) "Expected refusal '$expectedError' missing: exit=$exit log=$log"}
 Write-Output "PASS: $label snapshot exit=$exit expected refusal=$expectedError"
}
try{
 $baseline=New-SnapshotFixture 'baseline'
 Check-SnapshotFixture $baseline ''
 $stale=New-SnapshotFixture 'stale-readme'
 [IO.File]::AppendAllText((Join-Path $stale 'README.md'),"`nStale snapshot fixture only.`n",[Text.UTF8Encoding]::new($false))
 $manifest=Get-Content -LiteralPath (Join-Path $stale 'sdk-package.json') -Raw -Encoding UTF8 | ConvertFrom-Json
 foreach($entry in $manifest.files){if($entry.path -eq 'README.md'){$entry.sha256=(Get-FileHash -LiteralPath (Join-Path $stale 'README.md') -Algorithm SHA256).Hash.ToLowerInvariant()}}
 [IO.File]::WriteAllText((Join-Path $stale 'sdk-package.json'),($manifest | ConvertTo-Json -Depth 7),[Text.UTF8Encoding]::new($false))
 Check-SnapshotFixture $stale 'Snapshot mismatch:'
 # An actual source header outside the observed closure passes individual source
 # hashing and a freshly consistent manifest, but must fail exact closure inventory.
 $extra=New-SnapshotFixture 'extra-current-source-header'
 $extraSource=@(Get-ChildItem -LiteralPath (Join-Path $fixtureSource 'engine') -File -Recurse -Filter '*.h' | Where-Object {
  !(Test-Path -LiteralPath (Join-Path $fixtureSdk ('include\'+$_.FullName.Substring($fixtureSource.Length+1))))
 } | Sort-Object FullName | Select-Object -First 1)
 Require ($extraSource.Count -eq 1) 'No out-of-closure source header available for negative fixture'
 $extraRelative='include/'+$extraSource[0].FullName.Substring($fixtureSource.Length+1).Replace('\','/')
 $extraTarget=Join-Path $extra $extraRelative
 New-Item -ItemType Directory -Path (Split-Path -Parent $extraTarget) -Force | Out-Null
 Copy-Item -LiteralPath $extraSource[0].FullName -Destination $extraTarget
 $manifest=Get-Content -LiteralPath (Join-Path $extra 'sdk-package.json') -Raw -Encoding UTF8 | ConvertFrom-Json
 $manifest.files+=@{path=$extraRelative;sha256=(Get-FileHash -LiteralPath $extraTarget -Algorithm SHA256).Hash.ToLowerInvariant()}
 [IO.File]::WriteAllText((Join-Path $extra 'sdk-package.json'),($manifest | ConvertTo-Json -Depth 7),[Text.UTF8Encoding]::new($false))
 Check-SnapshotFixture $extra 'SDK include inventory differs from current Runtime compiled closure'
 foreach($entry in $fixtureOriginal.files){Require ((Get-FileHash -LiteralPath (Join-Path $fixtureSdk $entry.path) -Algorithm SHA256).Hash.ToLowerInvariant() -eq $entry.sha256) 'Original SDK changed during read-only fixture checks'}
 Write-Output 'PASS: positive clone and self-consistent stale README/extra current-source header refusals; original SDK digests preserved'
} finally {Write-Output "Retained snapshot fixtures/logs: $fixtureRoot"}
