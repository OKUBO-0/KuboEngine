param(
    [double]$FramesPerSecond = 4,
    [string]$OutputRoot = "tmp\youtube_reference_frames"
)

$ErrorActionPreference = "Stop"

$references = @(
    @{
        Name = "beam_triple_beam"
        Url = "https://youtube.com/shorts/6cfoKsKk320?si=0HtifimchKClSPwx"
    },
    @{
        Name = "shockwave_01"
        Url = "https://www.youtube.com/shorts/MCclvphPzHM"
    },
    @{
        Name = "shockwave_02"
        Url = "https://www.youtube.com/shorts/xjZCAzPBb74"
    },
    @{
        Name = "shockwave_03"
        Url = "https://www.youtube.com/shorts/JmMuGBtadcw"
    },
    @{
        Name = "radial_bullet_hell"
        Url = "https://youtube.com/shorts/ry4znX11q84?si=BakI7FaVEX8wYJvv"
    },
    @{
        Name = "converging_bullet_hell"
        Url = "https://youtube.com/shorts/PpZW43Sr9pY?si=aEN_EgRv9bVS3SM-"
    },
    @{
        Name = "dome_burst"
        Url = "https://youtube.com/shorts/UzSNoiXhE8Q?si=lh1UmptzrZ5b4K3Q"
    }
)

$scriptPath = Join-Path $PSScriptRoot "Extract-YouTubeReferenceFrames.ps1"
foreach ($reference in $references) {
    Write-Host "Extracting $($reference.Name)"
    & powershell -NoProfile -ExecutionPolicy Bypass -File $scriptPath `
        -Url $reference.Url `
        -StartSeconds 0 `
        -FramesPerSecond $FramesPerSecond `
        -OutputRoot $OutputRoot `
        -Name $reference.Name
    if ($LASTEXITCODE -ne 0) {
        throw "Extraction failed for $($reference.Name)"
    }
}
