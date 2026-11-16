[CmdletBinding()]
param([string]$MSBuild)
$ErrorActionPreference='Stop'
function Require($value,[string]$message){if(!$value){throw $message}}
$buildPlanProject=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).TrimEnd('\')
$buildPlanScript=Join-Path $buildPlanProject 'tools/build_engine.ps1'
$buildPlanRoot=[IO.Path]::GetFullPath((Join-Path $buildPlanProject '../generated'))
$buildPlanFixture=Join-Path $buildPlanRoot ('build-plan-checks/'+[guid]::NewGuid().ToString('N'))
$buildPlanWorking=Join-Path $buildPlanFixture ((-join ([char[]]@(0x65e5,0x672c,0x8a9e)))+" & '$ Working")
New-Item -ItemType Directory -Path $buildPlanWorking | Out-Null
$buildPlanBefore=@(Get-ChildItem -LiteralPath (Join-Path $buildPlanRoot 'build-engine') -Directory -ErrorAction SilentlyContinue).Count
$buildPlanOriginalLocation=(Get-Location).Path
$buildPlanCases=0
try {
 Set-Location -LiteralPath $buildPlanWorking
 foreach($buildPlanTarget in @('All','Editor','Player','Runtime','Launcher','Mcp','AssetValidator')) {
  $buildPlanOptions=@{Target=$buildPlanTarget;Configuration='Both';PlanOnly=$true}
  if($MSBuild){$buildPlanOptions.MSBuild=$MSBuild}
  $buildPlan=& $buildPlanScript @buildPlanOptions | ConvertFrom-Json
  Require ($buildPlan.format -ceq 'kubo-engine-build-plan-1' -and $buildPlan.target -ceq $buildPlanTarget -and $buildPlan.root -ceq $buildPlanProject) 'Plan identity/cwd isolation failed'
  Require ($buildPlan.invocations.Count -eq 2 -and $buildPlan.invocations[0].configuration -ceq 'Debug' -and $buildPlan.invocations[1].configuration -ceq 'Release') 'Both ordering failed'
  foreach($buildPlanInvocation in $buildPlan.invocations) {
   $buildPlanArgs=@($buildPlanInvocation.arguments)
   Require ([IO.Path]::IsPathRooted($buildPlanArgs[0]) -and (Test-Path -LiteralPath $buildPlanArgs[0] -PathType Leaf)) 'Plan target path invalid'
   Require ($buildPlanArgs -contains '/m:1' -and $buildPlanArgs -contains '/p:Platform=x64' -and $buildPlanArgs -contains '/p:VCToolsVersion=14.44.35207') 'Plan compiler/platform contract changed'
   Require ($buildPlanArgs -contains ('/p:SolutionDir='+($buildPlanProject.Replace('\','/')+'/').Replace('%','%25').Replace(';','%3B'))) 'Referenced-project root not anchored'
   if($buildPlanTarget -ne 'All') {
    $buildPlanMetadataText=& $buildPlan.msbuild @buildPlanArgs /getProperty:OutDir,IntDir | Out-String
    Require ($LASTEXITCODE -eq 0) 'Actual MSBuild project evaluation failed'
    $buildPlanMetadata=($buildPlanMetadataText | ConvertFrom-Json).Properties
    $buildPlanExpected=Join-Path $buildPlanRoot ('outputs/'+$buildPlanInvocation.configuration)
    if($buildPlanTarget -eq 'AssetValidator'){$buildPlanExpected=Join-Path $buildPlanExpected 'tools'}
    Require ([IO.Path]::GetFullPath($buildPlanMetadata.OutDir).TrimEnd('\') -ieq [IO.Path]::GetFullPath($buildPlanExpected)) "Selected project output escaped common generated tree: $buildPlanTarget/$($buildPlanInvocation.configuration)/$($buildPlanMetadata.OutDir)"
    Require ([IO.Path]::GetFullPath($buildPlanMetadata.IntDir).StartsWith((Join-Path $buildPlanRoot 'obj')+'\',[StringComparison]::OrdinalIgnoreCase)) 'Selected project objects escaped common generated tree'
   }
   $buildPlanCases++
  }
 }
 $buildSingle=& $buildPlanScript -Target Player -Configuration Release -PlanOnly | ConvertFrom-Json
 Require ($buildSingle.invocations.Count -eq 1 -and $buildSingle.invocations[0].configuration -eq 'Release') 'Single configuration failed'
 $buildMissingRefused=$false
 try {& $buildPlanScript -MSBuild (Join-Path $buildPlanFixture 'absent.exe') -PlanOnly | Out-Null} catch {$buildMissingRefused=$_.Exception.Message.Contains('MSBuild not found')}
 Require $buildMissingRefused 'Missing build tool accepted'
 $buildInvalidRefused=$false
 try {& $buildPlanScript -VCToolsVersion '14.44.35207;Unexpected=1' -PlanOnly | Out-Null} catch {$buildInvalidRefused=$true}
 Require $buildInvalidRefused 'Additional compiler property accepted'
 # Exercise actual repository-root metacharacters, not only the caller cwd.
 $buildLiteralRoot=Join-Path $buildPlanFixture ((-join ([char[]]@(0x65e5,0x672c,0x8a9e)))+" & '$ ; 100% Root")
 New-Item -ItemType Directory -Path (Join-Path $buildLiteralRoot 'tools'),(Join-Path $buildLiteralRoot 'mcp') | Out-Null
 Copy-Item -LiteralPath $buildPlanScript -Destination (Join-Path $buildLiteralRoot 'tools/build_engine.ps1')
 [IO.File]::WriteAllText((Join-Path $buildLiteralRoot 'mcp/KuboMcpServer.vcxproj'),'<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003"><PropertyGroup><EchoRoot>$(SolutionDir)</EchoRoot></PropertyGroup></Project>',[Text.UTF8Encoding]::new($false))
 $buildLiteralPlan=& (Join-Path $buildLiteralRoot 'tools/build_engine.ps1') -Target Mcp -Configuration Release -MSBuild $buildSingle.msbuild -PlanOnly | ConvertFrom-Json
 $buildLiteralArgs=@($buildLiteralPlan.invocations[0].arguments)
 $buildLiteralText=& $buildLiteralPlan.msbuild @buildLiteralArgs /getProperty:EchoRoot | Out-String
 Require ($LASTEXITCODE -eq 0 -and [IO.Path]::GetFullPath($buildLiteralText.Trim()).TrimEnd('\') -ieq [IO.Path]::GetFullPath($buildLiteralRoot)) 'Native MSBuild percent/semicolon/Unicode root literal changed'
 $buildPlanAfter=@(Get-ChildItem -LiteralPath (Join-Path $buildPlanRoot 'build-engine') -Directory -ErrorAction SilentlyContinue).Count
 Require ($buildPlanBefore -eq $buildPlanAfter) 'Plan-only created build logs or executed build'
 Write-Output "PASS: engine CLI plans cases=$buildPlanCases/single/missing-tool/invalid-toolchain, actual selected-project metadata, Unicode/metacharacter unrelated cwd and native percent/semicolon repository root, no build execution/log writes (not native build completion)"
} finally {
 Set-Location -LiteralPath $buildPlanOriginalLocation
 Write-Output "BUILD_PLAN_FIXTURE_RETAINED=$buildPlanFixture"
}
