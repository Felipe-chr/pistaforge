# Gera resources/pistaforge.ico a partir de um desenho vetorial simples.
#
# Uso: powershell -ExecutionPolicy Bypass -File tools/make-icon.ps1
# Requer Windows PowerShell (System.Drawing).

param(
    [string]$Output = (Join-Path $PSScriptRoot "..\resources\pistaforge.ico")
)

Add-Type -AssemblyName System.Drawing

$sizes = @(16, 24, 32, 48, 64, 128, 256)
$frames = New-Object System.Collections.Generic.List[object]

foreach ($size in $sizes) {
    $bitmap = New-Object System.Drawing.Bitmap -ArgumentList $size, $size, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $graphics.Clear([System.Drawing.Color]::Transparent)

    $pad = [Math]::Max(1.0, $size * 0.035)
    $box = New-Object System.Drawing.RectangleF -ArgumentList $pad, $pad, ($size - 2 * $pad), ($size - 2 * $pad)
    $radius = [Math]::Max(2.0, $size * 0.20)
    $diameter = $radius * 2

    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $path.AddArc($box.X, $box.Y, $diameter, $diameter, 180, 90)
    $path.AddArc(($box.Right - $diameter), $box.Y, $diameter, $diameter, 270, 90)
    $path.AddArc(($box.Right - $diameter), ($box.Bottom - $diameter), $diameter, $diameter, 0, 90)
    $path.AddArc($box.X, ($box.Bottom - $diameter), $diameter, $diameter, 90, 90)
    $path.CloseFigure()

    $top = [System.Drawing.Color]::FromArgb(255, 30, 41, 59)
    $bottom = [System.Drawing.Color]::FromArgb(255, 2, 6, 23)
    $background = New-Object System.Drawing.Drawing2D.LinearGradientBrush -ArgumentList $box, $top, $bottom, 90.0
    $graphics.FillPath($background, $path)

    $inset = [Math]::Max(1.5, $size * 0.24)
    $track = New-Object System.Drawing.RectangleF -ArgumentList $inset, $inset, ($size - 2 * $inset), ($size - 2 * $inset)
    $penWidth = [Math]::Max(1.2, $size * 0.105)
    $trackColor = [System.Drawing.Color]::FromArgb(255, 226, 232, 240)
    $trackPen = New-Object System.Drawing.Pen -ArgumentList $trackColor, $penWidth
    $trackPen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
    $trackPen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
    $graphics.DrawEllipse($trackPen, $track)

    $accentColor = [System.Drawing.Color]::FromArgb(255, 34, 211, 238)
    $accentPen = New-Object System.Drawing.Pen -ArgumentList $accentColor, $penWidth
    $accentPen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
    $accentPen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
    $graphics.DrawArc($accentPen, $track, 200.0, 115.0)

    $centerX = $size / 2.0
    $centerY = $size / 2.0
    $ringRadius = ($size - 2 * $inset) / 2.0
    $angle = 135.0 * [Math]::PI / 180.0
    $dotX = $centerX + $ringRadius * [Math]::Cos($angle)
    $dotY = $centerY + $ringRadius * [Math]::Sin($angle)
    $dotRadius = [Math]::Max(1.0, $size * 0.075)
    $dotColor = [System.Drawing.Color]::FromArgb(255, 249, 115, 22)
    $dotBrush = New-Object System.Drawing.SolidBrush -ArgumentList $dotColor
    $dotBox = New-Object System.Drawing.RectangleF -ArgumentList ($dotX - $dotRadius), ($dotY - $dotRadius), (2 * $dotRadius), (2 * $dotRadius)
    $graphics.FillEllipse($dotBrush, $dotBox)

    $dotRingColor = [System.Drawing.Color]::FromArgb(255, 15, 23, 42)
    $dotRing = New-Object System.Drawing.Pen -ArgumentList $dotRingColor, ([Math]::Max(0.8, $size * 0.018))
    $graphics.DrawEllipse($dotRing, $dotBox)

    $graphics.Dispose()

    $rect = New-Object System.Drawing.Rectangle -ArgumentList 0, 0, $size, $size
    $locked = $bitmap.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $stride = $locked.Stride
    $pixels = New-Object byte[] ($stride * $size)
    [System.Runtime.InteropServices.Marshal]::Copy($locked.Scan0, $pixels, 0, $pixels.Length)
    $bitmap.UnlockBits($locked)
    $bitmap.Dispose()

    $maskStride = [int]([Math]::Floor(($size + 31) / 32) * 4)
    $maskSize = $maskStride * $size

    $stream = New-Object System.IO.MemoryStream
    $writer = New-Object System.IO.BinaryWriter -ArgumentList $stream
    $writer.Write([int]40)                    # biSize
    $writer.Write([int]$size)                 # biWidth
    $writer.Write([int]($size * 2))           # biHeight (XOR + AND)
    $writer.Write([int16]1)                   # biPlanes
    $writer.Write([int16]32)                  # biBitCount
    $writer.Write([int]0)                     # biCompression
    $writer.Write([int]($stride * $size + $maskSize))
    $writer.Write([int]0)
    $writer.Write([int]0)
    $writer.Write([int]0)
    $writer.Write([int]0)

    for ($y = $size - 1; $y -ge 0; $y--) {
        $writer.Write($pixels, ($y * $stride), $stride)
    }
    $writer.Write((New-Object byte[] $maskSize), 0, $maskSize)
    $writer.Flush()
    $image = $stream.ToArray()
    $writer.Dispose()

    $frames.Add([pscustomobject]@{ Size = $size; Data = $image })
}

$count = $frames.Count
$offset = 6 + 16 * $count
$outputStream = New-Object System.IO.MemoryStream
$outputWriter = New-Object System.IO.BinaryWriter -ArgumentList $outputStream
$outputWriter.Write([int16]0)
$outputWriter.Write([int16]1)
$outputWriter.Write([int16]$count)

foreach ($frame in $frames) {
    $dimension = if ($frame.Size -ge 256) { 0 } else { $frame.Size }
    $outputWriter.Write([byte]$dimension)
    $outputWriter.Write([byte]$dimension)
    $outputWriter.Write([byte]0)
    $outputWriter.Write([byte]0)
    $outputWriter.Write([int16]1)
    $outputWriter.Write([int16]32)
    $outputWriter.Write([int]$frame.Data.Length)
    $outputWriter.Write([int]$offset)
    $offset += $frame.Data.Length
}

foreach ($frame in $frames) {
    $outputWriter.Write($frame.Data, 0, $frame.Data.Length)
}

$outputWriter.Flush()
[System.IO.Directory]::CreateDirectory([System.IO.Path]::GetDirectoryName($Output)) | Out-Null
[System.IO.File]::WriteAllBytes($Output, $outputStream.ToArray())
$outputWriter.Dispose()

Write-Output "Gerado $Output com $count tamanhos."
