[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
if(!(Get-Command cl.exe -ErrorAction SilentlyContinue)){throw 'Use an x64 Developer PowerShell with the matching C++ toolchain'}
$controllerCandidateProject=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$controllerCandidateOutput=[IO.Path]::GetFullPath((Join-Path $controllerCandidateProject ('../generated/controller-candidate/'+[guid]::NewGuid().ToString('N'))))
New-Item -ItemType Directory -Path $controllerCandidateOutput | Out-Null
Write-Output "CONTROLLER_CANDIDATE_LOGS=$controllerCandidateOutput"
$controllerCandidateSources=@((Join-Path $PSScriptRoot 'animator_controller_candidate_checks.cpp'),(Join-Path $controllerCandidateProject 'externals/imgui_node_editor/crude_json.cpp'))
& cl.exe /nologo /std:c++20 /EHsc /utf-8 /W3 /WX "/I$controllerCandidateProject" "/I$PSScriptRoot" "/I$controllerCandidateProject/externals/imgui_node_editor" @controllerCandidateSources "/Fo:$controllerCandidateOutput/" "/Fe:$controllerCandidateOutput/ControllerCandidateChecks.exe" *> (Join-Path $controllerCandidateOutput 'build.log')
if($LASTEXITCODE -ne 0){Get-Content (Join-Path $controllerCandidateOutput 'build.log') -Tail 16;throw 'Private controller candidate compile failed'}
& (Join-Path $controllerCandidateOutput 'ControllerCandidateChecks.exe') *> (Join-Path $controllerCandidateOutput 'native.log')
$controllerCandidateExit=$LASTEXITCODE
Get-Content (Join-Path $controllerCandidateOutput 'native.log')
if($controllerCandidateExit -ne 0){throw "Private controller candidate native failure: $controllerCandidateExit"}
Write-Output 'PASS: private controller candidate compiler/native exit0; production/registry/SDK untouched'
