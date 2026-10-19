using UnrealBuildTool;

public class InheritanceEditorTarget : TargetRules
{
	public InheritanceEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("Inheritance");
	}
}
