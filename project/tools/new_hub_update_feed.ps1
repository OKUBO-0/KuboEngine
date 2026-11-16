[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$PackageDirectory,
    [Parameter(Mandatory)][uri]$ManifestUrl,
    [Parameter(Mandatory)][string]$OutputFile,
    [string]$ReleaseNotes = ''
)
$ErrorActionPreference = 'Stop'
if (!$ManifestUrl.IsAbsoluteUri -or $ManifestUrl.Scheme -ne 'https' -or
    $ManifestUrl.UserInfo -or $ManifestUrl.Query -or $ManifestUrl.Fragment -or
    !$ManifestUrl.AbsolutePath.EndsWith('/engine-package.json',[StringComparison]::Ordinal)) {
    throw 'Use the HTTPS URL of the hosted engine-package.json without credentials, a query or a fragment.'
}
$hubFeedPackage = (Resolve-Path -LiteralPath $PackageDirectory).Path
$hubFeedManifest = Join-Path $hubFeedPackage 'engine-package.json'
$hubFeedData = Get-Content -LiteralPath $hubFeedManifest -Raw | ConvertFrom-Json
if ($hubFeedData.format -ne 'kubo-portable-engine-2' -or $hubFeedData.platform -ne 'Windows-x64' -or
    $hubFeedData.engineVersion -notmatch '^\d+(\.\d+)*$') {
    throw 'Build a Windows-x64 Portable package with -EngineVersion first.'
}
$hubFeedOutput = [IO.Path]::GetFullPath($OutputFile)
if ($hubFeedOutput.StartsWith($hubFeedPackage.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)) {
    throw 'Write the feed outside the package directory so it does not change its inventory.'
}
$hubFeedEncoding = [Text.UTF8Encoding]::new($false,$true)
if ($hubFeedEncoding.GetByteCount($ReleaseNotes) -gt 8192) { throw 'Release notes exceed 8192 UTF-8 bytes.' }
$hubFeedText = [ordered]@{
    format = 'kubo-hub-update-1'
    platform = 'Windows-x64'
    version = $hubFeedData.engineVersion
    manifestUrl = $ManifestUrl.AbsoluteUri
    manifestSha256 = (Get-FileHash -LiteralPath $hubFeedManifest -Algorithm SHA256).Hash.ToLowerInvariant()
    notes = $ReleaseNotes
} | ConvertTo-Json
$hubFeedBytes = $hubFeedEncoding.GetBytes($hubFeedText)
$hubFeedStream = [IO.File]::Open($hubFeedOutput,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
try { $hubFeedStream.Write($hubFeedBytes,0,$hubFeedBytes.Length) }
finally { $hubFeedStream.Dispose() }
Write-Output $hubFeedOutput
