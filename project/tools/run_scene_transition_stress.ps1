param(
	[int]$Cycles = 3,
	[int]$TimeoutSeconds = 120
)

$ErrorActionPreference = "Stop"

if ($Cycles -lt 1) {
	throw "Cycles must be at least 1"
}

$projectRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$executable = Join-Path $projectRoot "..\generated\outputs\Debug\KuboEngine.exe"
$report = Join-Path $projectRoot "generated\outputs\scene_transition_stress.txt"

if (-not (Test-Path $executable)) {
	throw "Debug executable was not found: $executable"
}

if (Test-Path $report) {
	Remove-Item -LiteralPath $report -Force
}

$previousCycles = $env:KUBO_SCENE_STRESS_CYCLES
try {
	$env:KUBO_SCENE_STRESS_CYCLES = [string]$Cycles
	$process = Start-Process `
		-FilePath $executable `
		-WorkingDirectory $projectRoot `
		-WindowStyle Hidden `
		-PassThru

	if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
		Stop-Process -Id $process.Id -Force
		throw "Scene transition stress timed out after $TimeoutSeconds seconds"
	}
	if ($process.ExitCode -ne 0) {
		throw "Scene transition stress exited with code $($process.ExitCode)"
	}
	if (-not (Test-Path $report)) {
		throw "Scene transition stress report was not generated"
	}

	$contents = Get-Content -LiteralPath $report
	if ($contents -notcontains "status=PASS") {
		throw "Scene transition stress report did not contain status=PASS"
	}
	$contents
} finally {
	if ($null -eq $previousCycles) {
		Remove-Item Env:KUBO_SCENE_STRESS_CYCLES -ErrorAction SilentlyContinue
	} else {
		$env:KUBO_SCENE_STRESS_CYCLES = $previousCycles
	}
}
