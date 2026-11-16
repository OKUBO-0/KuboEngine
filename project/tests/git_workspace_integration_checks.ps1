$ErrorActionPreference = 'Stop'
$japanese = -join (@(0x65e5, 0x672c, 0x8a9e) | ForEach-Object { [char]$_ })
$fixture = Join-Path ([IO.Path]::GetTempPath()) ('kubo-git-fixture-' + $japanese + ' & %PATH%-' + [Guid]::NewGuid().ToString('N'))
$check = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\generated\cpu-checks\git_workspace_checks.exe'))
function Run-Git([string[]]$Arguments) {
    & git -C $fixture @Arguments
    if ($LASTEXITCODE -ne 0) { throw "Fixture Git setup failed: $Arguments" }
}
function Get-IndexDigest([string]$Path) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($sha.ComputeHash([IO.File]::ReadAllBytes($Path))) }
    finally { $sha.Dispose() }
}
try {
    foreach($variable in @('GIT_DIR','GIT_WORK_TREE','GIT_INDEX_FILE','GIT_COMMON_DIR','GIT_OBJECT_DIRECTORY')) {if([Environment]::GetEnvironmentVariable($variable)) {throw 'Fixture refuses inherited Git location override'}}
    if (!(Test-Path -LiteralPath $check -PathType Leaf)) { throw 'Build git_workspace_checks first' }
    New-Item -ItemType Directory -Path $fixture | Out-Null
    Run-Git @('init', '-b', 'main')
    Run-Git @('config', 'user.name', 'Kubo test')
    Run-Git @('config', 'user.email', 'test@example.invalid')
    [IO.File]::WriteAllText((Join-Path $fixture 'tracked.txt'), 'tracked content')
    Run-Git @('add', '--', 'tracked.txt')
    Run-Git @('commit', '-m', 'isolated test fixture')
    Run-Git @('mv', '--', 'tracked.txt', 'renamed.txt')
    [IO.File]::WriteAllText((Join-Path $fixture 'untracked.txt'), 'untracked content')
    $index = Join-Path $fixture '.git\index'
    $beforeHash = Get-IndexDigest $index
    $beforeTime = (Get-Item -LiteralPath $index).LastWriteTimeUtc
    & $check $fixture
    if ($LASTEXITCODE -ne 0) { throw 'Live Git fixture check failed' }
    if ((Get-IndexDigest $index) -ne $beforeHash -or (Get-Item -LiteralPath $index).LastWriteTimeUtc -ne $beforeTime) { throw 'Read-only status changed the Git index' }
    Run-Git @('checkout', '--detach', 'HEAD')
    & $check $fixture 'detached'
    if ($LASTEXITCODE -ne 0) { throw 'Detached HEAD check failed' }
    Write-Output 'PASS: live Unicode/metacharacter repo, staged rename/untracked files, unchanged index and detached HEAD'
} finally {
    $resolved = [IO.Path]::GetFullPath($fixture)
    $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    if (!$resolved.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase) -or
        !(Split-Path -Leaf $resolved).StartsWith('kubo-git-fixture-')) { throw 'Unsafe fixture cleanup path' }
    if (Test-Path -LiteralPath $resolved) { Remove-Item -LiteralPath $resolved -Recurse -Force }
}
