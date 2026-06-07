// Copyright (c) 2026 yuki-is-taka. UE port of ruccho/BitonicPixelSorter (MIT). See LICENSE.

#include "BitonicPixelSorterSceneViewExtension.h"

FBitonicPixelSorterSceneViewExtension::FBitonicPixelSorterSceneViewExtension(const FAutoRegister& AutoRegister)
	: FSceneViewExtensionBase(AutoRegister)
{
}

bool FBitonicPixelSorterSceneViewExtension::IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const
{
	// Skeleton: disabled until the bitonic-sort compute passes are implemented and wired to settings.
	return false;
}

void FBitonicPixelSorterSceneViewExtension::SubscribeToPostProcessingPass(
	EPostProcessingPass Pass,
	const FSceneView& InView,
	FPostProcessingPassDelegateArray& InOutPassCallbacks,
	bool bIsPassEnabled)
{
	// TODO(port): when Pass == EPostProcessingPass::Tonemap, append an FAfterPassCallbackDelegate
	// that builds the MetaPass + SortPass RDG compute passes over the scene color and returns the
	// sorted result. Left empty in the skeleton.
}
