param(
    [Parameter(Mandatory = $true)]
    [string]$Url,

    [double]$StartSeconds = 0,

    [double]$EndSeconds = -1,
    [double]$DurationSeconds = -1,
    [double]$FramesPerSecond = 4,
    [string]$OutputRoot = "tmp\youtube_reference_frames",
    [string]$Name = ""
)

$ErrorActionPreference = "Stop"

function Resolve-Tool([string]$CommandName, [string]$InstallHint) {
    $command = Get-Command $CommandName -ErrorAction SilentlyContinue
    if (-not $command) {
        throw "$CommandName was not found. $InstallHint"
    }
    return $command.Source
}

if ($FramesPerSecond -le 0) {
    throw "FramesPerSecond must be greater than zero."
}

$ytDlp = Resolve-Tool "yt-dlp" "Install with: winget install yt-dlp.yt-dlp"
$ffmpeg = Resolve-Tool "ffmpeg" "Install with: winget install Gyan.FFmpeg"
$ffprobe = Resolve-Tool "ffprobe" "Install with: winget install Gyan.FFmpeg"

$safeName = if ($Name.Trim().Length -gt 0) {
    $Name.Trim() -replace '[^a-zA-Z0-9_.-]', '_'
} else {
    "youtube_reference"
}
$timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
$outputDir = Join-Path $OutputRoot "$safeName`_$timestamp"
$framesDir = Join-Path $outputDir "frames"
New-Item -ItemType Directory -Force -Path $framesDir | Out-Null

$sourcePath = Join-Path $outputDir "source.mp4"
$clipPath = Join-Path $outputDir "clip.mp4"
$metadataPath = Join-Path $outputDir "metadata.txt"

& $ytDlp `
    --no-playlist `
    -f "mp4/bestvideo[ext=mp4]+bestaudio[ext=m4a]/best[ext=mp4]/best" `
    -o $sourcePath `
    $Url
if ($LASTEXITCODE -ne 0) {
    throw "yt-dlp failed with exit code $LASTEXITCODE"
}

$sourceDurationText = & $ffprobe `
    -v error `
    -show_entries format=duration `
    -of default=noprint_wrappers=1:nokey=1 `
    $sourcePath
if ($LASTEXITCODE -ne 0) {
    throw "ffprobe failed with exit code $LASTEXITCODE"
}
$sourceDuration = [double]::Parse(
    $sourceDurationText.Trim(),
    [System.Globalization.CultureInfo]::InvariantCulture)

if ($EndSeconds -lt 0 -and $DurationSeconds -lt 0) {
    $DurationSeconds = $sourceDuration - $StartSeconds
}
if ($DurationSeconds -lt 0) {
    $DurationSeconds = $EndSeconds - $StartSeconds
}
if ($DurationSeconds -le 0) {
    throw "Duration must be greater than zero."
}

& $ffmpeg `
    -y `
    -ss $StartSeconds `
    -i $sourcePath `
    -t $DurationSeconds `
    -c:v libx264 `
    -c:a aac `
    $clipPath
if ($LASTEXITCODE -ne 0) {
    throw "ffmpeg clip extraction failed with exit code $LASTEXITCODE"
}

& $ffmpeg `
    -y `
    -i $clipPath `
    -vf "fps=$FramesPerSecond" `
    (Join-Path $framesDir "frame_%04d.png")
if ($LASTEXITCODE -ne 0) {
    throw "ffmpeg frame extraction failed with exit code $LASTEXITCODE"
}

@"
url=$Url
startSeconds=$StartSeconds
durationSeconds=$DurationSeconds
framesPerSecond=$FramesPerSecond
clip=$clipPath
frames=$framesDir
"@ | Set-Content -Encoding UTF8 $metadataPath

Write-Host "Clip: $clipPath"
Write-Host "Frames: $framesDir"
Write-Host "Metadata: $metadataPath"
