// Copyright (c) 2026 yuki-is-taka. UE port of ruccho/BitonicPixelSorter (MIT). See LICENSE.

#include "BitonicPixelSorterRenderer.h"

#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "DataDrivenShaderPlatformInfo.h"

// META_THREADS_PER_GROUP in the .usf (META_LINES_PER_GROUP(8) * 2).
static constexpr uint32 kMetaThreadsPerGroup = 16;
// Sorted axis must be smaller than this (group-shared-memory limit, MAX_SIZE in the .usf).
static constexpr uint32 kMaxSize = 2048;

// ---- MetaPass: marks per-line brightness ranges to be sorted, into metaTex ----
class FMetaPassCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FMetaPassCS);
	SHADER_USE_PARAMETER_STRUCT(FMetaPassCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, srcTex)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint2>, metaTex)
		SHADER_PARAMETER(float, thresholdMin)
		SHADER_PARAMETER(float, thresholdMax)
		SHADER_PARAMETER(uint32, direction)
		SHADER_PARAMETER(FUintVector2, viewportMin)
		SHADER_PARAMETER(FUintVector2, viewportSize)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return GetMaxSupportedFeatureLevel(Parameters.Platform) >= ERHIFeatureLevel::SM5;
	}
};
IMPLEMENT_GLOBAL_SHADER(FMetaPassCS, "/Plugin/BitonicPixelSorter/Private/BitonicPixelSorter.usf", "MetaPass", SF_Compute);

// ---- SortPass: one thread group per line; bitonic-sorts the in-range pixels into sortTex ----
class FSortPassCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSortPassCS);
	SHADER_USE_PARAMETER_STRUCT(FSortPassCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, srcTex)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint2>, srcMetaTex)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, sortTex)
		SHADER_PARAMETER(int32, maxLevels)
		SHADER_PARAMETER(uint32, ordering)
		SHADER_PARAMETER(uint32, direction)
		SHADER_PARAMETER(FUintVector2, viewportMin)
		SHADER_PARAMETER(FUintVector2, viewportSize)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return GetMaxSupportedFeatureLevel(Parameters.Platform) >= ERHIFeatureLevel::SM5;
	}
};
IMPLEMENT_GLOBAL_SHADER(FSortPassCS, "/Plugin/BitonicPixelSorter/Private/BitonicPixelSorter.usf", "SortPass", SF_Compute);

FRDGTextureRef AddBitonicPixelSortPasses(
	FRDGBuilder& GraphBuilder,
	ERHIFeatureLevel::Type FeatureLevel,
	FRDGTextureRef SrcTexture,
	const FIntRect& ViewRect,
	const FBitonicPixelSorterParams& Params)
{
	// Operate on the ViewRect sub-region only; SrcTexture->Desc.Extent is often padded larger and
	// the padding texels are uninitialized (sorting them pulls garbage into the visible image).
	const FIntPoint Extent = SrcTexture->Desc.Extent;
	const FUintVector2 ViewMin((uint32)ViewRect.Min.X, (uint32)ViewRect.Min.Y);
	const uint32 W = (uint32)ViewRect.Width();
	const uint32 H = (uint32)ViewRect.Height();
	const uint32 SortAxis = Params.bHorizontal ? W : H;
	const uint32 Lines    = Params.bHorizontal ? H : W;

	// Algorithm sorts an entire line in group-shared memory; bail if it can't fit.
	if (SortAxis == 0 || Lines == 0 || SortAxis >= kMaxSize)
	{
		return SrcTexture;
	}

	// metaTex is half-width along the sorted axis (it stores one entry per pixel pair).
	const FIntPoint MetaExtent(
		Params.bHorizontal ? W / 2 : W,
		Params.bHorizontal ? H : H / 2);

	FRDGTextureRef MetaTexture = GraphBuilder.CreateTexture(
		FRDGTextureDesc::Create2D(MetaExtent, PF_R32G32_UINT, FClearValueBinding::None,
			TexCreate_UAV | TexCreate_ShaderResource),
		TEXT("BitonicPixelSorter.Meta"));

	FRDGTextureRef SortTexture = GraphBuilder.CreateTexture(
		FRDGTextureDesc::Create2D(Extent, SrcTexture->Desc.Format, FClearValueBinding::None,
			TexCreate_UAV | TexCreate_ShaderResource | TexCreate_RenderTargetable),
		TEXT("BitonicPixelSorter.Sorted"));

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(FeatureLevel);
	const uint32 DirectionFlag = Params.bHorizontal ? 1u : 0u;

	// --- MetaPass: ceil(Lines*2 / META_THREADS_PER_GROUP) groups (2 threads per line) ---
	{
		FMetaPassCS::FParameters* P = GraphBuilder.AllocParameters<FMetaPassCS::FParameters>();
		P->srcTex = SrcTexture;
		P->metaTex = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(MetaTexture));
		P->thresholdMin = Params.ThresholdMin;
		P->thresholdMax = Params.ThresholdMax;
		P->direction = DirectionFlag;
		P->viewportMin = ViewMin;
		P->viewportSize = FUintVector2(W, H);

		TShaderMapRef<FMetaPassCS> ComputeShader(ShaderMap);
		const uint32 Groups = FMath::DivideAndRoundUp(Lines * 2, kMetaThreadsPerGroup);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("BitonicPixelSorter.Meta"),
			ComputeShader, P, FIntVector(Groups, 1, 1));
	}

	// --- SortPass: one group per line ---
	{
		FSortPassCS::FParameters* P = GraphBuilder.AllocParameters<FSortPassCS::FParameters>();
		P->srcTex = SrcTexture;
		P->srcMetaTex = MetaTexture;
		P->sortTex = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(SortTexture));
		P->maxLevels = FMath::CeilToInt(FMath::Log2((float)SortAxis));
		P->ordering = Params.bAscending ? 1u : 0u;
		P->direction = DirectionFlag;
		P->viewportMin = ViewMin;
		P->viewportSize = FUintVector2(W, H);

		TShaderMapRef<FSortPassCS> ComputeShader(ShaderMap);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("BitonicPixelSorter.Sort"),
			ComputeShader, P, FIntVector(Lines, 1, 1));
	}

	return SortTexture;
}
