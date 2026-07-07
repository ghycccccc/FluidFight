// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class FluidFight : ModuleRules
{
	public FluidFight(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"FluidFight",
			"FluidFight/Variant_Platforming",
			"FluidFight/Variant_Platforming/Animation",
			"FluidFight/Variant_Combat",
			"FluidFight/Variant_Combat/AI",
			"FluidFight/Variant_Combat/Animation",
			"FluidFight/Variant_Combat/Gameplay",
			"FluidFight/Variant_Combat/Interfaces",
			"FluidFight/Variant_Combat/UI",
			"FluidFight/Variant_SideScrolling",
			"FluidFight/Variant_SideScrolling/AI",
			"FluidFight/Variant_SideScrolling/Gameplay",
			"FluidFight/Variant_SideScrolling/Interfaces",
			"FluidFight/Variant_SideScrolling/UI"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
