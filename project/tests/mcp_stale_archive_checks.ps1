[CmdletBinding()]
param([string]$MSBuild='C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe')
$ErrorActionPreference='Stop'
$archiveProject=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$archiveFixture=[IO.Path]::GetFullPath((Join-Path $archiveProject ('..\generated\mcp-stale-archive\'+[guid]::NewGuid().ToString('N'))))
New-Item -ItemType Directory -Path $archiveFixture | Out-Null
Write-Output "MCP_STALE_ARCHIVE_LOGS=$archiveFixture PRODUCTION_RECOVERY=true"
& $MSBuild (Join-Path $PSScriptRoot 'mcp_stale_archive_checks.vcxproj') /m:1 /p:Configuration=Debug /p:Platform=x64 /p:VCToolsVersion=14.44.35207 "/p:OutDir=$archiveFixture\bin\" "/p:IntDir=$archiveFixture\obj\" /verbosity:minimal *> (Join-Path $archiveFixture 'build.log')
if($LASTEXITCODE -ne 0){Get-Content (Join-Path $archiveFixture 'build.log') -Tail 20;throw 'Private archive build failed'}
$archiveChild=$null
try {
 $archiveChild=Start-Process -FilePath (Join-Path $archiveFixture 'bin\McpStaleArchiveChecks.exe') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $archiveFixture 'native.log') -RedirectStandardError (Join-Path $archiveFixture 'native.stderr.log')
 $archiveHandle=$archiveChild.Handle
 if(!$archiveChild.WaitForExit(20000)){throw 'Owned archive probe exceeded20s'}
 $archiveChild.WaitForExit();$archiveExit=$archiveChild.ExitCode
} finally {if($archiveChild){if(!$archiveChild.HasExited){Stop-Process -Id $archiveChild.Id;$archiveChild.WaitForExit()};$archiveChild.Dispose()}}
Get-Content -LiteralPath (Join-Path $archiveFixture 'native.log') -Encoding UTF8
Get-Content -LiteralPath (Join-Path $archiveFixture 'native.stderr.log') -Encoding UTF8
Write-Output "MCP_STALE_ARCHIVE_NATIVE_EXIT=$archiveExit PRODUCTION_RECOVERY=true"
if($archiveExit -ne 0){throw 'Private archive probe failed; retained evidence'}
