// Copyright (c) 2026 yuki-is-taka. UE port of ruccho/BitonicPixelSorter (MIT). See LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "SceneViewExtension.h"

struct FScreenPassTexture;
struct FPostProcessMaterialInputs;

/**
 * SceneViewExtension that runs the bitonic pixel-sort compute passes as a post-process effect.
 *
 * Controlled by console variables (r.BitonicPixelSorter.*). When r.BitonicPixelSorter.Enable != 0
 * the extension subscribes to the Tonemap post-processing pass and replaces the scene color with
 * the sorted result.
 *
 * NOTE: the algorithm sorts an entire line in group-shared memory, so it only runs when the sorted
 * axis is < 2048 px (otherwise the scene color is returned unchanged).
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

private:
	/** Post-process pass callback: scene color -> bitonic sort -> result. */
	FScreenPassTexture PostProcessPass_RenderThread(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		const FPostProcessMaterialInputs& Inputs);
};
