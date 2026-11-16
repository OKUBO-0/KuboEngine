[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$SdkDirectory,[switch]$WithRender,[switch]$WithUI,[switch]$WithParticles,[string]$MSBuild='C:/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/amd64/MSBuild.exe')
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'sdk_link_inputs.ps1')
$prefabSdk=(Resolve-Path -LiteralPath $SdkDirectory).Path.TrimEnd('\')
$prefabSdkUnicode=-join ([char[]]@(0x65e5,0x672c,0x8a9e))
$prefabSdkManifest=Get-Content -LiteralPath (Join-Path $prefabSdk 'sdk-package.json') -Raw -Encoding UTF8 | ConvertFrom-Json
function Assert-PrefabSdkHashes {foreach($entry in $prefabSdkManifest.files){if((Get-FileHash -LiteralPath (Join-Path $prefabSdk $entry.path)).Hash.ToLowerInvariant() -cne $entry.sha256){throw "SDK digest mismatch: $($entry.path)"}}}
Assert-PrefabSdkHashes
$prefabCheckRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot ('../../generated/sdk-prefab/'+[guid]::NewGuid().ToString('N'))))
New-Item -ItemType Directory -Path $prefabCheckRoot,(Join-Path $prefabCheckRoot 'Unrelated cwd') | Out-Null
Write-Output "SDK_PREFAB_LOGS=$prefabCheckRoot"
foreach($file in @('sdk_prefab_fixture.cpp','sdk_prefab_fixture.vcxproj')){Copy-Item -LiteralPath (Join-Path $PSScriptRoot $file) -Destination $prefabCheckRoot}
if($WithRender){foreach($file in @('player_lighting_fixture.cpp','player_lighting_fixture.h')){Copy-Item -LiteralPath (Join-Path $PSScriptRoot $file) -Destination $prefabCheckRoot}}
if($WithUI){foreach($file in @('player_sprite_fixture.cpp','player_sprite_fixture.h')){Copy-Item -LiteralPath (Join-Path $PSScriptRoot $file) -Destination $prefabCheckRoot}}
[xml]$prefabSdkIncludes=Get-Content -LiteralPath (Join-Path $prefabSdk 'KuboRuntime.Includes.props') -Raw
$prefabSdkVersion=$prefabSdkIncludes.SelectSingleNode('//*[local-name()="KuboSdkVCToolsVersion"]').InnerText
function Invoke-OwnedPrefab([string]$Exe,[string[]]$Arguments,[string]$Label){
    $child=$null
    try{
        $out=Join-Path $prefabCheckRoot ($Label+'.log');$err=Join-Path $prefabCheckRoot ($Label+'.stderr.log')
        $start=@{FilePath=$Exe;WorkingDirectory=(Join-Path $prefabCheckRoot 'Unrelated cwd');WindowStyle='Hidden';PassThru=$true;RedirectStandardOutput=$out;RedirectStandardError=$err}
        if($Arguments.Count){$start.ArgumentList=$Arguments}
        $child=Start-Process @start
        $handle=$child.Handle
        [ordered]@{processId=$child.Id;startTimeUtc=$child.StartTime.ToUniversalTime().ToString('o');executable=$Exe;arguments=$Arguments} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $prefabCheckRoot ($Label+'.process.json')) -Encoding UTF8
        if($handle -eq [IntPtr]::Zero -or !$child.WaitForExit(50000)){throw "SDK Prefab $Label timed out"}
        $child.WaitForExit();$exit=$child.ExitCode;Get-Content -LiteralPath $out;Get-Content -LiteralPath $err
        [ordered]@{processId=$child.Id;exitCode=$exit;completedAtUtc=[DateTime]::UtcNow.ToString('o')} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $prefabCheckRoot ($Label+'.terminal.json')) -Encoding UTF8
        if($exit -ne 0 -or (Get-Content -LiteralPath $out -Raw) -notmatch '(?m)^PASS: SDK'){throw "SDK Prefab $Label failed: exit=$exit"}
        if($Arguments.Count -and $Arguments[0] -eq '--render'){
            $renderLog=Get-Content -LiteralPath $out
            if(@($renderLog | Where-Object {$_ -match '^PLAYER_LIGHT_PIXEL '}).Count -ne 12 -or @($renderLog | Where-Object {$_ -eq 'PASS: SDK-only Light/Material Lua lifecycle/3-kind pixels/multiple swapchain indices/rollback/source preservation/GPU errors0/shutdown'}).Count -ne 1){throw 'SDK rendered Light/Material evidence missing'}
        }
        if($Arguments.Count -and $Arguments[0] -eq '--ui'){
            $uiLog=Get-Content -LiteralPath $out
            $uiPixels=@($uiLog | Where-Object {$_ -match '^PLAYER_LUA_UI_PIXEL '})
            if($uiPixels.Count -ne 12 -or @($uiLog | Where-Object {$_ -eq 'PASS: SDK-only Canvas/Sprite Lua lifecycle/RGB pixels/multiple swapchain indices/rollback/source preservation/GPU errors0/shutdown'}).Count -ne 1){throw 'SDK rendered Canvas/Sprite evidence missing'}
            for($stage=0;$stage -lt 12;$stage++){
                $color=@('255,0,0','0,255,0','0,0,255')[$stage%3]
                if($uiPixels[$stage] -notmatch ('^PLAYER_LUA_UI_PIXEL stage='+$stage+' frame=[01] rgb='+$color+'$')){throw 'SDK UI pixel sequence incorrect'}
            }
            foreach($frame in 0,1){if(@($uiPixels | Where-Object {$_ -match " frame=$frame "}).Count -ne 6){throw 'SDK UI swapchain indices missing'}}
        }
        Write-Output "SDK_PREFAB_EXIT=$exit CASE=$Label"
    }finally{if($child){if(!$child.HasExited){Stop-Process -Id $child.Id;$child.WaitForExit()};$child.Dispose()}}
}
foreach($config in @('Debug','Release')){
    if((Get-PSDrive C).Free -lt 4GB){throw 'SDK Prefab verification requires at least 4 GiB free'}
    $projectFile=Join-Path $prefabCheckRoot 'sdk_prefab_fixture.vcxproj'
    $prefabRenderProperty=if($WithRender){'true'}else{'false'}
    $prefabUIProperty=if($WithUI){'true'}else{'false'}
    & $MSBuild $projectFile /m:1 "/p:Configuration=$config" /p:Platform=x64 "/p:VCToolsVersion=$prefabSdkVersion" "/p:KuboSdkRoot=$prefabSdk/" "/p:WithRender=$prefabRenderProperty" "/p:WithUI=$prefabUIProperty" /v:minimal *> (Join-Path $prefabCheckRoot ($config+'-build.log'))
    if($LASTEXITCODE -ne 0){Get-Content -LiteralPath (Join-Path $prefabCheckRoot ($config+'-build.log')) -Tail 25;throw "SDK Prefab build failed: $config"}
    $reads=Get-Content -LiteralPath (Join-Path $prefabCheckRoot "obj/$config/SdkPrefab.tlog/CL.read.1.tlog")
    $repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).TrimEnd('\')+'\'
    foreach($line in $reads){if($line.TrimStart('^').StartsWith($repo,[StringComparison]::OrdinalIgnoreCase)){throw 'External Prefab consumer read original repository source/header'}}
    $link=Get-Content -LiteralPath (Join-Path $prefabCheckRoot "obj/$config/SdkPrefab.tlog/link.command.1.tlog") -Raw
    if(!(Test-SdkRuntimeLinkInputs $link)){throw 'SDK Prefab link retained Editor or missed runtime inputs'}
    $resolvedLink=Get-Content -LiteralPath (Join-Path $prefabCheckRoot "obj/$config/SdkPrefab.tlog/link.read.1.tlog")
    if(!(Test-SdkResolvedArchiveInputs $resolvedLink (Join-Path $prefabSdk "lib/$config"))){throw 'SDK Prefab resolved archives did not come exclusively from the selected SDK/configuration'}
    $exe=Join-Path $prefabCheckRoot "bin/$config/SdkPrefab.exe";$gameRoot=Join-Path $prefabCheckRoot ($config+' Game '+$prefabSdkUnicode)
    Invoke-OwnedPrefab $exe @('--prepare',('"'+$gameRoot+'"')) ($config+'-prepare')
    Invoke-OwnedPrefab $exe @('--project',('"'+(Join-Path $gameRoot 'Game.kubo')+'"')) ($config+'-native')
    $export=Join-Path $prefabCheckRoot ($config+'-Export')
    & (Join-Path $prefabSdk 'tools/package_project.ps1') -ProjectFile (Join-Path $gameRoot 'Game.kubo') -RuntimeExecutable $exe -OutputDirectory $export -Configuration $config
    foreach($excluded in @('sdk_prefab_fixture.cpp','KuboRuntime.lib','KuboEditor.exe')){if(@(Get-ChildItem -LiteralPath $export -Recurse -Filter $excluded).Count){throw "Export leaked $excluded"}}
    $renamed=Join-Path $export 'RenamedPrefabGame.exe';Copy-Item -LiteralPath (Join-Path $export 'KuboPlayer.exe') -Destination $renamed
    Invoke-OwnedPrefab $renamed @() ($config+'-export')
    if($WithRender){
        if((Get-PSDrive C).Free -lt 4GB){throw 'SDK rendered Light/Material verification requires at least 4 GiB free'}
        $renderRoot=Join-Path $prefabCheckRoot ($config+' Light '+$prefabSdkUnicode)
        Invoke-OwnedPrefab $exe @('--prepare-render',('"'+$renderRoot+'"')) ($config+'-render-prepare')
        Invoke-OwnedPrefab $exe @('--render','--project',('"'+(Join-Path $renderRoot 'Game.kubo')+'"')) ($config+'-render-native')
        $renderExport=Join-Path $prefabCheckRoot ($config+'-Render-Export')
        & (Join-Path $prefabSdk 'tools/package_project.ps1') -ProjectFile (Join-Path $renderRoot 'Game.kubo') -RuntimeExecutable $exe -OutputDirectory $renderExport -Configuration $config
        foreach($excluded in @('sdk_prefab_fixture.cpp','player_lighting_fixture.cpp','player_lighting_fixture.h','KuboRuntime.lib','KuboEditor.exe','KuboMcpServer.exe')){if(@(Get-ChildItem -LiteralPath $renderExport -Recurse -Filter $excluded).Count){throw "Render export leaked $excluded"}}
        $renderRenamed=Join-Path $renderExport 'RenamedLightingGame.exe';Copy-Item -LiteralPath (Join-Path $renderExport 'KuboPlayer.exe') -Destination $renderRenamed
        Invoke-OwnedPrefab $renderRenamed @('--render') ($config+'-render-export')
    }
    if($WithUI){
        if((Get-PSDrive C).Free -lt 4GB){throw 'SDK rendered Canvas/Sprite verification requires at least 4 GiB free'}
        $uiRoot=Join-Path $prefabCheckRoot ($config+' UI '+$prefabSdkUnicode)
        Invoke-OwnedPrefab $exe @('--prepare-ui',('"'+$uiRoot+'"')) ($config+'-ui-prepare')
        Invoke-OwnedPrefab $exe @('--ui','--project',('"'+(Join-Path $uiRoot 'Game.kubo')+'"')) ($config+'-ui-native')
        $uiExport=Join-Path $prefabCheckRoot ($config+'-UI-Export')
        & (Join-Path $prefabSdk 'tools/package_project.ps1') -ProjectFile (Join-Path $uiRoot 'Game.kubo') -RuntimeExecutable $exe -OutputDirectory $uiExport -Configuration $config
        foreach($excluded in @('sdk_prefab_fixture.cpp','player_sprite_fixture.cpp','player_sprite_fixture.h','KuboRuntime.lib','KuboEditor.exe','KuboMcpServer.exe')){if(@(Get-ChildItem -LiteralPath $uiExport -Recurse -Filter $excluded).Count){throw "UI export leaked $excluded"}}
        $uiRenamed=Join-Path $uiExport 'RenamedUIGame.exe';Copy-Item -LiteralPath (Join-Path $uiExport 'KuboPlayer.exe') -Destination $uiRenamed
        Invoke-OwnedPrefab $uiRenamed @('--ui') ($config+'-ui-export')
    }
}
if($WithParticles){
    & (Join-Path $PSScriptRoot 'sdk_particle_group_checks.ps1') -SdkDirectory $prefabSdk -MSBuild $MSBuild
    & (Join-Path $PSScriptRoot 'sdk_particle_authoring_checks.ps1') -SdkDirectory $prefabSdk -MSBuild $MSBuild
}
Assert-PrefabSdkHashes
[ordered]@{complete=$true;sdk=$prefabSdk;sdkManifestSha256=(Get-FileHash -LiteralPath (Join-Path $prefabSdk 'sdk-package.json')).Hash;renderedNativeCases=(2+2*[int][bool]$WithRender+2*[int][bool]$WithUI);renderedExportCases=(2+2*[int][bool]$WithRender+2*[int][bool]$WithUI);authoredRenderComponents=[bool]$WithRender;authoredUIComponents=[bool]$WithUI;particleGroupConsumer=[bool]$WithParticles;particleAuthoringConsumer=[bool]$WithParticles;configurations=@('Debug','Release');originalSourceHeadersRead=$false;editorLinkInputs=$false;resolvedSdkArchives=$true;fullCpuCoverage=$false} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $prefabCheckRoot 'complete.json') -Encoding UTF8
foreach($config in @('Debug','Release')){Copy-Item -LiteralPath (Join-Path $prefabCheckRoot "obj/$config/SdkPrefab.tlog") -Destination (Join-Path $prefabCheckRoot ($config+'-tlogs')) -Recurse}
Write-Output 'PASS: SDK-only Prefab native APIs/rendered hosts/exported renamed game, Debug+Release, no source headers/Editor linkage; SDK hashes unchanged'
if($WithRender){Write-Output 'PASS: SDK-only Light/Material Lua pixels/rollback/lifecycle and renamed source-free export startup, Debug+Release; original source reads/Editor linkage absent and SDK hashes unchanged'}
if($WithUI){Write-Output 'PASS: SDK-only Canvas/Sprite Lua pixels/rollback/lifecycle and renamed source-free export startup, Debug+Release; original source reads/Editor linkage absent and SDK hashes unchanged'}
