[CmdletBinding()]
param([string]$MSBuild = 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe')
$ErrorActionPreference = 'Stop'
$playerProjectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
function Require($condition, [string]$why) { if (-not $condition) { throw $why } }
foreach ($playerConfiguration in @('Debug', 'Release')) {
    $playerEvaluationText = & $MSBuild (Join-Path $playerProjectRoot 'KuboPlayer.vcxproj') "/p:Configuration=$playerConfiguration" /p:Platform=x64 /getProperty:TargetName,IntDir /getItem:ClCompile,ProjectReference | Out-String
    Require ($LASTEXITCODE -eq 0) 'Player project evaluation failed'
    $playerEvaluation = $playerEvaluationText | ConvertFrom-Json
    Require ($playerEvaluation.Properties.TargetName -eq 'KuboPlayer') 'Player target name changed'
    Require ($playerEvaluation.Properties.IntDir -match '\\obj\\KuboPlayer\\') 'Player shares Editor object directory'
    $playerSources = @($playerEvaluation.Items.ClCompile)
    Require ($playerSources.Count -eq 1 -and $playerSources[0].Identity -eq 'main.cpp') 'Player is not a thin entry-point consumer'
    Require (@($playerEvaluation.Items.ProjectReference | Where-Object {$_.Identity -eq 'KuboRuntime.vcxproj' -and $_.LinkLibraryDependencies -eq 'true' -and $_.UseLibraryDependencyInputs -eq 'false'}).Count -eq 1) 'Player does not link the runtime archive'
    $runtimeEvaluationText = & $MSBuild (Join-Path $playerProjectRoot 'KuboRuntime.vcxproj') "/p:Configuration=$playerConfiguration" /p:Platform=x64 /getProperty:TargetName,IntDir,ConfigurationType /getItem:ClCompile,ProjectReference | Out-String
    Require ($LASTEXITCODE -eq 0) 'Runtime project evaluation failed'
    $runtimeEvaluation = $runtimeEvaluationText | ConvertFrom-Json
    Require ($runtimeEvaluation.Properties.TargetName -eq 'KuboRuntime' -and $runtimeEvaluation.Properties.ConfigurationType -eq 'StaticLibrary' -and $runtimeEvaluation.Properties.IntDir -match '\\obj\\KuboRuntime\\') 'Runtime is not an isolated static-library target'
    $runtimeSources = @($runtimeEvaluation.Items.ClCompile)
    Require ($runtimeSources.Identity -contains 'engine\app\RuntimeGame.cpp' -and $runtimeSources.Identity -contains 'engine\app\Application.cpp' -and $runtimeSources.Identity -notcontains 'main.cpp') 'Runtime archive source boundary changed'
    foreach ($playerSource in @($playerSources) + @($runtimeSources)) {
        Require ($playerSource.Identity -notmatch 'engine\\editor\\|engine\\services\\|ImGuiManager\.cpp$') "Editor source retained: $($playerSource.Identity)"
        $playerDefinitions = @($playerSource.PreprocessorDefinitions -split ';')
        Require ($playerDefinitions -contains 'KUBO_RUNTIME_ONLY') 'Runtime compilation gate missing'
        Require ($playerDefinitions -notcontains 'KUBO_WITH_EDITOR') 'Editor compilation gate leaked into Player'
        Require ($playerDefinitions -contains 'UNICODE' -and $playerDefinitions -contains '_UNICODE') 'Player lost Unicode compilation settings'
    }
    foreach($runtimeSource in $runtimeSources) {Require ($runtimeSource.DebugInformationFormat -eq 'OldStyle') 'Runtime archive requires embedded object debug symbols, not a compiler PDB'}
    Require (@($playerEvaluation.Items.ProjectReference | Where-Object { $_.Identity -match 'imgui' }).Count -eq 0) 'Player references ImGui library project'
    Require (@($runtimeEvaluation.Items.ProjectReference | Where-Object { $_.Identity -match 'imgui' }).Count -eq 0) 'Runtime references ImGui library project'
    $playerLinkLog = Join-Path $playerEvaluation.Properties.IntDir 'KuboPlayer.tlog\link.command.1.tlog'
    Require (Test-Path -LiteralPath $playerLinkLog) 'Build Player before checking actual link inputs'
    # MSBuild retains separate records for instrumented and normal targets.
    # Inspect the normal executable's exact /OUT record, not historical runners.
    $playerLinkRecords = @((Get-Content -LiteralPath $playerLinkLog -Raw) -split '(?m)(?=^\^)' | Where-Object { $_ -match '/OUT:"[^"\r\n]*\\KUBOPLAYER\.EXE"' })
    Require ($playerLinkRecords.Count -eq 1) 'Normal Player link record missing or ambiguous'
    $playerLinkInputs = $playerLinkRecords[0]
    Require ($playerLinkInputs -match 'KUBORUNTIME\.LIB' -and $playerLinkInputs -notmatch 'RUNTIMEGAME\.OBJ|FRAMEWORK\.OBJ|APPLICATION\.OBJ') 'Player does not consume implementation exclusively through the archive'
    Require ($playerLinkInputs -notmatch 'IMGUI\.LIB|IMGUI.*\.OBJ|DEBUGEDITORMANAGER\.OBJ|IMGUIZMO.*\.OBJ|GITWORKSPACE\.OBJ|MCP.*\.OBJ|UPDATESERVICE\.OBJ|\\GAME\.OBJ') 'Actual Player link retains Editor UI/services'
    Require ($playerLinkInputs -match 'KUBOPLAYER\.EXE' -and $playerLinkInputs -notmatch 'PLAYERRUNTIMECHECKS\.EXE') 'Restore normal Player build after instrumented checks'
    if($playerConfiguration -eq 'Release') {Require ($playerLinkInputs -match '/LTCG' -and $playerLinkInputs -notmatch '/LTCG:INCREMENTAL') 'Reusable runtime archives must not consume stale incremental LTCG caches'}
    $playerOutput = Join-Path (Split-Path -Parent $playerProjectRoot) "generated\outputs\$playerConfiguration"
    $runtimeLibLog=Join-Path $runtimeEvaluation.Properties.IntDir 'KuboRuntime.tlog\Lib.command.1.tlog'
    Require (Test-Path -LiteralPath $runtimeLibLog) 'Build the runtime archive before checking librarian inputs'
    $runtimeRecords=@((Get-Content -LiteralPath $runtimeLibLog -Raw) -split '(?m)(?=^\^)' | Where-Object {$_ -match '/OUT:"[^"\r\n]*\\KUBORUNTIME\.LIB"'})
    Require ($runtimeRecords.Count -eq 1) 'Runtime archive librarian record missing/ambiguous'
    Require ($runtimeRecords[0] -match 'RUNTIMEGAME\.OBJ' -and $runtimeRecords[0] -match 'FRAMEWORK\.OBJ' -and $runtimeRecords[0] -match 'APPLICATION\.OBJ' -and $runtimeRecords[0] -notmatch 'IMGUI\.LIB|IMGUI.*\.OBJ|DEBUGEDITORMANAGER\.OBJ|IMGUIZMO.*\.OBJ|GITWORKSPACE\.OBJ|MCP.*\.OBJ|UPDATESERVICE\.OBJ|\\GAME\.OBJ|\\MAIN\.OBJ') 'Runtime archive contains Editor/entry-point objects or lacks implementation'
    Require ($runtimeRecords[0] -notmatch 'KUBOLUA\.LIB|DIRECTXTEX\.LIB') 'Runtime archive silently embeds dependency archives'
    Require ((Get-Item -LiteralPath (Join-Path $playerOutput 'KuboRuntime.lib')).Length -gt 0) 'Runtime archive is empty/missing'
    $playerHash = (Get-FileHash -LiteralPath (Join-Path $playerOutput 'KuboPlayer.exe') -Algorithm SHA256).Hash
    $editorHash = (Get-FileHash -LiteralPath (Join-Path $playerOutput 'KuboEditor.exe') -Algorithm SHA256).Hash
    Require ($playerHash -ne $editorHash) 'Player is still an Editor binary copy'
    Write-Output "PASS: $playerConfiguration thin Player/runtime-only archive source/define boundaries, actual separate librarian/link inputs and distinct Player/Editor binaries"
}
