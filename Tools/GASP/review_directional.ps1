param([Parameter(Mandatory=$true)][ValidatePattern('^[A-Za-z0-9_-]+$')][string]$Name,[switch]$Visual,[int]$Fps=60)
$ErrorActionPreference='Stop'
$directionRoot=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if(Get-Process -Name 'UnrealEditor*' -ErrorAction SilentlyContinue){throw 'Close Unreal before an isolated review.'}
$directionDir=Join-Path $directionRoot "Saved/DirectionalReview/$Name"
$directionLog=Join-Path $directionRoot "Saved/Logs/Directional-$Name.log"
if((Test-Path -LiteralPath $directionDir) -or (Test-Path -LiteralPath $directionLog)){throw 'Review already exists; choose a fresh name.'}
$env:RPG_DIRECTION_REVIEW_DIR=$directionDir.Replace('\','/')
$directionExe='C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
$directionTest='RPG.Movement.Directional';$directionRender=@('-nullrhi')
if($Visual){$directionExe=$directionExe.Replace('UnrealEditor-Cmd.exe','UnrealEditor.exe');$directionTest='Visual.DirectionalMovement';$directionRender=@('-RenderOffscreen','-windowed','-ResX=1280','-ResY=720')}
$directionArgs=@(('"'+(Join-Path $directionRoot 'RPG_CODEX.uproject')+'"'),('"-ExecCmds=Automation RunTests '+$directionTest+'"'),'"-TestExit=Automation Test Queue Empty"','-unattended','-NoSound','-NoSplash','-UseFixedTimeStep',"-FPS=$Fps",('"-abslog='+$directionLog+'"'))+$directionRender
$directionProcess=Start-Process -FilePath $directionExe -ArgumentList $directionArgs -WindowStyle Hidden -PassThru
[pscustomobject]@{Pid=$directionProcess.Id;Log=$directionLog;Review=$directionDir}
