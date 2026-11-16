param([ValidateSet('2025-11-25', '2024-11-05')][string]$ProtocolVersion = '2025-11-25', [switch]$WithCamera, [switch]$WithModel, [switch]$WithScript, [switch]$UnicodePaths, [switch]$OrthographicCamera, [switch]$WithAudio, [switch]$WithRender, [switch]$WithUI, [switch]$WithEditorStatus, [switch]$WithEditorAuthoring, [string]$EngineDirectory)
$ErrorActionPreference = 'Stop'
if ($OrthographicCamera) { $WithCamera = $true }
$fixture = Join-Path ([IO.Path]::GetTempPath()) ('kubo-mcp-e2e-' + [Guid]::NewGuid().ToString('N'))
$unicodeLabel = -join @([char]0x65e5, [char]0x672c, [char]0x8a9e)
$unicodeLabel += [char]::ConvertFromUtf32(0x1f680)
if ($UnicodePaths) { $fixture += '-' + $unicodeLabel }
$engine = if([string]::IsNullOrWhiteSpace($EngineDirectory)) {[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\generated\outputs\Release'))} else {[IO.Path]::GetFullPath($EngineDirectory)}
$editor = $null
$adapter = $null
$adapterInput = $null
$player = $null
$fixturePassed = $false
$rpcLogRoot=Join-Path $PSScriptRoot '../../generated/cpu-checks'
New-Item -ItemType Directory -Path $rpcLogRoot -Force|Out-Null
$rpcLog=Join-Path $rpcLogRoot ('mcp-rpc-'+(Split-Path -Leaf $fixture)+'.log')
function Require([bool]$Value, [string]$Message) { if (!$Value) { throw $Message } }
function Rpc([string]$Line, $ExpectedId, [int]$ExpectedError = 0) {
    for($attempt=0;$attempt -lt 5;++$attempt){
        $adapterInput.WriteLine($Line)
        $adapterInput.Flush()
        $read = $adapter.StandardOutput.ReadLineAsync()
        if (!$read.Wait(15000)) { throw 'MCP adapter response timeout' }
        if (!$read.Result) { throw 'MCP adapter closed stdout' }
        [IO.File]::AppendAllText($rpcLog,"SEND $Line`nRECV $($read.Result)`n",[Text.UTF8Encoding]::new($false))
        $response = $read.Result | ConvertFrom-Json
        if(!$response.result.isError -or $response.result.content[0].text -notlike '*Wait for the asset import to finish.*'){break}
        # The live Asset Browser may be refreshing this fixture mid-scenario.
        # Retry only the explicit transient lock. Every refusal remains logged.
        Start-Sleep -Milliseconds 1100
    }
    Require ($response.id -eq $ExpectedId) "Response id mismatch: expected=$ExpectedId actual=$($response.id) payload=$($read.Result)"
    if ($ExpectedError -ne 0) { Require ($response.error.code -eq $ExpectedError) 'Expected JSON-RPC error was not returned' }
    else { Require (!$response.error) ('JSON-RPC error: ' + ($response.error | ConvertTo-Json -Compress)) }
    return $response
}
try {
    New-Item -ItemType Directory -Path $fixture | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $fixture 'Assets') | Out-Null
    $manifest = Join-Path $fixture 'Sample.kubo'
    $startupSceneName = if($UnicodePaths) {$unicodeLabel+'.scene'} else {'entry.scene'}
    $startupManifest = @{name='McpTest';engineVersion='0.1.0';startupScene=$startupSceneName;assetDirectory='Assets'} | ConvertTo-Json -Compress
    [IO.File]::WriteAllText($manifest, $startupManifest)
    # Parent tests below need an authored Entity distinct from the MCP-created
    # child even when the optional camera/model/script/audio fixtures are off.
    $startup = '{"version":1,"entities":[{"id":1,"parent":0,"name":"Authored parent","active":true,"transform":{"position":[0,0,0],"rotation":[0,0,0],"scale":[1,1,1]},"model":""}]}'
    $expectedEntities = 2
    if ($WithCamera) {
        $startup = '{"version":1,"entities":[{"id":1,"parent":0,"name":"Authored camera","active":true,"transform":{"position":[2,3,-8],"rotation":[0.1,0.2,0],"scale":[1,1,1]},"model":"","camera":{"enabled":true,"primary":true,"fovY":0.8,"nearClip":0.2,"farClip":500}}]}'
        $expectedEntities = 2
        if ($OrthographicCamera) {
            $orthoStartup = $startup | ConvertFrom-Json
            $orthoStartup.entities[0].camera | Add-Member -NotePropertyName projection -NotePropertyValue orthographic
            $orthoStartup.entities[0].camera | Add-Member -NotePropertyName orthographicHeight -NotePropertyValue 12
            $startup = $orthoStartup | ConvertTo-Json -Depth 12 -Compress
        }
    }
    if ($WithModel) {
        $modelGuid = '0123456789abcdef0123456789abcdef'
        $modelFileName = if ($UnicodePaths) { $unicodeLabel + '.obj' } else { 'moved.obj' }
        [IO.File]::WriteAllText((Join-Path $fixture ('Assets\' + $modelFileName)), "v 0 0 0`nv 1 0 0`nv 0 1 0`nvt 0 0`nvt 1 0`nvt 0 1`nvn 0 0 1`nf 1/1/1 2/2/1 3/3/1`n")
        [IO.File]::WriteAllText((Join-Path $fixture ('Assets\' + $modelFileName + '.kubo-meta.json')), ('{"version":1,"guid":"' + $modelGuid + '"}'))
        $scene = $startup | ConvertFrom-Json
        $model = '{"id":2,"parent":0,"name":"GUID model","active":true,"transform":{"position":[0,0,0],"rotation":[0,0,0],"scale":[1,1,1]},"model":"Assets/original.obj","modelGuid":"0123456789abcdef0123456789abcdef"}' | ConvertFrom-Json
        $scene.entities = @($scene.entities) + @($model)
        $startup = $scene | ConvertTo-Json -Depth 12 -Compress
        $expectedEntities++
    }
    if ($WithScript) {
        $scriptGuid = 'fedcba9876543210fedcba9876543210'
        $scriptFileName = if ($UnicodePaths) { $unicodeLabel + '.kubo' } else { 'moved.kubo' }
        [IO.File]::WriteAllText((Join-Path $fixture ('Assets\' + $scriptFileName)), "on_create translate 10 0 0`non_update translate 2 0 0`non_destroy translate 100 0 0`n")
        [IO.File]::WriteAllText((Join-Path $fixture ('Assets\' + $scriptFileName + '.kubo-meta.json')), ('{"version":1,"guid":"' + $scriptGuid + '"}'))
        $scene = $startup | ConvertFrom-Json
        $scriptEntity = '{"id":3,"parent":0,"name":"GUID script","active":true,"transform":{"position":[0,0,0],"rotation":[0,0,0],"scale":[1,1,1]},"model":"","script":{"asset":"Assets/original.kubo","guid":"fedcba9876543210fedcba9876543210","enabled":true}}' | ConvertFrom-Json
        $scriptEntity.script | Add-Member -NotePropertyName properties -NotePropertyValue @{speed=2.5;flag=$true;label=$unicodeLabel}
        $scene.entities = @($scene.entities) + @($scriptEntity)
        $startup = $scene | ConvertTo-Json -Depth 12 -Compress
        $expectedEntities++
    }
    if ($WithAudio) {
        $audioGuid = 'aabbccddeeff00112233445566778899'
        $audioFileName = if ($UnicodePaths) { $unicodeLabel + '.wav' } else { 'moved.wav' }
        [IO.File]::WriteAllBytes((Join-Path $fixture ('Assets\'+$audioFileName)),[byte[]]@(82,73,70,70,40,0,0,0,87,65,86,69,102,109,116,32,16,0,0,0,1,0,1,0,64,31,0,0,128,62,0,0,2,0,16,0,100,97,116,97,4,0,0,0,0,0,0,0))
        [IO.File]::WriteAllText((Join-Path $fixture ('Assets\'+$audioFileName+'.kubo-meta.json')),('{"version":1,"guid":"'+$audioGuid+'"}'))
        $scene = $startup | ConvertFrom-Json
        $audioEntity = '{"id":4,"parent":0,"name":"GUID Audio","active":true,"transform":{"position":[0,0,0],"rotation":[0,0,0],"scale":[1,1,1]},"model":"","audio":{"asset":"Assets/original.wav","guid":"aabbccddeeff00112233445566778899","enabled":true,"loop":true,"playOnAwake":true,"volume":0}}' | ConvertFrom-Json
        $scene.entities = @($scene.entities) + @($audioEntity)
        $startup = $scene | ConvertTo-Json -Depth 12 -Compress
        $expectedEntities++
    }
    if ($WithRender) {
        $scene=$startup | ConvertFrom-Json
        $scene.entities[0] | Add-Member -NotePropertyName light -NotePropertyValue @{type='point';enabled=$true;color=@(0,1,0);intensity=0.7;range=10;decay=2;innerAngle=0.35;outerAngle=0.6}
        $scene.entities[0] | Add-Member -NotePropertyName material -NotePropertyValue @{lighting=$true;color=@(1,1,1,1);shininess=64;roughness=0.25;reflectionStrength=0;textureInfluence=1}
        $startup=$scene | ConvertTo-Json -Depth 12 -Compress
    }
    if($WithUI){
        $uiGuid='ddeeff00112233445566778899aabbcc';$uiFileName=if($UnicodePaths){$unicodeLabel+'-ui.png'}else{'ui.png'}
        $uiSource=Join-Path $fixture ('Assets/'+$uiFileName);$uiMetadata=$uiSource+'.kubo-meta.json'
        Copy-Item -LiteralPath (Join-Path $engine 'Resources/textures/common/white.png') -Destination $uiSource
        [IO.File]::WriteAllText($uiMetadata,('{"version":1,"guid":"'+$uiGuid+'"}'),[Text.UTF8Encoding]::new($false))
        $uiBytes=(Get-FileHash -LiteralPath $uiSource).Hash;$uiMetadataBytes=(Get-FileHash -LiteralPath $uiMetadata).Hash
        $scene=$startup|ConvertFrom-Json;$scene.entities[0]|Add-Member -NotePropertyName canvas -NotePropertyValue @{referenceSize=@(640,360);scaleMode='scaleWithOutput';sortOrder=-1}
        $scene.entities[0]|Add-Member -NotePropertyName sprite -NotePropertyValue @{asset='Assets/startup-stale-ui.png';guid=$uiGuid;size=@(40,30);color=@(0,1,0,1)}
        $startup=$scene|ConvertTo-Json -Depth 12 -Compress
    }
    [IO.File]::WriteAllText((Join-Path $fixture $startupSceneName), $startup)
    if ($WithCamera -or $WithModel -or $WithScript -or $WithAudio -or $WithRender -or $WithUI) {
        $player = Start-Process -FilePath (Join-Path $engine 'KuboPlayer.exe') -ArgumentList @('--project', ('"' + $manifest + '"')) -WorkingDirectory $fixture -WindowStyle Hidden -PassThru
    }
    $editor = Start-Process -FilePath (Join-Path $engine 'KuboEditor.exe') -ArgumentList @('--project', ('"' + $manifest + '"'), '--enable-mcp') -WorkingDirectory $fixture -WindowStyle Hidden -PassThru
    $descriptor = Join-Path $fixture 'Library\Mcp\editor-session.json'
    $ready = $false
    for ($attempt = 0; $attempt -lt 300; $attempt++) {
        $editor.Refresh()
        if ($editor.HasExited) { throw "Editor exited before MCP was ready: $($editor.ExitCode)" }
        if (Test-Path -LiteralPath $descriptor -PathType Leaf) {
            if ((Get-Item -LiteralPath $descriptor).Length -gt 0) { $ready = $true; break }
        }
        Start-Sleep -Milliseconds 100
    }
    Require $ready 'Editor MCP descriptor was not published'
    $start = New-Object Diagnostics.ProcessStartInfo
    $start.FileName = Join-Path $engine 'KuboMcpServer.exe'
    $start.Arguments = '--project "' + $manifest + '"'
    $start.WorkingDirectory = $fixture
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $start.RedirectStandardInput = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.StandardOutputEncoding = [Text.UTF8Encoding]::new($false)
    $adapter = [Diagnostics.Process]::Start($start)
    # .NET Framework's default stdin writer follows the local console codepage.
    # Own a BOM-free UTF-8 writer, leaving the pipe open for Process to close on EOF.
    $adapterInput=[IO.StreamWriter]::new($adapter.StandardInput.BaseStream,[Text.UTF8Encoding]::new($false),1024,$true)
    $adapterInput.AutoFlush=$true
    $blocked = Rpc '{"jsonrpc":"2.0","id":10,"method":"tools/call","params":{"name":"scene_create_entity","arguments":{"name":"Must not exist"}}}' 10 -32002
    $initializeRequest = @{ jsonrpc = '2.0'; id = 1; method = 'initialize'; params = @{ protocolVersion = $ProtocolVersion; capabilities = @{}; clientInfo = @{ name = 'KuboTest'; version = '1' } } } | ConvertTo-Json -Depth 6 -Compress
    $initialized = Rpc $initializeRequest 1
    Require ($initialized.result.serverInfo.name -eq 'KuboEngine') 'Initialize did not identify the adapter'
    Require ($initialized.result.protocolVersion -eq $ProtocolVersion) 'Negotiated version mismatch'
    $beforeNotification = Rpc '{"jsonrpc":"2.0","id":11,"method":"tools/list"}' 11 -32002
    $adapterInput.WriteLine('{"jsonrpc":"2.0","method":"notifications/initialized"}')
    $tools = Rpc '{"jsonrpc":"2.0","id":2,"method":"tools/list"}' 2
    # Legacy package replay keeps its six-component contract unless explicitly opted in.
    $expectedComponentBranches=if($WithUI){10}elseif($WithRender){8}else{6}
    $statusTool = $tools.result.tools | Where-Object name -EQ 'editor_get_status'
    $authoringNames=@('editor_select_entity','editor_undo','editor_redo','editor_play','editor_pause','editor_stop')
    $authoringTools=@($tools.result.tools | Where-Object {$_.name -in $authoringNames})
    if($WithEditorAuthoring){Require ($authoringTools.Count -eq 6) 'Live Editor authoring tools missing'}
    $expectedTools = $(if ($statusTool) { 8 } else { 7 }) + $authoringTools.Count
    Require ($tools.result.tools.Count -eq $expectedTools -and $tools.result.tools[0].inputSchema.type -eq 'object' -and $tools.result.tools[3].inputSchema.anyOf.Count -eq 3 -and $tools.result.tools[4].annotations.destructiveHint -and $tools.result.tools[5].inputSchema.properties.parentId.minimum -eq 0 -and $tools.result.tools[6].inputSchema.oneOf.Count -eq $expectedComponentBranches) 'Tool schema missing'
    if ($WithEditorStatus) { Require ($statusTool -and $statusTool.annotations.readOnlyHint) 'Editor status tool missing' }
    if ($statusTool) {
        $status = Rpc '{"jsonrpc":"2.0","id":190,"method":"tools/call","params":{"name":"editor_get_status"}}' 190
        Require (!$status.result.isError) 'Editor status call failed'
        $initialStatus = ($status.result.content[0].text | ConvertFrom-Json).result
        Require ($initialStatus.scenePath -ceq $startupSceneName -and $initialStatus.playback -eq 'editing' -and
            !$initialStatus.dirty -and $initialStatus.primaryEntityId -eq 1 -and $initialStatus.selectionCount -eq 1) 'Initial Editor status differs'
        if ($ProtocolVersion -eq '2025-11-25') { Require ($status.result.structuredContent.scenePath -ceq $startupSceneName) 'Structured Editor status missing' }
    }
    Require ($tools.result.tools[5].inputSchema.properties.keepWorld.type -eq 'boolean' -and $tools.result.tools[5].inputSchema.properties.keepWorld.default -eq $false) 'Keep-World optional boolean/default schema missing'
    $unknown = Rpc '{"jsonrpc":"2.0","id":12,"method":"tools/call","params":{"name":"scene_unknown"}}' 12 -32602
    $bypass = Rpc '{"jsonrpc":"2.0","id":13,"method":"scene.create_entity","params":{"name":"Must not exist"}}' 13 -32601
    $malformed = Rpc '{' $null -32700
    $oversized = Rpc ([string]::new([char]'x', 65537)) $null -32700
    $ping = Rpc '{"jsonrpc":"2.0","id":14,"method":"ping"}' 14
    $adapterInput.WriteLine('{"jsonrpc":"2.0","method":"tools/call","params":{"name":"scene_create_entity","arguments":{"name":"Must not exist"}}}')
    $create = Rpc '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"scene_create_entity","arguments":{"name":"Created through MCP adapter"}}}' 3
    Require (!$create.result.isError) ('Create tool failed: ' + $create.result.content[0].text)
    if ($statusTool) {
        $status = Rpc '{"jsonrpc":"2.0","id":191,"method":"tools/call","params":{"name":"editor_get_status","arguments":{}}}' 191
        Require (!$status.result.isError -and ($status.result.content[0].text | ConvertFrom-Json).result.dirty) 'Editor status did not report the MCP edit'
    }
    $createdReply=$create.result.content[0].text | ConvertFrom-Json
    $editArguments=@{jsonrpc='2.0';id=30;method='tools/call';params=@{name='scene_set_transform';arguments=@{entityId=$createdReply.result.entityId;position=@(3,4,5);rotation=@(0.1,0.2,0.3);scale=@(2,2,2)}}} | ConvertTo-Json -Depth 8 -Compress
    $edited=Rpc $editArguments 30
    Require (!$edited.result.isError) 'Live Editor Transform tool failed'
    if($ProtocolVersion -eq '2025-11-25') {Require $edited.result.structuredContent.updated 'Modern Transform structured reply missing'}
    $invalidTransform=@{jsonrpc='2.0';id=33;method='tools/call';params=@{name='scene_set_transform';arguments=@{entityId=$createdReply.result.entityId;position=@(999,999,999);rotation=@(0,1e100,0)}}} | ConvertTo-Json -Depth 8 -Compress
    $invalidEdit=Rpc $invalidTransform 33
    Require ([bool]$invalidEdit.result.isError) 'Malformed Transform tool was accepted'
    $scratch=Rpc '{"jsonrpc":"2.0","id":31,"method":"tools/call","params":{"name":"scene_create_entity","arguments":{"name":"Delete fixture"}}}' 31
    $scratchReply=$scratch.result.content[0].text | ConvertFrom-Json
    $deleteRequest=@{jsonrpc='2.0';id=32;method='tools/call';params=@{name='scene_delete_entity';arguments=@{entityId=$scratchReply.result.entityId}}} | ConvertTo-Json -Depth 8 -Compress
    $deleted=Rpc $deleteRequest 32
    Require (!$deleted.result.isError) 'Live Editor Delete tool failed'
    if($ProtocolVersion -eq '2025-11-25') {Require $deleted.result.structuredContent.deleted 'Modern deletion structured reply missing'}
    $deleteAgain=($deleteRequest | ConvertFrom-Json);$deleteAgain.id=34
    $missingDelete=Rpc ($deleteAgain | ConvertTo-Json -Depth 8 -Compress) 34
    Require ([bool]$missingDelete.result.isError) 'Repeated missing-Entity deletion reported success'
    $parentRequest=@{jsonrpc='2.0';id=35;method='tools/call';params=@{name='scene_set_parent';arguments=@{entityId=$createdReply.result.entityId;parentId=1}}}
    $parented=Rpc ($parentRequest | ConvertTo-Json -Depth 8 -Compress) 35
    Require (!$parented.result.isError) 'Live Editor parent tool failed'
    if($ProtocolVersion -eq '2025-11-25') {Require $parented.result.structuredContent.updated 'Modern parent structured reply missing'}
    $cycleRequest=@{jsonrpc='2.0';id=36;method='tools/call';params=@{name='scene_set_parent';arguments=@{entityId=1;parentId=$createdReply.result.entityId}}}
    $cycleReply=Rpc ($cycleRequest | ConvertTo-Json -Depth 8 -Compress) 36
    Require ([bool]$cycleReply.result.isError) 'Live Editor accepted hierarchy cycle'
    $parentRequest.id=37;$parentRequest.params.arguments.parentId=0;$parentRequest.params.arguments.keepWorld=$true
    $rooted=Rpc ($parentRequest | ConvertTo-Json -Depth 8 -Compress) 37
    Require (!$rooted.result.isError) 'Root parent tool failed'
    $rootList=Rpc '{"jsonrpc":"2.0","id":39,"method":"tools/call","params":{"name":"scene_list_entities","arguments":{}}}' 39
    $rootPayload=$rootList.result.content[0].text | ConvertFrom-Json
    $rootChild=$rootPayload.result | Where-Object {$_.name -eq 'Created through MCP adapter'}
    Require ($rootChild.parent -eq 0) 'Keep-World root parent missing'
    if($WithCamera) {
        $parentX=3*[Math]::Cos(0.2)+4*[Math]::Sin(0.1)*[Math]::Sin(0.2)+5*[Math]::Cos(0.1)*[Math]::Sin(0.2)+2
        $parentY=4*[Math]::Cos(0.1)-5*[Math]::Sin(0.1)+3
        $parentZ=-3*[Math]::Sin(0.2)+4*[Math]::Sin(0.1)*[Math]::Cos(0.2)+5*[Math]::Cos(0.1)*[Math]::Cos(0.2)-8
        Require ([Math]::Abs($rootChild.transform.position[0]-$parentX) -lt 0.001 -and [Math]::Abs($rootChild.transform.position[1]-$parentY) -lt 0.001 -and [Math]::Abs($rootChild.transform.position[2]-$parentZ) -lt 0.001) 'Keep-World root lost composed rotated camera-parent position'
    }
    $parentRequest.id=38;$parentRequest.params.arguments.parentId=1
    $reparented=Rpc ($parentRequest | ConvertTo-Json -Depth 8 -Compress) 38
    Require (!$reparented.result.isError) 'Reparent after rooting failed'
    # Start a new rate window before this additional component-edit group.
    Start-Sleep -Milliseconds 1000
    function Component-Rpc([string]$component,$patch,[int]$requestId){
        $request=@{jsonrpc='2.0';id=$requestId;method='tools/call';params=@{name='scene_set_component';arguments=@{entityId=$createdReply.result.entityId;component=$component;patch=$patch}}}
        return Rpc ($request | ConvertTo-Json -Depth 12 -Compress) $requestId
    }
    $componentCamera=Component-Rpc camera @{enabled=$true;primary=$false;projection='orthographic';orthographicHeight=14} 80
    Require (!$componentCamera.result.isError) 'Actual Editor Camera addition failed'
    $componentCollider=Component-Rpc collider @{enabled=$true;halfExtents=@(0.25,0.5,0.75);friction=0.5;restitution=0.25} 81
    Require (!$componentCollider.result.isError) 'Actual Editor Collider addition failed'
    $componentBody=Component-Rpc rigidBody @{enabled=$true;mass=2;dynamic=$false;gravity=$false;velocity=@(0,0,0)} 82
    Require (!$componentBody.result.isError) 'Actual Editor RigidBody addition failed'
    Require (!(Component-Rpc collider @{friction=0.75} 83).result.isError) 'Partial Collider update failed'
    Require ((Component-Rpc collider @{enabled=$false;friction=2} 84).result.isError) 'Invalid Collider patch accepted'
    Require ((Component-Rpc camera @{nearClip=10000;primary=$true} 85).result.isError) 'Invalid Camera cross-field patch accepted'
    Require (!(Component-Rpc collider $null 86).result.isError) 'Actual Editor Collider removal failed'
    $removedComponents=Rpc '{"jsonrpc":"2.0","id":87,"method":"tools/call","params":{"name":"scene_list_entities","arguments":{}}}' 87
    $removedPayload=$removedComponents.result.content[0].text | ConvertFrom-Json
    Require (!($removedPayload.result | Where-Object {$_.name -eq 'Created through MCP adapter'}).collider.enabled) 'Removed Collider still enabled'
    Require (!(Component-Rpc collider @{enabled=$true;halfExtents=@(0.25,0.5,0.75);friction=0.75;restitution=0.25} 88).result.isError) 'Collider re-add failed'
    if($ProtocolVersion -eq '2025-11-25'){Require ($componentBody.result.structuredContent.component -eq 'rigidBody' -and $componentBody.result.structuredContent.updated) 'Modern component structured result missing'}
    Start-Sleep -Milliseconds 1000
    if($WithModel){
        $modelSource=Join-Path $fixture ('Assets/'+$modelFileName);$modelMetadata=$modelSource+'.kubo-meta.json'
        $modelBytes=(Get-FileHash -LiteralPath $modelSource).Hash;$modelMetadataBytes=(Get-FileHash -LiteralPath $modelMetadata).Hash
        Require (!(Component-Rpc model @{asset='Assets/mcp-old.obj';guid=$modelGuid} 90).result.isError) 'Live GUID Model attachment failed'
        Require ((Component-Rpc model @{asset='Assets/missing.obj';guid=$null} 91).result.isError) 'Live missing path Model accepted'
        Require (!(Component-Rpc model $null 92).result.isError) 'Live Model removal failed'
        Require (!(Component-Rpc model @{asset=('Assets/'+$modelFileName);guid=$null} 93).result.isError) 'Live path Model binding failed'
        Require (!(Component-Rpc model @{asset='Assets/mcp-old.obj';guid=$modelGuid} 94).result.isError) 'Live Model GUID re-attachment failed'
        if($WithScript){Require ((Component-Rpc model @{guid=$scriptGuid} 95).result.isError) 'Live Model accepted wrong-type GUID'}
        Require ((Get-FileHash -LiteralPath $modelSource).Hash -eq $modelBytes -and (Get-FileHash -LiteralPath $modelMetadata).Hash -eq $modelMetadataBytes) 'Model attachment changed source/identity bytes'
    }
    if($WithScript){
        $scriptSource=Join-Path $fixture ('Assets/'+$scriptFileName);$scriptMetadata=$scriptSource+'.kubo-meta.json'
        $scriptBytes=(Get-FileHash -LiteralPath $scriptSource).Hash;$scriptMetadataBytes=(Get-FileHash -LiteralPath $scriptMetadata).Hash
        Require (!(Component-Rpc script @{asset='Assets/mcp-old.kubo';guid=$scriptGuid;enabled=$true;properties=@{speed=1;flag=$true;label=$unicodeLabel}} 96).result.isError) 'Live Script attachment failed'
        $scriptOverrideReply=Component-Rpc script @{properties=@{speed=4}} 97
        if($scriptOverrideReply.result.isError) {
            $editor.Refresh();Write-Output "MCP_OVERRIDE_EDITOR_EXITED=$($editor.HasExited)"
            if($editor.HasExited){Write-Output "MCP_OVERRIDE_EDITOR_EXIT=$($editor.ExitCode)"}
            Write-Output ('MCP_OVERRIDE_REPLY='+($scriptOverrideReply | ConvertTo-Json -Depth 12 -Compress))
            Write-Output "MCP_OVERRIDE_RPC_EVIDENCE=$rpcLog"
        }
        Require (!$scriptOverrideReply.result.isError) 'Live Script override replacement failed'
        Require ((Component-Rpc script @{enabled=$false;properties=@{invalid=@(1,2)}} 98).result.isError) 'Live Script accepted non-scalar properties'
        $scriptList=Rpc '{"jsonrpc":"2.0","id":99,"method":"tools/call","params":{"name":"scene_list_entities","arguments":{}}}' 99
        $scriptPayload=$scriptList.result.content[0].text | ConvertFrom-Json
        $attachedScript=($scriptPayload.result | Where-Object {$_.name -eq 'Created through MCP adapter'}).script
        Require ($attachedScript.enabled -and $attachedScript.properties.speed -eq 4 -and !$attachedScript.properties.flag -and !$attachedScript.properties.label) 'Override replacement/refusal did not preserve correct Script'
        Require (!(Component-Rpc script $null 100).result.isError) 'Live Script removal failed'
        Require (!(Component-Rpc script @{asset='Assets/mcp-old.kubo';guid=$scriptGuid;enabled=$true;properties=@{speed=2.5;label=$unicodeLabel}} 101).result.isError) 'Live Script re-attachment failed'
        Require ((Get-FileHash -LiteralPath $scriptSource).Hash -eq $scriptBytes -and (Get-FileHash -LiteralPath $scriptMetadata).Hash -eq $scriptMetadataBytes) 'Script attachment changed source/identity bytes'
    }
    Start-Sleep -Milliseconds 1000
    if($WithAudio){
        $audioSource=Join-Path $fixture ('Assets/'+$audioFileName);$audioMetadata=$audioSource+'.kubo-meta.json'
        $audioBytes=(Get-FileHash -LiteralPath $audioSource).Hash;$audioMetadataBytes=(Get-FileHash -LiteralPath $audioMetadata).Hash
        Require (!(Component-Rpc audio @{asset='Assets/mcp-old.wav';guid=$audioGuid;enabled=$true;loop=$true;playOnAwake=$false;volume=0} 102).result.isError) 'Live Audio attachment failed'
        Require ((Component-Rpc audio @{volume=2;loop=$false} 103).result.isError) 'Live Audio accepted invalid volume'
        Require (!(Component-Rpc audio $null 104).result.isError) 'Live Audio removal failed'
        Require (!(Component-Rpc audio @{asset='Assets/mcp-old.wav';guid=$audioGuid;enabled=$true;loop=$true;playOnAwake=$false;volume=0} 105).result.isError) 'Live Audio re-attachment failed'
        Require ((Get-FileHash -LiteralPath $audioSource).Hash -eq $audioBytes -and (Get-FileHash -LiteralPath $audioMetadata).Hash -eq $audioMetadataBytes) 'Audio attachment changed source/identity bytes'
    }
    if($WithRender){
        Start-Sleep -Milliseconds 1000
        Require (!(Component-Rpc light @{type='spot';enabled=$true;color=@(1,0,0);intensity=0.7;range=10;innerAngle=0.8;outerAngle=1} 120).result.isError) 'Live Light attachment failed'
        Require (!(Component-Rpc light @{outerAngle=0.9} 121).result.isError) 'Light partial merge failed'
        Require ((Component-Rpc light @{enabled=$false;outerAngle=0.7} 122).result.isError) 'Invalid coupled Light patch accepted'
        Require (!(Component-Rpc material @{lighting=$true;color=@(1,1,1,1);shininess=128;roughness=0.25} 123).result.isError) 'Live Material attachment failed'
        Require (!(Component-Rpc material @{roughness=0.75} 124).result.isError) 'Material partial merge failed'
        Require ((Component-Rpc material @{color=@(1,1,1)} 125).result.isError) 'Invalid Material color accepted'
        $renderList=Rpc '{"jsonrpc":"2.0","id":126,"method":"tools/call","params":{"name":"scene_list_entities","arguments":{}}}' 126
        $renderPayload=$renderList.result.content[0].text | ConvertFrom-Json
        $renderEntity=$renderPayload.result | Where-Object {$_.name -eq 'Created through MCP adapter'}
        Require ($renderEntity.light.enabled -and [Math]::Abs($renderEntity.light.innerAngle-0.8) -lt 0.000001 -and [Math]::Abs($renderEntity.light.outerAngle-0.9) -lt 0.000001 -and $renderEntity.material.shininess -eq 128 -and $renderEntity.material.roughness -eq 0.75) 'Live render patch/refusal changed wrong fields'
        Require (!(Component-Rpc light $null 127).result.isError -and !(Component-Rpc material $null 128).result.isError) 'Live render component removal failed'
        $removedRender=Rpc '{"jsonrpc":"2.0","id":129,"method":"tools/call","params":{"name":"scene_list_entities","arguments":{}}}' 129
        $removedRenderPayload=$removedRender.result.content[0].text | ConvertFrom-Json
        $removedRenderEntity=$removedRenderPayload.result | Where-Object {$_.name -eq 'Created through MCP adapter'}
        Require (!$removedRenderEntity.light -and !$removedRenderEntity.material) 'Removed render component persisted in live World'
        Require (!(Component-Rpc light @{type='point';color=@(0,0,1);intensity=0.7;range=10} 130).result.isError) 'Live Light re-add failed'
        Require (!(Component-Rpc material @{lighting=$true;color=@(1,1,1,1);shininess=128;roughness=0.75;textureInfluence=0} 131).result.isError) 'Live Material re-add failed'
        Start-Sleep -Milliseconds 1000
    }
    if($WithUI){
        Start-Sleep -Milliseconds 1000
        Require (!(Component-Rpc canvas @{referenceSize=@(640,360);scaleMode='constantPixels';sortOrder=2} 150).result.isError) 'Live Canvas add failed'
        Require (!(Component-Rpc canvas @{enabled=$false} 151).result.isError) 'Live Canvas partial patch failed'
        Require ((Component-Rpc canvas @{sortOrder=0.5} 152).result.isError) 'Fractional Canvas sortOrder accepted'
        Require (!(Component-Rpc sprite @{asset='Assets/stale-ui.png';guid=$uiGuid;size=@(120,80);color=@(1,0,0,1);flipX=$true;sortOrder=3} 153).result.isError) 'Live Sprite add failed'
        Require (!(Component-Rpc sprite @{pivot=@(0.25,0.75)} 154).result.isError) 'Live Sprite partial patch failed'
        Require ((Component-Rpc sprite @{enabled=$false;size=@(-1,80)} 155).result.isError) 'Invalid Sprite prefix accepted'
        Require ((Component-Rpc sprite @{guid='ffffffffffffffff'} 156).result.isError) 'Missing Sprite GUID fell back'
        if($WithScript){Require ((Component-Rpc sprite @{guid=$scriptGuid} 157).result.isError) 'Sprite accepted Script GUID'}
        Require (!(Component-Rpc sprite @{asset=('Assets/'+$uiFileName);guid=$null} 158).result.isError) 'Sprite GUID clear failed'
        Require (!(Component-Rpc sprite $null 159).result.isError -and !(Component-Rpc canvas $null 160).result.isError) 'UI component removal failed'
        $uiRemoved=Rpc '{"jsonrpc":"2.0","id":161,"method":"tools/call","params":{"name":"scene_list_entities","arguments":{}}}' 161
        $uiRemovedPayload=$uiRemoved.result.content[0].text|ConvertFrom-Json;$uiRemovedEntity=$uiRemovedPayload.result|Where-Object {$_.name -eq 'Created through MCP adapter'}
        Require (!$uiRemovedEntity.canvas -and !$uiRemovedEntity.sprite) 'UI removal did not omit fields'
        Start-Sleep -Milliseconds 1000
        Require (!(Component-Rpc canvas @{referenceSize=@(640,360);scaleMode='constantPixels';sortOrder=2} 162).result.isError) 'Canvas re-add failed'
        Require (!(Component-Rpc sprite @{asset='Assets/stale-ui.png';guid=$uiGuid;size=@(120,80);color=@(0,0,1,1);flipX=$true;sortOrder=3} 163).result.isError) 'Sprite re-add failed'
        Require (!(Component-Rpc sprite @{pivot=@(0.25,0.75)} 164).result.isError) 'Sprite final partial failed'
        Require ((Get-FileHash -LiteralPath $uiSource).Hash -eq $uiBytes -and (Get-FileHash -LiteralPath $uiMetadata).Hash -eq $uiMetadataBytes) 'UI edit changed image/metadata'
    }
    $list = Rpc '{"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"scene_list_entities","arguments":{}}}' 4
    $live = $list.result.content[0].text | ConvertFrom-Json
    Require ($live.result.Count -eq $expectedEntities -and @($live.result | Where-Object { $_.name -eq 'Created through MCP adapter' }).Count -eq 1) 'Adapter did not read the live Editor mutation'
    $transformed=$live.result | Where-Object {$_.name -eq 'Created through MCP adapter'}
    Require ($transformed.parent -eq 1) 'Live Editor parent data missing'
    if($WithModel){Require ($transformed.modelGuid -eq $modelGuid -and $transformed.model -eq 'Assets/mcp-old.obj') 'Live attached Model identity missing'}
    if($WithScript){Require ($transformed.script.guid -eq $scriptGuid -and $transformed.script.enabled -and $transformed.script.properties.speed -eq 2.5 -and $transformed.script.properties.label -eq $unicodeLabel) 'Live attached Script/properties missing'}
    if($WithAudio){Require ($transformed.audio.guid -eq $audioGuid -and $transformed.audio.enabled -and $transformed.audio.loop -and !$transformed.audio.playOnAwake -and $transformed.audio.volume -eq 0) 'Live attached Audio state missing'}
    Require ($transformed.camera.enabled -and !$transformed.camera.primary -and $transformed.camera.projection -eq 'orthographic' -and $transformed.camera.orthographicHeight -eq 14 -and [Math]::Abs($transformed.camera.nearClip-0.1) -lt 0.000001) 'Live Component Camera data/atomic refusal missing'
    Require ($transformed.collider.enabled -and $transformed.collider.friction -eq 0.75 -and $transformed.collider.restitution -eq 0.25 -and $transformed.collider.halfExtents[2] -eq 0.75 -and $transformed.rigidBody.enabled -and $transformed.rigidBody.mass -eq 2 -and !$transformed.rigidBody.dynamic) 'Live Collider/Body data missing'
    Require ([Math]::Abs($transformed.transform.position[0]-3) -lt 0.001 -and [Math]::Abs($transformed.transform.position[2]-5) -lt 0.001 -and [Math]::Abs($transformed.transform.rotation[1]-0.2) -lt 0.001 -and [Math]::Abs($transformed.transform.scale[0]-2) -lt 0.001) 'Live Editor Transform data missing'
    if ($ProtocolVersion -eq '2025-11-25') { Require ($list.result.structuredContent.entities.Count -eq $expectedEntities) 'Modern structured content missing' }
    $legacyTemporary = Join-Path $fixture 'Assets\Scenes\remote.scene.tmp'
    New-Item -ItemType Directory -Path (Split-Path -Parent $legacyTemporary) -Force | Out-Null
    [IO.File]::WriteAllText($legacyTemporary,'unrelated legacy temporary - preserve exactly',[Text.UTF8Encoding]::new($false))
    $legacyTemporaryDigest = (Get-FileHash -LiteralPath $legacyTemporary -Algorithm SHA256).Hash
    $save = Rpc '{"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"scene_save","arguments":{"path":"Assets/Scenes/remote.scene"}}}' 5
    Require (!$save.result.isError) ('Save tool failed: ' + $save.result.content[0].text)
    Require ((Get-FileHash -LiteralPath $legacyTemporary -Algorithm SHA256).Hash -eq $legacyTemporaryDigest) 'MCP save changed unrelated legacy temporary bytes'
    $saved = [IO.File]::ReadAllText((Join-Path $fixture 'Assets\Scenes\remote.scene')) | ConvertFrom-Json
    $savedChild=$saved.entities | Where-Object {$_.name -eq 'Created through MCP adapter'}
    if($WithRender){
        foreach($renderEntity in @($transformed,$savedChild)){
            Require ($renderEntity.light.enabled -and $renderEntity.light.type -eq 'point' -and $renderEntity.light.color[2] -eq 1 -and $renderEntity.light.range -eq 10 -and $renderEntity.material.lighting -and $renderEntity.material.shininess -eq 128 -and $renderEntity.material.roughness -eq 0.75 -and $renderEntity.material.textureInfluence -eq 0) 'Light/Material live/save persistence mismatch'
        }
        $authoredRender=$saved.entities | Where-Object {$_.id -eq 1}
        Require ($authoredRender.light.type -eq 'point' -and $authoredRender.light.color[1] -eq 1 -and $authoredRender.material.shininess -eq 64 -and $authoredRender.material.roughness -eq 0.25) 'Startup authored render components changed during unrelated edits'
    }
    if($WithUI){
        foreach($uiEntity in @($transformed,$savedChild)){
            Require ($uiEntity.canvas.enabled -and $uiEntity.canvas.referenceSize[0] -eq 640 -and $uiEntity.canvas.scaleMode -eq 'constantPixels' -and $uiEntity.canvas.sortOrder -eq 2 -and $uiEntity.sprite.enabled -and $uiEntity.sprite.guid -eq $uiGuid -and $uiEntity.sprite.asset -eq 'Assets/stale-ui.png' -and $uiEntity.sprite.size[0] -eq 120 -and $uiEntity.sprite.pivot[0] -eq 0.25 -and $uiEntity.sprite.color[2] -eq 1 -and $uiEntity.sprite.flipX -and $uiEntity.sprite.sortOrder -eq 3) 'UI live/save persistence mismatch'
        }
        $startupUI=$saved.entities|Where-Object {$_.id -eq 1};Require ($startupUI.canvas.sortOrder -eq -1 -and $startupUI.sprite.guid -eq $uiGuid -and $startupUI.sprite.size[0] -eq 40) 'Startup UI changed during unrelated edits'
    }
    Require ($savedChild.parent -eq 1 -and [Math]::Abs($savedChild.transform.position[0]-3) -lt 0.001 -and [Math]::Abs($savedChild.transform.scale[0]-2) -lt 0.001) 'Saved parent/local TRS did not persist'
    Require ($savedChild.camera.enabled -and !$savedChild.camera.primary -and $savedChild.camera.orthographicHeight -eq 14 -and $savedChild.collider.enabled -and $savedChild.collider.friction -eq 0.75 -and $savedChild.rigidBody.enabled -and $savedChild.rigidBody.mass -eq 2 -and !$savedChild.rigidBody.dynamic) 'MCP component edits did not persist in saved Scene'
    if($WithModel){Require ($savedChild.modelGuid -eq $modelGuid -and $savedChild.model -eq 'Assets/mcp-old.obj') 'Attached Model did not persist'}
    if($WithScript){Require ($savedChild.script.guid -eq $scriptGuid -and $savedChild.script.enabled -and $savedChild.script.properties.speed -eq 2.5 -and $savedChild.script.properties.label -eq $unicodeLabel) 'Attached Script/properties did not persist'}
    if($WithAudio){Require ($savedChild.audio.guid -eq $audioGuid -and $savedChild.audio.enabled -and $savedChild.audio.loop -and $savedChild.audio.volume -eq 0) 'Attached Audio did not persist'}
    Require ($saved.entities.Count -eq $expectedEntities -and @($saved.entities | Where-Object { $_.name -eq 'Created through MCP adapter' }).Count -eq 1) 'Saved scene differs from the live Editor world'
    $savedTransform=$saved.entities | Where-Object {$_.name -eq 'Created through MCP adapter'}
    Require ([Math]::Abs($savedTransform.transform.position[0]-3) -lt 0.001 -and [Math]::Abs($savedTransform.transform.scale[0]-2) -lt 0.001 -and @($saved.entities | Where-Object {$_.name -eq 'Delete fixture'}).Count -eq 0) 'Editor save lost Transform/delete effects'
    if ($WithCamera) {
        $camera = $saved.entities | Where-Object { $_.name -eq 'Authored camera' }
        Require ($camera.camera.enabled -and $camera.camera.primary -and [Math]::Abs($camera.camera.fovY - 0.8) -lt 0.001 -and [Math]::Abs($camera.camera.nearClip - 0.2) -lt 0.001 -and $camera.camera.farClip -eq 500 -and $camera.transform.position[2] -eq -8) 'Actual Editor load/save lost authored camera data'
        if ($OrthographicCamera) { Require ($camera.camera.projection -eq 'orthographic' -and $camera.camera.orthographicHeight -eq 12) 'Actual Editor load/save lost orthographic camera settings' }
        else { Require ($camera.camera.projection -eq 'perspective') 'Legacy camera did not default to perspective' }
    }
    if ($WithModel) {
        $model = $saved.entities | Where-Object { $_.name -eq 'GUID model' }
        Require ($model.modelGuid -eq $modelGuid -and $model.model -eq 'Assets/original.obj') 'Actual Editor load/save lost model identity or rewrote the authored hint'
        Require (!(Test-Path -LiteralPath (Join-Path $fixture 'Assets\original.obj'))) 'GUID startup fixture accidentally contains the old-path model'
    }
    if ($WithScript) {
        $scriptEntity = $saved.entities | Where-Object { $_.name -eq 'GUID script' }
        Require ($scriptEntity.script.guid -eq $scriptGuid -and $scriptEntity.script.asset -eq 'Assets/original.kubo' -and $scriptEntity.script.enabled) 'Actual Editor load/save lost script GUID reference'
        Require ($scriptEntity.script.properties.speed -eq 2.5 -and $scriptEntity.script.properties.flag -eq $true -and $scriptEntity.script.properties.label -eq $unicodeLabel) 'Actual Editor load/save lost typed script properties'
        Require ($scriptEntity.transform.position[0] -eq 0) 'Editor ran script while Editing'
        Require (!(Test-Path -LiteralPath (Join-Path $fixture 'Assets\original.kubo'))) 'Script fixture accidentally contains old-path source'
    }
    if ($WithAudio) {
        $audioEntity = $saved.entities | Where-Object { $_.name -eq 'GUID Audio' }
        Require ($audioEntity.audio.guid -eq $audioGuid -and $audioEntity.audio.asset -eq 'Assets/original.wav' -and $audioEntity.audio.enabled -and $audioEntity.audio.loop -and $audioEntity.audio.playOnAwake -and $audioEntity.audio.volume -eq 0) 'Actual Editor load/save lost Audio component settings'
        Require (!(Test-Path -LiteralPath (Join-Path $fixture 'Assets\original.wav'))) 'Audio fixture accidentally contains old-path source'
    }
    $denied = Rpc '{"jsonrpc":"2.0","id":6,"method":"tools/call","params":{"name":"scene_save","arguments":{"path":"../outside.json"}}}' 6
    Require ([bool]$denied.result.isError) 'Path traversal was accepted'
    if($WithEditorAuthoring){
        Start-Sleep -Milliseconds 1100
        function Command([string]$Name,[int]$RpcId,[hashtable]$Arguments=@{},[bool]$MustReject=$false) {
            $reply=Rpc (@{jsonrpc='2.0';id=$RpcId;method='tools/call';params=@{name=$Name;arguments=$Arguments}} | ConvertTo-Json -Depth 8 -Compress) $RpcId
            Require ([bool]$reply.result.isError -eq $MustReject) "$Name unexpected response: $($reply.result.content[0].text)"
            if(!$MustReject){ return ($reply.result.content[0].text | ConvertFrom-Json).result }
        }
        # A project-relative spelling through an existing junction must not write
        # outside the selected project, even when its final file is a valid Scene.
        $external=[IO.Path]::GetFullPath($fixture+'-outside')
        $junction=Join-Path $fixture 'ExternalLink'
        $null=[IO.Directory]::CreateDirectory($external)
        try {
            $protected=Join-Path $external 'protected.scene'
            [IO.File]::WriteAllText($protected,$startup,[Text.UTF8Encoding]::new($false))
            $digest=(Get-FileHash -LiteralPath $protected).Hash
            $null=New-Item -ItemType Junction -Path $junction -Target $external
            Command 'scene_save' 199 @{path='ExternalLink/protected.scene'} $true
            Require ((Get-FileHash -LiteralPath $protected).Hash -eq $digest) 'Scene save through a junction changed the external file'
        }finally {
            if(Test-Path -LiteralPath $junction){[IO.Directory]::Delete($junction)}
            [IO.File]::Delete((Join-Path $external 'protected.scene'))
            [IO.Directory]::Delete($external)
        }
        $copyStatus=Command 'editor_get_status' 200
        Require ($copyStatus.dirty -and $copyStatus.scenePath -ceq $startupSceneName) 'Copy save changed the open document baseline'
        $authoredX=$transformed.transform.position[0]
        $selected=Command 'editor_select_entity' 201 @{entityId=$createdReply.result.entityId}
        Require ($selected.primaryEntityId -eq $createdReply.result.entityId) 'Selection did not reach the Editor'
        Command 'editor_select_entity' 202 @{entityId=999999} $true
        Command 'scene_set_transform' 203 @{entityId=$createdReply.result.entityId;position=@(9,4,5)}|Out-Null
        $undo=Command 'editor_undo' 204
        $undoEntity=Command 'scene_list_entities' 205 | Where-Object id -EQ $createdReply.result.entityId
        Require ($undo.canUndo -and $undo.canRedo -and $undo.primaryEntityId -eq $createdReply.result.entityId -and $undoEntity.transform.position[0] -eq $authoredX) 'Undo did not restore Transform and selection'
        $redo=Command 'editor_redo' 206
        $redoEntity=Command 'scene_list_entities' 207 | Where-Object id -EQ $createdReply.result.entityId
        Require (!$redo.canRedo -and $redoEntity.transform.position[0] -eq 9) 'Redo did not restore the edited Transform'
        Command 'editor_undo' 208|Out-Null
        $currentSave=Command 'scene_save' 209
        $currentSaved=Get-Content -LiteralPath (Join-Path $fixture $startupSceneName) -Encoding UTF8 -Raw | ConvertFrom-Json
        Require ($currentSave.savedCurrentScene -and $currentSave.path -ceq $startupSceneName -and
            ($currentSaved.entities | Where-Object id -EQ $createdReply.result.entityId).transform.position[0] -eq $authoredX) 'Pathless save did not persist the current document'
        Require (!(Command 'editor_get_status' 210).dirty) 'Current save did not clear the Editor dirty flag'
        $playing=Command 'editor_play' 211
        Require ($playing.playback -eq 'playing' -and !$playing.canEditScene) 'Play did not lock editing'
        Command 'scene_save' 212 @{} $true
        Command 'editor_undo' 213 @{} $true
        Command 'editor_select_entity' 214 @{entityId=1} $true
        Command 'scene_create_entity' 215 @{name='Refused while playing'} $true
        $paused=Command 'editor_pause' 216
        Require ($paused.playback -eq 'paused' -and !$paused.canEditScene) 'Pause did not retain the edit lock'
        Command 'editor_redo' 217 @{} $true
        $resumed=Command 'editor_play' 218
        Require ($resumed.playback -eq 'playing') 'Resume did not resume the same Scene'
        $stopped=Command 'editor_stop' 219
        $restored=Command 'scene_list_entities' 220 | Where-Object id -EQ $createdReply.result.entityId
        Require ($stopped.playback -eq 'editing' -and $stopped.canEditScene -and !$stopped.dirty -and
            $stopped.primaryEntityId -eq $createdReply.result.entityId -and $restored.transform.position[0] -eq $authoredX) 'Stop did not restore authored Scene, selection and saved baseline'
        $explicit=Command 'scene_save' 221 @{path=$startupSceneName}
        Require ($explicit.savedCurrentScene -and $explicit.path -ceq $startupSceneName) 'Explicit save of current path did not use the Editor baseline route'
        Write-Output 'PASS: live MCP selection, history Undo/Redo, current vs copy save, Play/Pause/resume/Stop, authored restoration and mutation locks'
    }
    $adapterInput.Dispose();$adapterInput=$null
    $adapter.StandardInput.Close()
    Require ($adapter.WaitForExit(5000) -and $adapter.ExitCode -eq 0) 'Adapter did not exit cleanly on stdin EOF'
    if ($WithCamera -or $WithModel -or $WithScript -or $WithAudio -or $WithUI) {
        Start-Sleep -Milliseconds 1000
        $player.Refresh()
        Require (!$player.HasExited) 'Player exited while loading/running the authored scene'
    }
    if($WithRender){Write-Output 'PASS: actual Release Editor/Player render-component startup, live MCP Light/Material add/partial/remove/re-add, coupled cone/color refusal and saved Scene persistence (not pixel/physical Undo QA)'}
    if($WithUI){Write-Output 'PASS: actual Release Editor/Player UI startup, live MCP Canvas/Sprite add/partial/GUID clear/remove/re-add, integer/range/typed reference refusal, source image+metadata preservation and saved Scene persistence (not pixel/physical Undo QA)'}
    Write-Output "PASS: real Editor/stdio MCP $ProtocolVersion, lifecycle/version/errors/frame recovery, live create/list/save and rejected bypass/notification mutations"
    Write-Output "PASS: actual Editor $expectedTools tool schemas, local Transform/keep-World parent/root/delete/list/save persistence, cycle refusal, malformed Transform isolation and missing-Entity refusal (not physical Undo/UI verification)"
    if ($statusTool) { Write-Output 'PASS: live read-only Editor status with initial selection and dirty state after an MCP edit' }
    Write-Output 'PASS: live Editor MCP save succeeds beside an existing legacy temporary and preserves its exact hash'
    if ($WithCamera) { Write-Output 'PASS: actual Release Editor/Player authored-camera startup and live Editor scene persistence (not visual rendering verification)' }
    Write-Output 'PASS: actual Release Editor MCP Camera/Collider/RigidBody add/partial patch/remove/re-add, schema and cross-field refusal, structured replies and saved Scene persistence (not physical UI/Undo or pixel QA)'
    if($WithModel -and $WithScript -and $WithAudio){Write-Output 'PASS: live MCP Model/Script/Audio GUID/path attachment, clear/remove/re-attach, scalar property replacement/refusal, source/metadata byte preservation and authored Scene persistence (not pixel/audio/physical Undo QA)'}
    if ($WithModel) { Write-Output 'PASS: actual Release Editor/Player GUID model startup with absent old path and live Editor identity persistence (not visual rendering verification)' }
    if ($WithScript) { Write-Output 'PASS: actual Release Editor/Player GUID script startup, absent old path, authored persistence and Editing-mode isolation (not direct Player hook observation)' }
    if ($WithAudio) { Write-Output 'PASS: actual Release Editor/Player GUID Audio startup with absent old path and live Editor Audio component persistence (not voice/pixel verification)' }
    if ($UnicodePaths) { Write-Output 'PASS: Unicode project root and native asset names in actual Editor/Player startup and MCP persistence (not visual rendering verification)' }
    $fixturePassed=$true
} finally {
    if($adapterInput){try{$adapterInput.Dispose()}catch{};$adapterInput=$null}
    if ($adapter) { if (!$adapter.HasExited) { $adapter.Kill(); $adapter.WaitForExit(5000) | Out-Null }; $adapter.Dispose() }
    if ($editor) {
        $editor.Refresh()
        if (!$editor.HasExited) { Stop-Process -Id $editor.Id }
        if (!$editor.WaitForExit(5000)) { throw 'Test Editor did not terminate; temporary files retained' }
        $editor.Dispose()
    }
    if ($player) {
        $player.Refresh()
        if (!$player.HasExited) { Stop-Process -Id $player.Id }
        if (!$player.WaitForExit(5000)) { throw 'Test Player did not terminate; temporary files retained' }
        $player.Dispose()
    }
    # The test owns this exact generated directory; no user project is touched.
    $resolved = [IO.Path]::GetFullPath($fixture)
    $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    if (!$resolved.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase) -or !(Split-Path -Leaf $resolved).StartsWith('kubo-mcp-e2e-')) { throw 'Unsafe test cleanup path' }
    if ($fixturePassed) { if (Test-Path -LiteralPath $resolved) { Remove-Item -LiteralPath $resolved -Recurse -Force } }
    else { Write-Output "MCP_E2E_FAILED_FIXTURE_RETAINED=$resolved" }
}
