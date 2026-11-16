[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$ProjectFile,
    [switch]$Walkthrough
)
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$manifest=(Resolve-Path -LiteralPath $ProjectFile).Path
$root=Split-Path $manifest -Parent
$settings=Get-Content -LiteralPath $manifest -Encoding UTF8 -Raw | ConvertFrom-Json
if($settings.assetDirectory -cne 'Assets' -or $settings.startupScene -cne 'Assets/Scenes/Main.scene'){
    throw 'Use a fresh generic 3D project created by KuboLauncher'
}
$main=Join-Path $root $settings.startupScene
$template=Get-Content -LiteralPath $main -Encoding UTF8 -Raw | ConvertFrom-Json
if(@($template.entities).Count){throw 'Use the empty Scene from the Hub template'}
$encoding=[Text.UTF8Encoding]::new($false)
$ids=@{
    Model='0940cd76107249c9bdbe0c6539acd551'
    Floor='eed675aaab39472a9f57c011f891d907'
    Exit='9049d28e261b45f486d70919ece6539e'
    Image='9db8d04c720b45bc89aeb86b64f5758a'
    Gate='095d9fab15514e7ab7a8f051a26b6030'
    Main='2f0c7a2becd94431a5533ded81733000'
    Stage='c4035aa4ec3c486e8dd530224309e2dc'
    Result='afd0409cd68845a9adb17f8f6cd84588'
}
function WriteText([string]$Relative,[string]$Text){
    $file=Join-Path $root $Relative
    $null=[IO.Directory]::CreateDirectory((Split-Path $file -Parent))
    [IO.File]::WriteAllText($file,$Text,$encoding)
}
function WriteJson([string]$Relative,$Value){WriteText $Relative (ConvertTo-Json -InputObject $Value -Depth 60 -Compress)}
function Identity([string]$Relative,[string]$Guid){WriteJson ($Relative+'.kubo-meta.json') @{version=1;guid=$Guid}}
function CopyAsset([string]$Source,[string]$Relative){
    $destination=Join-Path $root $Relative
    $null=[IO.Directory]::CreateDirectory((Split-Path $destination -Parent))
    $sourcePath=(Resolve-Path -LiteralPath (Join-Path $repo $Source)).Path
    if([IO.File]::Exists($destination)){
        if((Get-FileHash -LiteralPath $sourcePath).Hash -ne (Get-FileHash -LiteralPath $destination).Hash){
            throw "Existing asset differs from the trial source: $Relative"
        }
    }else{[IO.File]::Copy($sourcePath,$destination)}
}
function Entity([int]$Id,[string]$Name,[int]$Parent,[array]$Position,[hashtable]$Fields){
    $value=[ordered]@{id=$Id;name=$Name;parent=$Parent;active=$true;model='';
        transform=@{position=$Position;rotation=@(0,0,0);scale=@(1,1,1)}}
    foreach($key in $Fields.Keys){$value[$key]=$Fields[$key]}
    return $value
}
function Label([string]$Text,[int]$Size,[array]$Box,[array]$Tint){
    return @{text=$Text;textSize=$Size;size=$Box;anchor=@(.5,.5);pivot=@(.5,.5);textColor=$Tint}
}
function Script([string]$Name,[hashtable]$Overrides=@{}){
    return @{asset="Assets/Scripts/$Name.lua";enabled=$true;properties=$Overrides}
}
$white=@(1,1,1,1)
$mint=@(.35,1,.78,1)
$canvas=@{canvas=@{scaleMode='scaleWithOutput';referenceSize=@(1280,720)}}
$block=@{model='Assets/Models/cube.obj';modelGuid=$ids.Model;material=@{guid=$ids.Floor}}
$beacon=@{model='Assets/Models/cube.obj';modelGuid=$ids.Model;material=@{guid=$ids.Exit}}
CopyAsset 'Resources/models/cube/cube.obj' 'Assets/Models/cube.obj'
CopyAsset 'Resources/models/cube/cube.mtl' 'Assets/Models/cube.mtl'
CopyAsset 'Resources/textures/common/uvChecker.png' 'Assets/Models/uvChecker.png'
CopyAsset 'Resources/textures/common/circle.png' 'Assets/Effects/光.png'
WriteJson 'Assets/Materials/通路.material' @{version=1;material=@{
    lighting=$true;color=@(.32,.45,.67,1);roughness=.7;reflectionStrength=.25;textureInfluence=0
}}
WriteJson 'Assets/Materials/出口.material' @{version=1;material=@{lighting=$false;color=@(.2,.95,.72,1);textureInfluence=0}}
Identity 'Assets/Models/cube.obj' $ids.Model
Identity 'Assets/Effects/光.png' $ids.Image
Identity 'Assets/Materials/通路.material' $ids.Floor
Identity 'Assets/Materials/出口.material' $ids.Exit

