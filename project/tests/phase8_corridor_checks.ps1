[CmdletBinding()]
param(
    [ValidateSet('Debug','Release')][string]$Configuration='Release'
)
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$output=(Resolve-Path -LiteralPath (Join-Path $repo "../generated/outputs/$Configuration")).Path
$launcher=Join-Path $output 'KuboLauncher.exe'
$editorExe=Join-Path $output 'KuboEditor.exe'
$playerExe=Join-Path $output 'KuboPlayer.exe'
$adapterExe=Join-Path $output 'KuboMcpServer.exe'
$temp=[IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')
$root=Join-Path $temp ('kubo-phase8-corridor-'+[guid]::NewGuid().ToString('N'))
$null=New-Item -Path $root -ItemType Directory
$manifest=Join-Path $root '光の回廊/光の回廊.kubo'
$unrelated=Join-Path $root 'Unrelated'
$null=New-Item -Path $unrelated -ItemType Directory
$editor=$null
$adapter=$null
$player=$null
$sequence=0
function Need([bool]$Value,[string]$Why){if(!$Value){throw $Why}}
function StopOwned($Process){
    if(!$Process){return}
    try{
        if(!$Process.HasExited -and (!$Process.CloseMainWindow() -or !$Process.WaitForExit(4000))){
            Stop-Process -Id $Process.Id
            $Process.WaitForExit()
        }
    }finally{$Process.Dispose()}
}
function ConnectEditor {
    $script:editor=Start-Process -FilePath $editorExe -WorkingDirectory $unrelated -ArgumentList @(
        '--project', ('"'+$manifest+'"'), '--enable-mcp') -WindowStyle Hidden -PassThru
    $null=$editor.Handle
    $descriptor=Join-Path (Split-Path $manifest -Parent) 'Library/Mcp/editor-session.json'
    $until=[DateTime]::UtcNow.AddSeconds(20)
    do {
        Need (!$editor.HasExited) 'Editor exited before publishing its MCP descriptor'
        if(Test-Path -LiteralPath $descriptor -PathType Leaf){break}
        Start-Sleep -Milliseconds 100
    }while([DateTime]::UtcNow -lt $until)
    Need (Test-Path -LiteralPath $descriptor -PathType Leaf) 'Editor session timed out'
    $start=[Diagnostics.ProcessStartInfo]::new()
    $start.FileName=$adapterExe
    $start.Arguments='--project "'+$manifest+'"'
    $start.WorkingDirectory=$unrelated
    $start.UseShellExecute=$false
    $start.CreateNoWindow=$true
    $start.RedirectStandardInput=$start.RedirectStandardOutput=$true
    $start.StandardInputEncoding=$start.StandardOutputEncoding=[Text.UTF8Encoding]::new($false)
    $script:adapter=[Diagnostics.Process]::Start($start)
    $null=Rpc initialize @{protocolVersion='2025-11-25';capabilities=@{};clientInfo=@{name='Corridor test';version='1'}}
    $adapter.StandardInput.WriteLine('{"jsonrpc":"2.0","method":"notifications/initialized"}')
    $adapter.StandardInput.Flush()
}
function Rpc([string]$Method,[hashtable]$Params){
    $id=++$script:sequence
    $request=@{jsonrpc='2.0';id=$id;method=$Method;params=$Params}|ConvertTo-Json -Depth 16 -Compress
    $adapter.StandardInput.WriteLine($request);$adapter.StandardInput.Flush()
    $read=$adapter.StandardOutput.ReadLineAsync()
    Need ($read.Wait(15000) -and $read.Result) "No response for $Method"
    $response=$read.Result|ConvertFrom-Json
    Need ($response.id -eq $id -and !$response.error) "RPC failed: $($read.Result)"
    return $response.result
}
function Tool([string]$Name,[hashtable]$Arguments=@{}){
    for($attempt=0;$attempt -lt 5;++$attempt){
        $response=Rpc 'tools/call' @{name=$Name;arguments=$Arguments}
        if(!$response.isError){return $response.structuredContent}
        $message=$response.content[0].text
        if($message -notlike '*Wait for the asset import to finish.*'){break}
        Start-Sleep -Milliseconds 1100
    }
    throw "$Name failed: $message"
}
try {
    $hub=Start-Process -FilePath $launcher -ArgumentList @('--create-project',('"'+$manifest+'"'),'--no-launch') -WorkingDirectory $unrelated -WindowStyle Hidden -PassThru
    try {
        $null=$hub.Handle
        Need ($hub.WaitForExit(20000) -and $hub.ExitCode -eq 0) 'KuboLauncher did not create the project'
    }finally{StopOwned $hub}
    & (Join-Path $PSScriptRoot 'prepare_phase8_corridor.ps1') -ProjectFile $manifest -Walkthrough
    $project=Split-Path $manifest -Parent
    ConnectEditor
    $before=Tool 'editor_get_status'
    Need ($before.scenePath -eq 'Assets/Scenes/Main.scene' -and !$before.dirty) 'Editor did not open the prepared Hub project'

    # A real UI component edit through the Editor, followed by normal Save/Play.
    $null=Tool 'editor_select_entity' @{entityId=3}
    $null=Tool 'scene_set_component' @{entityId=3;component='sprite';patch=@{text='WASDで移動 ／ 光の出口へ'}}
    Need ((Tool 'editor_get_status').dirty) 'The title edit did not dirty the Scene'
    Need ((Tool 'scene_save').savedCurrentScene) 'Editor did not save the title'
    $main=Join-Path $project 'Assets/Scenes/Main.scene'
    $saved=Get-Content -LiteralPath $main -Raw -Encoding UTF8 | ConvertFrom-Json
    Need (($saved.entities|Where-Object id -EQ 3).sprite.text -ceq 'WASDで移動 ／ 光の出口へ') 'Saved title text differs'
    $originalHash=(Get-FileHash -LiteralPath $main).Hash
    Need ((Tool 'editor_play').playback -eq 'playing') 'Editor refused Play'
    $until=[DateTime]::UtcNow.AddSeconds(18)
    $sawStage=$false
    $reached=$false
    do {
        Start-Sleep -Milliseconds 500
        $entities=(Tool 'scene_list_entities').entities
        $names=$entities|ForEach-Object name
        if('Runner' -in $names -and 'Exit Beacon' -in $names){$sawStage=$true}
        if('Clear Heading' -in $names){$reached=$true;break}
    }while([DateTime]::UtcNow -lt $until)
    Need ($sawStage -and $reached) 'Editor Play did not pass the spawned Prefab and reach the results'
    $stopped=Tool 'editor_stop'
    Need ($stopped.playback -eq 'editing' -and $stopped.primaryEntityId -eq 3 -and !$stopped.dirty) 'Stop lost selection or saved baseline'
    Need ((Get-FileHash -LiteralPath $main).Hash -eq $originalHash) 'Editor playback wrote the Scene'
    StopOwned $adapter;$adapter=$null
    StopOwned $editor;$editor=$null

    $package=Join-Path $root 'Package'
    $packager=Join-Path $repo 'tools/package_project.ps1'
    $shared=Join-Path $project 'Assets/Materials/出口.material'
    $held=Join-Path $root 'held.material'
    [IO.File]::Move($shared,$held)
    try {
        $refused=$false
        try{& $packager -ProjectFile $manifest -OutputDirectory $package -Configuration $Configuration -RuntimeExecutable $playerExe}
        catch {
            $refused=$_.Exception.Message -like '*dependency validation failed*'
            if(!$refused){throw}
        }
        Need ($refused -and !(Test-Path -LiteralPath (Join-Path $package 'package.json'))) 'Missing Prefab Material was accepted for distribution'
    } finally {[IO.File]::Move($held,$shared)}
    & $packager -ProjectFile $manifest -OutputDirectory $package -Configuration $Configuration -RuntimeExecutable $playerExe
    $packedManifest=Join-Path $package (Split-Path $manifest -Leaf)
    $packagedExe=Join-Path $package 'KuboPlayer.exe'
    Need ((Get-FileHash -LiteralPath $packagedExe).Hash -eq (Get-FileHash -LiteralPath $playerExe).Hash) 'Export did not use the standard Player executable'
    Need ((Get-FileHash -LiteralPath (Join-Path $package 'Assets/Scenes/Main.scene')).Hash -eq $originalHash) 'Export changed the saved title'
    $stdout=Join-Path $root 'Player.log'
    $stderr=Join-Path $root 'Player.stderr.log'
    $player=Start-Process -FilePath $packagedExe -WorkingDirectory $unrelated -ArgumentList @(
        '--project',('"'+$packedManifest+'"'),'--diagnostics') -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    $null=$player.Handle
    $until=[DateTime]::UtcNow.AddSeconds(22)
    do {
        Need (!$player.HasExited) 'Packaged Player exited before the results'
        $faults=@(Get-Content -LiteralPath $stdout -ErrorAction SilentlyContinue|Where-Object {$_ -like 'KUBO_LUA_FAULT *'})
        if($faults.Count){break}
        Start-Sleep -Milliseconds 250
    }while([DateTime]::UtcNow -lt $until)
    Need ($faults.Count -eq 1 -and $faults[0] -match 'KUBO_CORRIDOR_RESULTS_REACHED$') (
        'Packaged Player did not reach the results: '+($faults -join '; '))
    Write-Output "PASS: $Configuration production Hub -> Editor title edit/Save/Play/Prefab/trigger/result/Stop -> refused missing Material/repair/Export -> packaged Player result"
} finally {
    StopOwned $player
    StopOwned $adapter
    StopOwned $editor
    $resolved=(Resolve-Path -LiteralPath $root).Path
    if([IO.Path]::GetDirectoryName($resolved) -ne $temp -or $resolved -ne $root){throw 'Unexpected fixture cleanup path'}
    [IO.Directory]::Delete($resolved,$true)
}
