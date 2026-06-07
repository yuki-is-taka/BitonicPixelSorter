// Copyright (c) 2026 yuki-is-taka. UE port of ruccho/BitonicPixelSorter (MIT). See LICENSE.

#include "BitonicPixelSorterSceneViewExtension.h"
#include "BitonicPixelSorterRenderer.h"
#include "BitonicPixelSorterBlendable.h"

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

static TAutoConsoleVariable<float> CVarAngle(
	TEXT("r.BitonicPixelSorter.Angle"), 0.0f,
	TEXT("Sort line angle in degrees [0,180). 0=horizontal rows, 90=vertical columns, 45=diagonal."),
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

static TAutoConsoleVariable<float> CVarStrength(
	TEXT("r.BitonicPixelSorter.Strength"), 1.0f,
	TEXT("Effect strength 0..1: cross-fade between the original (0) and fully sorted (1) image. <=0 bypasses."),
	ECVF_RenderThreadSafe);

// Returns the per-view blended sorter settings if a Post Process Volume / camera / component is
// driving this view, or nullptr to fall back to the CVars.
static const FBitonicPixelSorterBlendData* FindBlendable(const FSceneView& View)
{
	FBlendableEntry* Iterator = nullptr;
	return View.FinalPostProcessSettings.BlendableManager.IterateBlendables<FBitonicPixelSorterBlendData>(Iterator);
}

FBitonicPixelSorterSceneViewExtension::FBitonicPixelSorterSceneViewExtension(const FAutoRegister& AutoRegister)
	: FSceneViewExtensionBase(AutoRegister)
{
}

bool FBitonicPixelSorterSceneViewExtension::IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const
{
	// Participate every frame; the per-view gating (CVar fallback, or a Post Process Volume blendable
	// which is only visible from the view's FinalPostProcessSettings) happens in
	// SubscribeToPostProcessingPass. No pass -- hence no GPU work -- is scheduled unless it adds one.
	return true;
}

void FBitonicPixelSorterSceneViewExtension::SubscribeToPostProcessingPass(
	EPostProcessingPass Pass,
	const FSceneView& InView,
	FPostProcessingPassDelegateArray& InOutPassCallbacks,
	bool bIsPassEnabled)
{
	if (Pass != EPostProcessingPass::Tonemap)
	{
		return;
	}

	// A Post Process Volume / camera / component blendable drives this view if present; otherwise the
	// global CVars do. Either way, only subscribe when the effect will actually do something --
	// zero strength or an empty threshold window means no pass at all (a true bypass).
	const FBitonicPixelSorterBlendData* Blend = FindBlendable(InView);

	bool bEffectActive;
	if (Blend != nullptr)
	{
		bEffectActive = Blend->Strength > 0.0f && Blend->ThresholdMin < Blend->ThresholdMax;
	}
	else
	{
		bEffectActive =
			CVarEnable.GetValueOnRenderThread() != 0
			&& CVarStrength.GetValueOnRenderThread() > 0.0f
			&& CVarThresholdMin.GetValueOnRenderThread() < CVarThresholdMax.GetValueOnRenderThread();
	}

	if (bEffectActive)
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
	if (const FBitonicPixelSorterBlendData* Blend = FindBlendable(View))
	{
		Params.Angle = Blend->Angle;
		Params.bAscending = Blend->bAscending != 0;
		Params.ThresholdMin = Blend->ThresholdMin;
		Params.ThresholdMax = Blend->ThresholdMax;
		Params.Strength = Blend->Strength;
	}
	else
	{
		Params.Angle = CVarAngle.GetValueOnRenderThread();
		Params.bAscending = CVarAscending.GetValueOnRenderThread() != 0;
		Params.ThresholdMin = CVarThresholdMin.GetValueOnRenderThread();
		Params.ThresholdMax = CVarThresholdMax.GetValueOnRenderThread();
		Params.Strength = CVarStrength.GetValueOnRenderThread();
	}

	FRDGTextureRef Sorted = AddBitonicPixelSortPasses(
		GraphBuilder, View.GetFeatureLevel(), SceneColor.Texture, SceneColor.ViewRect, Params);

	const FScreenPassTexture SortedOutput(Sorted, SceneColor.ViewRect);

	// When the post-process chain provides a target to render into (typically when this is the final
	// pass before the back buffer), copy the sorted result there and return it; otherwise return our
	// own texture and let the next pass consume it.
	FScreenPassRenderTarget OverrideOutput = Inputs.OverrideOutput;
	if (OverrideOutput.IsValid())
	{
		AddDrawTexturePass(GraphBuilder, View, SortedOutput, OverrideOutput);
		return MoveTemp(OverrideOutput);
	}

	return SortedOutput;
}
