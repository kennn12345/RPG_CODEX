# Saves the test-square ground fixes of 2026-11-09 (Docs/GROUND_FIX_2026-11-09.md) for real.
# 1. Close the Unreal editor (it locks the terrain, rocks and level files).  2. Run:
#      powershell -ExecutionPolicy Bypass -File "C:\UNREAL PROJECTS\RPG_CODEX\Tools\World\apply_ground_fixes.ps1"
# It refuses to run while any Unreal editor is open, then applies + saves every fix in a hidden editor, then re-checks
# the saved result (collision gap, stray water) and prints a summary.
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
if (Get-Process UnrealEditor -ErrorAction SilentlyContinue) { Write-Host "An Unreal editor is still open - close it (saving your work) and run this again."; exit 1 }
$env:RPG_GROUND_SAVE = '1'; Remove-Item Env:RPG_GROUND_VERIFY -ErrorAction SilentlyContinue
& "$here\run_hidden_editor.ps1" -Script "C:/UNREAL PROJECTS/RPG_CODEX/Tools/World/apply_ground_fixes.py" -Log GroundApplySave -TimeoutSec 1800 | Out-Null
$log = "C:\UNREAL PROJECTS\RPG_CODEX\Saved\Logs\GroundApplySave.log"
Select-String -Path $log -Pattern "GROUNDEDIT|ROCKFIX|TERRAIN_COLLISION|GROUND_APPLY_DONE|HIDDEN_RUN_FAILED|save failed" | ForEach-Object { $_.Line -replace '^.*LogPython: ','' }
if (-not (Select-String -Path $log -Pattern "GROUND_APPLY_DONE save=True" -Quiet)) { Write-Host "NOT SAVED - see $log"; exit 1 }
Remove-Item Env:RPG_GROUND_SAVE
# re-check the saved files in a fresh editor
$env:RPG_WATER_TAG = 'saved'; $env:RPG_GAP_TAG = 'saved'
& "$here\run_hidden_editor.ps1" -Script "C:/UNREAL PROJECTS/RPG_CODEX/Tools/World/analyze_vista_water.py" -Log GroundCheckWater -TimeoutSec 900 | Out-Null
Select-String -Path "C:\UNREAL PROJECTS\RPG_CODEX\Saved\Logs\GroundCheckWater.log" -Pattern "WATER_POND|WATER_RIVER" | ForEach-Object { $_.Line -replace '^.*LogPython: ','' }
& "$here\run_hidden_editor.ps1" -Script "C:/UNREAL PROJECTS/RPG_CODEX/Tools/World/measure_terrain_collision.py" -Log GroundCheckGap -TimeoutSec 900 | Out-Null
Select-String -Path "C:\UNREAL PROJECTS\RPG_CODEX\Saved\Logs\GroundCheckGap.log" -Pattern "GAP_DONE" | ForEach-Object { $_.Line -replace '^.*LogPython: ','' }
Write-Host "Done. Open Unreal and L_WorldVista to see the changes."
