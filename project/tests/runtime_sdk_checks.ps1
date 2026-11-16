[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$SdkDirectory,[switch]$NativeBuildExport,[switch]$PhysicsMaterials,[switch]$PhysicsHierarchy,[switch]$GlbModels,[string]$NativeJobRunner,[string]$NativeCreationLauncher,
    [string]$MSBuild='C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe')
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'sdk_link_inputs.ps1')
if($PhysicsHierarchy) {$PhysicsMaterials=$true}
function Require($condition,$why) {if(!$condition) {throw $why}}
$consumerSdk=[IO.Path]::GetFullPath($SdkDirectory).TrimEnd('\')
$consumerRepo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).TrimEnd('\')+'\'
$consumerLabel=-join ([char[]]@(0x958b,0x767a,0x65e5,0x672c,0x8a9e))
$consumerFixture=Join-Path ([IO.Path]::GetTempPath()) ('kubo-runtime-sdk-'+[guid]::NewGuid().ToString('N')+'-'+$consumerLabel)
$consumerProcess=$null
$consumerCompleted=$false
try {
    $consumerManifest=Get-Content -LiteralPath (Join-Path $consumerSdk 'sdk-package.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    Require ($consumerManifest.format -eq 'kubo-runtime-sdk-1') 'SDK manifest missing/wrong format'
    foreach($entry in $consumerManifest.files) {Require ((Get-FileHash -LiteralPath (Join-Path $consumerSdk $entry.path) -Algorithm SHA256).Hash.ToLowerInvariant() -eq $entry.sha256) ('SDK digest mismatch: '+$entry.path)}
    Require (@(Get-ChildItem -LiteralPath (Join-Path $consumerSdk 'include') -Filter '*.cpp' -Recurse).Count -eq 0) 'SDK contains engine implementation sources'
    foreach($file in @('KuboEditor.exe','KuboMcpServer.exe','KuboLauncher.exe')) {Require (@(Get-ChildItem -LiteralPath $consumerSdk -Filter $file -Recurse).Count -eq 0) 'SDK ships an Editor executable'}
    $consumerProject=Join-Path $consumerFixture 'Native Game'
    $consumerAssets=Join-Path $consumerFixture 'Project\Assets'
    $consumerWorking=Join-Path $consumerFixture 'Unrelated cwd'
    if(![string]::IsNullOrWhiteSpace($NativeCreationLauncher)) {
        New-Item -ItemType Directory -Path $consumerWorking -Force | Out-Null
        $creationManifest=Join-Path $consumerFixture 'Project\Game.kubo'
        $creationProcess=Start-Process -FilePath $NativeCreationLauncher -ArgumentList @('--create-native',('"'+$creationManifest+'"'),'--sdk',('"'+$consumerSdk+'"'),'--no-launch') -WorkingDirectory $consumerWorking -WindowStyle Hidden -PassThru
        try {
            $creationHandle=$creationProcess.Handle
            Require ($creationHandle -ne [IntPtr]::Zero -and $creationProcess.WaitForExit(30000)) 'Hub native creation timed out'
            $creationProcess.WaitForExit();Require ($creationProcess.ExitCode -eq 0) 'Hub native creation failed'
        } finally {if(!$creationProcess.HasExited) {Stop-Process -Id $creationProcess.Id;$creationProcess.WaitForExit()};$creationProcess.Dispose()}
        $consumerProject=Join-Path $consumerFixture 'Project\Native'
        $creationSettings=Get-Content -LiteralPath (Join-Path $consumerFixture 'Project\UserSettings\native-build.json') -Raw -Encoding UTF8 | ConvertFrom-Json
        Require ($creationSettings.native -and $creationSettings.project -eq 'Native/Starter.vcxproj' -and $creationSettings.sdk -eq $consumerSdk) 'Hub native build settings missing'
        Write-Output 'PASS: actual Launcher native creation, templates/local build settings without compilation or Editor launch'
    }
    New-Item -ItemType Directory -Path $consumerProject,$consumerAssets,$consumerWorking -Force | Out-Null
    if([string]::IsNullOrWhiteSpace($NativeCreationLauncher)) {foreach($file in @('Starter.cpp','Starter.vcxproj')) {Copy-Item -LiteralPath (Join-Path $consumerSdk "examples\Starter\$file") -Destination (Join-Path $consumerProject $file)}}
    $gameManifest=Join-Path $consumerFixture 'Project\Game.kubo'
    [IO.File]::WriteAllText($gameManifest,'{"name":"SDK","engineVersion":"0.1.0","assetDirectory":"Assets","startupScene":"entry.scene","gameOutput":{"width":512,"height":768},"luaLimits":{"memoryBytes":4194304,"states":7,"frameInstructions":3000}}')
    $sdkInputDirectory=Join-Path $consumerFixture 'Project\UserSettings'
    New-Item -ItemType Directory -Path $sdkInputDirectory -Force | Out-Null
    $sdkInputBindings='{"version":2,"actions":{"jump":[{"source":"pad_button","code":10,"contribution":[1,0,0]}],"pad_move":[{"source":"pad_axis","code":0,"threshold":0.5,"contribution":[2,0,0]}]}}'
    [IO.File]::WriteAllText((Join-Path $sdkInputDirectory 'input_bindings.json'),$sdkInputBindings)
    $modelMaterial=$consumerLabel+'.mtl';$modelTexture=$consumerLabel+'.png'
    [IO.File]::WriteAllText((Join-Path $consumerAssets 'triangle.obj'),("mtllib "+$modelMaterial+"`nv 0 0 0`nv 1 0 0`nv 0 1 0`nvt 0 0`nvt 1 0`nvt 0 1`nvn 0 0 1`nusemtl SDKMaterial`nf 1/1/1 2/2/1 3/3/1`n"))
    [IO.File]::WriteAllText((Join-Path $consumerAssets $modelMaterial),("newmtl SDKMaterial`nKd 1 1 1`nmap_Kd "+$modelTexture+"`n"))
    Copy-Item -LiteralPath (Join-Path $consumerSdk 'runtime\Resources\textures\common\white.png') -Destination (Join-Path $consumerAssets $modelTexture)
    $waveName=$consumerLabel+'.wav'
    [IO.File]::WriteAllText((Join-Path $consumerAssets 'triangle.obj.kubo-meta.json'),'{"version":1,"guid":"0123456789abcdef0123456789abcdef"}')
    [IO.File]::WriteAllBytes((Join-Path $consumerAssets $waveName),[byte[]]@(82,73,70,70,40,0,0,0,87,65,86,69,102,109,116,32,16,0,0,0,1,0,1,0,64,31,0,0,128,62,0,0,2,0,16,0,100,97,116,97,4,0,0,0,0,0,0,0))
    [IO.File]::WriteAllText((Join-Path $consumerAssets 'move.lua'),"function on_create(e) assert(audio.available() and scene.exists(e:id()));audio.play('Assets/$waveName',true,0) end`nfunction on_update(e,dt) assert(scene.exists(e:id()) and scene.entity_count()>=1);assert(input.action_down('jump')==input.pad_down(input.pad_buttons.A));local x,y,z=input.action_axis('pad_move');local sx=input.pad_stick();assert(x==2*sx and y==0 and z==0); e:translate(dt,0,0);local xx=scene.world_position(e:id());local own=e:position();assert(xx==own and scene.world_matrix(e:id())[13]==own) end`nfunction on_fixed_update(e,dt) assert(scene.exists(e:id()));e:translate(0,dt,0) end`n")
    [IO.File]::AppendAllText((Join-Path $consumerAssets 'move.lua'),"local original_update=on_update`nfunction on_update(e,dt) original_update(e,dt);local camera=scene.find('Camera');if camera then assert(scene.set_rotation(camera,0,0.0625,0));local x,y,z=scene.rotation(camera);assert(y==0.0625) end end`n")
    [IO.File]::AppendAllText((Join-Path $consumerAssets 'move.lua'),"local scalar_update=on_update`nfunction on_update(e,dt) scalar_update(e,dt);local camera=scene.find('Camera');if camera then local born=scene.find('SDK runtime spawned');if not born then born=scene.spawn('SDK runtime spawned',camera);assert(born and scene.set_position(born,4,0,0)) end;assert(scene.parent(born)==camera and scene.position(born)==4);local temp=scene.spawn('SDK runtime temporary');local child=scene.spawn('SDK runtime temporary child',temp);assert(scene.destroy_subtree(temp) and not scene.exists(temp) and not scene.exists(child)) end end`n")
    $scene='{"version":1,"entities":[{"id":1,"parent":0,"name":"Camera","active":true,"model":"","transform":{"position":[0,0,-5],"rotation":[0,0,0],"scale":[1,1,1]},"camera":{"enabled":true,"primary":true,"fovY":0.8,"nearClip":0.1,"farClip":100}},{"id":2,"parent":0,"name":"Native SDK Lua/Audio/model","active":true,"model":"Assets/triangle.obj","transform":{"position":[0,0,0],"rotation":[0,0,0],"scale":[1,1,1]},"script":{"asset":"Assets/move.lua","enabled":true},"audio":{"asset":"Assets/WAVE","enabled":true,"loop":true,"playOnAwake":true,"volume":0}}]}'
    if($GlbModels) {
        $glbJson=@{asset=@{version='2.0'};buffers=@(@{byteLength=96});bufferViews=@(@{buffer=0;byteOffset=0;byteLength=36},@{buffer=0;byteOffset=36;byteLength=36},@{buffer=0;byteOffset=72;byteLength=24});accessors=@(@{bufferView=0;componentType=5126;count=3;type='VEC3';min=@(0,0,0);max=@(1,1,0)},@{bufferView=1;componentType=5126;count=3;type='VEC3'},@{bufferView=2;componentType=5126;count=3;type='VEC2'});meshes=@(@{primitives=@(@{attributes=@{POSITION=0;NORMAL=1;TEXCOORD_0=2};material=0})});nodes=@(@{mesh=0});scenes=@(@{nodes=@(0)});scene=0;images=@(@{uri=$modelTexture});textures=@(@{source=0});materials=@(@{pbrMetallicRoughness=@{baseColorTexture=@{index=0};metallicFactor=0;roughnessFactor=1}})} | ConvertTo-Json -Depth 15 -Compress
        $glbJsonBytes=[Text.UTF8Encoding]::new($false).GetBytes($glbJson)
        $glbJsonPadding=(4-$glbJsonBytes.Length%4)%4
        $glbStream=[IO.File]::Open((Join-Path $consumerAssets 'triangle.glb'),[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
        $glbWriter=[IO.BinaryWriter]::new($glbStream)
        try {
            $glbWriter.Write([uint32]0x46546c67);$glbWriter.Write([uint32]2);$glbWriter.Write([uint32](28+$glbJsonBytes.Length+$glbJsonPadding+96))
            $glbWriter.Write([uint32]($glbJsonBytes.Length+$glbJsonPadding));$glbWriter.Write([uint32]0x4e4f534a);$glbWriter.Write($glbJsonBytes)
            for($padding=0;$padding -lt $glbJsonPadding;$padding++) {$glbWriter.Write([byte]32)}
            $glbWriter.Write([uint32]96);$glbWriter.Write([uint32]0x004e4942)
            foreach($value in [single[]]@(0,0,0,1,0,0,0,1,0,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1)) {$glbWriter.Write([single]$value)}
        } finally {$glbWriter.Dispose()}
        $scene=$scene.Replace('Assets/triangle.obj','Assets/triangle.glb')
    }
    if($PhysicsMaterials) {
        $materialScene=',{"id":3,"parent":0,"name":"Material floor","active":true,"model":"","transform":{"position":[1000,0,0],"rotation":[0,0,0],"scale":[1,1,1]},"collider":{"enabled":true,"halfExtents":[5,0.5,5],"friction":1,"restitution":1}},{"id":4,"parent":0,"name":"Material bounce","active":true,"model":"","transform":{"position":[1000,0.8,0],"rotation":[0,0,0],"scale":[1,1,1]},"collider":{"enabled":true,"halfExtents":[0.5,0.5,0.5],"friction":1,"restitution":1},"rigidBody":{"enabled":true,"velocity":[1,-2,0],"mass":1,"dynamic":true,"gravity":false}}]}'
        $scene=$scene.Substring(0,$scene.Length-2)+$materialScene
    }
    if($PhysicsHierarchy) {
        $hierarchyScene=$scene | ConvertFrom-Json
        $hierarchyScene.entities[3].parent=5
        $hierarchyScene.entities[3].transform.position=@(-0.4,0,0)
        $hierarchyScene.entities+=@{id=5;parent=0;name='Physics group';active=$true;model='';transform=@{position=@(1000,0,0);rotation=@(0,0,1.57079632679);scale=@(-2,3,1)}}
        $scene=$hierarchyScene | ConvertTo-Json -Depth 12 -Compress
    }
    [IO.File]::AppendAllText((Join-Path $consumerAssets 'move.lua'),"local structural_update=on_update;local cloned`nfunction on_update(e,dt) structural_update(e,dt);if e:id()==2 and not cloned then cloned=scene.clone(e:id());assert(cloned and scene.set_position(cloned,10,0,0)) end end`n")
    [IO.File]::AppendAllText((Join-Path $consumerAssets 'move.lua'),"local clone_update=on_update`nfunction on_update(e,dt) clone_update(e,dt);if not scene.find('SDK runtime Model') then local id=scene.spawn_model('SDK runtime Model','Assets/triangle.obj');assert(id and scene.set_position(id,-2,0,0) and scene.set_scale(id,0.5,0.5,0.5)) end end`n")
    [IO.File]::WriteAllText((Join-Path $consumerFixture 'Project\entry.scene'),$scene.Replace('Assets/WAVE','Assets/'+$waveName),[Text.UTF8Encoding]::new($true))
    $sdkProperty='/p:KuboSdkRoot='+$consumerSdk.Replace('\','/')+'/'
    foreach($configuration in @('Debug','Release')) {
        $projectPath=Join-Path $consumerProject 'Starter.vcxproj'
        & $MSBuild $projectPath /m:1 "/p:Configuration=$configuration" /p:Platform=x64 $sdkProperty /v:minimal
        Require ($LASTEXITCODE -eq 0) 'External SDK consumer build failed'
        $evaluation=& $MSBuild $projectPath "/p:Configuration=$configuration" /p:Platform=x64 $sdkProperty /getProperty:IntDir /getItem:ClCompile,ProjectReference | Out-String
        Require ($LASTEXITCODE -eq 0) 'SDK consumer evaluation failed'
        $consumerEvaluated=$evaluation | ConvertFrom-Json
        Require ($consumerEvaluated.Items.ClCompile.Count -eq 1 -and $consumerEvaluated.Items.ClCompile[0].Identity -eq 'Starter.cpp' -and @($consumerEvaluated.Items.ProjectReference).Count -eq 0) 'Consumer builds engine source/projects'
        $readLog=Join-Path $consumerEvaluated.Properties.IntDir 'Starter.tlog\CL.read.1.tlog'
        foreach($line in Get-Content -LiteralPath $readLog) {Require (!$line.TrimStart('^').StartsWith($consumerRepo,[StringComparison]::OrdinalIgnoreCase)) 'External consumer read original repository source/header'}
        $linkLog=Get-Content -LiteralPath (Join-Path $consumerEvaluated.Properties.IntDir 'Starter.tlog\link.command.1.tlog') -Raw
        Require (Test-SdkRuntimeLinkInputs $linkLog) 'SDK consumer link is missing runtime or retains Editor code'
        $consumerExe=Join-Path $consumerProject "bin\$configuration\Starter.exe"
        $consumerArguments=@('--project',('"'+$gameManifest+'"'),'--sdk-verify')
        if($PhysicsMaterials) {$consumerArguments+='--sdk-verify-materials'}
        if($PhysicsHierarchy) {$consumerArguments+='--sdk-verify-hierarchy'}
        $consumerProcess=Start-Process -FilePath $consumerExe -ArgumentList $consumerArguments -WorkingDirectory $consumerWorking -WindowStyle Hidden -PassThru
        $consumerHandle=$consumerProcess.Handle
        Require ($consumerHandle -ne [IntPtr]::Zero -and $consumerProcess.WaitForExit(30000)) 'External SDK consumer did not finish rendered verification'
        $consumerProcess.WaitForExit();Require ($consumerProcess.ExitCode -eq 0) 'External SDK consumer failed native rendered Scene/Lua/Audio/cleanup verification'
        $consumerProcess.Dispose();$consumerProcess=$null
        Write-Output "PASS: external $configuration SDK-only C++ build/link, no original source/header reads, Unicode/BOM authored Scene, eight render frames, Lua/component Audio and native close/cleanup"
        if($PhysicsMaterials) {
            $materialPath=Join-Path $consumerFixture 'Project\entry.scene'
            $materialBytes=[IO.File]::ReadAllBytes($materialPath)
            $materialText=[IO.File]::ReadAllText($materialPath)
            try {
                foreach($coefficient in @('friction','restitution')) {
                    $changedMaterial=$materialText.Replace(('"'+$coefficient+'":1'),('"'+$coefficient+'":0'))
                    Require ($changedMaterial -ne $materialText) 'Material negative fixture did not change'
                    [IO.File]::WriteAllText($materialPath,$changedMaterial,[Text.UTF8Encoding]::new($true))
                    $consumerProcess=Start-Process -FilePath $consumerExe -ArgumentList $consumerArguments -WorkingDirectory $consumerWorking -WindowStyle Hidden -PassThru
                    $consumerHandle=$consumerProcess.Handle
                    Require ($consumerHandle -ne [IntPtr]::Zero -and $consumerProcess.WaitForExit(30000)) 'Material negative consumer did not finish'
                    $consumerProcess.WaitForExit();Write-Output "SDK_MATERIAL_NEGATIVE configuration=$configuration coefficient=$coefficient exit=$($consumerProcess.ExitCode)";Require ($consumerProcess.ExitCode -eq 1) 'SDK material verification was bypassed or ignored'
                    $consumerProcess.Dispose();$consumerProcess=$null
                }
            } finally {[IO.File]::WriteAllBytes($materialPath,$materialBytes)}
            Write-Output "PASS: $configuration actual SDK material velocity/restoration assertions and both negative fixtures prove verification is active"
        }
        if($PhysicsHierarchy) {
            $hierarchyPath=Join-Path $consumerFixture 'Project\entry.scene'
            $hierarchyBytes=[IO.File]::ReadAllBytes($hierarchyPath)
            try {
                $negativeHierarchy=[IO.File]::ReadAllText($hierarchyPath) | ConvertFrom-Json
                $negativeHierarchy.entities[3].transform.position=@(0,0.8,0)
                $negativeHierarchy.entities[4].transform.rotation=@(0,0,0)
                $negativeHierarchy.entities[4].transform.scale=@(1,1,1)
                [IO.File]::WriteAllText($hierarchyPath,($negativeHierarchy | ConvertTo-Json -Depth 12 -Compress),[Text.UTF8Encoding]::new($true))
                $consumerProcess=Start-Process -FilePath $consumerExe -ArgumentList $consumerArguments -WorkingDirectory $consumerWorking -WindowStyle Hidden -PassThru
                $consumerHandle=$consumerProcess.Handle
                Require ($consumerHandle -ne [IntPtr]::Zero -and $consumerProcess.WaitForExit(30000)) 'Hierarchy negative consumer did not finish'
                $consumerProcess.WaitForExit();Require ($consumerProcess.ExitCode -eq 1) 'SDK hierarchy verification was bypassed or ignored'
                $consumerProcess.Dispose();$consumerProcess=$null
            } finally {[IO.File]::WriteAllBytes($hierarchyPath,$hierarchyBytes)}
            Write-Output "PASS: $configuration SDK parent-composed mirrored/scaled physics, inverse local publication and hierarchy-specific negative fixture"
        }
        if($NativeBuildExport) {
            $nativeBuilder=Join-Path $consumerSdk 'tools\build_native_game.ps1'
            $nativeExport=Join-Path $consumerFixture ('NativeExport-'+$configuration)
            if([string]::IsNullOrWhiteSpace($NativeJobRunner)) {
                & $nativeBuilder -ProjectFile $gameManifest -NativeProject $projectPath -SdkDirectory $consumerSdk -OutputDirectory $nativeExport -Configuration $configuration
            } else {
                & $NativeJobRunner $consumerSdk $projectPath $gameManifest $nativeExport $configuration
                Require ($LASTEXITCODE -eq 0) 'Production asynchronous native export service failed'
            }
            $nativeRecord=Get-Content -LiteralPath (Join-Path $nativeExport 'native-build.json') -Raw | ConvertFrom-Json
            Require ($nativeRecord.format -eq 'kubo-native-game-build-1' -and $nativeRecord.runtimeSha256 -eq (Get-FileHash -LiteralPath (Join-Path $nativeExport 'KuboPlayer.exe')).Hash.ToLowerInvariant()) 'Native export build record/hash missing'
            Require ([IO.File]::ReadAllText((Join-Path $nativeExport 'UserSettings\input_bindings.json')) -ceq $sdkInputBindings) 'Native export lost mixed pad gameplay input bindings'
            foreach($excluded in @('Starter.cpp','Starter.vcxproj','KuboRuntime.lib','KuboEditor.exe','KuboMcpServer.exe')) {Require (@(Get-ChildItem -LiteralPath $nativeExport -Filter $excluded -Recurse).Count -eq 0) 'Native export leaks source/SDK/Editor files'}
            $renamedNative=Join-Path $nativeExport 'RenamedNativeGame.exe';Copy-Item -LiteralPath (Join-Path $nativeExport 'KuboPlayer.exe') -Destination $renamedNative
            $nativeArguments=@('--sdk-verify');if($PhysicsMaterials) {$nativeArguments+='--sdk-verify-materials'}
            if($PhysicsHierarchy) {$nativeArguments+='--sdk-verify-hierarchy'}
            $consumerProcess=Start-Process -FilePath $renamedNative -ArgumentList $nativeArguments -WorkingDirectory $consumerWorking -WindowStyle Hidden -PassThru
            $consumerHandle=$consumerProcess.Handle
            Require ($consumerHandle -ne [IntPtr]::Zero -and $consumerProcess.WaitForExit(30000)) 'Packaged native game did not finish executable-relative project verification'
            $consumerProcess.WaitForExit();Require ($consumerProcess.ExitCode -eq 0) 'Packaged native game failed rendered Lua/Audio/cleanup verification without --project'
            $consumerProcess.Dispose();$consumerProcess=$null
            $exportDigest=(Get-FileHash -LiteralPath (Join-Path $nativeExport 'KuboPlayer.exe')).Hash
            $nativeRejected=$false
            try {& $nativeBuilder -ProjectFile $gameManifest -NativeProject $projectPath -SdkDirectory $consumerSdk -OutputDirectory $nativeExport -Configuration $configuration} catch {$nativeRejected=$true}
            Require ($nativeRejected -and $exportDigest -eq (Get-FileHash -LiteralPath (Join-Path $nativeExport 'KuboPlayer.exe')).Hash) 'Native export overwrote nonempty output'
            Write-Output "PASS: bundled $configuration native build/export, source-free renamed package startup without --project, render/Lua/Audio/cleanup and nonempty-output preservation"
            if($PhysicsMaterials) {Write-Output "PASS: $configuration renamed native export executes authored friction/bounce verification"}
        }
    }
    if($NativeBuildExport) {
        # The real bundled validator must reject a missing transitive Texture,
        # not just the synthetic dependency-parser fixture.
        $textureSource=Join-Path $consumerAssets $modelTexture
        $textureRetained=Join-Path $consumerFixture 'temporarily-retained-texture.png'
        $missingTextureExport=Join-Path $consumerFixture 'MissingTextureExport'
        New-Item -ItemType Directory -Path $missingTextureExport | Out-Null
        [IO.File]::Move($textureSource,$textureRetained)
        try {
            $dependencyRejected=$false
            try {& (Join-Path $consumerSdk 'tools\package_project.ps1') -ProjectFile $gameManifest -RuntimeExecutable (Join-Path $consumerFixture 'NativeExport-Release\KuboPlayer.exe') -OutputDirectory $missingTextureExport} catch {$dependencyRejected=$true}
            Require ($dependencyRejected -and @(Get-ChildItem -LiteralPath $missingTextureExport -Force).Count -eq 0) 'Missing SDK Model Texture was published'
        } finally {[IO.File]::Move($textureRetained,$textureSource)}
        Write-Output ('PASS: real SDK textured '+$(if($GlbModels){'GLB'}else{'OBJ/MTL'})+' export and missing transitive Texture refusal before publication')
        $nativeSource=Join-Path $consumerProject 'Starter.cpp';$originalNative=[IO.File]::ReadAllBytes($nativeSource)
        [IO.File]::WriteAllText($nativeSource,'#error Intentional SDK consumer compilation failure')
        $failureExport=Join-Path $consumerFixture 'FailedNativeExport';$compileRejected=$false
        try {& (Join-Path $consumerSdk 'tools\build_native_game.ps1') -ProjectFile $gameManifest -NativeProject (Join-Path $consumerProject 'Starter.vcxproj') -SdkDirectory $consumerSdk -OutputDirectory $failureExport -Configuration Release} catch {$compileRejected=$_.Exception.Message.Contains('compilation failed')}
        [IO.File]::WriteAllBytes($nativeSource,$originalNative)
        Require ($compileRejected -and !(Test-Path -LiteralPath $failureExport)) 'Failed native compilation created an export'
        Write-Output 'PASS: native compiler failure is reported before export creation'
    }
    $wrongVersion=& $MSBuild (Join-Path $consumerProject 'Starter.vcxproj') /t:ValidateKuboRuntimeSdk /p:Configuration=Release /p:Platform=x64 $sdkProperty /p:KuboSdkVCToolsVersion=0.0 /v:minimal 2>&1 | Out-String
    Require ($LASTEXITCODE -ne 0 -and $wrongVersion.Contains('recorded MSVC')) 'SDK accepted an incompatible compiler-version contract'
    Write-Output 'PASS: SDK rejects mismatched MSVC version contract before consumer compilation'
    foreach($entry in $consumerManifest.files) {Require ((Get-FileHash -LiteralPath (Join-Path $consumerSdk $entry.path) -Algorithm SHA256).Hash.ToLowerInvariant() -eq $entry.sha256) ('Consumer modified SDK content: '+$entry.path)}
    Write-Output ('PASS: all '+$consumerManifest.files.Count+' SDK SHA-256 digests remain unchanged after external builds/runs')
    $consumerCompleted=$true
} finally {
    if($consumerProcess) {if(!$consumerProcess.HasExited) {Stop-Process -Id $consumerProcess.Id};Require ($consumerProcess.WaitForExit(5000)) 'SDK consumer remains alive; fixture retained';$consumerProcess.Dispose()}
    $consumerResolved=[IO.Path]::GetFullPath($consumerFixture)
    $consumerTempRoot=[IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')+'\'
    if(!$consumerResolved.StartsWith($consumerTempRoot,[StringComparison]::OrdinalIgnoreCase) -or !(Split-Path -Leaf $consumerResolved).StartsWith('kubo-runtime-sdk-')) {throw 'Unsafe SDK fixture cleanup'}
    if(Test-Path -LiteralPath $consumerResolved) {
        if($consumerCompleted){Remove-Item -LiteralPath $consumerResolved -Recurse -Force}
        else {Write-Output "Retained failed SDK fixture: $consumerResolved"}
    }
}
