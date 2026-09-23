// Fill out your copyright notice in the Description page of Project Settings.

using UnrealBuildTool;

public class FPSRL : ModuleRules
{
	public FPSRL(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Headers are included relative to the module root, e.g. "Types/FPSRLTypes.h".
		PublicIncludePaths.Add(ModuleDirectory);

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",	// Input Actions / Mapping Contexts
			"GameplayTags",		// Native tags (Types/FPSRLGameplayTags.h)
			"GameplayAbilities",	// GAS: ability system component, attributes, effects, abilities
			"GameplayTasks",		// GAS ability tasks
			"NetCore",			// Replication helpers (push model, FFastArraySerializer)
			"DeveloperSettings"	// Project Settings pages for tuning/config
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"UMG",
			"Slate",
			"SlateCore",
			"AIModule",
			"NavigationSystem",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"OnlineSubsystem",		// Steam sessions via OnlineSubsystemSteam (enabled in .uproject)
			"OnlineSubsystemUtils"
		});
	}
}
