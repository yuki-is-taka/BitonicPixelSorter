// Copyright (c) 2026 yuki-is-taka. UE port of ruccho/BitonicPixelSorter (MIT). See LICENSE.

#include "BitonicPixelSorterBlendable.h"
#include "SceneView.h"

void UBitonicPixelSorterBlendable::OverrideBlendableSettings(FSceneView& View, float Weight) const
{
	// FBlendableManager asserts Weight is in (0,1]; the engine excludes 0, but guard defensively.
	if (Weight <= 0.0f)
	{
		return;
	}

	// GetSingleFinalData returns the one accumulating entry for this type (creating a SetBaseValues()
	// base on first use). Each contributing volume lerps it toward its own values by Weight, so
	// overlapping volumes blend by falloff/priority -- the same scheme FPostProcessSettings uses.
	FBitonicPixelSorterBlendData& Dest =
		View.FinalPostProcessSettings.BlendableManager.GetSingleFinalData<FBitonicPixelSorterBlendData>();

	Dest.Angle = FMath::Lerp(Dest.Angle, Angle, Weight);
	Dest.ThresholdMin = FMath::Lerp(Dest.ThresholdMin, ThresholdMin, Weight);
	Dest.ThresholdMax = FMath::Lerp(Dest.ThresholdMax, ThresholdMax, Weight);
	Dest.Strength = FMath::Lerp(Dest.Strength, Strength, Weight);

	// bAscending is discrete; take the dominant contributor.
	if (Weight >= 0.5f)
	{
		Dest.bAscending = bAscending ? 1 : 0;
	}
}
