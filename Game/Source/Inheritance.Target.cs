using UnrealBuildTool;

public class InheritanceTarget : TargetRules
{
	public InheritanceTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("Inheritance");
	}
}
