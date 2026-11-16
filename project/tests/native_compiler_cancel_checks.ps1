[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$SdkDirectory,[string]$Runner,[ValidateSet('Debug','Release')][string]$Configuration='Debug')
$ErrorActionPreference='Stop'
if([string]::IsNullOrWhiteSpace($Runner)) {$Runner=Join-Path $PSScriptRoot '..\..\generated\cpu-checks\export_job_checks.exe'}
$compilerRunner=[IO.Path]::GetFullPath($Runner)
$compilerSdk=[IO.Path]::GetFullPath($SdkDirectory)
if(!(Test-Path -LiteralPath $compilerRunner -PathType Leaf)) {throw 'Build export_job_checks before this test'}
$compilerManifest=Get-Content -LiteralPath (Join-Path $compilerSdk 'sdk-package.json') -Raw -Encoding UTF8 | ConvertFrom-Json
if($compilerManifest.format -ne 'kubo-runtime-sdk-1') {throw 'Wrong SDK format'}
function Check-CompilerSdk {
    foreach($entry in $compilerManifest.files) {
        if((Get-FileHash -LiteralPath (Join-Path $compilerSdk $entry.path) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry.sha256) {throw ('SDK hash mismatch: '+$entry.path)}
    }
}
Check-CompilerSdk
$compilerFixture=Join-Path ([IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\generated\cpu-checks'))) ('native-compiler-cancel-'+[guid]::NewGuid().ToString('N'))
$compilerProject=Join-Path $compilerFixture 'Project'
New-Item -ItemType Directory -Path (Join-Path $compilerProject 'Native'),(Join-Path $compilerProject 'Assets') -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $compilerProject 'Restart') | Out-Null
foreach($compilerTemplate in @('Starter.cpp','Starter.vcxproj')) {Copy-Item -LiteralPath (Join-Path $compilerSdk ('examples\Starter\'+$compilerTemplate)) -Destination (Join-Path $compilerProject ('Restart\'+$compilerTemplate))}
Copy-Item -LiteralPath (Join-Path $compilerSdk 'examples\Starter\Starter.vcxproj') -Destination (Join-Path $compilerProject 'Native\Starter.vcxproj')
[IO.File]::WriteAllText((Join-Path $compilerProject 'Game.kubo'),'{"name":"Compiler cancellation","engineVersion":"0.1.0","assetDirectory":"Assets","startupScene":"entry.scene"}')
[IO.File]::WriteAllText((Join-Path $compilerProject 'entry.scene'),'{"version":1,"entities":[]}')
# A real, bounded translation unit keeps cl.exe observable long enough for a
# job-membership check. No sleeping pre-build task stands in for the compiler.
$compilerSource=[Text.StringBuilder]::new()
$null=$compilerSource.AppendLine('template<int N> __declspec(noinline) int work(int x) { return x * N + (N % 17); }')
for($compilerFunction=1;$compilerFunction -le 30000;$compilerFunction++) {$null=$compilerSource.AppendLine("int f_$compilerFunction(int x) { return work<$compilerFunction>(x); }")}
$null=$compilerSource.AppendLine('int main() { return f_1(0); }')
$compilerSourcePath=Join-Path $compilerProject 'Native\Starter.cpp'
[IO.File]::WriteAllText($compilerSourcePath,$compilerSource.ToString())
$compilerSourceDigest=(Get-FileHash -LiteralPath $compilerSourcePath -Algorithm SHA256).Hash
$compilerOut=Join-Path $compilerFixture 'runner.out.log';$compilerErr=Join-Path $compilerFixture 'runner.err.log'
$compilerMode=if($Configuration -eq 'Debug') {'--cancel-native'} else {'--cancel-native-release'}
$compilerArguments=@($compilerMode,('"'+$compilerSdk+'"'),('"'+(Join-Path $compilerProject 'Native\Starter.vcxproj')+'"'),('"'+(Join-Path $compilerProject 'Game.kubo')+'"'),('"'+(Join-Path $compilerFixture 'Export')+'"'),('"'+(Join-Path $compilerProject 'Restart\Starter.vcxproj')+'"'))
Write-Output "Retained actual compiler fixture/logs: $compilerFixture"
$compilerProcess=$null
try {
    $compilerProcess=Start-Process -FilePath $compilerRunner -ArgumentList $compilerArguments -WorkingDirectory (Split-Path $PSScriptRoot -Parent) -WindowStyle Hidden -PassThru -RedirectStandardOutput $compilerOut -RedirectStandardError $compilerErr
    $compilerHandle=$compilerProcess.Handle
    if($compilerHandle -eq [IntPtr]::Zero) {throw 'Runner handle unavailable'}
    while(!$compilerProcess.WaitForExit(1000)) { }
    $compilerProcess.WaitForExit()
    [string]$compilerText=Get-Content -LiteralPath $compilerOut -Raw -Encoding UTF8
    [string]$compilerErrors=Get-Content -LiteralPath $compilerErr -Raw -Encoding UTF8
    if($compilerText) {Write-Output $compilerText.TrimEnd()};if($compilerErrors) {Write-Output $compilerErrors.TrimEnd()}
    if($compilerProcess.ExitCode -ne 0 -or $compilerText -notmatch 'OBSERVED_OWNED_COMPILER_PID=\d+' -or !$compilerText.Contains('PASS: observed live job-owned MSVC compiler, production native cancellation, compiler handle exit and no published game') -or !$compilerText.Contains('PASS: same ExportJob restarts a real SDK-native build and publishes a game after compiler cancellation')) {throw "Actual compiler cancellation failed; logs: $compilerFixture"}
    if((Get-FileHash -LiteralPath $compilerSourcePath -Algorithm SHA256).Hash -ne $compilerSourceDigest) {throw 'Cancellation changed native source'}
    Check-CompilerSdk
    Write-Output "PASS: actual $Configuration SDK-native MSVC cancellation/restart, source preservation and unchanged SDK digests; fixture and any interrupted build stage retained"
} finally {if($compilerProcess) {$compilerProcess.Dispose()}}
