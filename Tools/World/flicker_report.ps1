# Measures frame-to-frame flicker from Visual.FlickerShots photos (Source/RPG_CODEX/RPGWaterShots.cpp).
#   Run through the PowerShell tool (scripts are disabled for plain -File), as a ScriptBlock:
#   params: -Root Saved/Screenshots/Flicker  -Runs base,freeze,...  -Heat (writes <run>_<camera>_heat.png, red = flickers)
# For each run + camera: mean brightness change between consecutive photos (0-255), the share of 32 px blocks that change
# a lot, and the worst block's position (% of image, x,y). Compare a frozen run with one setting changed against the frozen base.
param([string]$Root, [string[]]$Runs, [switch]$Heat, [int]$Block = 32)
Add-Type -AssemblyName System.Drawing
foreach ($run in $Runs) {
  $dir = Join-Path $Root $run
  $cams = Get-ChildItem $dir -Filter '*_0.png' | ForEach-Object { $_.Name -replace '_0\.png$', '' }
  foreach ($cam in $cams) {
    $files = Get-ChildItem $dir -Filter "${cam}_*.png" | Where-Object { $_.Name -match "^$([regex]::Escape($cam))_\d+\.png$" } | Sort-Object { [int]($_.BaseName -replace '.*_', '') }
    $prev = $null; $W = 0; $H = 0; $B = $Block; $acc = $null; $pairs = 0
    foreach ($f in $files) {
      $bmp = New-Object System.Drawing.Bitmap $f.FullName
      $W = $bmp.Width; $H = $bmp.Height
      $rect = New-Object System.Drawing.Rectangle 0, 0, $W, $H
      $d = $bmp.LockBits($rect, 'ReadOnly', 'Format24bppRgb'); $s = $d.Stride
      $buf = New-Object byte[] ($s * $H); [Runtime.InteropServices.Marshal]::Copy($d.Scan0, $buf, 0, $buf.Length); $bmp.UnlockBits($d); $bmp.Dispose()
      $bw = [int][math]::Floor($W / $B); $bh = [int][math]::Floor($H / $B)
      $cur = New-Object 'double[]' ($bw * $bh)
      for ($y = 0; $y -lt $bh * $B; $y += [math]::Max(1, [int]($B / 8))) { $o = $y * $s; for ($x = 0; $x -lt $bw * $B; $x += [math]::Max(1, [int]($B / 8))) { $i = $o + $x * 3; $cur[[int][math]::Floor($y / $B) * $bw + [int][math]::Floor($x / $B)] += $buf[$i] + $buf[$i + 1] + $buf[$i + 2] } }
      $st = [math]::Max(1, [int]($B / 8)); $n = 3.0 * [math]::Ceiling($B / $st) * [math]::Ceiling($B / $st)
      for ($k = 0; $k -lt $cur.Length; $k++) { $cur[$k] /= $n }
      if ($prev) {
        if (-not $acc) { $acc = New-Object 'double[]' $cur.Length }
        for ($k = 0; $k -lt $cur.Length; $k++) { $acc[$k] += [math]::Abs($cur[$k] - $prev[$k]) }
        $pairs++
      }
      $prev = $cur
    }
    if (-not $acc) { continue }
    $sum = 0.0; $big = 0; $worst = 0.0; $wi = 0
    for ($k = 0; $k -lt $acc.Length; $k++) { $acc[$k] /= $pairs; $sum += $acc[$k]; if ($acc[$k] -gt 3.0) { $big++ }; if ($acc[$k] -gt $worst) { $worst = $acc[$k]; $wi = $k } }
    "{0,-14} {1,-26} mean {2,6:N3}  flickering blocks {3,5:N1}%  worst {4,6:N2} at x{5,3}% y{6,3}%" -f $run, $cam, ($sum / $acc.Length), (100.0 * $big / $acc.Length), $worst, [int](100 * ($wi % $bw) / $bw), [int](100 * [math]::Floor($wi / $bw) / $bh)
    if ($Heat) {
      $out = New-Object System.Drawing.Bitmap $bw, $bh
      for ($k = 0; $k -lt $acc.Length; $k++) { $v = [int][math]::Min(255, $acc[$k] * 40); $out.SetPixel($k % $bw, [int][math]::Floor($k / $bw), [System.Drawing.Color]::FromArgb(255, $v, 0, 0)) }
      $out.Save((Join-Path $Root "${run}_${cam}_heat.png")); $out.Dispose()
    }
  }
}
