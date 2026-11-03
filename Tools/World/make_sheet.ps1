# Side-by-side review sheet: images in a grid, each with a caption (reference | ours ...).
#   powershell -File make_sheet.ps1 -Out sheet.png -Cols 2 -Width 640 -Images a.png,b.png -Labels "Reference t081","Ours R02"
param([string]$Out, [int]$Cols = 2, [int]$Width = 640, [string[]]$Images, [string[]]$Labels, [string]$Title = '')
Add-Type -AssemblyName System.Drawing
$imgs = $Images | ForEach-Object { [System.Drawing.Image]::FromFile($_) }
$h = [int]($Width * 9 / 16); $cap = 22; $top = $(if ($Title) { 30 } else { 0 })
$rows = [math]::Ceiling($imgs.Count / $Cols)
$bmp = New-Object System.Drawing.Bitmap ($Cols * $Width), ($top + $rows * ($h + $cap))
$g = [System.Drawing.Graphics]::FromImage($bmp); $g.Clear([System.Drawing.Color]::FromArgb(24, 24, 24))
$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$font = New-Object System.Drawing.Font 'Segoe UI', 11; $tf = New-Object System.Drawing.Font 'Segoe UI', 14, ([System.Drawing.FontStyle]::Bold)
if ($Title) { $g.DrawString($Title, $tf, [System.Drawing.Brushes]::White, 8, 4) }
for ($i = 0; $i -lt $imgs.Count; $i++) {
  $im = $imgs[$i]; $x = ($i % $Cols) * $Width; $y = $top + [math]::Floor($i / $Cols) * ($h + $cap)
  # cover-fit the image into Width x h
  $s = [math]::Max($Width / $im.Width, $h / $im.Height); $sw = $Width / $s; $sh = $h / $s
  $src = New-Object System.Drawing.RectangleF (($im.Width - $sw) / 2), (($im.Height - $sh) / 2), $sw, $sh
  $g.DrawImage($im, (New-Object System.Drawing.RectangleF $x, ($y + $cap), $Width, $h), $src, [System.Drawing.GraphicsUnit]::Pixel)
  if ($Labels -and $i -lt $Labels.Count) { $g.DrawString($Labels[$i], $font, [System.Drawing.Brushes]::White, ($x + 6), ($y + 2)) }
}
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png); $g.Dispose(); $bmp.Dispose(); $imgs | ForEach-Object { $_.Dispose() }
Write-Output "SHEET $Out"
