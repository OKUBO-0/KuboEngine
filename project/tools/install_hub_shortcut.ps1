[CmdletBinding()]
param(
    [string]$EngineDirectory = (Split-Path $PSScriptRoot -Parent),
    [string]$ProgramsDirectory = [Environment]::GetFolderPath('Programs'),
    [ValidatePattern('^[^<>:"/\\|?*\x00-\x1F]{1,64}$')]
    [string]$ShortcutName = 'KuboEngine Hub'
)
$ErrorActionPreference = 'Stop'
$hubShortcutRoot = (Resolve-Path -LiteralPath $EngineDirectory).Path
$hubShortcutLauncher = Join-Path $hubShortcutRoot 'KuboLauncher.exe'
if (!(Test-Path -LiteralPath $hubShortcutLauncher -PathType Leaf)) {
    throw 'Choose the complete KuboEngine distribution folder.'
}
$hubShortcutFolder = $ProgramsDirectory
if ([string]::IsNullOrWhiteSpace($hubShortcutFolder) -or !(Test-Path -LiteralPath $hubShortcutFolder -PathType Container)) {
    throw 'The current user Start menu Programs folder is unavailable.'
}
$hubShortcutPath = Join-Path $hubShortcutFolder ($ShortcutName + '.lnk')
$hubShortcutShell = New-Object -ComObject WScript.Shell
$hubShortcut = $hubShortcutShell.CreateShortcut($hubShortcutPath)
if ((Test-Path -LiteralPath $hubShortcutPath) -and
    !$hubShortcut.TargetPath.Equals($hubShortcutLauncher,[StringComparison]::OrdinalIgnoreCase)) {
    throw 'A shortcut with this name points to another location. Choose a different -ShortcutName.'
}
$hubShortcut.TargetPath = $hubShortcutLauncher
$hubShortcut.WorkingDirectory = $hubShortcutRoot
$hubShortcut.Description = 'Open KuboEngine Hub'
$hubShortcut.IconLocation = $hubShortcutLauncher + ',0'
$hubShortcut.Save()
Write-Output $hubShortcutPath
