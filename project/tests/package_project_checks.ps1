$ErrorActionPreference = 'Stop'
$fixture = Join-Path ([IO.Path]::GetTempPath()) ('kubo-package-check-' + [Guid]::NewGuid().ToString('N'))
$project = Join-Path $fixture 'Project'
$runtime = Join-Path $fixture 'Engine'
$output = Join-Path $fixture 'Export'
$packager = Join-Path $PSScriptRoot '..\tools\package_project.ps1'
function Require([bool]$Condition, [string]$Message) { if (-not $Condition) { throw $Message } }
try {
    foreach ($directory in @($project, $runtime, (Join-Path $project 'Content'), (Join-Path $runtime 'Resources\Shaders'), (Join-Path $runtime 'Resources\textures\skybox'), (Join-Path $runtime 'Resources\textures\common'), (Join-Path $runtime 'ThirdPartyLicenses'))) {
        New-Item -ItemType Directory -Path $directory -Force | Out-Null
    }
    [IO.File]::WriteAllText((Join-Path $project 'Sample.kubo'), '{"name":"Sample","startupScene":"entry.scene","assetDirectory":"Content","engineVersion":"0.1.0","gameOutput":{"width":512,"height":768},"luaLimits":{"memoryBytes":12345678,"states":19,"frameInstructions":234567,"extension":{"keep":true}}}')
    [IO.File]::WriteAllText((Join-Path $project 'entry.scene'), '{"version":1,"entities":[]}')
    [IO.File]::WriteAllText((Join-Path $project 'Content\asset.txt'), 'custom asset payload')
    $identity = '{"version":1,"guid":"1234567890abcdef1234567890abcdef"}'
    [IO.File]::WriteAllText((Join-Path $project 'Content\asset.txt.kubo-meta.json'), $identity)
    New-Item -ItemType Directory -Path (Join-Path $project 'UserSettings') | Out-Null
    $bindings = '{"version":1,"actions":{"move":[{"key":17,"contribution":[0,0,1]}]}}'
    [IO.File]::WriteAllText((Join-Path $project 'UserSettings\input_bindings.json'), $bindings)
    [IO.File]::WriteAllText((Join-Path $project 'UserSettings\editor-layout.json'), 'must not travel')
    [IO.File]::WriteAllText((Join-Path $project 'UserSettings\native-build.json'), '{"sdk":"private machine path","native":true}')
    [IO.File]::WriteAllText((Join-Path $project 'UserSettings\native-build.json.lock'), '')
    foreach ($file in @('KuboPlayer.exe','dxcompiler.dll','dxil.dll','Resources\Shaders\test.hlsl','Resources\textures\skybox\test.dds','Resources\textures\common\white.png','ThirdPartyLicenses\Lua-5.4.9.txt')) {
        [IO.File]::WriteAllText((Join-Path $runtime $file), 'test fixture, not an executable')
    }
    & $packager -ProjectFile (Join-Path $project 'Sample.kubo') -RuntimeExecutable (Join-Path $runtime 'KuboPlayer.exe') -OutputDirectory $output
    foreach ($file in @('entry.scene','Content\asset.txt','Content\asset.txt.kubo-meta.json','Resources\Shaders\test.hlsl','Resources\textures\skybox\test.dds','Resources\textures\common\white.png','ThirdPartyLicenses\Lua-5.4.9.txt','dxcompiler.dll','dxil.dll','Sample.kubo','package.json')) {
        Require (Test-Path -LiteralPath (Join-Path $output $file) -PathType Leaf) "Missing package file: $file"
    }
    Require ([IO.File]::ReadAllText((Join-Path $output 'Content\asset.txt.kubo-meta.json')) -eq $identity) 'Export changed the persistent asset identity'
    Require ((Get-FileHash -LiteralPath (Join-Path $project 'Sample.kubo')).Hash -eq (Get-FileHash -LiteralPath (Join-Path $output 'Sample.kubo')).Hash) 'Export modified project Game output settings'
    $validOutputManifest=[IO.File]::ReadAllText((Join-Path $project 'Sample.kubo'))
    $invalidOutputIndex=0
    foreach($invalidOutput in @('null','{}','{"width":true,"height":1}','{"width":1.5,"height":2}','{"width":4096,"height":4096}','{"width":0,"height":2}')) {
        $invalidOutputIndex++
        [IO.File]::WriteAllText((Join-Path $project 'Sample.kubo'),('{"name":"Sample","startupScene":"entry.scene","assetDirectory":"Content","gameOutput":'+$invalidOutput+'}'))
        $invalidOutputPath=Join-Path $fixture ('InvalidOutput'+$invalidOutputIndex)
        $invalidRejected=$false
        try { & $packager -ProjectFile (Join-Path $project 'Sample.kubo') -RuntimeExecutable (Join-Path $runtime 'KuboPlayer.exe') -OutputDirectory $invalidOutputPath } catch {$invalidRejected=$true}
        Require ($invalidRejected -and !(Test-Path -LiteralPath $invalidOutputPath)) 'Invalid Game output reached export file creation'
    }
    [IO.File]::WriteAllText((Join-Path $project 'Sample.kubo'),$validOutputManifest)
    $invalidLuaIndex=0
    foreach($invalidLua in @('null','false','[]','{}','{"memoryBytes":1}',
        '{"memoryBytes":true,"states":1,"frameInstructions":1}',
        '{"memoryBytes":1,"states":"1","frameInstructions":1}',
        '{"memoryBytes":1,"states":1,"frameInstructions":1.5}',
        '{"memoryBytes":0,"states":1,"frameInstructions":1}',
        '{"memoryBytes":1073741825,"states":1,"frameInstructions":1}',
        '{"memoryBytes":1,"states":0,"frameInstructions":1}',
        '{"memoryBytes":1,"states":4097,"frameInstructions":1}',
        '{"memoryBytes":1,"states":1,"frameInstructions":0}',
        '{"memoryBytes":1,"states":1,"frameInstructions":100000001}',
        '{"memoryBytes":1e100,"states":1,"frameInstructions":1}')) {
        $invalidLuaIndex++
        [IO.File]::WriteAllText((Join-Path $project 'Sample.kubo'),('{"name":"Sample","startupScene":"entry.scene","assetDirectory":"Content","luaLimits":'+$invalidLua+'}'))
        $invalidLuaPath=Join-Path $fixture ('InvalidLua'+$invalidLuaIndex)
        $invalidRejected=$false
        try { & $packager -ProjectFile (Join-Path $project 'Sample.kubo') -RuntimeExecutable (Join-Path $runtime 'KuboPlayer.exe') -OutputDirectory $invalidLuaPath } catch {$invalidRejected=$_.Exception.Message -match 'luaLimits'}
        Require ($invalidRejected -and !(Test-Path -LiteralPath $invalidLuaPath)) 'Invalid Lua limits reached export publication or failed for an unrelated reason'
    }
    [IO.File]::WriteAllText((Join-Path $project 'Sample.kubo'),$validOutputManifest)
    Require ((Get-FileHash -LiteralPath (Join-Path $project 'Sample.kubo')).Hash -eq (Get-FileHash -LiteralPath (Join-Path $output 'Sample.kubo')).Hash) 'Export changed Lua settings/extension fields'
    Write-Output 'PASS: Lua manifest limits copied exactly and malformed/type/fraction/zero/upper-bound limits refused before export publication'
    Require ([IO.File]::ReadAllText((Join-Path $output 'UserSettings\input_bindings.json')) -eq $bindings) 'Export omitted or modified gameplay input bindings'
    $extendedBindings='{"version":2,"actions":{"jump":[{"source":"keyboard","code":57,"contribution":[1,0,0]},{"source":"pad_button","code":10,"contribution":[1,0,0]}],"move":[{"source":"pad_axis","code":0,"threshold":0.5,"contribution":[1,0,0]}]}}'
    [IO.File]::WriteAllText((Join-Path $project 'UserSettings\input_bindings.json'),$extendedBindings)
    $extendedExport=Join-Path $fixture 'ExtendedBindingsExport'
    & $packager -ProjectFile (Join-Path $project 'Sample.kubo') -RuntimeExecutable (Join-Path $runtime 'KuboPlayer.exe') -OutputDirectory $extendedExport
    Require ([IO.File]::ReadAllText((Join-Path $extendedExport 'UserSettings\input_bindings.json')) -ceq $extendedBindings) 'Export modified extended mixed pad action configuration'
    Require (!(Test-Path -LiteralPath (Join-Path $extendedExport 'UserSettings\native-build.json'))) 'Extended action export leaked local native settings'
    [IO.File]::WriteAllText((Join-Path $project 'UserSettings\input_bindings.json'),$bindings)
    Write-Output 'PASS: version2 mixed keyboard/pad button/axis gameplay bindings export exactly, excluding local native settings'
    Require (-not (Test-Path -LiteralPath (Join-Path $output 'UserSettings\editor-layout.json'))) 'Export leaked editor settings'
    Require (-not (Test-Path -LiteralPath (Join-Path $output 'UserSettings\native-build.json'))) 'Export leaked native build settings'
    Require (-not (Test-Path -LiteralPath (Join-Path $output 'UserSettings\native-build.json.lock'))) 'Export leaked native build lock'
    $rejected = $false
    try { & $packager -ProjectFile (Join-Path $project 'Sample.kubo') -RuntimeExecutable (Join-Path $runtime 'KuboPlayer.exe') -OutputDirectory $output } catch { $rejected = $true }
    Require $rejected 'Non-empty output was not rejected'
    Require ((Get-Content -LiteralPath (Join-Path $output 'Content\asset.txt') -Raw) -eq 'custom asset payload') 'Rejected export mutated existing files'
    $rejected = $false
    try { & $packager -ProjectFile (Join-Path $project 'Sample.kubo') -RuntimeExecutable (Join-Path $runtime 'KuboPlayer.exe') -OutputDirectory (Join-Path $project 'Content\Export') } catch { $rejected = $true }
    Require $rejected 'Output inside copied assets was not rejected'
    $journalDirectory = Join-Path $project 'Content\.kubo'
    New-Item -ItemType Directory -Path $journalDirectory | Out-Null
    [IO.File]::WriteAllText((Join-Path $journalDirectory 'asset-move.pending.json'), '{}')
    $blockedOutput = Join-Path $fixture 'BlockedExport'
    $rejected = $false
    try { & $packager -ProjectFile (Join-Path $project 'Sample.kubo') -RuntimeExecutable (Join-Path $runtime 'KuboPlayer.exe') -OutputDirectory $blockedOutput } catch { $rejected = $true }
    Require $rejected 'Pending asset move did not block export'
    Require (-not (Test-Path -LiteralPath $blockedOutput)) 'Blocked export created output files'
    Require (Test-Path -LiteralPath (Join-Path $journalDirectory 'asset-move.pending.json')) 'Blocked export altered recovery marker'
    # Exercise Windows PowerShell's manifest decoding without relying on its ANSI default.
    $unicodeName=-join ([char[]]@(0x65e5,0x672c,0x8a9e))
    $unicodeAssets=$unicodeName+'/Models';$unicodeScene=$unicodeName+'/Scenes/'+$unicodeName+'.scene'
    New-Item -ItemType Directory -Path (Join-Path $project $unicodeAssets),(Split-Path -Parent (Join-Path $project $unicodeScene)) -Force | Out-Null
    [IO.File]::WriteAllText((Join-Path $project $unicodeScene), '{"version":1,"entities":[]}')
    [IO.File]::WriteAllText((Join-Path $project ($unicodeAssets+'/asset.txt')), 'Unicode nested payload')
    $unicodeManifest=Join-Path $project 'Unicode.kubo'
    $unicodeSettings=@{name=$unicodeName;engineVersion='0.1.0';startupScene=$unicodeScene;assetDirectory=$unicodeAssets}
    [IO.File]::WriteAllText($unicodeManifest,($unicodeSettings | ConvertTo-Json),[Text.UTF8Encoding]::new($true))
    $unicodeOutput=Join-Path $fixture 'UnicodeExport'
    & $packager -ProjectFile $unicodeManifest -RuntimeExecutable (Join-Path $runtime 'KuboPlayer.exe') -OutputDirectory $unicodeOutput
    Require (Test-Path -LiteralPath (Join-Path $unicodeOutput $unicodeScene) -PathType Leaf) 'UTF-8 BOM Unicode startup scene was not copied'
    Require ([IO.File]::ReadAllText((Join-Path $unicodeOutput ($unicodeAssets+'/asset.txt'))) -eq 'Unicode nested payload') 'Unicode nested assets were not copied'
    foreach($badPath in @('../outside','C:relative','C:/outside','Content:stream','Content./asset','Content /asset','.')) {
        $invalidSettings=$unicodeSettings.Clone();$invalidSettings.assetDirectory=$badPath
        [IO.File]::WriteAllText($unicodeManifest,($invalidSettings | ConvertTo-Json),[Text.UTF8Encoding]::new($false))
        $invalidOutput=Join-Path $fixture 'InvalidExport';$rejected=$false
        try { & $packager -ProjectFile $unicodeManifest -RuntimeExecutable (Join-Path $runtime 'KuboPlayer.exe') -OutputDirectory $invalidOutput } catch {$rejected=$true}
        Require $rejected "Invalid relative path was accepted: $badPath"
        Require (-not (Test-Path -LiteralPath $invalidOutput)) 'Invalid path created export files'
    }
    [IO.File]::WriteAllBytes($unicodeManifest,[byte[]]@(0xff,0xfe,0xff));$rejected=$false
    try { & $packager -ProjectFile $unicodeManifest -RuntimeExecutable (Join-Path $runtime 'KuboPlayer.exe') -OutputDirectory (Join-Path $fixture 'MalformedExport') } catch {$rejected=$true}
    Require $rejected 'Malformed UTF-8 manifest was accepted'
    Require (-not (Test-Path -LiteralPath (Join-Path $fixture 'MalformedExport'))) 'Malformed UTF-8 created output files'
    Write-Output 'PASS: root startup scene, custom/Unicode nested assets, UTF-8 BOM/malformed manifests, engine resources, input binding isolation and preflight path/copy rejection'
} finally {
    # fixture is a newly generated, exact temporary directory owned by this test.
    $resolvedFixture = [IO.Path]::GetFullPath($fixture)
    $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    if (-not $resolvedFixture.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase) -or
        -not (Split-Path -Leaf $resolvedFixture).StartsWith('kubo-package-check-')) { throw 'Unsafe test cleanup path' }
    Remove-Item -LiteralPath $fixture -Recurse -Force
}
