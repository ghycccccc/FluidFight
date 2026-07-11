// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class SmokeCharacter : ModuleRules
{
	public SmokeCharacter(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"RenderCore",
			"RHI"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Projects"
		});
	}
}
