[CmdletBinding()]
param(
    [ValidateSet('All','Editor','Player','Runtime','Launcher','Mcp','AssetValidator')][string]$Target='All',
    [ValidateSet('Debug','Release','Both')][string]$Configuration='Both',
    [ValidatePattern('^\d+\.\d+\.\d+$')][string]$VCToolsVersion='14.44.35207',
    [string]$MSBuild,
    [switch]$PlanOnly
)
$ErrorActionPreference='Stop'
$engineBuildRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).TrimEnd('\')
$engineBuildTargets=@{All='KuboEngine.sln';Editor='KuboEngine.vcxproj';Player='KuboPlayer.vcxproj';Runtime='KuboRuntime.vcxproj';Launcher='launcher/KuboLauncher.vcxproj';Mcp='mcp/KuboMcpServer.vcxproj';AssetValidator='asset-validator/KuboAssetValidator.vcxproj'}
$engineBuildProject=Join-Path $engineBuildRoot $engineBuildTargets[$Target]
if(!(Test-Path -LiteralPath $engineBuildProject -PathType Leaf)){throw "Engine build input missing: $engineBuildProject"}
if([string]::IsNullOrWhiteSpace($MSBuild)) {
    $engineBuildVswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if(Test-Path -LiteralPath $engineBuildVswhere -PathType Leaf) {
        $MSBuild=@(& $engineBuildVswhere -latest -products '*' -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\amd64\MSBuild.exe') | Select-Object -First 1
        if($LASTEXITCODE -ne 0){throw 'MSBuild discovery failed; pass -MSBuild explicitly'}
    }
    if([string]::IsNullOrWhiteSpace($MSBuild)) {
        $engineBuildCommands=@(Get-Command MSBuild.exe -CommandType Application -ErrorAction SilentlyContinue)
        if($engineBuildCommands.Count){$MSBuild=$engineBuildCommands[0].Source}
    }
}
if([string]::IsNullOrWhiteSpace($MSBuild) -or !(Test-Path -LiteralPath $MSBuild -PathType Leaf)) {
    throw 'MSBuild not found. Install compatible C++ Build Tools and Windows SDK or pass -MSBuild. No Visual Studio GUI is required.'
}
$engineBuildProgram=(Resolve-Path -LiteralPath $MSBuild).Path
$engineBuildConfigurations=if($Configuration -eq 'Both'){@('Debug','Release')}else{@($Configuration)}
# A global solution root also keeps referenced third-party library projects in
# the same output tree when a selected vcxproj is built directly. Percent/semicolon
# are escaped as MSBuild property literals, not interpreted as extra properties.
$engineBuildSolutionLiteral=($engineBuildRoot.Replace('\','/')+'/').Replace('%','%25').Replace(';','%3B')
$engineBuildInvocations=@()
foreach($engineBuildConfiguration in $engineBuildConfigurations) {
    $engineBuildArguments=@($engineBuildProject,'/m:1',"/p:Configuration=$engineBuildConfiguration",'/p:Platform=x64',"/p:VCToolsVersion=$VCToolsVersion","/p:SolutionDir=$engineBuildSolutionLiteral",'/v:minimal')
    $engineBuildInvocations+=@{configuration=$engineBuildConfiguration;arguments=$engineBuildArguments}
}
$engineBuildPlan=[ordered]@{format='kubo-engine-build-plan-1';target=$Target;root=$engineBuildRoot;msbuild=$engineBuildProgram;toolchain=$VCToolsVersion;invocations=$engineBuildInvocations}
if($PlanOnly){$engineBuildPlan | ConvertTo-Json -Depth 6;return}
$engineBuildLogs=[IO.Path]::GetFullPath((Join-Path $engineBuildRoot ('../generated/build-engine/'+[guid]::NewGuid().ToString('N'))))
New-Item -ItemType Directory -Path $engineBuildLogs | Out-Null
$engineBuildPlan | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $engineBuildLogs 'plan.json') -Encoding UTF8
Write-Output "KUBO_ENGINE_BUILD_LOGS=$engineBuildLogs"
foreach($engineBuildInvocation in $engineBuildInvocations) {
    Write-Output "KUBO_ENGINE_BUILD_START=$Target CONFIG=$($engineBuildInvocation.configuration)"
    $engineBuildArguments=$engineBuildInvocation.arguments
    & $engineBuildProgram @engineBuildArguments | Tee-Object -FilePath (Join-Path $engineBuildLogs ($engineBuildInvocation.configuration+'.log'))
    $engineBuildExit=$LASTEXITCODE
    Write-Output "KUBO_ENGINE_BUILD_EXIT=$engineBuildExit CONFIG=$($engineBuildInvocation.configuration)"
    if($engineBuildExit -ne 0){throw "Engine build failed for $Target/$($engineBuildInvocation.configuration), exit=$engineBuildExit; logs retained at $engineBuildLogs"}
}
Write-Output "PASS: command-line engine build target=$Target configurations=$($engineBuildConfigurations -join ','); Visual Studio GUI not launched"