WriteText 'Assets/Scripts/Title.lua' @"
properties={walkthrough=false}
local elapsed=0
function on_update(e,dt)
    elapsed=elapsed+dt
    if input.action_pressed("start") or (e:property("walkthrough") and elapsed>0.6) then
        assert(scene.change_scene("Scenes/Stage.scene"))
    end
end
"@
WriteText 'Assets/Scripts/Runner.lua' @"
properties={walkthrough=false}
function on_create(e)
    assert(scene.spawn_prefab("Assets/Prefabs/出口標識.prefab"))
end
function on_fixed_update(e,dt)
    local x,_,z=input.action_axis("move")
    if e:property("walkthrough") then x=0;z=1 end
    local length=math.sqrt(x*x+z*z)
    if length>1 then x=x/length;z=z/length end
    e:set_velocity(x*5,0,z*5)
end
"@

WriteText 'Assets/Scripts/Goal.lua' @"
local reached=false
function on_trigger_enter(e,other)
    if reached or other~=scene.find("Runner") then return end
    reached=true
    assert(scene.find("Exit Beacon"))
    assert(scene.set_active(assert(scene.find("Goal Sparks")),true))
    local label=assert(scene.find("Stage HUD"))
    local sprite=assert(scene.sprite(label))
    sprite.text="到達！ 結果画面へ"
    assert(scene.set_sprite(label,sprite))
    assert(scene.change_scene("Scenes/Result.scene",1.25))
end
"@
WriteText 'Assets/Scripts/Result.lua' @"
properties={report=false}
function on_update(e,dt)
    if input.action_pressed("start") then assert(scene.change_scene("Scenes/Main.scene")) end
    if e:property("report") then error("KUBO_CORRIDOR_RESULTS_REACHED",0) end
end
"@

$gate=Entity 1 'Exit Beacon' 0 @(0,1.65,8.65) $beacon
$gate.transform.scale=@(.4,.4,.4)
$gateL=Entity 2 'Beacon Left' 1 @(-4,-1,0) $beacon
$gateR=Entity 3 'Beacon Right' 1 @(4,-1,0) $beacon
$gateL.transform.scale=@(.4,3.5,.4)
$gateR.transform.scale=@(.4,3.5,.4)
WriteJson 'Assets/Prefabs/出口標識.prefab' @{format='kubo-prefab-1';root=1;world=@{version=1;entities=@($gate,$gateL,$gateR)}}
Identity 'Assets/Prefabs/出口標識.prefab' $ids.Gate

$start=Label 'はじめる' 48 @(330,78) $mint
$start.buttonSceneGuid=$ids.Stage
$restart=Label 'タイトルへ' 40 @(330,68) $mint
$restart.buttonSceneGuid=$ids.Main
$background=@(.017,.026,.055)
$title=@(
    (Entity 1 'Title Canvas' 0 @(0,0,0) $canvas)
    (Entity 2 'Title Heading' 1 @(0,-140,0) @{sprite=(Label '光の回廊' 100 @(800,120) $white)})
    (Entity 3 'Title Subtitle' 1 @(0,-15,0) @{sprite=(Label 'WASDで移動して、光る出口を目指そう' 29 @(780,64) $white)})
    (Entity 4 'Start Button' 1 @(0,102,0) @{sprite=$start})
    (Entity 5 'Title Controller' 0 @(0,0,0) @{script=(Script 'Title' @{walkthrough=[bool]$Walkthrough})})
)
$camera=Entity 1 'Stage Camera' 0 @(0,6.5,-11) @{camera=@{
    enabled=$true;primary=$true;projection='perspective';fovY=.75;nearClip=.1;farClip=80}}
