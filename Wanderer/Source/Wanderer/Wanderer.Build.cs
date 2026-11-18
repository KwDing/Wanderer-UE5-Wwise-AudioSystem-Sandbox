// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Wanderer : ModuleRules
{
	public Wanderer(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "DeveloperSettings" });
		PublicDependencyModuleNames.AddRange(new string[] { "UMG", "SlateCore", "InteriorFitRuntime" });
		PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "PhysicsCore", "MoviePlayer", "RenderCore", "RHI", "PCG", "Landscape", "Niagara", "AkAudio", "WwiseSoundEngine", "WwiseResourceLoader", "WwiseFileHandler", "WwiseConcurrency" });
		if (Target.bBuildWithEditorOnlyData)
		{
			PrivateDependencyModuleNames.AddRange(new string[] { "WwiseResourceCooker", "WwiseProjectDatabase" });
		}
	}
}
