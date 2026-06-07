// Copyright (c) 2026 yuki-is-taka. UE port of ruccho/BitonicPixelSorter (MIT). See LICENSE.

using UnrealBuildTool;

public class BitonicPixelSorter : ModuleRules
{
	public BitonicPixelSorter(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Cross-platform by design: depends only on portable rendering modules (RDG/RHI/RenderCore).
		// No platform-specific dependencies — Win64 + Mac today, headroom for Linux/others.
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"RenderCore", // RDG (FRDGBuilder), AddShaderSourceDirectoryMapping
			"RHI",
			"Renderer",   // ScreenPass, PostProcessMaterialInputs (SceneViewExtension post-process hook)
			"Projects",   // IPluginManager (shader directory mapping)
		});
	}
}
