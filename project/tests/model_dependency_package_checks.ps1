[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$fixture=Join-Path ([IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\generated\cpu-checks'))) ('model-dependency-package-'+[guid]::NewGuid().ToString('N'))
$project=Join-Path $fixture 'Project';$runtime=Join-Path $fixture 'Engine'
$packager=Join-Path $PSScriptRoot '..\tools\package_project.ps1'
function Require($ok,[string]$why) {if(!$ok) {throw $why}}
foreach($folder in @((Join-Path $project 'Content/Models'),(Join-Path $runtime 'Resources/Shaders'),(Join-Path $runtime 'Resources/textures/skybox'),(Join-Path $runtime 'Resources/textures/common'),(Join-Path $runtime 'ThirdPartyLicenses'))) {[IO.Directory]::CreateDirectory($folder) | Out-Null}
foreach($file in @('KuboPlayer.exe','dxcompiler.dll','dxil.dll','Resources/Shaders/test.hlsl','Resources/textures/skybox/test.dds','Resources/textures/common/white.png','ThirdPartyLicenses/Lua-5.4.9.txt')) {[IO.File]::WriteAllText((Join-Path $runtime $file),'fixture runtime')}
$projectFile=Join-Path $project 'Game.kubo';$sceneFile=Join-Path $project 'entry.scene'
[IO.File]::WriteAllText($projectFile,'{"name":"Fixture","startupScene":"entry.scene","assetDirectory":"Content"}')
$materialName=(-join ([char[]]@(0x6750,0x8cea)))+'.mtl';$imageName=(-join ([char[]]@(0x8272)))+' #1.png'
$materialFile=Join-Path $project ('Content/Models/'+$materialName)
$imageFile=Join-Path $project ('Content/Models/'+$imageName)
[IO.File]::WriteAllText((Join-Path $project 'Content/Models/model.obj'),("mtllib "+$materialName+"`nv 0 0 0`n"))
$materialText='map_Kd "'+$imageName+'"'
[IO.File]::WriteAllText($materialFile,$materialText);[IO.File]::WriteAllText($imageFile,'fixture texture')
function Set-Model([string]$model) {[IO.File]::WriteAllText($sceneFile,(@{version=1;entities=@(@{model=$model})} | ConvertTo-Json -Depth 5))}
function Export-To([string]$name) {& $packager -ProjectFile $projectFile -RuntimeExecutable (Join-Path $runtime 'KuboPlayer.exe') -OutputDirectory (Join-Path $fixture $name)}
function Reject-Export([string]$name) {
    $output=Join-Path $fixture $name;[IO.Directory]::CreateDirectory($output) | Out-Null
    . (Join-Path $PSScriptRoot '..\tools\package_publication.ps1')
    $before=[Kubo.PackagePublicationNative]::Identity($output);$blocked=$false
    try {Export-To $name} catch {$blocked=$true}
    Require ($blocked -and [Kubo.PackagePublicationNative]::Identity($output) -ceq $before -and @(Get-ChildItem -LiteralPath $output -Force).Count -eq 0) "Invalid Model dependency changed output: $name"
}
Set-Model 'Content/Models/model.obj';Export-To 'ObjMaterialTexture'
Require (Test-Path -LiteralPath (Join-Path $fixture ('ObjMaterialTexture/Content/Models/'+$imageName))) 'Transitive Texture omitted'
[IO.File]::WriteAllText($materialFile,'map_Kd missing.png');Reject-Export 'MissingTexture'
[IO.File]::WriteAllText($materialFile,'map_Kd -s 1 1 1 image.png');Reject-Export 'UnknownMtlOptions'
[IO.File]::WriteAllText((Join-Path $project 'outside.png'),'not copied');[IO.File]::WriteAllText($materialFile,'map_Kd ../../outside.png');Reject-Export 'UncopiedTexture'
[IO.File]::WriteAllText($materialFile,$materialText)
$gltfFile=Join-Path $project 'Content/Models/model.gltf'
[IO.File]::WriteAllText((Join-Path $project 'Content/Models/vertex bin.bin'),'vertex bytes')
[IO.File]::WriteAllText($gltfFile,'{"buffers":[{"uri":"vertex%20bin.bin"}],"images":[{"bufferView":0}]}')
Set-Model 'Content/Models/model.gltf';Export-To 'GltfBuffer'
[IO.File]::WriteAllText($gltfFile,'{"buffers":[{"uri":"missing.bin"}]}');Reject-Export 'MissingGltfBuffer'
[IO.File]::WriteAllText($gltfFile,'{"extensionsUsed":["VENDOR_unknown"]}');Reject-Export 'UnknownGltfExtension'
function Get-GlbBytes([string]$image='glb.png') {
    $json=[Text.UTF8Encoding]::new($false).GetBytes((@{asset=@{version='2.0'};buffers=@(@{byteLength=4},@{uri='vertex%20bin.bin'});images=@(@{uri=$image})} | ConvertTo-Json -Depth 6 -Compress))
    $padding=(4-$json.Length%4)%4;$memory=[IO.MemoryStream]::new();$writer=[IO.BinaryWriter]::new($memory)
    try {
        $writer.Write([uint32]0x46546c67);$writer.Write([uint32]2);$writer.Write([uint32](32+$json.Length+$padding))
        $writer.Write([uint32]($json.Length+$padding));$writer.Write([uint32]0x4e4f534a);$writer.Write($json)
        for($index=0;$index -lt $padding;$index++) {$writer.Write([byte]32)}
        $writer.Write([uint32]4);$writer.Write([uint32]0x004e4942);$writer.Write([byte[]]@(1,2,3,4));$writer.Flush()
        return ,$memory.ToArray()
    } finally {$writer.Dispose()}
}
$glbFile=Join-Path $project 'Content/Models/model.glb';$glbBytes=Get-GlbBytes
[IO.File]::WriteAllText((Join-Path $project 'Content/Models/glb.png'),'fixture image');[IO.File]::WriteAllBytes($glbFile,$glbBytes)
Set-Model 'Content/Models/model.glb';Export-To 'GlbExternalImageBuffer'
[IO.File]::WriteAllBytes($glbFile,(Get-GlbBytes 'absent.png'));Reject-Export 'GlbMissingImage'
$badGlb=$glbBytes.Clone();$badGlb[8]=0;$badGlb[9]=0;$badGlb[10]=0;$badGlb[11]=0
[IO.File]::WriteAllBytes($glbFile,$badGlb);Reject-Export 'GlbInvalidExtent'
[IO.File]::WriteAllBytes($glbFile,$glbBytes)
Set-Model 'Content/Models/model.obj'
# Actual candidate mutation after copying startup Scene. Source stays unchanged.
$mutation=@{Done=$false;Glb=$false}
function Copy-Item {
    [CmdletBinding()]
    param([string]$LiteralPath,[string]$Destination,[switch]$Force,[switch]$Recurse)
    Microsoft.PowerShell.Management\Copy-Item @PSBoundParameters
    if($LiteralPath -ceq $sceneFile) {
        $stage=Split-Path -Parent $Destination
        Require ((Split-Path -Parent $stage) -ceq $fixture -and (Split-Path -Leaf $stage).StartsWith('.kubo-package-stage-')) 'Mutation escaped fixture stage'
        if($mutation.Glb) {[IO.File]::WriteAllBytes((Join-Path $stage 'Content/Models/model.glb'),(Get-GlbBytes 'absent-after-copy.png'))}
        else {[IO.File]::WriteAllText((Join-Path $stage ('Content/Models/'+$materialName)),'map_Kd absent-after-copy.png')}
        $mutation.Done=$true
    }
}
try {
    $blocked=$false;try {Export-To 'ChangedCandidate'} catch {$blocked=$true}
    Require ($mutation.Done -and $blocked -and !(Test-Path -LiteralPath (Join-Path $fixture 'ChangedCandidate'))) 'Candidate with missing Texture published'
    Require ([IO.File]::ReadAllText($materialFile) -ceq $materialText) 'Candidate validation changed source material'
    $mutation.Done=$false;$mutation.Glb=$true;Set-Model 'Content/Models/model.glb'
    $blocked=$false;try {Export-To 'ChangedGlbCandidate'} catch {$blocked=$true}
    Require ($mutation.Done -and $blocked -and !(Test-Path -LiteralPath (Join-Path $fixture 'ChangedGlbCandidate'))) 'GLB candidate with missing image published'
    Require ([Convert]::ToBase64String([IO.File]::ReadAllBytes($glbFile)) -ceq [Convert]::ToBase64String($glbBytes)) 'Candidate validation changed source GLB'
} finally {Remove-Item Function:\Copy-Item}
Require (@(Get-ChildItem -LiteralPath $project -Filter '*.kubo-meta.json' -File -Recurse).Count -eq 0) 'Validation generated project sidecars'
$validator=Join-Path $PSScriptRoot '..\..\generated\outputs\Release\tools\KuboAssetValidator.exe'
$badRequests=@(
    [Text.UTF8Encoding]::new($false).GetBytes('[{"format":"kubo-export-dependencies-1"}]'),
    [byte[]]@(0xff,0xfe),
    [Text.UTF8Encoding]::new($false).GetBytes(('['*100+'0'+']'*100)),
    [Text.UTF8Encoding]::new($false).GetBytes('{"format":"kubo-export-dependencies-1","root":"relative","models":[],"copyRoots":[]}'),
    [Text.UTF8Encoding]::new($false).GetBytes('{"format":"kubo-export-dependencies-1","root":123,"models":[],"copyRoots":[]}'),
    [Text.UTF8Encoding]::new($false).GetBytes((@{format='kubo-export-dependencies-1';root=($project+[char]0+'suffix');models=@();copyRoots=@()} | ConvertTo-Json -Depth 4))
)
$requestIndex=0
foreach($requestBytes in $badRequests) {
    $requestIndex++;$requestFile=Join-Path $fixture "BadRequest-$requestIndex.json"
    [IO.File]::WriteAllBytes($requestFile,$requestBytes)
    $child=$null
    try {
        $child=Start-Process -FilePath $validator -ArgumentList @('--request',('"'+$requestFile+'"')) -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $fixture "BadRequest-$requestIndex.out") -RedirectStandardError (Join-Path $fixture "BadRequest-$requestIndex.err")
        $ownedHandle=$child.Handle
        Require ($ownedHandle -ne [IntPtr]::Zero -and $child.WaitForExit(30000)) 'Owned validator request timed out'
        $child.WaitForExit();Require ($child.ExitCode -eq 1) 'Malformed validator request did not fail normally'
        if($requestIndex -eq 6) {Require ([IO.File]::ReadAllText((Join-Path $fixture "BadRequest-$requestIndex.err")).Contains('Invalid validator root text')) 'Escaped NUL root did not reach its explicit text guard'}
    } finally {if($child) {if(!$child.HasExited) {Stop-Process -Id $child.Id;$child.WaitForExit()};$child.Dispose()}}
}
Write-Output "PASS: shared-parser OBJ/MTL/Texture and glTF package preflight/candidate revalidation, read-only source and unchanged outputs; retained fixtures: $fixture"
