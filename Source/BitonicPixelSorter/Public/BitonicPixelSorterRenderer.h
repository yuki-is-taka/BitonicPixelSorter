// Copyright (c) 2026 yuki-is-taka. UE port of ruccho/BitonicPixelSorter (MIT). See LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "RenderGraphFwd.h"
#include "RHIDefinitions.h"

class FRDGBuilder;

/** Parameters for one bitonic pixel-sort dispatch. Mirrors the original Unity component's options. */
struct FBitonicPixelSorterParams
{
	/** Sort line angle in degrees, [0,180). 0 = horizontal rows, 90 = vertical columns, 45 = diagonal. */
	float Angle = 0.0f;

	/** Sort order. true = ascending by brightness. */
	bool bAscending = true;

	/** Pixels are sorted only where brightness is within [ThresholdMin, ThresholdMax]. */
	float ThresholdMin = 0.4f;
	float ThresholdMax = 0.6f;

	/** Effect strength 0..1: cross-fade between the original (0) and fully sorted (1) image. */
	float Strength = 1.0f;
};

/**
 * Adds the MetaPass + SortPass compute passes to the render graph.
 *
 * Returns a new texture (same extent & format as SrcTexture) containing the sorted result.
 * If the sorted axis is >= 2048 (group-shared-memory limit of the algorithm) the passes are
 * skipped and SrcTexture is returned unchanged.
 *
 * Only the ViewRect sub-region of SrcTexture is read and written. SrcTexture is frequently
 * larger than the view (render targets are padded to an alignment), and the padding texels are
 * uninitialized; sorting them drags that garbage into the visible image, so the passes must be
 * confined to ViewRect. The returned texture should likewise be presented at ViewRect.
 *
 * SrcTexture must be a float4-compatible color texture (e.g. scene color, PF_FloatRGBA).
 */
BITONICPIXELSORTER_API FRDGTextureRef AddBitonicPixelSortPasses(
	FRDGBuilder& GraphBuilder,
	ERHIFeatureLevel::Type FeatureLevel,
	FRDGTextureRef SrcTexture,
	const FIntRect& ViewRect,
	const FBitonicPixelSorterParams& Params);
