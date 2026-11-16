[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$EngineDirectory,
      [Parameter(Mandatory=$true)][string]$SdkDirectory)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'sdk_link_inputs.ps1')
function Require($condition,[string]$message){if(!$condition){throw $message}}
function Same-File([string]$left,[string]$right){
 Require ((Get-FileHash -LiteralPath $left -Algorithm SHA256).Hash -eq (Get-FileHash -LiteralPath $right -Algorithm SHA256).Hash) "Snapshot mismatch: $left"
}
$source=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path.TrimEnd('\')
$generated=(Resolve-Path (Join-Path $source '../generated')).Path.TrimEnd('\')
$engine=(Resolve-Path -LiteralPath $EngineDirectory).Path.TrimEnd('\')
$sdk=(Resolve-Path -LiteralPath $SdkDirectory).Path.TrimEnd('\')
foreach($snapshot in @(@{root=$engine;manifest='engine-package.json'},@{root=$sdk;manifest='sdk-package.json'})){
 $manifest=Get-Content -LiteralPath (Join-Path $snapshot.root $snapshot.manifest) -Raw -Encoding UTF8 | ConvertFrom-Json
 Require ($manifest.files.Count -gt 0) 'Empty distribution manifest'
 $seen=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
 foreach($entry in $manifest.files){
  Require (![IO.Path]::IsPathRooted($entry.path)) 'Rooted distribution path'
  $path=[IO.Path]::GetFullPath((Join-Path $snapshot.root $entry.path))
  Require ($path.StartsWith($snapshot.root+'\',[StringComparison]::OrdinalIgnoreCase)) 'Distribution path escapes root'
  Require ($seen.Add($path)) 'Duplicate normalized distribution path'
  Require ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -eq $entry.sha256) "Distribution digest mismatch: $path"
 }
 Write-Output "PASS: $($snapshot.manifest) $($manifest.files.Count) digests/normalized containment/uniqueness"
}
foreach($header in Get-ChildItem -LiteralPath (Join-Path $sdk 'include') -File -Recurse){
 Same-File $header.FullName (Join-Path $source $header.FullName.Substring($sdk.Length+9))
}
$observedHeaders=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach($configuration in @('Debug','Release')){
 foreach($dependency in Get-Content -LiteralPath (Join-Path $generated "obj/KuboRuntime/$configuration/KuboRuntime.tlog/CL.read.1.tlog")){
  if(!$dependency.StartsWith('^') -and $dependency.StartsWith($source+'\',[StringComparison]::OrdinalIgnoreCase) -and [IO.Path]::GetExtension($dependency) -in @('.h','.hpp','.inl')){
   $relative=$dependency.Substring($source.Length+1)
   $null=$observedHeaders.Add($relative)
   Same-File (Join-Path $sdk ('include\'+$relative)) $dependency
  }
 }
 foreach($library in @('KuboRuntime.lib','KuboLua.lib','DirectXTex.lib')){
  Same-File (Join-Path $sdk "lib/$configuration/$library") (Join-Path $generated "outputs/$configuration/$library")
 }
 $assimp=if($configuration -eq 'Debug'){'assimp-vc143-mtd.lib'}else{'assimp-vc143-mt.lib'}
 Same-File (Join-Path $sdk "lib/$configuration/assimp.lib") (Join-Path $source "externals/assimp/lib/$configuration/$assimp")
 $runtime=Get-Content (Join-Path $generated "obj/KuboRuntime/$configuration/KuboRuntime.tlog/Lib.command.1.tlog") -Raw
 Require ($runtime -match '(?:^|[\s"\\/])RUNTIMEGAME\.OBJ(?=$|[\s"])' -and $runtime -notmatch '(?:^|[\s"\\/])(?:GAME|IMGUI|MCP[^\s"\\/]*|GITWORKSPACE|ENGINEUPDATESERVICE)\.(?:OBJ|LIB)(?=$|[\s"])') 'Runtime archive boundary failed'
 Require (Test-SdkRuntimeLinkInputs (Get-Content (Join-Path $generated "obj/KuboPlayer/$configuration/KuboPlayer.tlog/link.command.1.tlog") -Raw)) 'Player link boundary failed'
 $adapter=Get-Content (Join-Path $generated "obj/KuboMcpServer/$configuration/KuboMcpServer.tlog/link.command.1.tlog") -Raw
 Require ($adapter -match '(?:^|[\s"\\/])KUBOMCPSERVER\.OBJ(?=$|[\s"])' -and $adapter -notmatch '(?:^|[\s"\\/])(?:MCPCOMMANDBRIDGE|ENTITYWORLD|SCENEDOCUMENT|ASSETREGISTRY|GAME|RUNTIMEGAME)\.OBJ(?=$|[\s"])') 'MCP adapter links World mutation/asset implementation'
 foreach($target in @('KuboEngine','KuboPlayer','KuboRuntime')){
  $lines=@(Get-Content (Join-Path $generated "obj/$target/$configuration/$target.tlog/CL.command.1.tlog"));$count=0
  for($i=0;$i -lt $lines.Count-1;$i++){
   if($lines[$i].StartsWith('^') -and $lines[$i] -notmatch '\\TESTS\\'){
    $count++;Require ($lines[$i+1] -notmatch 'KUBO_APPLICATION_CHECKS') 'Production application-check macro enabled'
   }
  }
  Require ($count -gt 0) 'No production compile records'
 }
 Write-Output "PASS: $configuration current SDK libraries, Runtime/Player and MCP adapter boundaries, production optional macro OFF"
}
Require ($observedHeaders.Count -gt 0) 'Empty Runtime compiled header closure'
Require (@(Get-ChildItem -LiteralPath (Join-Path $sdk 'include') -File -Recurse).Count -eq $observedHeaders.Count) 'SDK include inventory differs from current Runtime compiled closure'
foreach($file in @('README.md','KuboRuntime.props')){Same-File (Join-Path $sdk $file) (Join-Path $source "sdk/$file")}
Same-File (Join-Path $sdk 'KuboEngine.manifest') (Join-Path $source 'engine/app/KuboEngine.manifest')
foreach($file in @('Starter.cpp','Starter.vcxproj')){Same-File (Join-Path $sdk "examples/Starter/$file") (Join-Path $source "sdk/$file")}
foreach($tool in @('build_native_game.ps1','package_project.ps1','package_publication.ps1')){
 Same-File (Join-Path $sdk "tools/$tool") (Join-Path $source "tools/$tool")
}
Same-File (Join-Path $sdk 'tools/KuboAssetValidator.exe') (Join-Path $generated 'outputs/Release/tools/KuboAssetValidator.exe')
Write-Output "PASS: exact current Runtime header closure ($($observedHeaders.Count)), eight SDK library pairs, SDK README/props/manifest/Starter/tools"
foreach($file in @('KuboLauncher.exe','KuboEditor.exe','KuboPlayer.exe','KuboMcpServer.exe','tools/KuboAssetValidator.exe','dxcompiler.dll','dxil.dll')){
 Same-File (Join-Path $engine $file) (Join-Path $generated "outputs/Release/$file")
}
Same-File (Join-Path $engine 'README.md') (Join-Path $source 'docs/portable-engine-quickstart.md')
$live=@(Get-CimInstance Win32_Process | Where-Object {$_.ExecutablePath -and ($_.ExecutablePath.StartsWith($engine+'\',[StringComparison]::OrdinalIgnoreCase) -or $_.ExecutablePath.StartsWith($sdk+'\',[StringComparison]::OrdinalIgnoreCase))})
Require ($live.Count -eq 0) 'Distribution-owned processes still active'
Write-Output 'PASS: current SDK headers/Starter, seven Release binary pairs/README and distribution-owned processes zero'
