[CmdletBinding()]
param([string]$MSBuild='C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe')
$ErrorActionPreference='Stop'
$reportFixture=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot ('..\..\generated\run-report-checks\'+[guid]::NewGuid().ToString('N'))))
New-Item -ItemType Directory -Path $reportFixture | Out-Null
& $MSBuild (Join-Path $PSScriptRoot 'run_report_checks.vcxproj') /m:1 /p:Configuration=Debug /p:Platform=x64 /p:VCToolsVersion=14.44.35207 "/p:OutDir=$reportFixture\bin\" "/p:IntDir=$reportFixture\obj\" /verbosity:minimal *> (Join-Path $reportFixture 'build.log')
Get-Content -LiteralPath (Join-Path $reportFixture 'build.log') -Encoding UTF8
if($LASTEXITCODE -ne 0){throw 'Run report checks build failed'}
& (Join-Path $reportFixture 'bin\RunReportChecks.exe') (Join-Path $reportFixture 'owned-root')
if($LASTEXITCODE -ne 0){throw 'Run report checks failed'}
Write-Output "Retained report fixture: $reportFixture"
