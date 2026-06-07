// Copyright (c) 2026 yuki-is-taka. UE port of ruccho/BitonicPixelSorter (MIT). See LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "SceneViewExtension.h"

/**
 * SceneViewExtension that hosts the bitonic pixel-sort post-process compute passes.
 *
 * Injection point (planned): SubscribeToPostProcessingPass(EPostProcessingPass::Tonemap, ...),
 * where an FAfterPassCallbackDelegate will run the MetaPass + SortPass RDG compute dispatches
 * on the scene color. Implemented as compute (RDG) so it stays cross-platform (D3D12/Vulkan/Metal).
 *
 * Skeleton state: inert (IsActiveThisFrame_Internal returns false) until the port lands.
 */
class FBitonicPixelSorterSceneViewExtension : public FSceneViewExtensionBase
{
public:
	FBitonicPixelSorterSceneViewExtension(const FAutoRegister& AutoRegister);

	//~ Begin ISceneViewExtension interface
	virtual void SubscribeToPostProcessingPass(
		EPostProcessingPass Pass,
		const FSceneView& InView,
		FPostProcessingPassDelegateArray& InOutPassCallbacks,
		bool bIsPassEnabled) override;
	//~ End ISceneViewExtension interface

protected:
	virtual bool IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const override;
};
