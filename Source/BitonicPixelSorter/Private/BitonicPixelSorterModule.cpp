// Copyright (c) 2026 yuki-is-taka. UE port of ruccho/BitonicPixelSorter (MIT). See LICENSE.

#include "Modules/ModuleManager.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"
#include "Interfaces/IPluginManager.h"
#include "SceneViewExtension.h"
#include "BitonicPixelSorterSceneViewExtension.h"

/**
 * Module for the Bitonic Pixel Sorter plugin.
 * - Maps the plugin's Shaders directory to the virtual path /Plugin/BitonicPixelSorter.
 * - Registers a SceneViewExtension that will host the post-process compute passes.
 *   The extension is inert in this skeleton (IsActiveThisFrame_Internal returns false)
 *   until the bitonic-sort port is implemented.
 */
class FBitonicPixelSorterModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("BitonicPixelSorter"));
		if (Plugin.IsValid())
		{
			const FString ShaderDir = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders"));
			AddShaderSourceDirectoryMapping(TEXT("/Plugin/BitonicPixelSorter"), ShaderDir);
		}

		ViewExtension = FSceneViewExtensions::NewExtension<FBitonicPixelSorterSceneViewExtension>();
	}

	virtual void ShutdownModule() override
	{
		ViewExtension.Reset();
	}

private:
	TSharedPtr<FBitonicPixelSorterSceneViewExtension, ESPMode::ThreadSafe> ViewExtension;
};

IMPLEMENT_MODULE(FBitonicPixelSorterModule, BitonicPixelSorter)
