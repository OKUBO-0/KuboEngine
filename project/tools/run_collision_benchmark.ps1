param(
	[int]$GameFrames = 1200,
	[int[]]$EnemyCounts = @(25, 50, 84),
	[uint32]$Seed = 20260708,
	[int]$TimeoutSeconds = 180
)

$ErrorActionPreference = "Stop"
if ($GameFrames -lt 1) { throw "GameFrames must be at least 1" }

$projectRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$executable = Join-Path $projectRoot "..\generated\outputs\Release\KuboEngine.exe"
$telemetry = Join-Path $projectRoot "Resources\DirectXGame\data\collision_telemetry.csv"
$analyzer = Join-Path $PSScriptRoot "analyze_collision_telemetry.py"
if (-not (Test-Path $executable)) { throw "Release executable was not found: $executable" }
if (-not (Test-Path $analyzer)) { throw "Analyzer was not found: $analyzer" }
if (Test-Path $telemetry) { Remove-Item -LiteralPath $telemetry -Force }

$names = @(
	"KUBO_SCENE_STRESS_CYCLES",
	"KUBO_SCENE_STRESS_GAME_FRAMES",
	"KUBO_RANDOM_SEED",
	"KUBO_COLLISION_TELEMETRY",
	"KUBO_COLLISION_MODE",
	"KUBO_COLLISION_BENCHMARK_ENEMIES"
)
$previous = @{}
foreach ($name in $names) { $previous[$name] = [Environment]::GetEnvironmentVariable($name) }

try {
	$env:KUBO_SCENE_STRESS_CYCLES = "1"
	$env:KUBO_SCENE_STRESS_GAME_FRAMES = [string]$GameFrames
	$env:KUBO_RANDOM_SEED = [string]$Seed
	$env:KUBO_COLLISION_TELEMETRY = "1"
	foreach ($enemyCount in $EnemyCounts) {
		if ($enemyCount -lt 1) { throw "EnemyCounts must contain positive values" }
		$env:KUBO_COLLISION_BENCHMARK_ENEMIES = [string]$enemyCount
		foreach ($mode in @("spatial_grid", "brute_force")) {
			$env:KUBO_COLLISION_MODE = $mode
			$process = Start-Process -FilePath $executable -WorkingDirectory $projectRoot `
				-WindowStyle Hidden -PassThru
			if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
				Stop-Process -Id $process.Id -Force
				throw "$mode/$enemyCount benchmark timed out after $TimeoutSeconds seconds"
			}
			if ($process.ExitCode -ne 0) { throw "$mode/$enemyCount benchmark exited with code $($process.ExitCode)" }
		}
	}
	if (-not (Test-Path $telemetry)) { throw "Collision telemetry was not generated" }
	$bundledPython = Join-Path $HOME ".cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe"
	if (Test-Path $bundledPython) {
		& $bundledPython $analyzer $telemetry
	} elseif (Get-Command py -ErrorAction SilentlyContinue) {
		py -3 $analyzer $telemetry
	} else {
		throw "Python 3 was not found"
	}
} finally {
	foreach ($name in $names) {
		if ($null -eq $previous[$name]) {
			Remove-Item "Env:$name" -ErrorAction SilentlyContinue
		} else {
			[Environment]::SetEnvironmentVariable($name, $previous[$name])
		}
	}
}
