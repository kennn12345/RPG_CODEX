param([Parameter(Mandatory=$true)][ValidatePattern('^[A-Za-z0-9_-]+$')][string]$Name,[switch]$Visual,[int]$Fps=60)
$ErrorActionPreference='Stop'
$spellRoot=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if(Get-Process -Name 'UnrealEditor*' -ErrorAction SilentlyContinue) { throw 'Close Unreal before running an isolated review.' }
$spellDir=Join-Path $spellRoot "Saved/SpellReview/$Name"
$spellLog=Join-Path $spellRoot "Saved/Logs/Spell-$Name.log"
if((Test-Path -LiteralPath $spellDir) -or (Test-Path -LiteralPath $spellLog)) {throw 'Use a fresh review name.'}
$env:RPG_SPELL_REVIEW_DIR=$spellDir.Replace('\','/')
$spellExe='C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
$spellTest='RPG.Spells.Energy'
$spellRender=@('-nullrhi')
if($Visual) { $spellExe=$spellExe.Replace('UnrealEditor-Cmd.exe','UnrealEditor.exe');$spellTest='Visual.EnergySpell';$spellRender=@('-RenderOffscreen','-windowed','-ResX=1280','-ResY=720') }
$spellArgs=@(('"'+(Join-Path $spellRoot 'RPG_CODEX.uproject')+'"'),('"-ExecCmds=Automation RunTests '+$spellTest+'"'),'"-TestExit=Automation Test Queue Empty"','-unattended','-NoSound','-NoSplash','-UseFixedTimeStep',"-FPS=$Fps",('"-abslog='+$spellLog+'"'))+$spellRender
$spellProcess=Start-Process -FilePath $spellExe -ArgumentList $spellArgs -WindowStyle Hidden -PassThru
[pscustomobject]@{Pid=$spellProcess.Id;Log=$spellLog;Review=$spellDir;Visual=$Visual.IsPresent}
