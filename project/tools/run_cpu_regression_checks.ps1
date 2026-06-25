$ErrorActionPreference = "Stop"

$projectRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$project = Join-Path $projectRoot "tests\KuboEngineCpuTests.vcxproj"
$output = Join-Path $projectRoot "generated\outputs\tests\Debug\cpu_regression_checks.exe"
$msbuild = "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe"

& $msbuild $project /m /p:Configuration=Debug /p:Platform=x64 /nologo /verbosity:minimal
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $output
exit $LASTEXITCODE
