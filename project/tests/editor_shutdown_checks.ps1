[CmdletBinding()]
param([string]$BuildDirectory)
$ErrorActionPreference='Stop'
if([string]::IsNullOrWhiteSpace($BuildDirectory)){$BuildDirectory=Join-Path $PSScriptRoot '..\..\generated\outputs\Debug'}
$editorShutdownBinary=Join-Path ([IO.Path]::GetFullPath($BuildDirectory)) 'EngineResourceChecks.exe'
if(!(Test-Path -LiteralPath $editorShutdownBinary -PathType Leaf)){throw 'Build the optional Editor application runner first'}
$editorShutdownLogs=Join-Path $PSScriptRoot ('..\..\generated\editor-shutdown-runs\'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $editorShutdownLogs -Force | Out-Null
$editorShutdownOut=Join-Path $editorShutdownLogs 'native.log'
$editorShutdownErr=Join-Path $editorShutdownLogs 'native.stderr.log'
$editorShutdownChild=$null
try{
 $editorShutdownChild=Start-Process -FilePath $editorShutdownBinary -ArgumentList '--shutdown' -WorkingDirectory (Join-Path $PSScriptRoot '..') -WindowStyle Hidden -PassThru -RedirectStandardOutput $editorShutdownOut -RedirectStandardError $editorShutdownErr
 $editorShutdownHandle=$editorShutdownChild.Handle
 if(!$editorShutdownChild.WaitForExit(30000)){throw 'Owned Editor shutdown child exceeded 30s bound'}
 $editorShutdownChild.WaitForExit();$editorShutdownExit=$editorShutdownChild.ExitCode
 Get-Content -LiteralPath $editorShutdownOut;Get-Content -LiteralPath $editorShutdownErr
 if($editorShutdownExit -ne 0 -or !(Select-String -LiteralPath $editorShutdownOut -Pattern 'EDITOR_SHUTDOWN guarded=1 destroyed=32 reentries=32 mcp_stopped=1 ui_retained=1 gpu_errors=0 native_exit=0' -SimpleMatch -Quiet)){throw "Editor shutdown gate failed: $editorShutdownExit"}
 if(!(Select-String -LiteralPath $editorShutdownOut -Pattern 'EDITOR_DISPATCH_ADMISSION refused=96 owners_preserved=1 native_exit=0' -SimpleMatch -Quiet)){throw 'Missing Editor active-dispatch admission gate'}
 Write-Output 'PASS: actual Editor GPU-first/reentrant teardown, joined native exit0; not shipped Application failure ownership'
}finally{
 if($editorShutdownChild){if(!$editorShutdownChild.HasExited){Stop-Process -Id $editorShutdownChild.Id;$editorShutdownChild.WaitForExit()};$editorShutdownChild.Dispose()}
 Write-Output "Retained Editor shutdown logs: $editorShutdownLogs"
}
