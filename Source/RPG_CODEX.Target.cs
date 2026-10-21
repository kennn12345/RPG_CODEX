using UnrealBuildTool;
using System.Collections.Generic;

public class RPG_CODEXTarget : TargetRules
{
	public RPG_CODEXTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("RPG_CODEX");
	}
}
