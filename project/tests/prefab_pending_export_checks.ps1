[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$fixtureParent=[IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')
$fixture=[IO.Path]::GetFullPath((Join-Path $fixtureParent ('kubo-prefab-export-'+[guid]::NewGuid().ToString('N'))))
$packageScript=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\tools\package_project.ps1'))
$completed=$false
try {
    foreach($mode in @('history','publication','incomplete','unknown-file','namespace-file','metadata-file','empty')) {
        $root=Join-Path $fixture $mode
        $assets=Join-Path $root 'Assets'
        New-Item -ItemType Directory -Path $assets -Force | Out-Null
        $manifest=Join-Path $root 'Game.kubo'
        [IO.File]::WriteAllText($manifest,'{"name":"Pending export","startupScene":"Assets/Main.scene","assetDirectory":"Assets"}',[Text.UTF8Encoding]::new($false))
        [IO.File]::WriteAllText((Join-Path $assets 'Main.scene'),'{"version":1,"entities":[]}',[Text.UTF8Encoding]::new($false))
        $runtime=Join-Path $root 'DummyPlayer.exe'
        [IO.File]::WriteAllText($runtime,'fixture, never executed')
        $pending=Join-Path $assets '.kubo\prefab-transactions'
        if($mode -eq 'metadata-file') {[IO.File]::WriteAllText((Join-Path $assets '.kubo'),'foreign metadata namespace')}
        else {
        New-Item -ItemType Directory -Path (Split-Path $pending -Parent) -Force | Out-Null
        if($mode -eq 'namespace-file') {[IO.File]::WriteAllText($pending,'foreign namespace data')}
        else {
            New-Item -ItemType Directory -Path $pending -Force | Out-Null
            if($mode -eq 'unknown-file') {[IO.File]::WriteAllText((Join-Path $pending 'foreign-owner'),'retain me')}
            elseif($mode -ne 'empty') {
                $directory=Join-Path $pending ('a'*32)
                New-Item -ItemType Directory -Path $directory | Out-Null
                if($mode -ne 'incomplete') {[IO.File]::WriteAllText((Join-Path $directory $(if($mode -eq 'history') {'history.json'} else {'pending.json'})),'untrusted marker bytes')}
            }
        }
        }
        $before=@{}
        foreach($file in Get-ChildItem -LiteralPath $root -Recurse -Force -File) {$before[$file.FullName]=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash}
        $output=Join-Path $root 'Export'
        $message=$null
        try {& $packageScript -ProjectFile $manifest -OutputDirectory $output -RuntimeExecutable $runtime | Out-Null}
        catch {$message=$_.Exception.Message}
        if(!$message) {throw "Unexpected dummy export success: $mode"}
        if($mode -eq 'empty') {if($message -notlike '*Runtime dependency was not found*') {throw "Empty namespace was not admitted to later preflight: $message"}}
        elseif($message -notlike '*Pending Prefab*') {throw "Wrong pending preflight failure: $mode`: $message"}
        if(Test-Path -LiteralPath $output) {throw "Rejected preflight created final output: $mode"}
        $after=@(Get-ChildItem -LiteralPath $root -Recurse -Force -File)
        if($after.Count -ne $before.Count) {throw "Rejected preflight changed file inventory: $mode"}
        foreach($file in $after) {if(!$before.ContainsKey($file.FullName) -or (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash -ne $before[$file.FullName]) {throw "Rejected preflight changed fixture data: $mode"}}
    }
    $completed=$true
    Write-Output 'PASS: package preflight rejects history/publication/incomplete/unknown/unsafe pending without output or source changes; empty namespace reaches later dependency preflight (dummy runtime never executed)'
} finally {
    if($completed) {
        $resolvedFixture=[IO.Path]::GetFullPath((Get-Item -LiteralPath $fixture).FullName)
        if([IO.Path]::GetDirectoryName($resolvedFixture) -ne $fixtureParent -or [IO.Path]::GetFileName($resolvedFixture) -notmatch '^kubo-prefab-export-[a-f0-9]{32}$') {throw 'Unsafe owned fixture cleanup path'}
        Remove-Item -LiteralPath $resolvedFixture -Recurse -Force
    } else {Write-Output "Owned failed fixture retained: $fixture"}
}
