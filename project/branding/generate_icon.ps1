# Rebuild the original Kubo mark and its Windows icon frames.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

function New-KuboIconFrame([int]$size) {
    $bitmap = [Drawing.Bitmap]::new($size, $size, [Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    $shape = [Drawing.Drawing2D.GraphicsPath]::new()
    try {
        $graphics.SmoothingMode = [Drawing.Drawing2D.SmoothingMode]::AntiAlias
        $graphics.ScaleTransform($size / 128.0, $size / 128.0)
        foreach ($corner in @(@(3,3,180), @(85,3,270), @(85,85,0), @(3,85,90))) {
            $shape.AddArc($corner[0], $corner[1], 40, 40, $corner[2], 90)
        }
        $shape.CloseFigure()
        $tile = [Drawing.Drawing2D.LinearGradientBrush]::new(
            [Drawing.Rectangle]::new(3,3,122,122), [Drawing.Color]::FromArgb(67,24,37),
            [Drawing.Color]::FromArgb(22,12,19), 45.0)
        $rim = [Drawing.Pen]::new([Drawing.Color]::FromArgb(132,52,71), 2.0)
        try {
            $graphics.FillPath($tile, $shape)
            $graphics.DrawPath($rim, $shape)
        } finally { $tile.Dispose(); $rim.Dispose() }

        # A continuous outline keeps the stem and both diagonals connected at 16 px.
        $outline = @(28,27, 47,27, 47,56, 77,27, 101,27, 65,62, 103,101, 78,101, 47,69, 47,101, 28,101)
        $points = for ($i = 0; $i -lt $outline.Count; $i += 2) {
            [Drawing.Point]::new($outline[$i], $outline[$i + 1])
        }
        $mark = [Drawing.Drawing2D.LinearGradientBrush]::new(
            [Drawing.Rectangle]::new(27,26,78,77), [Drawing.Color]::FromArgb(255,179,181),
            [Drawing.Color]::FromArgb(246,68,101), 90.0)
        try { $graphics.FillPolygon($mark, [Drawing.Point[]]$points) }
        finally { $mark.Dispose() }

        $stream = [IO.MemoryStream]::new()
        try {
            $bitmap.Save($stream, [Drawing.Imaging.ImageFormat]::Png)
            return ,$stream.ToArray()
        } finally { $stream.Dispose() }
    } finally { $shape.Dispose(); $graphics.Dispose(); $bitmap.Dispose() }
}

$sizes = @(16, 20, 24, 32, 40, 48, 64, 128)
$frames = @{}
foreach ($size in $sizes) { $frames[$size] = New-KuboIconFrame $size }
[IO.File]::WriteAllBytes((Join-Path $PSScriptRoot 'KuboEngine-128.png'), $frames[128])
$output = [IO.BinaryWriter]::new([IO.File]::Create((Join-Path $PSScriptRoot 'KuboEngine.ico')))
try {
    $output.Write([uint16]0)
    $output.Write([uint16]1)
    $output.Write([uint16]$sizes.Count)
    $offset = 6 + 16 * $sizes.Count
    foreach ($size in $sizes) {
        $output.Write([byte]$size)
        $output.Write([byte]$size)
        $output.Write([uint16]0)
        $output.Write([uint16]1)
        $output.Write([uint16]32)
        $output.Write([uint32]$frames[$size].Length)
        $output.Write([uint32]$offset)
        $offset += $frames[$size].Length
    }
    foreach ($size in $sizes) { $output.Write([byte[]]$frames[$size]) }
} finally { $output.Dispose() }
