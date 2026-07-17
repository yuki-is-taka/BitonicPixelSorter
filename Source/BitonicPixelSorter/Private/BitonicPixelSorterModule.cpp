// Copyright (c) 2026 yuki-is-taka. UE port of ruccho/BitonicPixelSorter (MIT). See LICENSE.

#include "Modules/ModuleManager.h"
#include "Misc/Paths.h"
#include "Misc/CoreDelegates.h"
#include "ShaderCore.h"
#include "Interfaces/IPluginManager.h"
#include "SceneViewExtension.h"
#include "BitonicPixelSorterSceneViewExtension.h"

/**
 * Module for the Bitonic Pixel Sorter plugin.
 * - Maps the plugin's Shaders directory to /Plugin/BitonicPixelSorter (must happen early, before
 *   global shaders are initialized -> done in StartupModule at LoadingPhase PostConfigInit).
 * - Registers the SceneViewExtension that hosts the post-process compute passes. This is deferred
 *   to OnPostEngineInit because FSceneViewExtensions::NewExtension requires GEngine, which does not
 *   exist yet at PostConfigInit.
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

		PostEngineInitHandle = FCoreDelegates::GetOnPostEngineInit().AddLambda([this]()
		{
			ViewExtension = FSceneViewExtensions::NewExtension<FBitonicPixelSorterSceneViewExtension>();
		});
	}

	virtual void ShutdownModule() override
	{
		FCoreDelegates::GetOnPostEngineInit().Remove(PostEngineInitHandle);
		ViewExtension.Reset();
	}

private:
	FDelegateHandle PostEngineInitHandle;
	TSharedPtr<FBitonicPixelSorterSceneViewExtension, ESPMode::ThreadSafe> ViewExtension;
};

IMPLEMENT_MODULE(FBitonicPixelSorterModule, BitonicPixelSorter)
