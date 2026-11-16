[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$ProjectFile,
    [Parameter(Mandatory=$true)][string]$NativeProject,
    [Parameter(Mandatory=$true)][string]$SdkDirectory,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [ValidateSet('Debug','Release')][string]$Configuration='Release',
    [string]$MSBuild)
$ErrorActionPreference='Stop'
$nativeManifest=[IO.Path]::GetFullPath($ProjectFile)
$nativeProjectPath=[IO.Path]::GetFullPath($NativeProject)
$nativeSdk=[IO.Path]::GetFullPath($SdkDirectory).TrimEnd('\')
$nativeOutput=[IO.Path]::GetFullPath($OutputDirectory).TrimEnd('\')
foreach($file in @($nativeManifest,$nativeProjectPath,(Join-Path $nativeSdk 'sdk-package.json'),(Join-Path $PSScriptRoot 'package_project.ps1'))) {if(!(Test-Path -LiteralPath $file -PathType Leaf)) {throw "Native game input missing: $file"}}
if([IO.Path]::GetExtension($nativeProjectPath) -ine '.vcxproj') {throw 'NativeProject must be a trusted SDK consumer .vcxproj'}
foreach($protectedRoot in @((Split-Path -Parent $nativeManifest),(Split-Path -Parent $nativeProjectPath),$nativeSdk)) {
    if($nativeOutput -eq $protectedRoot -or $protectedRoot.StartsWith($nativeOutput+'\',[StringComparison]::OrdinalIgnoreCase) -or $nativeOutput.StartsWith($nativeSdk+'\',[StringComparison]::OrdinalIgnoreCase)) {throw 'Native export must not contain source roots or be inside the SDK'}
}
. (Join-Path $PSScriptRoot 'package_publication.ps1')
if(Recover-KuboPackagePublication $nativeOutput) {
    $recoveredNative=Get-Content -LiteralPath (Join-Path $nativeOutput 'native-build.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    if($recoveredNative.format -cne 'kubo-native-game-build-1' -or $recoveredNative.configuration -cne $Configuration -or
        $recoveredNative.runtimeSha256 -cne (Get-FileHash -LiteralPath (Join-Path $nativeOutput 'KuboPlayer.exe') -Algorithm SHA256).Hash.ToLowerInvariant()) {throw 'Recovered native package record mismatch; output retained for inspection'}
    Write-Output "Native SDK game recovered without recompilation: $nativeOutput";return
}
if((Test-Path -LiteralPath $nativeOutput) -and @(Get-ChildItem -LiteralPath $nativeOutput -Force).Count -gt 0) {throw 'Native export output must be empty'}
$nativeSdkManifest=Get-Content -LiteralPath (Join-Path $nativeSdk 'sdk-package.json') -Raw -Encoding UTF8 | ConvertFrom-Json
if($nativeSdkManifest.format -ne 'kubo-runtime-sdk-1') {throw 'Invalid SDK format'}
foreach($entry in $nativeSdkManifest.files) {
    $relative=[string]$entry.path
    if([IO.Path]::IsPathRooted($relative) -or $relative.IndexOf(':') -ge 0 -or ($relative -split '[/\\]') -contains '..') {throw 'Invalid SDK manifest path'}
    $path=Join-Path $nativeSdk $relative
    if(!(Test-Path -LiteralPath $path -PathType Leaf) -or (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry.sha256) {throw "SDK hash mismatch: $relative"}
}
if([string]::IsNullOrWhiteSpace($MSBuild)) {
    $vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if(Test-Path -LiteralPath $vswhere) {$MSBuild=@(& $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\amd64\MSBuild.exe') | Select-Object -First 1}
    if([string]::IsNullOrWhiteSpace($MSBuild)) {throw 'MSBuild not found; install compatible C++ Build Tools or pass -MSBuild. No IDE GUI is required.'}
}
if(!(Test-Path -LiteralPath $MSBuild -PathType Leaf)) {throw 'MSBuild executable not found'}
$nativeStage=Join-Path ([IO.Path]::GetTempPath()) ('kubo-native-build-'+[guid]::NewGuid().ToString('N'))
try {
    New-Item -ItemType Directory -Path $nativeStage | Out-Null
    $nativeBin=Join-Path $nativeStage 'bin'
    $nativeObj=Join-Path $nativeStage 'obj'
    $properties=@("/p:Configuration=$Configuration",'/p:Platform=x64',('/p:KuboSdkRoot='+$nativeSdk.Replace('\','/')+'/'),('/p:OutDir='+$nativeBin.Replace('\','/')+'/'),('/p:IntDir='+$nativeObj.Replace('\','/')+'/'))
    $metadata=& $MSBuild $nativeProjectPath @properties /getProperty:TargetPath,ConfigurationType /getItem:ClCompile,ProjectReference | Out-String
    if($LASTEXITCODE -ne 0) {throw 'Native project evaluation failed'}
    $evaluated=$metadata | ConvertFrom-Json
    if($evaluated.Properties.ConfigurationType -ne 'Application' -or @($evaluated.Items.ProjectReference).Count -ne 0) {throw 'Use an application SDK consumer with no engine/source ProjectReferences'}
    $target=[IO.Path]::GetFullPath($evaluated.Properties.TargetPath)
    if(!$target.StartsWith($nativeBin+'\',[StringComparison]::OrdinalIgnoreCase) -or [IO.Path]::GetExtension($target) -ine '.exe') {throw 'Native executable must remain within the isolated build stage'}
    & $MSBuild $nativeProjectPath @properties /m:1 /v:minimal
    if($LASTEXITCODE -ne 0) {throw "Native game compilation failed (exit $LASTEXITCODE); export was not started"}
    if(!(Test-Path -LiteralPath $target -PathType Leaf)) {throw 'Native build did not produce its executable'}
    $buildRecord=[ordered]@{format='kubo-native-game-build-1';configuration=$Configuration;sdkFormat=$nativeSdkManifest.format;toolchain=$nativeSdkManifest.toolchain.$Configuration;runtimeSha256=(Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant();nativeProjectName=[IO.Path]::GetFileName($nativeProjectPath)}
    & (Join-Path $PSScriptRoot 'package_project.ps1') -ProjectFile $nativeManifest -RuntimeExecutable $target -OutputDirectory $nativeOutput -Configuration $Configuration -NativeBuildRecord $buildRecord
    Write-Output "Native SDK game built and exported without an IDE GUI: $nativeOutput"
} finally {
    $resolvedStage=[IO.Path]::GetFullPath($nativeStage)
    $nativeTempRoot=[IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')+'\'
    if(!$resolvedStage.StartsWith($nativeTempRoot,[StringComparison]::OrdinalIgnoreCase) -or !(Split-Path -Leaf $resolvedStage).StartsWith('kubo-native-build-')) {throw 'Unsafe native build stage cleanup'}
    if(Test-Path -LiteralPath $resolvedStage) {Remove-Item -LiteralPath $resolvedStage -Recurse -Force}
}
