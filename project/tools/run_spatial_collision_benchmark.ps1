$ErrorActionPreference = "Stop"

$projectRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$project = Join-Path $projectRoot "tests\KuboEngineSpatialBenchmark.vcxproj"
$executable = Join-Path $projectRoot "generated\outputs\benchmarks\Release\spatial_collision_benchmark.exe"
$csv = Join-Path $projectRoot "generated\outputs\spatial_collision_benchmark.csv"
$msbuild = "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe"

if (Test-Path $csv) {
	Remove-Item -LiteralPath $csv -Force
}

& $msbuild $project /m /p:Configuration=Release /p:Platform=x64 /nologo /verbosity:minimal
if ($LASTEXITCODE -ne 0) {
	exit $LASTEXITCODE
}

& $executable
if ($LASTEXITCODE -ne 0) {
	exit $LASTEXITCODE
}
if (-not (Test-Path $csv)) {
	throw "Spatial collision benchmark CSV was not generated"
}

"metricsCsv=$csv"
