// Copyright (c) 2026 yuki-is-taka. UE port of ruccho/BitonicPixelSorter (MIT). See LICENSE.

#include "BitonicPixelSorterSceneViewExtension.h"
#include "BitonicPixelSorterRenderer.h"

#include "PostProcess/PostProcessMaterialInputs.h"
#include "ScreenPass.h"
#include "SceneView.h"
#include "RenderGraphBuilder.h"
#include "HAL/IConsoleManager.h"

// ---- Console variables (runtime control; render-thread safe) ----
static TAutoConsoleVariable<int32> CVarEnable(
	TEXT("r.BitonicPixelSorter.Enable"), 0,
	TEXT("Enable the bitonic pixel-sort post-process effect (0=off, 1=on)."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarHorizontal(
	TEXT("r.BitonicPixelSorter.Horizontal"), 1,
	TEXT("Sort direction: 1=horizontal (rows), 0=vertical (columns)."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarAscending(
	TEXT("r.BitonicPixelSorter.Ascending"), 1,
	TEXT("Sort order: 1=ascending by brightness, 0=descending."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarThresholdMin(
	TEXT("r.BitonicPixelSorter.ThresholdMin"), 0.4f,
	TEXT("Only pixels with brightness >= this are sorted."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarThresholdMax(
	TEXT("r.BitonicPixelSorter.ThresholdMax"), 0.6f,
	TEXT("Only pixels with brightness <= this are sorted."),
	ECVF_RenderThreadSafe);

FBitonicPixelSorterSceneViewExtension::FBitonicPixelSorterSceneViewExtension(const FAutoRegister& AutoRegister)
	: FSceneViewExtensionBase(AutoRegister)
{
}

bool FBitonicPixelSorterSceneViewExtension::IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const
{
	return CVarEnable.GetValueOnAnyThread() != 0;
}

void FBitonicPixelSorterSceneViewExtension::SubscribeToPostProcessingPass(
	EPostProcessingPass Pass,
	const FSceneView& InView,
	FPostProcessingPassDelegateArray& InOutPassCallbacks,
	bool bIsPassEnabled)
{
	if (Pass == EPostProcessingPass::Tonemap && CVarEnable.GetValueOnRenderThread() != 0)
	{
		InOutPassCallbacks.Add(FAfterPassCallbackDelegate::CreateRaw(
			this, &FBitonicPixelSorterSceneViewExtension::PostProcessPass_RenderThread));
	}
}

FScreenPassTexture FBitonicPixelSorterSceneViewExtension::PostProcessPass_RenderThread(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	const FPostProcessMaterialInputs& Inputs)
{
	// Get the scene color as a plain 2D texture (copies if it was a texture-array slice).
	const FScreenPassTexture SceneColor =
		FScreenPassTexture::CopyFromSlice(GraphBuilder, Inputs.GetInput(EPostProcessMaterialInput::SceneColor));

	if (!SceneColor.IsValid())
	{
		return SceneColor;
	}

	FBitonicPixelSorterParams Params;
	Params.bHorizontal = CVarHorizontal.GetValueOnRenderThread() != 0;
	Params.bAscending  = CVarAscending.GetValueOnRenderThread() != 0;
	Params.ThresholdMin = CVarThresholdMin.GetValueOnRenderThread();
	Params.ThresholdMax = CVarThresholdMax.GetValueOnRenderThread();

	FRDGTextureRef Sorted = AddBitonicPixelSortPasses(
		GraphBuilder, View.GetFeatureLevel(), SceneColor.Texture, SceneColor.ViewRect, Params);

	// TODO: honor Inputs.OverrideOutput when this is the final back-buffer pass.
	return FScreenPassTexture(Sorted, SceneColor.ViewRect);
}
