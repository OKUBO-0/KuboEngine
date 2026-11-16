[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$SdkDirectory,
      [string]$OnlyCase,
      [string]$MSBuild='C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe')
$ErrorActionPreference='Stop'
$serviceProject=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).TrimEnd('\')
$serviceSdk=(Resolve-Path -LiteralPath $SdkDirectory).Path.TrimEnd('\')
$serviceSource=Join-Path $serviceProject 'engine\base\core'
foreach($serviceContract in @('engine\base\core\Framework.h','engine\base\core\ServiceRetirementJournal.h','engine\base\render\OffscreenRenderManager.h')){
 if((Get-FileHash -LiteralPath (Join-Path $serviceProject $serviceContract)).Hash -ne
    (Get-FileHash -LiteralPath (Join-Path $serviceSdk ('include\'+$serviceContract))).Hash){throw "Use the SDK matching the current service/renderer header: $serviceContract"}
}
[xml]$serviceProps=Get-Content -LiteralPath (Join-Path $serviceSdk 'KuboRuntime.Includes.props') -Raw
$serviceTool=$serviceProps.SelectSingleNode('//*[local-name()="KuboSdkVCToolsVersion"]')
if(!$serviceTool){throw 'Missing SDK compiler contract'}
$serviceFixture=Join-Path ([IO.Path]::GetTempPath()) ('kubo-framework-services-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $serviceFixture | Out-Null
$serviceChild=$null
function Replace-ServiceAnchor([string]$body,[string]$before,[string]$after){
 if(([regex]::Matches($body,[regex]::Escape($before))).Count -ne 1){throw "Changed Framework fixture anchor: $before"}
 return $body.Replace($before,$after)
}
try {
 foreach($serviceFile in @('framework_services_fixture.cpp','framework_services_fixture.vcxproj')){
  Copy-Item -LiteralPath (Join-Path $PSScriptRoot $serviceFile) -Destination (Join-Path $serviceFixture $serviceFile)
 }
 $serviceHeader=Get-Content -LiteralPath (Join-Path $serviceSource 'Framework.h') -Raw
 $serviceHeader=Replace-ServiceAnchor $serviceHeader "`t`treturn manager;" "`t`tFixtureServiceAcquired(static_cast<size_t>(service));`r`n`t`treturn manager;"
 $serviceHeader="#include <cstddef>`r`nextern void FixtureServiceAcquired(std::size_t);`r`n"+$serviceHeader
 [IO.File]::WriteAllText((Join-Path $serviceFixture 'Framework.h'),$serviceHeader,[Text.UTF8Encoding]::new($false))
 $serviceBody=Get-Content -LiteralPath (Join-Path $serviceSource 'Framework.cpp') -Raw
 $coreAnchors=@(
  'winApp = std::make_unique<Engine::Base::WinApp>();',
  'dxCommon = std::make_shared<Engine::Base::DirectXCommon>();',
  'srvManager = std::make_unique<Engine::Base::SrvManager>();',
  'offscreenRenderManager = std::make_unique<Engine::Base::OffscreenRenderManager>();'
 )
 for($serviceIndex=0;$serviceIndex -lt $coreAnchors.Count;$serviceIndex++){
  $serviceBody=Replace-ServiceAnchor $serviceBody $coreAnchors[$serviceIndex] ($coreAnchors[$serviceIndex]+"`r`n`tFixtureCoreAcquired($serviceIndex);")
 }
 $serviceBody=Replace-ServiceAnchor $serviceBody 'winApp->Initialize();' "winApp->Initialize();`r`n`tFixtureWindowReady(winApp.get());"
 $serviceBody=Replace-ServiceAnchor $serviceBody 'dxCommon->Initialize(winApp.get());' "dxCommon->Initialize(winApp.get());`r`n`tFixtureGraphicsReady(dxCommon.get());"
 $serviceBody=Replace-ServiceAnchor $serviceBody "`tInitializeDebugTools();" "`tInitializeDebugTools();`r`n`tFixtureReady();"
 $servicePattern='(?m)^(\tAcquireService\(Service::(\w+),[^\r\n]*;)\r?$'
 if(([regex]::Matches($serviceBody,$servicePattern)).Count -ne 11){throw 'Expected eleven actual acquisition stages'}
 $serviceBody=[regex]::Replace($serviceBody,$servicePattern,{param($match)
  $match.Groups[1].Value+"`r`n`tFixtureServiceReady(static_cast<size_t>(Service::"+$match.Groups[2].Value+"));"
 })
 $retireAnchor="`t`tservices_.Retire(static_cast<size_t>(service));"
 $serviceBody=Replace-ServiceAnchor $serviceBody $retireAnchor ("`t`tif (services_.Contains(static_cast<size_t>(service))) FixtureBeforeRetire(static_cast<size_t>(service),this);`r`n"+$retireAnchor)
 $serviceBody=Replace-ServiceAnchor $serviceBody '#include "Framework.h"' "#include `"Framework.h`"`r`nextern void FixtureCoreAcquired(std::size_t);`r`nextern void FixtureServiceReady(std::size_t);`r`nextern void FixtureWindowReady(Engine::Base::WinApp*);`r`nextern void FixtureGraphicsReady(Engine::Base::DirectXCommon*);`r`nextern void FixtureReady();`r`nextern void FixtureBeforeRetire(std::size_t, Engine::Base::Framework*);"
 [IO.File]::WriteAllText((Join-Path $serviceFixture 'pinned_Framework.cpp'),$serviceBody,[Text.UTF8Encoding]::new($false))
 Copy-Item -LiteralPath (Join-Path $serviceProject '..\generated\outputs\Debug\imgui.lib') -Destination (Join-Path $serviceFixture 'pinned-imgui.lib')
 @(Join-Path $serviceSource 'Framework.h';Join-Path $serviceSource 'Framework.cpp';Join-Path $serviceSource 'ServiceRetirementJournal.h') |
  ForEach-Object {Get-FileHash -LiteralPath $_} | Format-List | Out-File -LiteralPath (Join-Path $serviceFixture 'input-hashes.log') -Encoding utf8
 $serviceBuildLog=Join-Path $serviceFixture 'build-Debug.log'
 & $MSBuild (Join-Path $serviceFixture 'framework_services_fixture.vcxproj') /m:1 /p:Configuration=Debug /p:Platform=x64 "/p:VCToolsVersion=$($serviceTool.InnerText)" "/p:KuboProjectRoot=$serviceProject/" "/p:KuboSdkRoot=$serviceSdk/" /v:minimal *> $serviceBuildLog
 if($LASTEXITCODE -ne 0){Get-Content -LiteralPath $serviceBuildLog -Tail 30;throw 'Framework services fixture build failed'}
 $serviceBin=Join-Path $serviceFixture 'bin'
 # Full Framework initialization compiles executable-relative post-effect
 # shaders; device-only fixtures do not exercise this deployment dependency.
 Copy-Item -LiteralPath (Join-Path $serviceSdk 'runtime\Resources') -Destination (Join-Path $serviceBin 'Resources') -Recurse
 foreach($serviceDll in @('dxcompiler.dll','dxil.dll')){Copy-Item -LiteralPath (Join-Path $serviceProject "..\generated\outputs\Debug\$serviceDll") -Destination (Join-Path $serviceBin $serviceDll)}
 $serviceRedist=Join-Path ([IO.Path]::GetDirectoryName($MSBuild)) '..\..\..\..\VC\Redist\MSVC'
 $serviceVersionPrefix=($serviceTool.InnerText.Split('.')[0..1] -join '.')+'.'
 $serviceOpenMp=@(Get-ChildItem -LiteralPath $serviceRedist -Filter vcomp140d.dll -Recurse -File | Where-Object {$_.FullName -match '\\x64\\Microsoft\.VC143\.DebugOpenMP\\' -and $_.FullName -notmatch '\\onecore\\' -and $_.FullName.Contains(('\MSVC\'+$serviceVersionPrefix))} | Sort-Object FullName -Descending | Select-Object -First 1)
 if($serviceOpenMp.Count -ne 1){throw 'Missing installed matching Debug x64 OpenMP runtime'}
 Copy-Item -LiteralPath $serviceOpenMp[0].FullName -Destination (Join-Path $serviceBin vcomp140d.dll)
 $serviceModes=@('cold')+@(0..3 | ForEach-Object {"core-$_"})+@(0..10 | ForEach-Object {"before-$_"})+@(0..10 | ForEach-Object {"after-$_"})+@('ready','normal','retire','shader-missing','repeat-ready','repeat-failed','repeat-retired')
 if($OnlyCase){if($OnlyCase -notin $serviceModes){throw 'Unknown Framework service case'};$serviceModes=@($OnlyCase)}
 $serviceCount=0
 foreach($serviceMode in $serviceModes){
  $serviceRoot=Join-Path $serviceFixture "project-$serviceMode";New-Item -ItemType Directory -Path $serviceRoot | Out-Null
  $serviceOut=Join-Path $serviceFixture "$serviceMode.log";$serviceErr=Join-Path $serviceFixture "$serviceMode.stderr.log"
  $serviceShaderPath=Join-Path $serviceBin 'Resources\Shaders'
  $serviceHiddenShaderPath=Join-Path $serviceBin 'Resources\Shaders-fixture-missing'
  $serviceShadersMoved=$false
  try {
   if($serviceMode -eq 'shader-missing'){
    $serviceResolvedShader=(Resolve-Path -LiteralPath $serviceShaderPath).Path
    if(!$serviceResolvedShader.StartsWith(([IO.Path]::GetFullPath($serviceFixture).TrimEnd('\')+'\'),[StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $serviceHiddenShaderPath)){throw 'Refuse shader move outside owned fixture or over existing data'}
    Rename-Item -LiteralPath $serviceResolvedShader -NewName 'Shaders-fixture-missing';$serviceShadersMoved=$true
   }
   $serviceChild=Start-Process -FilePath (Join-Path $serviceBin FrameworkServicesChecks.exe) -ArgumentList ($serviceMode+' "'+$serviceRoot+'"') -WorkingDirectory $serviceProject -WindowStyle Hidden -PassThru -RedirectStandardOutput $serviceOut -RedirectStandardError $serviceErr
  $serviceHandle=$serviceChild.Handle
  if(!$serviceChild.WaitForExit(50000)){throw "Owned Framework services child timeout: $serviceMode"}
  $serviceChild.WaitForExit();$serviceExit=$serviceChild.ExitCode;$serviceChild.Dispose();$serviceChild=$null
  } finally {
   if($serviceChild){if(!$serviceChild.HasExited){Stop-Process -Id $serviceChild.Id;$serviceChild.WaitForExit()};$serviceChild.Dispose();$serviceChild=$null}
   if($serviceShadersMoved){Rename-Item -LiteralPath $serviceHiddenShaderPath -NewName 'Shaders'}
  }
  Get-Content -LiteralPath $serviceOut;Get-Content -LiteralPath $serviceErr
  if($serviceExit -ne 0 -or !(Select-String -LiteralPath $serviceOut -Pattern "FRAMEWORK_SERVICES mode=$serviceMode " -SimpleMatch -Quiet)){throw "Framework services $serviceMode exit=$serviceExit"}
  $serviceCount++;Write-Output "FRAMEWORK_SERVICES_CHECK mode=$serviceMode native_exit=$serviceExit"
 }
 if($serviceCount -ne $serviceModes.Count){throw 'Framework services case coverage mismatch'}
 Write-Output "PASS: $serviceCount Runtime-profile Framework retirement source-probe cases; only=$OnlyCase; not SDK-only, Run exception or multi-host/device-loss proof"
} finally {
 if($serviceChild){if(!$serviceChild.HasExited){Stop-Process -Id $serviceChild.Id;$serviceChild.WaitForExit()};$serviceChild.Dispose()}
 Write-Output "Retained Framework services fixture/logs: $serviceFixture"
}
