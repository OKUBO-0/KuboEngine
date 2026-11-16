param([string]$HistoricalConsumerDirectory,[string]$SourceLinkedObjectDirectory)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'sdk_link_inputs.ps1')
$sdkSafe='"C:\mcp-parent SDK\lib\KuboRuntime.lib" "C:\Native\obj\Starter.obj"'
foreach($command in @($sdkSafe,'KuboRuntime.lib Starter.obj','C:/mcp-directory/lib/KuboRuntime.lib C:/IMGUI.LIB-folder/Starter.obj')) {
    if(!(Test-SdkRuntimeLinkInputs $command)) {throw ('Valid runtime input rejected: '+$command)}
}
foreach($editor in @('"C:\lib\imgui.lib"','"C:\obj\McpCommandBridge.obj"','"C:\obj\Game.obj"','C:/obj/EditorMcpServer.obj','MCP.OBJ','GAME.OBJ')) {
    if(Test-SdkRuntimeLinkInputs ($sdkSafe+' '+$editor)) {throw ('Editor input accepted: '+$editor)}
}
foreach($missing in @('"C:\KuboRuntime.lib-parent\Starter.obj"','Starter.obj','NotKuboRuntime.lib','KuboRuntime.libx')) {
    if(Test-SdkRuntimeLinkInputs $missing) {throw ('Runtime-like name accepted without runtime archive: '+$missing)}
}
Write-Output 'PASS: SDK link input filename boundaries, MCP-like directory isolation, actual runtime archive requirement and Editor object/library rejection'
$sdkLinkLabel=-join ([char[]]@(0x65e5,0x672c,0x8a9e))
$sdkLinkRoot='C:\sdk-mcp-parent '+$sdkLinkLabel+'\lib\Debug'
$sdkLinkNames=@('KuboRuntime.lib','KuboLua.lib','DirectXTex.lib','assimp.lib')
$sdkLinkValid=@($sdkLinkNames|ForEach-Object {Join-Path $sdkLinkRoot $_})
foreach($lines in @(@{values=$sdkLinkValid},@{values=@('^C:\game\main.obj')+$sdkLinkValid+@('C:\kit\kernel32.lib')},@{values=@($sdkLinkValid|ForEach-Object {$_.ToUpperInvariant()})},@{values=@($sdkLinkValid|ForEach-Object {$_.Replace('\','/')})},@{values=$sdkLinkValid+$sdkLinkValid})){
    if(!(Test-SdkResolvedArchiveInputs $lines.values $sdkLinkRoot)){throw 'Valid resolved SDK archive paths rejected'}
}
$sdkLinkNegatives=0
foreach($name in $sdkLinkNames){
    $missing=@($sdkLinkValid|Where-Object {[IO.Path]::GetFileName($_) -ne $name})
    foreach($wrong in @((Join-Path 'C:\repo\generated\outputs\Debug' $name),(Join-Path 'C:\other-sdk\lib\Debug' $name),(Join-Path 'C:\sdk-mcp-parent\lib\Release' $name),$name)){
        if(Test-SdkResolvedArchiveInputs ($missing+@($wrong)) $sdkLinkRoot){throw ('Foreign/wrong-config/relative archive accepted: '+$wrong)};$sdkLinkNegatives++
    }
    if(Test-SdkResolvedArchiveInputs $missing $sdkLinkRoot){throw ('Missing archive accepted: '+$name)};$sdkLinkNegatives++
    if(Test-SdkResolvedArchiveInputs ($sdkLinkValid+@(Join-Path 'C:\other-sdk\lib\Debug' $name)) $sdkLinkRoot){throw 'Mixed duplicate archive accepted'};$sdkLinkNegatives++
}
foreach($lines in @(@{values=@()},@{values=@('^'+($sdkLinkValid -join '|'))},@{values=$sdkLinkValid+@('C:\repo\assimp-vc143-mtd.lib')})){
    if(Test-SdkResolvedArchiveInputs $lines.values $sdkLinkRoot){throw 'Empty/header-only/alternate Assimp inputs accepted'};$sdkLinkNegatives++
}
foreach($badRoot in @('','relative\lib','C:relative\lib')){if(Test-SdkResolvedArchiveInputs $sdkLinkValid $badRoot){throw 'Unqualified SDK library root accepted'};$sdkLinkNegatives++}
Write-Output "PASS: resolved SDK four-archive provenance, case/Unicode/slash/duplicate positives, $sdkLinkNegatives missing/foreign/config/relative/mixed/header/Assimp/root negatives"
if($HistoricalConsumerDirectory -or $SourceLinkedObjectDirectory){
    if(!$HistoricalConsumerDirectory -or !$SourceLinkedObjectDirectory){throw 'Provide both historical consumer and source-linked object directories'}
    $sdkHistorical=Get-Content -LiteralPath (Join-Path $HistoricalConsumerDirectory 'complete.json') -Raw -Encoding UTF8|ConvertFrom-Json
    if(!$sdkHistorical.complete -or !$sdkHistorical.sdk){throw 'Historical consumer receipt missing'}
    foreach($config in @('Debug','Release')){
        $expected=Join-Path $sdkHistorical.sdk "lib/$config"
        $historical=Get-Content -LiteralPath (Join-Path $HistoricalConsumerDirectory "$config-tlogs/link.read.1.tlog")
        if(!(Test-SdkResolvedArchiveInputs $historical $expected)){throw "Historical real SDK $config link.read rejected"}
        $source=Get-Content -LiteralPath (Join-Path $SourceLinkedObjectDirectory "$config/KuboPlayer.tlog/link.read.1.tlog")
        if(Test-SdkResolvedArchiveInputs $source $expected){throw "Source-linked $config probe mislabeled as the historical SDK"}
    }
    Write-Output 'PASS: actual historical SDK Debug/Release link.read positives and current source-linked link.read negatives; no new SDK/consumer execution claim'
}
