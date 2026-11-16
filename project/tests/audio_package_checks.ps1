[CmdletBinding()]
param([string]$Packager,[string]$NativeValidator)
$ErrorActionPreference='Stop'
if([string]::IsNullOrWhiteSpace($Packager)) {$Packager=Join-Path $PSScriptRoot '..\tools\package_project.ps1'}
$audioFixture=Join-Path ([IO.Path]::GetTempPath()) ('kubo-audio-package-'+[guid]::NewGuid().ToString('N'))
$audioProject=Join-Path $audioFixture 'Project'
$audioEngine=Join-Path $audioFixture 'Engine'
$audioPackager=[IO.Path]::GetFullPath($Packager)
function Require($condition,$message) {if(!$condition) {throw $message}}
function Export-Audio([string]$name) {
    & $audioPackager -ProjectFile (Join-Path $audioProject 'Sample.kubo') -RuntimeExecutable (Join-Path $audioEngine 'KuboPlayer.exe') -OutputDirectory (Join-Path $audioFixture $name)
}
function Save-AudioScene($component) {
    $entity=@{id=1;parent=0;name='Audio';active=$true;model='';transform=@{position=@(0,0,0);rotation=@(0,0,0);scale=@(1,1,1)};audio=$component}
    [IO.File]::WriteAllText((Join-Path $audioProject 'entry.scene'),(@{version=1;entities=@($entity)} | ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($true))
}
function Reject-Audio($component,[string]$name,[string]$diagnostic) {
    Save-AudioScene $component
    $audioRejected=$false
    try {Export-Audio $name} catch {$audioRejected=$true;Require ($_.Exception.Message -match $diagnostic) ('Unexpected error: '+$_.Exception.Message)}
    Require $audioRejected ('Broken Audio export accepted: '+$name)
    Require (!(Test-Path -LiteralPath (Join-Path $audioFixture $name))) ('Rejected Audio export created files: '+$name)
}
function New-WaveChunk([string]$Tag,[byte[]]$Body) {
    $memory=[IO.MemoryStream]::new();$writer=[IO.BinaryWriter]::new($memory)
    try {$writer.Write([Text.Encoding]::ASCII.GetBytes($Tag));$writer.Write([uint32]$Body.Length);$writer.Write($Body);if($Body.Length%2) {$writer.Write([byte]0)};$writer.Flush();return ,$memory.ToArray()} finally {$writer.Dispose();$memory.Dispose()}
}
function New-WaveFormat([int]$Encoding=1,[int]$Bits=16,[int]$Channels=1,[int]$Rate=8000,[bool]$Extended=$false) {
    $memory=[IO.MemoryStream]::new();$writer=[IO.BinaryWriter]::new($memory)
    try {
        $align=[int]($Channels*$Bits/8)
        $writer.Write([uint16]$Encoding);$writer.Write([uint16]$Channels);$writer.Write([uint32]$Rate);$writer.Write([uint32]($Rate*$align));$writer.Write([uint16]$align);$writer.Write([uint16]$Bits)
        if($Extended) {$writer.Write([uint16]0)};$writer.Flush();return ,(New-WaveChunk 'fmt ' $memory.ToArray())
    } finally {$writer.Dispose();$memory.Dispose()}
}
function New-WaveRiff([byte[]]$Chunks) {
    $memory=[IO.MemoryStream]::new();$writer=[IO.BinaryWriter]::new($memory)
    try {$writer.Write([Text.Encoding]::ASCII.GetBytes('RIFF'));$writer.Write([uint32]($Chunks.Length+4));$writer.Write([Text.Encoding]::ASCII.GetBytes('WAVE'));$writer.Write($Chunks);$writer.Flush();return ,$memory.ToArray()} finally {$writer.Dispose();$memory.Dispose()}
}
try {
    foreach($folder in @($audioProject,$audioEngine,(Join-Path $audioProject 'Content'),(Join-Path $audioEngine 'Resources\Shaders'),(Join-Path $audioEngine 'Resources\textures\skybox'),(Join-Path $audioEngine 'Resources\textures\common'),(Join-Path $audioEngine 'ThirdPartyLicenses'))) {New-Item -ItemType Directory -Path $folder -Force | Out-Null}
    foreach($file in @('KuboPlayer.exe','dxcompiler.dll','dxil.dll','Resources\Shaders\test.hlsl','Resources\textures\skybox\test.dds','Resources\textures\common\white.png','ThirdPartyLicenses\Lua-5.4.9.txt')) {[IO.File]::WriteAllText((Join-Path $audioEngine $file),'fixture only')}
    [IO.File]::WriteAllText((Join-Path $audioProject 'Sample.kubo'),'{"name":"Audio","engineVersion":"0.1.0","startupScene":"entry.scene","assetDirectory":"Content"}')
    $audioName=(-join ([char[]]@(0x97f3,0x58f0)))+'.wav'
    $audioWave=Join-Path $audioProject ('Content/'+$audioName)
    [IO.File]::WriteAllBytes($audioWave,[byte[]]@(82,73,70,70,40,0,0,0,87,65,86,69,102,109,116,32,16,0,0,0,1,0,1,0,64,31,0,0,128,62,0,0,2,0,16,0,100,97,116,97,4,0,0,0,0,0,0,0))
    $audioGuid='aabbccddeeff00112233445566778899'
    $audioMeta=$audioWave+'.kubo-meta.json'
    $audioIdentity='{"version":1,"guid":"'+$audioGuid+'"}'
    [IO.File]::WriteAllText($audioMeta,$audioIdentity)
    $audioComponent=@{asset='Content/absent.wav';guid=$audioGuid;enabled=$true;playOnAwake=$true;loop=$true;volume=0}
    $audioMissing=$audioComponent.Clone();$audioMissing.guid='00000000000000000000000000000000'
    Reject-Audio $audioMissing 'MissingGuid' 'Audio.*GUID'
    Save-AudioScene $audioComponent;Export-Audio 'ValidGuid'
    foreach($file in @($audioWave,$audioMeta,(Join-Path $audioProject 'entry.scene'))) {
        $relative=$file.Substring($audioProject.Length+1)
        Require ((Get-FileHash -LiteralPath $file).Hash -eq (Get-FileHash -LiteralPath (Join-Path $audioFixture ('ValidGuid/'+$relative))).Hash) 'Audio export changed source or identity bytes'
    }
    [IO.File]::WriteAllText((Join-Path $audioProject 'Content/absent.wav'),'old-path competitor')
    $audioMetaBad='{"version":true,"guid":"'+$audioGuid+'"}'
    [IO.File]::WriteAllText($audioMeta,$audioMetaBad)
    Reject-Audio $audioComponent 'BadMeta' 'identity'
    [IO.File]::WriteAllText($audioMeta,$audioIdentity,[Text.UTF8Encoding]::new($true))
    Reject-Audio $audioComponent 'MetaBom' 'identity'
    [IO.File]::WriteAllText($audioMeta,(' '*16385))
    Reject-Audio $audioComponent 'MetaSize' 'identity'
    [IO.File]::WriteAllText($audioMeta,$audioIdentity)
    [IO.File]::WriteAllText((Join-Path $audioProject 'Content/duplicate.wav'),'duplicate')
    [IO.File]::WriteAllText((Join-Path $audioProject 'Content/duplicate.wav.kubo-meta.json'),$audioIdentity)
    Reject-Audio $audioComponent 'Duplicate' 'Duplicate.*GUID'
    # Only these exact test-owned duplicate files are removed to test recovery.
    Remove-Item -LiteralPath (Join-Path $audioProject 'Content/duplicate.wav'),(Join-Path $audioProject 'Content/duplicate.wav.kubo-meta.json')
    $audioPathOnly=@{asset=('Content/'+$audioName);enabled=$false;playOnAwake=$false;volume=0.5}
    Save-AudioScene $audioPathOnly;Export-Audio 'PathOnly'
    Save-AudioScene @{};Export-Audio 'Draft'
    $audioCaseIndex=0
    foreach($invalidAudio in @(@{asset='Content/missing.wav'},@{asset='../outside.wav'},@{asset='Content/sub/../sound.wav'},@{asset='C:relative.wav'},@{asset=123},@{asset='Content/'+$audioName;volume=-1e-100},@{asset='Content/'+$audioName;volume=1.000000001},@{asset='Content/'+$audioName;loop='true'},@{asset='Content/'+$audioName;guid=''},@{asset='Content/'+$audioName;enabled=1},'not an object')) {
        $audioCaseIndex++;Reject-Audio $invalidAudio ('Invalid'+$audioCaseIndex) 'Audio'
    }
    [IO.File]::WriteAllBytes((Join-Path $audioProject 'outside.wav'),[IO.File]::ReadAllBytes($audioWave))
    Reject-Audio @{asset='outside.wav'} 'NotCopied' 'Audio.*copied'
    $audioWrongType=$audioComponent.Clone();$audioWrongType.guid='0123456789abcdef0123456789abcdef'
    [IO.File]::WriteAllText((Join-Path $audioProject 'Content/not-audio.obj'),'v 0 0 0')
    [IO.File]::WriteAllText((Join-Path $audioProject 'Content/not-audio.obj.kubo-meta.json'),'{"version":1,"guid":"0123456789abcdef0123456789abcdef"}')
    Reject-Audio $audioWrongType 'WrongType' 'Audio.*Wave'
    $audioWaveBytes=[IO.File]::ReadAllBytes($audioWave)
    [IO.File]::WriteAllBytes($audioWave,[byte[]]@(82,73,70,70))
    Reject-Audio $audioPathOnly 'WaveTooShort' 'Audio.*size'
    [IO.File]::WriteAllBytes($audioWave,$audioWaveBytes)
    $audioHash=(Get-FileHash -LiteralPath $audioWave).Hash
    Save-AudioScene $audioComponent;Export-Audio 'Recovery'
    Require ((Get-FileHash -LiteralPath $audioWave).Hash -eq $audioHash -and [IO.File]::ReadAllText($audioMeta) -eq $audioIdentity) 'Export mutated Audio source/metadata'
    if($NativeValidator) {Require (Test-Path -LiteralPath $NativeValidator -PathType Leaf) 'Build wave_file_checks before requesting native parity'}
    $waveCases=@()
    $fmt=New-WaveFormat;$data=New-WaveChunk 'data' ([byte[]]@(0,0,0,0))
    foreach($bits in @(8,16,24,32)) {$waveCases+=@{Name=('Pcm'+$bits);Accept=$true;Bytes=(New-WaveRiff ((New-WaveFormat 1 $bits)+(New-WaveChunk 'data' (New-Object byte[] ([int]($bits/8*2))))))}}
    $waveCases+=@{Name='Float32';Accept=$true;Bytes=(New-WaveRiff ((New-WaveFormat 3 32)+$data))}
    $waveCases+=@{Name='EightChannels';Accept=$true;Bytes=(New-WaveRiff ((New-WaveFormat 1 16 8 192000)+(New-WaveChunk 'data' (New-Object byte[] 32))))}
    $waveCases+=@{Name='Fmt18';Accept=$true;Bytes=(New-WaveRiff ((New-WaveFormat 1 16 1 8000 $true)+$data))}
    $waveCases+=@{Name='OddUnknownBeforeDataFmt';Accept=$true;Bytes=(New-WaveRiff ((New-WaveChunk 'JUNK' ([byte[]]@(1)))+$data+$fmt))}
    foreach($field in @(@{Name='Signature';At=0;Value=88},@{Name='RiffExtent';At=4;Value=255},@{Name='FormatExtent';At=16;Value=255},@{Name='Align';At=32;Value=3},@{Name='ByteRate';At=28;Value=1},@{Name='Channels';At=22;Value=0},@{Name='Rate';At=24;Value=0},@{Name='DataExtent';At=40;Value=255})) {
        $bytes=[byte[]]$audioWaveBytes.Clone();$bytes[$field.At]=[byte]$field.Value
        if($field.Name -eq 'Rate') {$bytes[25]=0}
        $waveCases+=@{Name=$field.Name;Accept=$false;Bytes=$bytes}
    }
    foreach($case in @(@{Name='DuplicateFormat';Chunks=($fmt+$fmt+$data)},@{Name='DuplicateData';Chunks=($fmt+$data+$data)},@{Name='MissingFormat';Chunks=$data},@{Name='MissingData';Chunks=$fmt},@{Name='Extensible';Chunks=((New-WaveFormat 65534)+$data)},@{Name='Float16';Chunks=((New-WaveFormat 3 16)+$data)},@{Name='EmptyData';Chunks=($fmt+(New-WaveChunk 'data' ([byte[]]@())))},@{Name='IncompleteFrame';Chunks=($fmt+(New-WaveChunk 'data' ([byte[]]@(1))))},@{Name='ShortChunkHeader';Chunks=($fmt+$data+[byte[]]@(1,2,3,4))})) {
        $waveCases+=@{Name=$case.Name;Accept=$false;Bytes=(New-WaveRiff $case.Chunks)}
    }
    $odd=New-WaveChunk 'JUNK' ([byte[]]@(1));$waveCases+=@{Name='MissingOddPad';Accept=$false;Bytes=(New-WaveRiff ($fmt+$data+$odd[0..($odd.Length-2)]))}
    $fmt18=New-WaveFormat 1 16 1 8000 $true;$fmt18[24]=1
    $waveCases+=@{Name='NonzeroExtension';Accept=$false;Bytes=(New-WaveRiff ($fmt18+$data))}
    $waveCases+=@{Name='TrailingBytes';Accept=$false;Bytes=($audioWaveBytes+[byte[]]@(0))}
    foreach($waveCase in $waveCases) {
        [IO.File]::WriteAllBytes($audioWave,$waveCase.Bytes)
        if($NativeValidator) {
            $probeOutput=& $NativeValidator $audioWave
            Require (($LASTEXITCODE -eq 0 -and $probeOutput -eq 'ACCEPT' -and $waveCase.Accept) -or ($LASTEXITCODE -eq 2 -and $probeOutput -eq 'REJECT' -and !$waveCase.Accept)) ('Native Wave verdict mismatch: '+$waveCase.Name)
        }
        if($waveCase.Accept) {Save-AudioScene $audioPathOnly;Export-Audio ('Wave'+$waveCase.Name)}
        else {Reject-Audio $audioPathOnly ('Wave'+$waveCase.Name) 'Audio.*Wave'}
    }
    [IO.File]::WriteAllBytes($audioWave,$audioWaveBytes)
    Write-Output ('PASS: '+$waveCases.Count+' supported/malformed Wave export cases'+$(if($NativeValidator){' match actual native WaveFile acceptance'}else{' (native parity not requested)'}))
    $audioRawIndex=0
    foreach($invalidScene in @([byte[]]@(0xff,0xfe),[Text.Encoding]::UTF8.GetBytes('{"entities":null}'),[Text.Encoding]::UTF8.GetBytes(('{'*129)),[Text.Encoding]::UTF8.GetBytes((' '*4194305)))) {
        $audioRawIndex++
        [IO.File]::WriteAllBytes((Join-Path $audioProject 'entry.scene'),$invalidScene)
        $rawRejected=$false;$rawOutput='InvalidScene'+$audioRawIndex
        try {Export-Audio $rawOutput} catch {$rawRejected=$true}
        Require ($rawRejected -and !(Test-Path -LiteralPath (Join-Path $audioFixture $rawOutput))) 'Invalid bounded startup JSON reached export creation'
    }
    [IO.File]::WriteAllText((Join-Path $audioProject 'entry.scene'),'{"version":1,"scene":"GamePlay","settings":{"speed":1}}')
    Export-Audio 'LegacyObject'
    Require ((Get-FileHash -LiteralPath (Join-Path $audioProject 'entry.scene')).Hash -eq (Get-FileHash -LiteralPath (Join-Path $audioFixture 'LegacyObject/entry.scene')).Hash) 'Audio preflight changed a non-generic Scene object'
    Write-Output 'PASS: startup Audio GUID/path and Wave structure export preflight, Unicode/BOM/hash preservation, draft/disabled settings, missing/duplicate/wrong identity, old-path refusal, strict settings and copied-root isolation (not playback or full Scene validation)'
} finally {
    $audioResolved=[IO.Path]::GetFullPath($audioFixture)
    $audioTempRoot=[IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')+'\'
    if(!$audioResolved.StartsWith($audioTempRoot,[StringComparison]::OrdinalIgnoreCase) -or !(Split-Path -Leaf $audioResolved).StartsWith('kubo-audio-package-')) {throw 'Unsafe Audio fixture cleanup'}
    if(Test-Path -LiteralPath $audioResolved) {Remove-Item -LiteralPath $audioResolved -Recurse -Force}
}