$camera.transform.rotation=@(.32,0,0)
$light=Entity 2 'Corridor Light' 0 @(0,4,0) @{light=@{type='directional';enabled=$true;color=@(.7,.82,1);intensity=1.2}}
$light.transform.rotation=@(1.05,-.32,0)
$floor=Entity 3 'Floor' 0 @(0,-.25,4.5) $block
$floor.transform.scale=@(3,.25,6)
$wallFields=$block.Clone()
$wallFields.collider=@{enabled=$true;halfExtents=@(1,1,1)}
$left=Entity 4 'Left Wall' 0 @(-2.5,.5,4.5) $wallFields
$right=Entity 5 'Right Wall' 0 @(2.5,.5,4.5) $wallFields
$left.transform.scale=$right.transform.scale=@(.22,.6,6)
$runner=Entity 6 'Runner' 0 @(0,.32,0) ($beacon + @{
    collider=@{enabled=$true;halfExtents=@(1,1,1)}
    rigidBody=@{enabled=$true;dynamic=$true;gravity=$false;mass=1}
    script=(Script 'Runner' @{walkthrough=[bool]$Walkthrough})
})
$runner.transform.scale=@(.32,.32,.32)
$particles=@{enabled=$true;asset='Assets/Effects/光.png';guid=$ids.Image;capacity=96;rate=18;burst=28;
    lifetime=.95;size=.12;endSize=.024;seed=56;velocity=@(0,2.6,0);spread=@(2.5,1.2,2.5);
    startColor=@(.25,1,.7,1);endColor=@(.25,.65,1,0);sizeKeys=@(,@(.25,.21))}
$hud=Label '出口へ  /  WASD' 34 @(600,68) $mint
$hud.anchor=@(.5,0)
$stage=@(
    $camera
    $light
    $floor
    $left
    $right
    $runner
    (Entity 7 'Exit Trigger' 0 @(0,.4,8.2) @{collider=@{enabled=$true;trigger=$true;halfExtents=@(2.09,1,.52)};script=(Script 'Goal')})
    (Entity 8 'Goal Sparks' 0 @(0,.33,8.55) @{active=$false;particle=$particles})
    (Entity 9 'HUD Canvas' 0 @(0,0,0) $canvas)
    (Entity 10 'Stage HUD' 9 @(0,48,0) @{sprite=$hud})

)
$result=@(
    (Entity 1 'Results Canvas' 0 @(0,0,0) $canvas)
    (Entity 2 'Clear Heading' 1 @(0,-125,0) @{sprite=(Label 'クリア！' 100 @(770,120) $mint)})
    (Entity 3 'Clear Subtitle' 1 @(0,3,0) @{sprite=(Label '光の回廊を抜けました' 40 @(720,68) $white)})
    (Entity 4 'Restart Button' 1 @(0,113,0) @{sprite=$restart})
    (Entity 5 'Results Controller' 0 @(0,0,0) @{script=(Script 'Result' @{report=[bool]$Walkthrough})})
)
WriteJson 'Assets/Scenes/Stage.scene' @{version=3;entities=$stage;environment=@{
    background=$background;exposure=-.2;reflectionIntensity=.42;postEffect='none';showSkybox=$false}}
WriteJson 'Assets/Scenes/Result.scene' @{version=3;entities=$result;environment=@{background=@(.013,.052,.064)}}
WriteJson 'Assets/Scenes/Main.scene' @{version=3;entities=$title;environment=@{background=$background}}
Identity 'Assets/Scenes/Main.scene' $ids.Main
Identity 'Assets/Scenes/Stage.scene' $ids.Stage
Identity 'Assets/Scenes/Result.scene' $ids.Result
Write-Output "Prepared corridor project $manifest"
