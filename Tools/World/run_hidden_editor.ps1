# Runs a Python script inside a hidden editor that stays alive until the script calls quit_editor (unlike
# -ExecutePythonScript, which exits as soon as the startup script returns). Never touches an open editor.
# Usage: .\run_hidden_editor.ps1 -Script "C:/UNREAL PROJECTS/RPG_CODEX/Tools/World/x.py" -Log Name [-TimeoutSec 900] [-Extra "-ExecCmds=..."]
param([Parameter(Mandatory)][string]$Script, [Parameter(Mandatory)][string]$Log, [int]$TimeoutSec = 900, [string]$Launcher = "")
$project = "C:\UNREAL PROJECTS\RPG_CODEX"
if (-not $Launcher) { $Launcher = Join-Path $env:TEMP "rpg_run_target.py" }
# Any Python error quits the hidden editor (otherwise it would sit idle until the timeout).
Set-Content -Path $Launcher -Encoding utf8 -Value "import os, runpy, traceback, unreal`ntry:`n    runpy.run_path(os.environ['RPG_PY_TARGET'], run_name='__main__')`nexcept Exception:`n    unreal.log_error('HIDDEN_RUN_FAILED ' + traceback.format_exc())`n    unreal.SystemLibrary.quit_editor()"
if ($Launcher -match ' ') { throw "launcher path must not contain spaces: $Launcher" }
$env:RPG_PY_TARGET = $Script
$logPath = "$project\Saved\Logs\$Log.log"
$args = "`"$project\RPG_CODEX.uproject`" -ExecCmds=`"py $($Launcher -replace '\\','/')`" -unattended -RenderOffscreen -NoSplash -NoLiveCoding -abslog=`"$logPath`""
$p = Start-Process -FilePath "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" -ArgumentList $args -PassThru
if (-not $p.WaitForExit($TimeoutSec * 1000)) { "TIMEOUT: stopping hidden editor $($p.Id)"; Stop-Process -Id $p.Id }
Select-String -Path $logPath -Pattern "LogPython: [A-Z_]{4,}|Traceback|LogPython: Error" | Select-Object -First 40 | ForEach-Object { $_.Line }
