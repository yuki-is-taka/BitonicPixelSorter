// Copyright (c) 2026 yuki-is-taka. UE port of ruccho/BitonicPixelSorter (MIT). See LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/BlendableInterface.h"
#include "BitonicPixelSorterBlendable.generated.h"

/**
 * Plain blended payload stored per view in FFinalPostProcessSettings::BlendableManager.
 * FBlendableManager requires GetFName() (type tag) and SetBaseValues() (blend starting point).
 */
struct FBitonicPixelSorterBlendData
{
	float Angle;
	float ThresholdMin;
	float ThresholdMax;
	float Strength;
	int32 bAscending;

	/** Starting point when no blendable has contributed yet. Strength 0 so volume falloff fades in. */
	void SetBaseValues()
	{
		Angle = 0.0f;
		ThresholdMin = 0.4f;
		ThresholdMax = 0.6f;
		Strength = 0.0f;
		bAscending = 1;
	}

	/** Type tag for FBlendableManager type safety. */
	static const FName& GetFName()
	{
		static const FName Name(TEXT("FBitonicPixelSorterBlendData"));
		return Name;
	}
};

/**
 * Drives the bitonic pixel sorter from a Post Process Volume, Camera, or PostProcessComponent.
 *
 * Create an instance asset and add it to the volume/camera/component "Blendables" (Post Process
 * Materials) array. Its properties are blended per view (weighted by the volume's falloff/priority)
 * into the view's FinalPostProcessSettings, where the plugin's SceneViewExtension reads them. When
 * no blendable affects a view, the effect falls back to the r.BitonicPixelSorter.* CVars.
 *
 * Properties are BlueprintReadWrite and (where it makes sense) interp, i.e. Sequencer-animatable.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Bitonic Pixel Sorter Blendable"))
class BITONICPIXELSORTER_API UBitonicPixelSorterBlendable : public UDataAsset, public IBlendableInterface
{
	GENERATED_BODY()

public:
	/** Sort line angle in degrees, [0,180). 0 = horizontal rows, 90 = vertical columns, 45 = diagonal. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, interp, Category = "Bitonic Pixel Sorter",
		meta = (ClampMin = "0.0", ClampMax = "180.0", UIMin = "0.0", UIMax = "180.0"))
	float Angle = 0.0f;

	/** Only pixels whose brightness is within [ThresholdMin, ThresholdMax] are sorted. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, interp, Category = "Bitonic Pixel Sorter",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ThresholdMin = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, interp, Category = "Bitonic Pixel Sorter",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ThresholdMax = 0.6f;

	/** Effect strength 0..1: cross-fade between the original (0) and the fully sorted (1) image. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, interp, Category = "Bitonic Pixel Sorter",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Strength = 1.0f;

	/** Sort order: true = ascending by brightness, false = descending. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bitonic Pixel Sorter")
	bool bAscending = true;

	//~ Begin IBlendableInterface
	virtual void OverrideBlendableSettings(FSceneView& View, float Weight) const override;
	//~ End IBlendableInterface
};
