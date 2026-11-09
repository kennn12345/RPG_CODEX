# Noon cloud captures in a hidden editor (the user's open editor is untouched).
# Example: .\capture_cloud_look.ps1 -Tag Puffs_A -Styles 2 -Scalar "Cloud_GlobalCoverage=-0.1"
param(
    [string]$Tag = "Noon",
    [string]$Styles = "123456",
    [string]$Scalar = "",
    [string]$Vector = "",
    [string]$Texture = ""
)
$project = "C:\UNREAL PROJECTS\RPG_CODEX"
$out = Join-Path $project "Saved\CloudLook\$Tag"
if (Test-Path $out) { Remove-Item "$out\*.png" -ErrorAction SilentlyContinue }
$args = "`"$project\RPG_CODEX.uproject`" /Game/RPG/Maps/L_WorldVista -RenderOffscreen -unattended -NoSplash -NoLiveCoding " +
        "-CloudTag=$Tag -CloudStyles=$Styles "
if ($Scalar) { $args += "-CloudScalar=`"$Scalar`" " }
if ($Vector) { $args += "-CloudVector=`"$Vector`" " }
if ($Texture) { $args += "-CloudTexture=`"$Texture`" " }
$args += "-ExecCmds=`"Automation RunTests Visual.CloudStylesNoon`" -TestExit=`"Automation Test Queue Empty`" -abslog=`"$project\Saved\Logs\CloudLook_$Tag.log`""
$p = Start-Process -FilePath "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" -ArgumentList $args -PassThru -Wait
Select-String -Path "$project\Saved\Logs\CloudLook_$Tag.log" -Pattern "CloudLook override|CloudLook texture|Test Completed" | ForEach-Object { $_.Line }
Get-ChildItem $out -Filter *.png | Select-Object -ExpandProperty Name
