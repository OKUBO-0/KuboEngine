[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$OutputDirectory,
    [string]$BuildDirectory,
    [string]$MSBuild='C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe')
$ErrorActionPreference='Stop'
$sdkSource=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).TrimEnd('\')
if([string]::IsNullOrWhiteSpace($BuildDirectory)) {$BuildDirectory=Join-Path $sdkSource '..\generated\outputs'}
$sdkBuild=[IO.Path]::GetFullPath($BuildDirectory)
$sdkOutput=[IO.Path]::GetFullPath($OutputDirectory).TrimEnd('\')
if($sdkOutput -eq $sdkSource -or $sdkSource.StartsWith($sdkOutput+'\',[StringComparison]::OrdinalIgnoreCase) -or $sdkOutput -eq $sdkBuild -or $sdkOutput.StartsWith($sdkBuild+'\',[StringComparison]::OrdinalIgnoreCase)) {throw 'SDK output must not overlap source/build roots'}
if((Test-Path -LiteralPath $sdkOutput) -and @(Get-ChildItem -LiteralPath $sdkOutput -Force).Count -gt 0) {throw 'Choose a new empty SDK output directory'}
$sdkHeaders=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$sdkConfigMetadata=@{}
foreach($configuration in @('Debug','Release')) {
    foreach($file in @('KuboRuntime.lib','KuboLua.lib','DirectXTex.lib')) {if(!(Test-Path -LiteralPath (Join-Path $sdkBuild "$configuration\$file") -PathType Leaf)) {throw "Build runtime dependencies first: $configuration/$file"}}
    $reads=Join-Path $sdkSource "..\generated\obj\KuboRuntime\$configuration\KuboRuntime.tlog\CL.read.1.tlog"
    if(!(Test-Path -LiteralPath $reads -PathType Leaf)) {throw 'Runtime header dependency records are required'}
    foreach($line in Get-Content -LiteralPath $reads) {
        if(!$line.StartsWith('^') -and $line.StartsWith($sdkSource+'\',[StringComparison]::OrdinalIgnoreCase) -and [IO.Path]::GetExtension($line) -in @('.h','.hpp','.inl')) {
            if(!(Test-Path -LiteralPath $line -PathType Leaf)) {throw "Missing compiled header: $line"}
            $null=$sdkHeaders.Add($line)
        }
    }
    $metadata=& $MSBuild (Join-Path $sdkSource 'KuboRuntime.vcxproj') "/p:Configuration=$configuration" /p:Platform=x64 /getProperty:VCToolsVersion,WindowsTargetPlatformVersion,PlatformToolset | Out-String
    if($LASTEXITCODE -ne 0) {throw 'Runtime toolchain evaluation failed'}
    $sdkConfigMetadata[$configuration]=($metadata | ConvertFrom-Json).Properties
    $compiledVersion='VC\TOOLS\MSVC\'+$sdkConfigMetadata[$configuration].VCToolsVersion+'\'
    if(!@(Get-Content -LiteralPath $reads | Where-Object {$_.IndexOf($compiledVersion,[StringComparison]::OrdinalIgnoreCase) -ge 0}).Count) {throw 'Toolchain metadata does not match runtime header records; rebuild before packaging'}
    $assimp=if($configuration -eq 'Debug') {'assimp-vc143-mtd.lib'} else {'assimp-vc143-mt.lib'}
    if(!(Test-Path -LiteralPath (Join-Path $sdkSource "externals\assimp\lib\$configuration\$assimp"))) {throw 'Missing Assimp archive'}
}
if($sdkConfigMetadata.Debug.VCToolsVersion -ne $sdkConfigMetadata.Release.VCToolsVersion) {throw 'Both SDK configurations must use the same MSVC tool version'}
foreach($header in @('engine\app\RuntimeGame.h','engine\base\core\Framework.h','engine\scene\entity\EntityWorld.h')) {if(!$sdkHeaders.Contains((Join-Path $sdkSource $header))) {throw "Compiled public header closure missing: $header"}}
foreach($file in @('dxcompiler.dll','dxil.dll','Resources','ThirdPartyLicenses')) {if(!(Test-Path -LiteralPath (Join-Path $sdkBuild "Release\$file"))) {throw "Missing runtime deployment: $file"}}
if(!(Test-Path -LiteralPath (Join-Path $sdkBuild 'Release\tools\KuboAssetValidator.exe') -PathType Leaf)) {throw 'Missing Model dependency validator'}
New-Item -ItemType Directory -Path (Join-Path $sdkOutput 'include'),(Join-Path $sdkOutput 'runtime'),(Join-Path $sdkOutput 'examples\Starter') -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $sdkOutput 'tools') -Force | Out-Null
foreach($tool in @('build_native_game.ps1','package_project.ps1','package_publication.ps1')) {Copy-Item -LiteralPath (Join-Path $PSScriptRoot $tool) -Destination (Join-Path $sdkOutput "tools\$tool")}
Copy-Item -LiteralPath (Join-Path $sdkBuild 'Release\tools\KuboAssetValidator.exe') -Destination (Join-Path $sdkOutput 'tools\KuboAssetValidator.exe')
$sdkIncludeDirectories=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$null=$sdkIncludeDirectories.Add('')
foreach($header in $sdkHeaders) {
    $relative=$header.Substring($sdkSource.Length+1)
    $destination=Join-Path $sdkOutput ('include\'+$relative)
    New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
    Copy-Item -LiteralPath $header -Destination $destination
    $null=$sdkIncludeDirectories.Add([IO.Path]::GetDirectoryName($relative))
}
$sdkIncludeValues=@($sdkIncludeDirectories | Sort-Object | ForEach-Object {'$(KuboSdkRoot)include\'+$_}) -join ';'
$sdkIncludesXml='<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003"><PropertyGroup><KuboSdkVCToolsVersion>'+[Security.SecurityElement]::Escape($sdkConfigMetadata.Release.VCToolsVersion)+'</KuboSdkVCToolsVersion></PropertyGroup><ItemDefinitionGroup><ClCompile><AdditionalIncludeDirectories>' + [Security.SecurityElement]::Escape($sdkIncludeValues) + ';%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories></ClCompile></ItemDefinitionGroup></Project>'
[IO.File]::WriteAllText((Join-Path $sdkOutput 'KuboRuntime.Includes.props'),$sdkIncludesXml,[Text.UTF8Encoding]::new($false))
foreach($configuration in @('Debug','Release')) {
    $destination=Join-Path $sdkOutput "lib\$configuration";New-Item -ItemType Directory -Path $destination -Force | Out-Null
    foreach($file in @('KuboRuntime.lib','KuboLua.lib','DirectXTex.lib')) {Copy-Item -LiteralPath (Join-Path $sdkBuild "$configuration\$file") -Destination (Join-Path $destination $file)}
    $assimp=if($configuration -eq 'Debug') {'assimp-vc143-mtd.lib'} else {'assimp-vc143-mt.lib'}
    Copy-Item -LiteralPath (Join-Path $sdkSource "externals\assimp\lib\$configuration\$assimp") -Destination (Join-Path $destination 'assimp.lib')
}
foreach($file in @('dxcompiler.dll','dxil.dll','Resources','ThirdPartyLicenses')) {Copy-Item -LiteralPath (Join-Path $sdkBuild "Release\$file") -Destination (Join-Path $sdkOutput "runtime\$file") -Recurse}
Copy-Item -LiteralPath (Join-Path $sdkSource 'externals\imgui\LICENSE.txt') -Destination (Join-Path $sdkOutput 'runtime\ThirdPartyLicenses\ImGui.txt')
foreach($file in @('KuboRuntime.props','README.md')) {Copy-Item -LiteralPath (Join-Path $sdkSource "sdk\$file") -Destination (Join-Path $sdkOutput $file)}
foreach($file in @('Starter.vcxproj','Starter.cpp')) {Copy-Item -LiteralPath (Join-Path $sdkSource "sdk\$file") -Destination (Join-Path $sdkOutput "examples\Starter\$file")}
Copy-Item -LiteralPath (Join-Path $sdkSource 'engine\app\KuboEngine.manifest') -Destination (Join-Path $sdkOutput 'KuboEngine.manifest')
$sdkManifest=[ordered]@{format='kubo-runtime-sdk-1';platform='Windows-x64';configurations=@('Debug','Release');toolchain=$sdkConfigMetadata;publicHeaderPolicy='observed-runtime-compile-closure';files=@()}
foreach($file in Get-ChildItem -LiteralPath $sdkOutput -File -Recurse) {$sdkManifest.files+=@{path=$file.FullName.Substring($sdkOutput.Length+1).Replace('\','/');sha256=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}}
$sdkManifest | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath (Join-Path $sdkOutput 'sdk-package.json') -Encoding UTF8
Write-Output "Runtime SDK created: $sdkOutput ($($sdkHeaders.Count) observed headers)"
