[CmdletBinding()]
param([ValidateSet('Debug', 'Release')][string]$Configuration = 'Release', [switch]$LuaScript)
$ErrorActionPreference = 'Stop'
function Require($condition, [string]$why) { if (-not $condition) { throw $why } }
Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class PlayerPackageWindow {
    delegate bool Callback(IntPtr window, IntPtr state);
    [DllImport("user32.dll")] static extern bool EnumWindows(Callback callback, IntPtr state);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window, out uint owner);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetClassNameW(IntPtr window, StringBuilder name, int count);
    [DllImport("user32.dll")] static extern bool PostMessageW(IntPtr window, uint message, IntPtr wparam, IntPtr lparam);
    public static IntPtr Find(uint process) {
        IntPtr found=IntPtr.Zero;
        EnumWindows((window, state) => {
            uint owner; GetWindowThreadProcessId(window, out owner);
            if(owner!=process) return true;
            var name=new StringBuilder(128); GetClassNameW(window,name,name.Capacity);
            if(name.ToString()!="WindowClass") return true;
            found=window; return false;
        }, IntPtr.Zero);
        return found;
    }
    public static bool Close(IntPtr window) { return PostMessageW(window,0x0010,IntPtr.Zero,IntPtr.Zero); }
}
'@
$playerPackageRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$playerPackageFixture = Join-Path ([IO.Path]::GetTempPath()) ('kubo-player-package-' + [guid]::NewGuid().ToString('N'))
$playerPackageProcess = $null
try {
    $playerPackageSource = Join-Path $playerPackageFixture 'Source'
    # Windows PowerShell reads BOM-less scripts in the local ANSI code page.
    # Construct real Unicode names independently of script file decoding.
    $playerPackageOutput = Join-Path $playerPackageFixture (-join ([char[]]@(0x914d,0x5e03,0x30b2,0x30fc,0x30e0)))
    $playerPackageModelName = (-join ([char[]]@(0x30e2,0x30c7,0x30eb))) + '.obj'
    $playerPackageScriptName = (-join ([char[]]@(0x52d5,0x4f5c))) + '.kubo'
    if ($LuaScript) { $playerPackageScriptName = (-join ([char[]]@(0x52d5,0x4f5c))) + '.lua' }
    $playerPackageWorking = Join-Path $playerPackageFixture 'UnrelatedWorkingDirectory'
    New-Item -ItemType Directory -Path (Join-Path $playerPackageSource 'Assets'), $playerPackageWorking | Out-Null
    $playerPackageManifest = Join-Path $playerPackageSource 'Sample.kubo'
    [IO.File]::WriteAllText($playerPackageManifest, '{"name":"RuntimePackage","engineVersion":"0.1.0","startupScene":"entry.scene","assetDirectory":"Assets"}')
    [IO.File]::WriteAllText((Join-Path $playerPackageSource ('Assets\' + $playerPackageModelName)), "v 0 0 0`nv 1 0 0`nv 0 1 0`nvt 0 0`nvt 1 0`nvt 0 1`nvn 0 0 1`nf 1/1/1 2/2/1 3/3/1`n")
    [IO.File]::WriteAllText((Join-Path $playerPackageSource ('Assets\' + $playerPackageScriptName)), "on_update translate 2 0 0`n")
    if ($LuaScript) {
        $playerPackageWaveName=(-join ([char[]]@(0x97f3,0x58f0)))+'.wav'
        [IO.File]::WriteAllBytes((Join-Path $playerPackageSource ('Assets\'+$playerPackageWaveName)),[byte[]]@(82,73,70,70,40,0,0,0,87,65,86,69,102,109,116,32,16,0,0,0,1,0,1,0,64,31,0,0,128,62,0,0,2,0,16,0,100,97,116,97,4,0,0,0,65,1,0,0))
        [IO.File]::WriteAllText((Join-Path $playerPackageSource ('Assets\'+$playerPackageWaveName+'.kubo-meta.json')),'{"version":1,"guid":"aabbccddeeff00112233445566778899"}')
        [IO.File]::WriteAllText((Join-Path $playerPackageSource ('Assets\' + $playerPackageScriptName)), "properties={speed=2,enabled=true,label='default'}`nfunction on_create(e) assert(audio.available());audio.play('Assets/$playerPackageWaveName',true,0) end`nfunction on_update(e,dt) assert(e:property('speed')==4 and e:property('enabled')==true and e:property('label')=='package');e:translate(e:property('speed')*dt,0,0) end`n")
    }
    [IO.File]::WriteAllText((Join-Path $playerPackageSource 'entry.scene'), '{"version":1,"entities":[{"id":1,"parent":0,"name":"Camera","active":true,"transform":{"position":[0,0,-5],"rotation":[0,0,0],"scale":[1,1,1]},"model":"","camera":{"enabled":true,"primary":true,"fovY":0.8,"nearClip":0.1,"farClip":100,"projection":"orthographic","orthographicHeight":8}},{"id":2,"parent":0,"name":"Model/script","active":true,"transform":{"position":[0,0,0],"rotation":[0,0,0],"scale":[1,1,1]},"model":"Assets/\u30e2\u30c7\u30eb.obj","script":{"asset":"Assets/\u52d5\u4f5c.kubo","enabled":true}}]}')
    if ($LuaScript) {
        $playerPackageScene = Join-Path $playerPackageSource 'entry.scene'
        [IO.File]::WriteAllText($playerPackageScene, [IO.File]::ReadAllText($playerPackageScene).Replace('.kubo"','.lua"').Replace('"script":{"asset"','"script":{"properties":{"speed":4,"enabled":true,"label":"package"},"asset"'))
        $playerAudioScene = [IO.File]::ReadAllText($playerPackageScene) | ConvertFrom-Json
        $playerAudioScene.entities[1] | Add-Member -NotePropertyName audio -NotePropertyValue @{asset='Assets/not-present.wav';guid='aabbccddeeff00112233445566778899';enabled=$true;loop=$true;playOnAwake=$true;volume=0}
        [IO.File]::WriteAllText($playerPackageScene,($playerAudioScene | ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($false))
    }
    $playerPackageRuntime = Join-Path (Split-Path -Parent $playerPackageRoot) "generated\outputs\$Configuration\KuboPlayer.exe"
    $playerOutputSettings=[IO.File]::ReadAllText($playerPackageManifest) | ConvertFrom-Json
    $playerOutputSettings | Add-Member -NotePropertyName gameOutput -NotePropertyValue @{width=512;height=768}
    $playerStartupFolder=-join ([char[]]@(0x5834,0x9762))
    $playerStartupName=(-join ([char[]]@(0x958b,0x59cb)))+'.scene'
    $playerStartupRelative='Assets/'+$playerStartupFolder+'/'+$playerStartupName
    $playerStartupPath=Join-Path $playerPackageSource $playerStartupRelative
    New-Item -ItemType Directory -Path (Split-Path -Parent $playerStartupPath) | Out-Null
    Copy-Item -LiteralPath (Join-Path $playerPackageSource 'entry.scene') -Destination $playerStartupPath
    [IO.File]::WriteAllText($playerStartupPath,[IO.File]::ReadAllText($playerStartupPath),[Text.UTF8Encoding]::new($true))
    [IO.File]::WriteAllText((Join-Path $playerPackageSource 'entry.scene'),'unselected invalid Scene')
    $playerOutputSettings.startupScene=$playerStartupRelative
    [IO.File]::WriteAllText($playerPackageManifest,($playerOutputSettings | ConvertTo-Json -Depth 10),[Text.UTF8Encoding]::new($false))
    New-Item -ItemType Directory -Path (Join-Path $playerPackageSource 'UserSettings') | Out-Null
    $playerPackageBindings = Join-Path $playerPackageSource 'UserSettings\input_bindings.json'
    [IO.File]::WriteAllText($playerPackageBindings, '{"version":1,"actions":{"move":[{"key":17,"contribution":[0,0,1]}]}}')
    & (Join-Path $playerPackageRoot 'tools\package_project.ps1') -ProjectFile $playerPackageManifest -RuntimeExecutable $playerPackageRuntime -OutputDirectory $playerPackageOutput -Configuration $Configuration
    Require ((Get-FileHash -LiteralPath $playerPackageRuntime).Hash -eq (Get-FileHash -LiteralPath (Join-Path $playerPackageOutput 'KuboPlayer.exe')).Hash) 'Packager did not copy the independent Player'
    Require ((Get-FileHash -LiteralPath $playerPackageManifest).Hash -eq (Get-FileHash -LiteralPath (Join-Path $playerPackageOutput ([IO.Path]::GetFileName($playerPackageManifest)))).Hash) 'Packager changed configured Game output manifest'
    Require ((Get-FileHash -LiteralPath $playerStartupPath).Hash -eq (Get-FileHash -LiteralPath (Join-Path $playerPackageOutput $playerStartupRelative)).Hash) 'Packager omitted or changed selected Unicode startup Scene'
    Require ((Get-FileHash -LiteralPath $playerPackageBindings).Hash -eq (Get-FileHash -LiteralPath (Join-Path $playerPackageOutput 'UserSettings\input_bindings.json')).Hash) 'Packager omitted or changed project input bindings'
    if ($LuaScript) {Require ((Get-FileHash -LiteralPath (Join-Path $playerPackageSource ('Assets\'+$playerPackageWaveName))).Hash -eq (Get-FileHash -LiteralPath (Join-Path $playerPackageOutput ('Assets\'+$playerPackageWaveName))).Hash) 'Packager omitted or changed Unicode Script Wave'}
    if ($LuaScript) {Require ((Get-FileHash -LiteralPath (Join-Path $playerPackageSource ('Assets\'+$playerPackageWaveName+'.kubo-meta.json'))).Hash -eq (Get-FileHash -LiteralPath (Join-Path $playerPackageOutput ('Assets\'+$playerPackageWaveName+'.kubo-meta.json'))).Hash) 'Packager omitted or changed Audio GUID metadata'}
    Require (Test-Path -LiteralPath (Join-Path $playerPackageOutput 'Resources\textures\common\white.png')) 'Packager omitted built-in model fallback texture'
    Require ((Get-FileHash -LiteralPath (Join-Path $playerPackageOutput 'ThirdPartyLicenses\Lua-5.4.9.txt')).Hash -eq (Get-FileHash -LiteralPath (Join-Path (Split-Path -Parent $playerPackageRuntime) 'ThirdPartyLicenses\Lua-5.4.9.txt')).Hash) 'Packager omitted or changed Lua license'
    foreach ($playerPackageExcluded in @('KuboEditor.exe', 'KuboMcpServer.exe', 'EditorResources', 'tools')) {
        Require (-not (Test-Path -LiteralPath (Join-Path $playerPackageOutput $playerPackageExcluded))) "Editor dependency in runtime package: $playerPackageExcluded"
    }
    # Renaming must not enable Editor mode; also launch without --project from
    # an unrelated cwd to exercise executable-relative manifest discovery.
    $playerPackageRenamed = Join-Path $playerPackageOutput 'RenamedGame.exe'
    Copy-Item -LiteralPath (Join-Path $playerPackageOutput 'KuboPlayer.exe') -Destination $playerPackageRenamed
    $playerPackageLog = Join-Path $playerPackageWorking 'player.stderr.log'
    $playerPackageProcess = Start-Process -FilePath $playerPackageRenamed -ArgumentList '--enable-mcp' -WorkingDirectory $playerPackageWorking -WindowStyle Hidden -PassThru -RedirectStandardError $playerPackageLog
    # Retain the process handle before exit; Windows PowerShell otherwise may
    # leave ExitCode unavailable on a Start-Process wrapper after native close.
    $playerPackageHandle = $playerPackageProcess.Handle
    Require ($playerPackageHandle -ne [IntPtr]::Zero) 'Owned Player process handle unavailable'
    $playerPackageWindow = [IntPtr]::Zero
    for ($playerPackageAttempt = 0; $playerPackageAttempt -lt 50; $playerPackageAttempt++) {
        $playerPackageProcess.Refresh()
        if ($playerPackageProcess.HasExited) { break }
        $playerPackageWindow = [PlayerPackageWindow]::Find($playerPackageProcess.Id)
        if ($playerPackageWindow -ne [IntPtr]::Zero) { break }
        Start-Sleep -Milliseconds 100
    }
    Start-Sleep -Milliseconds 1200
    $playerPackageProcess.Refresh()
    if (Test-Path -LiteralPath $playerPackageLog) { Get-Content -LiteralPath $playerPackageLog -Encoding UTF8 }
    Require (!$playerPackageProcess.HasExited) 'Renamed packaged Player exited during startup'
    Require ($playerPackageWindow -ne [IntPtr]::Zero) 'Player did not create its engine window'
    Require (-not (Test-Path -LiteralPath (Join-Path $playerPackageOutput 'Library\Mcp\editor-session.json'))) 'Player enabled Editor MCP'
    # Start-Process Hidden has no Process.MainWindowHandle. Address only the
    # engine HWND whose process ID belongs to this exact test process.
    Require ([PlayerPackageWindow]::Close($playerPackageWindow)) 'Packaged Player did not accept native close'
    Require ($playerPackageProcess.WaitForExit(10000)) 'Packaged Player did not finish normal shutdown'
    $playerPackageProcess.WaitForExit()
    Require ($playerPackageProcess.ExitCode -eq 0) "Packaged Player exited with $($playerPackageProcess.ExitCode)"
    Write-Output "PASS: actual $Configuration runtime package without Editor files, renamed Player/executable-relative project discovery, Japanese model/script paths, no MCP and native close exit 0 (not pixel verification)"
    if ($LuaScript) {Write-Output 'PASS: preserved Unicode Wave/GUID metadata and authored Audio component with absent path hint in packaged Player startup (not voice/pixel verification)'}
} finally {
    if ($playerPackageProcess) {
        if (!$playerPackageProcess.HasExited) { Stop-Process -Id $playerPackageProcess.Id }
        if (!$playerPackageProcess.WaitForExit(5000)) { throw 'Owned Player remains alive; fixture retained' }
        $playerPackageProcess.Dispose()
    }
    $playerPackageResolved = [IO.Path]::GetFullPath($playerPackageFixture)
    $playerPackageTempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    if (!$playerPackageResolved.StartsWith($playerPackageTempRoot, [StringComparison]::OrdinalIgnoreCase) -or
        !(Split-Path -Leaf $playerPackageResolved).StartsWith('kubo-player-package-')) { throw 'Unsafe runtime package fixture cleanup' }
    if (Test-Path -LiteralPath $playerPackageResolved) { Remove-Item -LiteralPath $playerPackageResolved -Recurse -Force }
}
