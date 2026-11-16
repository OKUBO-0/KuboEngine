[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$identityRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot ('../../generated/engine-package-version/'+[guid]::NewGuid().ToString('N'))))
$identityBuild=Join-Path $identityRoot 'Synthetic build'
New-Item -ItemType Directory -Path $identityBuild | Out-Null
function Require($value,[string]$why){if(!$value){throw $why}}
$identityFiles=@('KuboLauncher.exe','KuboEditor.exe','KuboPlayer.exe','KuboMcpServer.exe','dxcompiler.dll','dxil.dll','tools/KuboAssetValidator.exe','Resources/Shaders/test.hlsl','Resources/textures/common/white.png','Resources/textures/skybox/test.dds','EditorResources/Fonts/fa-solid-900.ttf','ThirdPartyLicenses/Lua-5.4.9.txt')
foreach($relative in $identityFiles){
 $file=Join-Path $identityBuild $relative
 New-Item -ItemType Directory -Path (Split-Path -Parent $file) -Force | Out-Null
 [IO.File]::WriteAllText($file,'inert fixture bytes, never executed')
}
$identityPackager=Join-Path $PSScriptRoot '../tools/package_engine.ps1'
$identityCase=0
foreach($version in @($null,'1.2.3','0.01.2147483647',('0.'+('0'*126)))){
 $destination=Join-Path $identityRoot ('Success-'+$identityCase);$identityCase++
 $options=@{OutputDirectory=$destination;BuildDirectory=$identityBuild}
 if($null -ne $version){$options.EngineVersion=$version}
 & $identityPackager @options
 $manifest=Get-Content -LiteralPath (Join-Path $destination 'engine-package.json') -Raw -Encoding UTF8 | ConvertFrom-Json
 if($null -eq $version){Require ($manifest.format -eq 'kubo-portable-engine-1' -and !($manifest.PSObject.Properties.Name -contains 'engineVersion') -and !($manifest.PSObject.Properties.Name -contains 'platform')) 'Legacy publisher invented release/platform'}
 else{Require ($manifest.format -eq 'kubo-portable-engine-2' -and $manifest.engineVersion -ceq $version -and $manifest.platform -ceq 'Windows-x64') 'Versioned publisher lost declaration'}
 foreach($entry in $manifest.files){Require ((Get-FileHash -LiteralPath (Join-Path $destination $entry.path)).Hash.ToLowerInvariant() -ceq $entry.sha256) 'Synthetic package digest mismatch'}
 Write-Output "PASS: package version declaration '$version' and complete copied-payload hashes (inert fixture)"
}
$invalid=@('1.','.1','1..2','-1','+1',' 1.2','1.2 ','1.2beta','1.2+build','2147483648',('1.'+('0'*127)),"1.0`n",(-join [char[]]@(0x661,0x2e,0x662)))
foreach($version in $invalid){
 $destination=Join-Path $identityRoot ('Refused-'+$identityCase);$identityCase++;$refused=$false
 try{& $identityPackager -OutputDirectory $destination -BuildDirectory $identityBuild -EngineVersion $version}catch{Require ($_.Exception.Message -match 'EngineVersion') 'Version rejection had unrelated cause';$refused=$true}
 Require ($refused -and !(Test-Path -LiteralPath $destination)) 'Invalid release declaration created output or was accepted'
}
$unsupportedPath=Join-Path $identityBuild ('Resources/bad'+[char]127+'.bin')
[IO.File]::WriteAllText($unsupportedPath,'preserved source for rejected metadata')
$unsupportedOutput=Join-Path $identityRoot 'Unsupported-path';$refused=$false
try{& $identityPackager -OutputDirectory $unsupportedOutput -BuildDirectory $identityBuild -EngineVersion '1.2.3'}catch{Require ($_.Exception.Message -match 'member path is unsupported') 'Member path refusal had unrelated cause';$refused=$true}
Require ($refused -and !(Test-Path -LiteralPath (Join-Path $unsupportedOutput 'engine-package.json')) -and [IO.File]::ReadAllText($unsupportedPath) -eq 'preserved source for rejected metadata') 'Unsupported member path published metadata or changed source'
$largeFolder=Join-Path $identityBuild 'Resources/Many'
New-Item -ItemType Directory -Path $largeFolder | Out-Null
for($i=0;$i -lt 4096;$i++){[IO.File]::WriteAllText((Join-Path $largeFolder ("f-$i.bin")),'')}
$tooMany=Join-Path $identityRoot 'Over-file-cap';$refused=$false
try{& $identityPackager -OutputDirectory $tooMany -BuildDirectory $identityBuild -EngineVersion '1.2.3'}catch{Require ($_.Exception.Message -match '4096 files or 1 MiB') 'Package cap refusal had unrelated cause';$refused=$true}
Require ($refused -and !(Test-Path -LiteralPath (Join-Path $tooMany 'engine-package.json'))) 'Over-budget package published a manifest'
Write-Output "PASS: engine package publisher legacy/v2 declaration, strict numeric version boundaries, $($invalid.Count) pre-output refusals, unsupported member and over-file-cap without manifest publication; partial assembly is retained, not atomic package publication; no fixture executable launched; retained=$identityRoot"
