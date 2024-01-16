// Fill out your copyright notice in the Description page of Project Settings.

using UnrealBuildTool;

public class S2STest : ModuleRules
{
	public S2STest(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
	
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "WebSockets", "BCClientPlugin", "BrainCloudS2SPlugin" });

		PrivateDependencyModuleNames.AddRange(new string[] { "JsonUtilities", "HTTP", "Json" });

	}
}
