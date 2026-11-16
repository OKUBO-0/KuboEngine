[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ProjectFile,
    [string]$OutputDirectory,
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [string]$RuntimeExecutable,
    [System.Collections.IDictionary]$NativeBuildRecord
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
. (Join-Path $PSScriptRoot 'package_publication.ps1')

function Resolve-ProjectPath([string]$RelativePath) {
    if ([string]::IsNullOrWhiteSpace($RelativePath) -or [IO.Path]::IsPathRooted($RelativePath)) {
        throw "Project paths must be non-empty relative paths: $RelativePath"
    }
    if($RelativePath.IndexOfAny('<>":|?*'.ToCharArray()) -ge 0 -or $RelativePath.IndexOf([char]0) -ge 0) {throw "Invalid project path characters: $RelativePath"}
    foreach($component in ($RelativePath -split '[/\\]')) {
        if($component -notin @('.','..') -and ($component.EndsWith('.') -or $component.EndsWith(' '))) {throw "Ambiguous trailing dot/space in project path: $RelativePath"}
    }
    $resolved = [IO.Path]::GetFullPath((Join-Path $projectRoot $RelativePath))
    if (-not $resolved.StartsWith($projectRoot.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw "Project path escapes the project root: $RelativePath"
    }
    return $resolved
}

function Read-BoundedJson([string]$Path,[int]$Limit,[bool]$AllowBom=$true) {
    $stream=[IO.File]::Open($Path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
    try {
        if($stream.Length -eq 0 -or $stream.Length -gt $Limit) {throw "JSON file exceeds its size bounds: $Path"}
        $bytes=New-Object byte[] ([int]$stream.Length)
        $offset=0
        while($offset -lt $bytes.Length) {
            $read=$stream.Read($bytes,$offset,$bytes.Length-$offset)
            if($read -eq 0) {throw "Incomplete JSON read: $Path"};$offset+=$read
        }
        if($stream.ReadByte() -ne -1) {throw "JSON changed while reading: $Path"}
    } finally {$stream.Dispose()}
    $text=[Text.UTF8Encoding]::new($false,$true).GetString($bytes)
    if($text[0] -eq [char]0xfeff) {
        if(!$AllowBom) {throw "Unexpected BOM in identity metadata: $Path"}
        $text=$text.Substring(1)
    }
    if($text.IndexOf([char]0) -ge 0) {throw "Raw NUL in JSON: $Path"}
    $depth=0;$quoted=$false;$escaped=$false
    foreach($character in $text.ToCharArray()) {
        if($quoted) {
            if($escaped) {$escaped=$false} elseif($character -eq '\') {$escaped=$true} elseif($character -eq '"') {$quoted=$false}
        } elseif($character -eq '"') {$quoted=$true}
        elseif($character -eq '{' -or $character -eq '[') {if(++$depth -gt 128) {throw "JSON nesting exceeds 128: $Path"}}
        elseif($character -eq '}' -or $character -eq ']') {$depth--}
    }
    # ConvertFrom-Json enumerates a one-element root array in some hosts.
    # Reject it lexically before conversion instead of accepting its lone object.
    if(!$text.TrimStart().StartsWith('{')) {throw "JSON root must be an object: $Path"}
    $json=$text | ConvertFrom-Json
    if($json -isnot [pscustomobject]) {throw "JSON root must be an object: $Path"}
    return $json
}
function Test-JsonNumber($Value) {return ($Value -is [int] -or $Value -is [long] -or $Value -is [double] -or $Value -is [decimal])}
function Assert-NativeAudioPath([string]$Path,[string]$Label='Audio') {
    # Inspect ancestors too: a lexical copied-root check is not enough for links.
    $current=$Path
    while($current.Length -ge $projectRoot.Length) {
        $item=Get-Item -LiteralPath $current -Force
        if(($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {throw "$Label linked paths are not supported: $Path"}
        if($current -eq $projectRoot) {break}
        $current=Split-Path -Parent $current
    }
}
function Get-AudioIdentityIndex([string]$Label='Audio') {
    $index=@{}
    if(!(Test-Path -LiteralPath $assetPath -PathType Container)) {return $index}
    Assert-NativeAudioPath $assetPath $Label
    $queue=New-Object 'System.Collections.Generic.Queue[string]'
    $queue.Enqueue($assetPath)
    while($queue.Count -gt 0) {
        $folder=$queue.Dequeue()
        foreach($entry in Get-ChildItem -LiteralPath $folder -Force) {
            if($folder -eq $assetPath -and $entry.Name -cin @('.kubo','.git','.vs','Library','Build','Builds','generated')) {continue}
            if(($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {throw "$Label identity scan refuses linked asset entries: $($entry.FullName)"}
            if($entry.PSIsContainer) {$queue.Enqueue($entry.FullName);continue}
            if($entry.Name.EndsWith('.kubo-meta.json',[StringComparison]::OrdinalIgnoreCase)) {continue}
            $metadata=$entry.FullName+'.kubo-meta.json'
            if(!(Test-Path -LiteralPath $metadata -PathType Leaf)) {continue}
            Assert-NativeAudioPath $metadata $Label
            try {
                $identity=Read-BoundedJson $metadata (16*1024) $false
                if(!(Test-JsonNumber $identity.version) -or $identity.version -ne 1 -or $identity.guid -isnot [string] -or $identity.guid -cnotmatch '^(?:[0-9a-f]{16}|[0-9a-f]{32})$') {throw 'Invalid identity fields'}
            } catch {throw "$Label identity metadata invalid: $metadata ($($_.Exception.Message))"}
            if($index.ContainsKey($identity.guid)) {throw "Duplicate $Label asset GUID: $($identity.guid)"}
            $index[$identity.guid]=$entry.FullName
        }
    }
    return $index
}
function Assert-WaveStructure([string]$Path) {
    $stream=$null;$reader=$null
    try {
        # Header-only validation of the same PCM/float RIFF subset as WaveFile.h.
        # Keep one read handle; never allocate/copy the sample payload here.
        $stream=[IO.File]::Open($Path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
        $size=$stream.Length
        if($size -lt 12 -or $size -gt 32*1024*1024) {throw 'Wave size exceeds runtime bounds'}
        $reader=[IO.BinaryReader]::new($stream)
        $riff=[Text.Encoding]::ASCII.GetString($reader.ReadBytes(4));$extent=[long]$reader.ReadUInt32()
        $wave=[Text.Encoding]::ASCII.GetString($reader.ReadBytes(4))
        if($riff -cne 'RIFF' -or $wave -cne 'WAVE' -or $extent -ne $size-8) {throw 'Invalid RIFF/WAVE extent'}
        $formatFound=$false;$dataFound=$false;$dataLength=0;$align=0
        while($stream.Position -lt $size) {
            if($size-$stream.Position -lt 8) {throw 'Truncated Wave chunk header'}
            $tag=[Text.Encoding]::ASCII.GetString($reader.ReadBytes(4));$length=[long]$reader.ReadUInt32();$body=$stream.Position
            $padding=$length%2
            if($length -gt $size-$body -or $padding -gt $size-$body-$length) {throw 'Wave chunk exceeds RIFF extent'}
            if($tag -ceq 'fmt ') {
                if($formatFound -or $length -notin @(16,18)) {throw 'Unsupported or duplicate Wave format chunk'}
                $encoding=$reader.ReadUInt16();$channels=$reader.ReadUInt16();$rate=$reader.ReadUInt32();$byteRate=$reader.ReadUInt32();$align=$reader.ReadUInt16();$bits=$reader.ReadUInt16()
                if($length -eq 18 -and $reader.ReadUInt16() -ne 0) {throw 'Extended Wave formats are not supported'}
                if($encoding -notin @(1,3) -or $channels -lt 1 -or $channels -gt 8 -or $rate -lt 1 -or $rate -gt 192000) {throw 'Unsupported Wave encoding, channel count or sample rate'}
                if(($encoding -eq 1 -and $bits -notin @(8,16,24,32)) -or ($encoding -eq 3 -and $bits -ne 32)) {throw 'Unsupported Wave sample width'}
                if($align -ne $channels*($bits/8) -or [long]$byteRate -ne [long]$rate*$align) {throw 'Inconsistent Wave block alignment or byte rate'}
                $formatFound=$true
            } elseif($tag -ceq 'data') {
                if($dataFound -or $length -eq 0) {throw 'Empty or duplicate Wave data chunk'}
                $dataLength=$length;$dataFound=$true
            }
            $stream.Position=$body+$length+$padding
        }
        if(!$formatFound -or !$dataFound -or $dataLength%$align -ne 0) {throw 'Missing Wave chunks or incomplete sample frame'}
    } catch {throw "Audio Wave validation failed: $Path ($($_.Exception.Message))"}
    finally {if($reader) {$reader.Dispose()} elseif($stream) {$stream.Dispose()}}
}
function Assert-StartupAudioDependencies {
    $scene=Read-BoundedJson $startupPath (4*1024*1024)
    # Legacy sample Scenes do not have generic EntityWorld components.
    if($null -eq $scene.PSObject.Properties['entities']) {return}
    if($scene.entities -isnot [array]) {throw 'Startup Scene entities must be an array'}
    $identityIndex=$null
    $validatedWaves=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach($entity in $scene.entities) {
        if($null -eq $entity.PSObject.Properties['audio']) {continue}
        $audio=$entity.audio
        if($audio -isnot [pscustomobject]) {throw 'Audio component must be an object'}
        foreach($field in @('enabled','loop','playOnAwake')) {
            if($null -ne $audio.PSObject.Properties[$field] -and $audio.$field -isnot [bool]) {throw "Audio $field must be boolean"}
        }
        if($null -ne $audio.PSObject.Properties['volume']) {
            if(!(Test-JsonNumber $audio.volume) -or [double]::IsNaN([double]$audio.volume) -or [double]::IsInfinity([double]$audio.volume) -or $audio.volume -lt 0 -or $audio.volume -gt 1) {throw 'Audio volume must be finite within 0..1'}
        }
        $hint='';$guid=''
        if($null -ne $audio.PSObject.Properties['asset']) {
            if($audio.asset -isnot [string] -or [Text.UTF8Encoding]::new($false,$true).GetByteCount($audio.asset) -gt 4096) {throw 'Audio asset must be a bounded UTF-8 string'}
            $hint=$audio.asset
            if($hint.Length -gt 0) {
                if(($hint -split '[/\\]') -contains '..') {throw 'Audio asset traversal is not supported'}
                try {$null=Resolve-ProjectPath $hint} catch {throw "Audio asset path invalid: $($_.Exception.Message)"}
            }
        }
        if($null -ne $audio.PSObject.Properties['guid']) {
            if($audio.guid -isnot [string] -or $audio.guid -cnotmatch '^(?:[0-9a-f]{16}|[0-9a-f]{32})$') {throw 'Audio GUID must be lowercase 16/32 hex characters'}
            $guid=$audio.guid
        }
        if(!$hint -and !$guid) {continue} # An unassigned draft is valid authoring state.
        if($guid) {
            if($null -eq $identityIndex) {$identityIndex=Get-AudioIdentityIndex}
            if(!$identityIndex.ContainsKey($guid)) {throw "Audio GUID source missing: $guid"}
            $source=$identityIndex[$guid]
        } else {$source=Resolve-ProjectPath $hint}
        if([IO.Path]::GetExtension($source) -ine '.wav') {throw "Audio source requires supported Wave: $source"}
        if(!(Test-Path -LiteralPath $source -PathType Leaf)) {throw "Audio source missing: $source"}
        Assert-NativeAudioPath $source
        $copied=$false
        foreach($relative in $sourceDirectories) {
            $root=Resolve-ProjectPath $relative
            if($source.StartsWith($root.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)) {$copied=$true;break}
        }
        if(!$copied) {throw "Audio source is not inside a copied asset directory: $source"}
        if($validatedWaves.Add($source)) {Assert-WaveStructure $source}
    }
}

function Get-ExportPrefabInventory {
    if(!(Test-Path -LiteralPath $assetPath)) {return}
    Assert-NativeAudioPath $assetPath 'Prefab asset root'
    $folders=[Collections.Generic.Queue[string]]::new()
    $folders.Enqueue($assetPath)
    $count=0
    while($folders.Count -gt 0) {
        $folder=$folders.Dequeue()
        foreach($entry in Get-ChildItem -LiteralPath $folder -Force -ErrorAction Stop) {
            if(++$count -gt 65536) {throw 'Prefab inventory exceeds 65536 entries'}
            # Match AssetRegistry root-level exclusions, including case sensitivity.
            if($folder -eq $assetPath -and $entry.Name -cin @('.kubo','.git','.vs','Library','Build','Builds','generated')) {continue}
            if(($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {throw "Linked Prefab inventory entry is not supported: $($entry.FullName)"}
            if($entry.PSIsContainer) {$folders.Enqueue($entry.FullName)}
            elseif($entry.Extension -ieq '.prefab') {$entry.FullName.Substring($projectRoot.TrimEnd('\').Length+1).Replace('\','/')}
        }
    }
}

function Assert-StartupModelAndGuidScriptDependencies {
    $scene=Read-BoundedJson $startupPath (4*1024*1024)
    $entities=@() # Legacy sample Scene still needs its exported Prefabs checked.
    if($null -ne $scene.PSObject.Properties['entities']) {
        if($scene.entities -isnot [array]) {throw 'Startup Scene entities must be an array'}
        $entities=$scene.entities
    }
    $identityIndex=$null
    $modelSources=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $controllerGuids=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    $withUI=$false
    $withMaterials=$false
    foreach($entity in $entities) {
        if($entity -isnot [pscustomobject]) {throw 'Startup Scene entity must be an object'}
        if($entity.material -is [pscustomobject] -and $null -ne $entity.material.PSObject.Properties['guid']) {$withMaterials=$true}
        if($null -ne $entity.PSObject.Properties['canvas'] -or $null -ne $entity.PSObject.Properties['sprite'] -or $null -ne $entity.PSObject.Properties['particle']) {$withUI=$true}
        $references=New-Object 'System.Collections.Generic.List[object]'
        if($entity.animator -is [pscustomobject] -and $null -ne $entity.animator.PSObject.Properties['controllerGuid']) {
            if($entity.animator.controllerGuid -isnot [string]) {throw 'Controller GUID must be a string'}
            if($entity.animator.controllerGuid.Length -gt 0) {
                $references.Add(@{Label='Controller';Hint='';Guid=$entity.animator.controllerGuid;HasHint=$false;HasGuid=$true})
            }
        }
        if($null -ne $entity.PSObject.Properties['model'] -or $null -ne $entity.PSObject.Properties['modelGuid']) {
            $references.Add(@{Label='Model';Hint=$entity.model;Guid=$entity.modelGuid;HasHint=($null -ne $entity.PSObject.Properties['model']);HasGuid=($null -ne $entity.PSObject.Properties['modelGuid'])})
        }
        # Path-only scripts may name native callbacks registered by the game.
        # GUID references are unambiguously registered file assets.
        if($entity.script -is [pscustomobject] -and $null -ne $entity.script.PSObject.Properties['guid']) {
            $references.Add(@{Label='Script';Hint=$entity.script.asset;Guid=$entity.script.guid;HasHint=($null -ne $entity.script.PSObject.Properties['asset']);HasGuid=$true})
        }
        foreach($reference in $references) {
            $label=$reference.Label;$hint='';$guid=''
            if($reference.HasHint) {
                if($reference.Hint -isnot [string] -or [Text.UTF8Encoding]::new($false,$true).GetByteCount($reference.Hint) -gt 4096) {throw "$label asset must be a bounded UTF-8 string"}
                $hint=$reference.Hint
                if($hint.Length -gt 0) {
                    if(($hint -split '[/\\]') -contains '..') {throw "$label asset traversal is not supported"}
                    $null=Resolve-ProjectPath $hint
                }
            }
            if($reference.HasGuid) {
                if($reference.Guid -isnot [string] -or $reference.Guid -cnotmatch '^(?:[0-9a-f]{16}|[0-9a-f]{32})$') {throw "$label GUID must be lowercase 16/32 hex characters"}
                $guid=$reference.Guid
            }
            if(!$hint -and !$guid) {continue}
            if($guid) {
                if($null -eq $identityIndex) {$identityIndex=Get-AudioIdentityIndex 'Asset'}
                if(!$identityIndex.ContainsKey($guid)) {throw "$label GUID source missing: $guid"}
                $source=$identityIndex[$guid]
                $extensions=if($label -eq 'Model') {@('.obj','.fbx','.gltf','.glb')} elseif($label -eq 'Controller') {@('.animfsm')} else {@('.kubo','.lua','.cs','.cpp','.h')}
                if([IO.Path]::GetExtension($source).ToLowerInvariant() -notin $extensions) {throw "$label GUID source has incorrect asset type: $source"}
            } else {$source=Resolve-ProjectPath $hint}
            if(!(Test-Path -LiteralPath $source -PathType Leaf)) {throw "$label source missing: $source"}
            Assert-NativeAudioPath $source $label
            $copied=$false
            foreach($relative in $sourceDirectories) {
                $root=Resolve-ProjectPath $relative
                if($source.StartsWith($root.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)) {$copied=$true;break}
            }
            if(!$copied) {throw "$label source is not inside a copied asset directory: $source"}
            if($label -eq 'Model') {$null=$modelSources.Add($source.Substring($projectRoot.TrimEnd('\').Length+1).Replace('\','/'))}
            if($label -eq 'Controller') {$null=$controllerGuids.Add($guid)}
            if($label -eq 'Script' -and (Get-Item -LiteralPath $source).Length -gt 1024*1024) {throw 'Script exceeds 1 MiB'}
        }
    }
    $prefabs=@(Get-ExportPrefabInventory)
    if($modelSources.Count -gt 0 -or $controllerGuids.Count -gt 0 -or $prefabs.Count -gt 0 -or $withUI -or $withMaterials) {
        # A prebuilt read-only tool reuses the Editor's actual dependency parser.
        # It does not require VS/compiler tools and never creates project metadata.
        $validator=Join-Path $PSScriptRoot 'KuboAssetValidator.exe'
        if(!(Test-Path -LiteralPath $validator -PathType Leaf)) {
            $validator=Join-Path $PSScriptRoot "..\..\generated\outputs\$Configuration\tools\KuboAssetValidator.exe"
        }
        if(!(Test-Path -LiteralPath $validator -PathType Leaf)) {throw 'Model dependency validator missing; deploy matching export tools'}
        $copyRoots=@($sourceDirectories | ForEach-Object {(Resolve-ProjectPath $_).Substring($projectRoot.TrimEnd('\').Length+1).Replace('\','/')})
        $requestValue=@{format='kubo-export-dependencies-1';root=$projectRoot;models=@($modelSources);copyRoots=$copyRoots}
        if($controllerGuids.Count -gt 0) {$requestValue.format='kubo-export-dependencies-2';$requestValue.project=(Join-Path $projectRoot ([IO.Path]::GetFileName($projectPath)));$requestValue.controllers=@($controllerGuids)}
        if($prefabs.Count -gt 0) {$requestValue.format='kubo-export-dependencies-3';$requestValue.project=(Join-Path $projectRoot ([IO.Path]::GetFileName($projectPath)));$requestValue.controllers=@($controllerGuids);$requestValue.prefabs=$prefabs}
        if($withUI) {$requestValue.format='kubo-export-dependencies-4';$requestValue.project=(Join-Path $projectRoot ([IO.Path]::GetFileName($projectPath)));$requestValue.controllers=@($controllerGuids);$requestValue.prefabs=$prefabs}
        # Format 5 requires a validator that understands shared Material references.
        if($withMaterials -or $prefabs.Count -gt 0) {$requestValue.format='kubo-export-dependencies-5';$requestValue.project=(Join-Path $projectRoot ([IO.Path]::GetFileName($projectPath)));$requestValue.controllers=@($controllerGuids);$requestValue.prefabs=$prefabs}
        $request=$requestValue | ConvertTo-Json -Depth 5
        $requestPath=Join-Path ([IO.Path]::GetTempPath()) ('kubo-export-dependencies-'+[guid]::NewGuid().ToString('N')+'.json')
        $requestHandle=$null;$requestOwned=$false
        try {
            $requestHandle=[IO.File]::Open($requestPath,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
            $requestOwned=$true
            $requestBytes=[Text.UTF8Encoding]::new($false,$true).GetBytes($request)
            if($requestBytes.Length -gt 4*1024*1024) {throw 'Model dependency request exceeds 4 MiB'}
            $requestHandle.Write($requestBytes,0,$requestBytes.Length);$requestHandle.Dispose();$requestHandle=$null
            & $validator --request $requestPath
            if($LASTEXITCODE -ne 0) {throw 'Model dependency validation failed; inspect the validator diagnostic above'}
        } finally {
            if($requestHandle) {$requestHandle.Dispose()}
            if($requestOwned -and (Test-Path -LiteralPath $requestPath -PathType Leaf)) {[IO.File]::Delete($requestPath)}
        }
    }
}

$projectPath = [IO.Path]::GetFullPath($ProjectFile)
if (-not (Test-Path -LiteralPath $projectPath -PathType Leaf)) { throw "Project file was not found: $projectPath" }
$projectRoot = Split-Path -Parent $projectPath
$manifestBytes = [IO.File]::ReadAllBytes($projectPath)
if($manifestBytes.Length -gt 1024*1024) {throw 'Project manifest exceeds 1 MiB'}
$manifestSha=[Security.Cryptography.SHA256]::Create()
try {$manifestDigest=[BitConverter]::ToString($manifestSha.ComputeHash($manifestBytes)).Replace('-','')} finally {$manifestSha.Dispose()}
$manifestText = [Text.UTF8Encoding]::new($false,$true).GetString($manifestBytes)
if($manifestText.Length -gt 0 -and $manifestText[0] -eq [char]0xfeff) {$manifestText=$manifestText.Substring(1)}
$manifest = $manifestText | ConvertFrom-Json
$gameOutputProperty=$manifest.PSObject.Properties['gameOutput']
if($null -ne $gameOutputProperty) {
    $gameOutput=$gameOutputProperty.Value
    if($gameOutput -isnot [pscustomobject]) {throw 'gameOutput must be an object'}
    foreach($field in @('width','height')) {
        $property=$gameOutput.PSObject.Properties[$field]
        if($null -eq $property) {throw 'gameOutput requires width and height'}
        $value=$property.Value
        if(!($value -is [int] -or $value -is [long] -or $value -is [double] -or $value -is [decimal]) -or
            [double]::IsNaN([double]$value) -or [double]::IsInfinity([double]$value) -or $value -lt 1 -or $value -gt 4096 -or [Math]::Floor([double]$value) -ne $value) {throw 'gameOutput dimensions must be integers from 1 to 4096'}
    }
    if([double]$gameOutput.width * [double]$gameOutput.height -gt 8388608) {throw 'gameOutput exceeds its pixel allocation limit'}
}
$luaLimitsProperty=$manifest.PSObject.Properties['luaLimits']
if($null -ne $luaLimitsProperty) {
    $luaLimits=$luaLimitsProperty.Value
    if($luaLimits -isnot [pscustomobject]) {throw 'luaLimits must be an object'}
    foreach($field in @('memoryBytes','states','frameInstructions')) {
        $property=$luaLimits.PSObject.Properties[$field]
        if($null -eq $property) {throw "luaLimits requires $field"}
        $value=$property.Value
        $maximum=switch($field){'memoryBytes'{1073741824};'states'{4096};'frameInstructions'{100000000}}
        if(!($value -is [int] -or $value -is [long] -or $value -is [double] -or $value -is [decimal]) -or
            [double]::IsNaN([double]$value) -or [double]::IsInfinity([double]$value) -or $value -lt 1 -or $value -gt $maximum -or [Math]::Floor([double]$value) -ne $value) {throw "luaLimits has invalid $field"}
    }
}
$packageName = if ([string]::IsNullOrWhiteSpace($manifest.name)) { [IO.Path]::GetFileNameWithoutExtension($projectPath) } else { $manifest.name }
if ($packageName -eq '.' -or $packageName -eq '..' -or $packageName.IndexOfAny([IO.Path]::GetInvalidFileNameChars()) -ge 0) { throw 'Invalid package name' }
$startupPath = Resolve-ProjectPath $manifest.startupScene
$assetPath = Resolve-ProjectPath $manifest.assetDirectory
if (-not (Test-Path -LiteralPath $startupPath -PathType Leaf)) { throw "Startup scene was not found: $startupPath" }
foreach($reserved in @('KuboPlayer.exe','dxcompiler.dll','dxil.dll','package.json',([IO.Path]::GetFileName($projectPath)))) {
    if($startupPath -ieq (Join-Path $projectRoot $reserved)) {throw 'Startup Scene conflicts with a package-owned root file'}
}
if($NativeBuildRecord -and $startupPath -ieq (Join-Path $projectRoot 'native-build.json')) {throw 'Startup Scene conflicts with the native build record'}

if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $projectRoot (Join-Path 'Builds' $packageName)
}
$outputPath = [IO.Path]::GetFullPath($OutputDirectory)
if ($outputPath.TrimEnd('\') -eq $projectRoot.TrimEnd('\') -or $projectRoot.StartsWith($outputPath.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'OutputDirectory must not contain the source project' }
if ((Test-Path -LiteralPath $outputPath) -and @(Get-ChildItem -LiteralPath $outputPath -Force).Count -gt 0 -and !(Test-Path -LiteralPath (Get-KuboPackageJournalPath $outputPath))) { throw 'OutputDirectory must be empty; choose a new export directory' }

if ([string]::IsNullOrWhiteSpace($RuntimeExecutable)) {
    $engineRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
    $RuntimeExecutable = Join-Path $engineRoot (Join-Path 'generated\outputs' (Join-Path $Configuration 'KuboPlayer.exe'))
}
$runtimePath = [IO.Path]::GetFullPath($RuntimeExecutable)
if (-not (Test-Path -LiteralPath $runtimePath -PathType Leaf)) { throw "Runtime executable was not found: $runtimePath" }
$sourceDirectories = @('Resources', 'Assets', 'UserScenes', 'Scripts', $manifest.assetDirectory) | Select-Object -Unique
function Assert-NoPendingPrefabTransactions([string]$source) {
    $metadata = Join-Path $source '.kubo'
    if (Test-Path -LiteralPath $metadata) {
        $metadataItem = Get-Item -LiteralPath $metadata -Force -ErrorAction Stop
        if (-not $metadataItem.PSIsContainer -or ($metadataItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw "Pending Prefab metadata namespace is unsafe; preserve data before export: $metadata" }
    }
    $pending = Join-Path $source '.kubo\prefab-transactions'
    if (-not (Test-Path -LiteralPath $pending)) { return }
    $item = Get-Item -LiteralPath $pending -Force -ErrorAction Stop
    if (-not $item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw "Pending Prefab namespace is unsafe; preserve data before export: $pending" }
    $entries = [IO.Directory]::EnumerateFileSystemEntries($pending).GetEnumerator()
    try { if ($entries.MoveNext()) { throw "Pending Prefab source transaction requires recovery before export: $pending" } }
    finally { $entries.Dispose() }
}
foreach ($relative in $sourceDirectories) {
    $source = Resolve-ProjectPath $relative
    Assert-NoPendingPrefabTransactions $source
    if (Test-Path -LiteralPath (Join-Path $source '.kubo\asset-move.pending.json')) { throw "Pending asset move requires recovery before export: $source" }
    if ($outputPath -eq $source -or $outputPath.StartsWith($source.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'OutputDirectory must not be inside a copied asset directory' }
}
Assert-StartupAudioDependencies
Assert-StartupModelAndGuidScriptDependencies
$startupDocument = Read-BoundedJson $startupPath (4*1024*1024)
$legacyGameplay = $null -eq $startupDocument.PSObject.Properties['entities'] -and $startupDocument.scene -ceq 'GAMEPLAY'
foreach ($dependency in @('dxcompiler.dll', 'dxil.dll')) {
    if (-not (Test-Path -LiteralPath (Join-Path (Split-Path -Parent $runtimePath) $dependency) -PathType Leaf)) { throw "Runtime dependency was not found: $dependency" }
}
foreach ($relative in @('Resources\Shaders', 'Resources\textures\skybox\test.dds', 'Resources\textures\common\white.png', 'ThirdPartyLicenses\Lua-5.4.9.txt')) {
    if (-not (Test-Path -LiteralPath (Join-Path (Split-Path -Parent $runtimePath) $relative))) { throw "Engine resource was not found: $relative" }
}
$publicOutputPath=$outputPath
$publication=Start-KuboPackagePublication $publicOutputPath
if($publication.Published) {Write-Output "Kubo runtime package recovered: $publicOutputPath";return}
try {
$outputPath=$publication.Stage
$runtimeDigest=(Get-FileHash -LiteralPath $runtimePath -Algorithm SHA256).Hash
$dependencyDigests=@{}
foreach($dependency in @('dxcompiler.dll','dxil.dll')) {$dependencyDigests[$dependency]=(Get-FileHash -LiteralPath (Join-Path (Split-Path -Parent $runtimePath) $dependency) -Algorithm SHA256).Hash}
Copy-Item -LiteralPath $runtimePath -Destination (Join-Path $outputPath 'KuboPlayer.exe') -Force
foreach ($dependency in @('dxcompiler.dll', 'dxil.dll')) {
    $dependencyPath = Join-Path (Split-Path -Parent $runtimePath) $dependency
    if (-not (Test-Path -LiteralPath $dependencyPath -PathType Leaf)) { throw "Runtime dependency was not found: $dependencyPath" }
    Copy-Item -LiteralPath $dependencyPath -Destination (Join-Path $outputPath $dependency) -Force
}
Copy-Item -LiteralPath $projectPath -Destination (Join-Path $outputPath ([IO.Path]::GetFileName($projectPath))) -Force

# Legacy Gameplay loads bundled sample assets by name. Project content is copied
# afterwards so its overrides keep the same precedence as in the Editor.
$builtins = @('Resources\Shaders', 'Resources\textures\skybox\test.dds', 'Resources\textures\common\white.png')
if ($legacyGameplay) { $builtins = @('Resources') }
foreach ($relative in ($builtins + 'ThirdPartyLicenses\Lua-5.4.9.txt')) {
    $source = Join-Path (Split-Path -Parent $runtimePath) $relative
    if (-not (Test-Path -LiteralPath $source)) { throw "Engine resource was not found: $source" }
    $destination = Join-Path $outputPath $relative
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
    Copy-Item -LiteralPath $source -Destination $destination -Recurse -Force
}

foreach ($directoryName in $sourceDirectories) {
    $source = Resolve-ProjectPath $directoryName
    if (Test-Path -LiteralPath $source -PathType Container) {
        # Copy contents to an explicitly created destination to avoid nested folders.
        $destination = Join-Path $outputPath $directoryName
        New-Item -ItemType Directory -Force -Path $destination | Out-Null
        foreach ($entry in Get-ChildItem -LiteralPath $source -Force) {
            Copy-Item -LiteralPath $entry.FullName -Destination $destination -Recurse -Force
        }
    }
}
$startupDestination = Join-Path $outputPath $manifest.startupScene
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $startupDestination) | Out-Null
Copy-Item -LiteralPath $startupPath -Destination $startupDestination -Force

# Include only gameplay input bindings, not Launcher history or editor layout.
$inputBindings = Resolve-ProjectPath 'UserSettings\input_bindings.json'
if (Test-Path -LiteralPath $inputBindings -PathType Leaf) {
    $inputDestination = Join-Path $outputPath 'UserSettings\input_bindings.json'
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $inputDestination) | Out-Null
    Copy-Item -LiteralPath $inputBindings -Destination $inputDestination -Force
}

$packageManifest = [ordered]@{
    format = 'kubo-runtime-package-1'
    name = $packageName
    engineVersion = $manifest.engineVersion
    startupScene = $manifest.startupScene
    runtime = 'KuboPlayer.exe'
    configuration = $Configuration
}
$packageManifest | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $outputPath 'package.json') -Encoding UTF8
$candidateFolders=[Collections.Generic.Queue[string]]::new();$candidateFolders.Enqueue($outputPath)
while($candidateFolders.Count -gt 0) {
    foreach($candidateEntry in Get-ChildItem -LiteralPath $candidateFolders.Dequeue() -Force) {
        if(($candidateEntry.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {throw 'Package candidate contains a linked entry; publication refused'}
        if($candidateEntry.PSIsContainer) {$candidateFolders.Enqueue($candidateEntry.FullName)}
    }
}
if((Get-FileHash -LiteralPath (Join-Path $outputPath 'KuboPlayer.exe') -Algorithm SHA256).Hash -ne $runtimeDigest) {throw 'Copied project content replaced or changed the runtime executable'}
foreach($dependency in @('dxcompiler.dll','dxil.dll')) {if((Get-FileHash -LiteralPath (Join-Path $outputPath $dependency) -Algorithm SHA256).Hash -ne $dependencyDigests[$dependency]) {throw 'Copied project content replaced a runtime dependency'}}
if((Get-FileHash -LiteralPath (Join-Path $outputPath ([IO.Path]::GetFileName($projectPath))) -Algorithm SHA256).Hash -ne $manifestDigest) {throw 'Project manifest changed during package assembly'}
# Repeat reference validation against the actual copied candidate.
$originalProjectRoot=$projectRoot;$originalStartupPath=$startupPath;$originalAssetPath=$assetPath
try {
    $projectRoot=$outputPath;$startupPath=Join-Path $outputPath $manifest.startupScene;$assetPath=Join-Path $outputPath $manifest.assetDirectory
    Assert-StartupAudioDependencies
    Assert-StartupModelAndGuidScriptDependencies
} finally {$projectRoot=$originalProjectRoot;$startupPath=$originalStartupPath;$assetPath=$originalAssetPath}
if($NativeBuildRecord) {
    if($NativeBuildRecord.format -cne 'kubo-native-game-build-1' -or $NativeBuildRecord.configuration -cne $Configuration -or $NativeBuildRecord.runtimeSha256 -cne $runtimeDigest.ToLowerInvariant()) {throw 'Native build record does not match the staged runtime'}
    $NativeBuildRecord | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $outputPath 'native-build.json') -Encoding UTF8
}
foreach ($relative in $sourceDirectories) { Assert-NoPendingPrefabTransactions (Resolve-ProjectPath $relative) }
Complete-KuboPackagePublication $publication
Write-Output "Kubo runtime package created: $publicOutputPath"
} finally {Close-KuboPackagePublication $publication}
