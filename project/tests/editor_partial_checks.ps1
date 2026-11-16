[CmdletBinding()]
param([string]$MSBuild='C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe')
$ErrorActionPreference='Stop'
$partialProject=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path.TrimEnd('\')
$partialFixture=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot ('..\..\generated\editor-partial-runs\'+[guid]::NewGuid().ToString('N'))))
New-Item -ItemType Directory -Path $partialFixture -Force | Out-Null
$partialTargets=(Resolve-Path (Join-Path $PSScriptRoot 'editor_partial_checks.targets')).Path
$partialBuildLog=Join-Path $partialFixture 'build.log'
$partialOut=Join-Path $partialFixture 'native.log';$partialErr=Join-Path $partialFixture 'native.stderr.log'
$partialChild=$null
try{
 @('engine\scene\game\Game.cpp','engine\scene\game\Game.h','engine\base\core\Framework.cpp','engine\script\ScriptRuntime.h','tests\editor_partial_checks.cpp','tests\editor_partial_checks.targets') | ForEach-Object {Get-FileHash -LiteralPath (Join-Path $partialProject $_)} | Format-List | Out-File -LiteralPath (Join-Path $partialFixture 'input-hashes.log') -Encoding utf8
 & $MSBuild (Join-Path $partialProject 'KuboEngine.vcxproj') /m:1 /p:Configuration=Debug /p:Platform=x64 "/p:SolutionDir=$partialProject\" "/p:KuboPartialRoot=$partialFixture\" "/p:ForceImportBeforeCppTargets=$partialTargets" /verbosity:minimal *> $partialBuildLog
 if($LASTEXITCODE -ne 0){Get-Content -LiteralPath $partialBuildLog -Tail 20;throw 'Actual Editor partial build failed'}
 $partialChild=Start-Process -FilePath (Join-Path $partialFixture 'bin\KuboEngine.exe') -WorkingDirectory $partialProject -WindowStyle Hidden -PassThru -RedirectStandardOutput $partialOut -RedirectStandardError $partialErr
 $partialHandle=$partialChild.Handle
 if(!$partialChild.WaitForExit(50000)){throw 'Owned partial Editor child exceeded 50s bound'}
 $partialChild.WaitForExit();$partialExit=$partialChild.ExitCode
 Get-Content -LiteralPath $partialOut;Get-Content -LiteralPath $partialErr
 if($partialExit -ne 0 -or !(Select-String -LiteralPath $partialOut -Pattern 'EDITOR_PARTIAL cases=4 cold_foreign_preserved=1 scene_fault=1 mcp_conflict_preserved=1 gpu_errors=0 native_exit=0' -SimpleMatch -Quiet)){throw "Actual Editor partial gate failed: $partialExit"}
 Write-Output 'PASS: actual Editor cold/base-only/Scene-load fault/MCP-conflict teardown; not exhaustive initialization injection or Application root ownership'
}finally{
 if($partialChild){if(!$partialChild.HasExited){Stop-Process -Id $partialChild.Id;$partialChild.WaitForExit()};$partialChild.Dispose()}
 Write-Output "Retained partial Editor source/build/native logs: $partialFixture"
}
