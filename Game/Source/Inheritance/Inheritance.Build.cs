using UnrealBuildTool;

public class Inheritance : ModuleRules
{
	public Inheritance(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicIncludePaths.Add(ModuleDirectory);

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"UMG",
			"Slate",
			"SlateCore",
			"HTTP",
			"Json",
			"DeveloperSettings",
			"AudioCaptureCore",
			// UCineCameraComponent: filmback, lenses and physically based depth of field for the camera pawn.
			"CinematicCamera",
		});

		if (Target.Platform == UnrealTargetPlatform.Android)
		{
			PrivateDependencyModuleNames.Add("AndroidPermission");
		}
	}
}
