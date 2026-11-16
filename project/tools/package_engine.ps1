[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$OutputDirectory,
    [string]$BuildDirectory,
    [string]$EngineVersion)
$ErrorActionPreference='Stop'
if(![string]::IsNullOrEmpty($EngineVersion)){
    if($EngineVersion.Length -gt 128 -or $EngineVersion -notmatch '^[0-9]+(?:\.[0-9]+)*$'){throw 'EngineVersion must be a dotted numeric release declaration, at most 128 characters'}
    foreach($component in $EngineVersion.Split('.')){
        $parsedComponent=0
        if(![int]::TryParse($component,[Globalization.NumberStyles]::None,[Globalization.CultureInfo]::InvariantCulture,[ref]$parsedComponent)){throw 'EngineVersion components must be in 0..2147483647'}
    }
}
if([string]::IsNullOrWhiteSpace($BuildDirectory)) {$BuildDirectory=Join-Path $PSScriptRoot '..\..\generated\outputs\Release'}
$engineSource=[IO.Path]::GetFullPath($BuildDirectory).TrimEnd('\')
$engineOutput=[IO.Path]::GetFullPath($OutputDirectory).TrimEnd('\')
if($engineOutput -eq $engineSource -or $engineOutput.StartsWith($engineSource+'\',[StringComparison]::OrdinalIgnoreCase) -or $engineSource.StartsWith($engineOutput+'\',[StringComparison]::OrdinalIgnoreCase)) {throw 'Engine source and output directories must not overlap'}
if((Test-Path -LiteralPath $engineOutput) -and @(Get-ChildItem -LiteralPath $engineOutput -Force).Count -gt 0) {throw 'Choose an empty engine output directory'}
$engineFiles=@('KuboLauncher.exe','KuboEditor.exe','KuboPlayer.exe','KuboMcpServer.exe','dxcompiler.dll','dxil.dll')
$engineFolders=@('Resources','EditorResources','ThirdPartyLicenses')
foreach($relative in $engineFiles) {if(!(Test-Path -LiteralPath (Join-Path $engineSource $relative) -PathType Leaf)) {throw "Missing engine binary: $relative"}}
foreach($relative in $engineFolders) {if(!(Test-Path -LiteralPath (Join-Path $engineSource $relative) -PathType Container)) {throw "Missing engine folder: $relative"}}
foreach($relative in @('Resources\Shaders','Resources\textures\common\white.png','Resources\textures\skybox\test.dds','EditorResources\Fonts\fa-solid-900.ttf','ThirdPartyLicenses\Lua-5.4.9.txt')) {if(!(Test-Path -LiteralPath (Join-Path $engineSource $relative))) {throw "Missing engine resource: $relative"}}
$exportTool=Join-Path $PSScriptRoot 'package_project.ps1'
if(!(Test-Path -LiteralPath $exportTool -PathType Leaf)) {throw 'Missing project export tool'}
if(!(Test-Path -LiteralPath (Join-Path $engineSource 'tools\KuboAssetValidator.exe') -PathType Leaf)) {throw 'Missing Model dependency validator'}
foreach($required in @((Join-Path $PSScriptRoot '..\docs\portable-engine-quickstart.md'),(Join-Path $PSScriptRoot '..\docs\hub-guide.md'),(Join-Path $PSScriptRoot '..\externals\imgui\LICENSE.txt'))) {if(!(Test-Path -LiteralPath $required -PathType Leaf)) {throw "Missing package supporting file: $required"}}
New-Item -ItemType Directory -Force -Path $engineOutput | Out-Null
foreach($relative in $engineFiles+$engineFolders) {Copy-Item -LiteralPath (Join-Path $engineSource $relative) -Destination (Join-Path $engineOutput $relative) -Recurse -Force}
New-Item -ItemType Directory -Path (Join-Path $engineOutput 'tools') | Out-Null
Copy-Item -LiteralPath $exportTool -Destination (Join-Path $engineOutput 'tools\package_project.ps1')
Copy-Item -LiteralPath (Join-Path $engineSource 'tools\KuboAssetValidator.exe') -Destination (Join-Path $engineOutput 'tools\KuboAssetValidator.exe')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'package_publication.ps1') -Destination (Join-Path $engineOutput 'tools\package_publication.ps1')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'install_hub_shortcut.ps1') -Destination (Join-Path $engineOutput 'tools\install_hub_shortcut.ps1')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot '..\docs\portable-engine-quickstart.md') -Destination (Join-Path $engineOutput 'README.md')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot '..\docs\hub-guide.md') -Destination (Join-Path $engineOutput 'HUB-GUIDE.md')
# Keep available notices; a full redistribution/third-party audit remains separate.
Copy-Item -LiteralPath (Join-Path $PSScriptRoot '..\externals\imgui\LICENSE.txt') -Destination (Join-Path $engineOutput 'ThirdPartyLicenses\ImGui.txt')
$engineManifest=[ordered]@{format='kubo-portable-engine-1';launcher='KuboLauncher.exe';editor='KuboEditor.exe';player='KuboPlayer.exe';configuration='Release';files=@()}
if(![string]::IsNullOrEmpty($EngineVersion)){
    $engineManifest.format='kubo-portable-engine-2'
    $engineManifest['engineVersion']=$EngineVersion
    $engineManifest['platform']='Windows-x64'
}
foreach($file in Get-ChildItem -LiteralPath $engineOutput -File -Recurse) {
    $relative=$file.FullName.Substring($engineOutput.Length+1).Replace('\','/')
    $engineManifest.files+=@{path=$relative;sha256=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
}
if(![string]::IsNullOrEmpty($EngineVersion)){
    if($engineManifest.files.Count -gt 4096){throw 'Versioned engine package declaration exceeds 4096 files or 1 MiB; no manifest was published'}
    $versionedPaths=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach($entry in $engineManifest.files){
        $relative=$entry.path
        if([Text.UTF8Encoding]::new($false,$true).GetByteCount($relative) -gt 1024 -or $relative -match '[\\:<>"|?*\x00-\x1F\x7F]' -or !$versionedPaths.Add($relative)){throw 'Versioned engine package member path is unsupported; no manifest was published'}
        foreach($part in $relative.Split('/')){
            if([string]::IsNullOrEmpty($part) -or $part -in @('.','..') -or $part.EndsWith('.') -or $part.EndsWith(' ') -or $part -match '^(?:CON|PRN|AUX|NUL|CONIN\$|CONOUT\$|CLOCK\$|(?:COM|LPT)[1-9\u00B9\u00B2\u00B3])(?:\.|$)'){throw 'Versioned engine package member path is unsupported; no manifest was published'}
        }
    }
}
$engineManifestText=$engineManifest | ConvertTo-Json -Depth 5
if(![string]::IsNullOrEmpty($EngineVersion)){
    # Reserve the PS5 UTF-8 BOM plus CRLF as well as the serialized text.
    if([Text.UTF8Encoding]::new($false,$true).GetByteCount($engineManifestText)+5 -gt 1MB){throw 'Versioned engine package declaration exceeds 4096 files or 1 MiB; no manifest was published'}
}
$engineManifestText | Set-Content -LiteralPath (Join-Path $engineOutput 'engine-package.json') -Encoding UTF8
Write-Output "Portable KuboEngine created: $engineOutput"
