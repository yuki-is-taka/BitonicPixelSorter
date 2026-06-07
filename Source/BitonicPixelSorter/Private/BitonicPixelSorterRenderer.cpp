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
		SHADER_PARAMETER(float, slope)
		SHADER_PARAMETER(int32, lineOffset)
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
		SHADER_PARAMETER(float, strength)
		SHADER_PARAMETER(uint32, direction)
		SHADER_PARAMETER(FUintVector2, viewportMin)
		SHADER_PARAMETER(FUintVector2, viewportSize)
		SHADER_PARAMETER(float, slope)
		SHADER_PARAMETER(int32, lineOffset)
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

	// Resolve the angle into a sheared-line parameterization: the line runs along the major axis
	// (X when more horizontal, Y when more vertical) and steps the minor axis by Slope (|Slope| <= 1)
	// each pixel. Slope == 0 reproduces the axis-aligned horizontal/vertical sort exactly.
	const float Theta = FMath::DegreesToRadians(Params.Angle);
	const float CosT = FMath::Cos(Theta);
	const float SinT = FMath::Sin(Theta);
	const bool bXMajor = FMath::Abs(SinT) <= FMath::Abs(CosT);
	const uint32 DirectionFlag = bXMajor ? 1u : 0u;
	const float Slope = bXMajor ? (SinT / CosT) : (CosT / SinT);
	const uint32 SortAxis = bXMajor ? W : H; // line length along the major axis
	const uint32 MinorDim = bXMajor ? H : W;

	// A pixel (major, minor) lies on line L = round(minor - major*Slope). Cover every line the view
	// can touch (its corners) plus a 1-line margin; lines that fall fully outside the view do nothing.
	int LineMin = MAX_int32;
	int LineMax = MIN_int32;
	for (int MajorEnd = 0; MajorEnd <= 1; ++MajorEnd)
	{
		for (int MinorEnd = 0; MinorEnd <= 1; ++MinorEnd)
		{
			const int Major = MajorEnd ? (int)SortAxis - 1 : 0;
			const int Minor = MinorEnd ? (int)MinorDim - 1 : 0;
			const int L = FMath::RoundToInt((float)Minor - (float)Major * Slope);
			LineMin = FMath::Min(LineMin, L);
			LineMax = FMath::Max(LineMax, L);
		}
	}
	LineMin -= 1;
	LineMax += 1;
	const int32 LineOffset = LineMin;
	const uint32 NumLines = (uint32)(LineMax - LineMin + 1);

	// Algorithm sorts an entire line in group-shared memory; bail if it can't fit.
	if (SortAxis == 0 || NumLines == 0 || SortAxis >= kMaxSize
		|| Params.Strength <= 0.0f || Params.ThresholdMin >= Params.ThresholdMax)
	{
		return SrcTexture;
	}

	// metaTex is our own intermediate, keyed by (position/2 along the line, line index).
	const FIntPoint MetaExtent(FMath::DivideAndRoundUp(SortAxis, 2u), NumLines);

	FRDGTextureRef MetaTexture = GraphBuilder.CreateTexture(
		FRDGTextureDesc::Create2D(MetaExtent, PF_R32G32_UINT, FClearValueBinding::None,
			TexCreate_UAV | TexCreate_ShaderResource),
		TEXT("BitonicPixelSorter.Meta"));

	FRDGTextureRef SortTexture = GraphBuilder.CreateTexture(
		FRDGTextureDesc::Create2D(Extent, SrcTexture->Desc.Format, FClearValueBinding::None,
			TexCreate_UAV | TexCreate_ShaderResource | TexCreate_RenderTargetable),
		TEXT("BitonicPixelSorter.Sorted"));

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(FeatureLevel);

	// --- MetaPass: ceil(NumLines*2 / META_THREADS_PER_GROUP) groups (2 threads per line) ---
	{
		FMetaPassCS::FParameters* P = GraphBuilder.AllocParameters<FMetaPassCS::FParameters>();
		P->srcTex = SrcTexture;
		P->metaTex = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(MetaTexture));
		P->thresholdMin = Params.ThresholdMin;
		P->thresholdMax = Params.ThresholdMax;
		P->direction = DirectionFlag;
		P->viewportMin = ViewMin;
		P->viewportSize = FUintVector2(W, H);
		P->slope = Slope;
		P->lineOffset = LineOffset;

		TShaderMapRef<FMetaPassCS> ComputeShader(ShaderMap);
		const uint32 Groups = FMath::DivideAndRoundUp(NumLines * 2, kMetaThreadsPerGroup);
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
		P->strength = Params.Strength;
		P->direction = DirectionFlag;
		P->viewportMin = ViewMin;
		P->viewportSize = FUintVector2(W, H);
		P->slope = Slope;
		P->lineOffset = LineOffset;

		TShaderMapRef<FSortPassCS> ComputeShader(ShaderMap);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("BitonicPixelSorter.Sort"),
			ComputeShader, P, FIntVector(NumLines, 1, 1));
	}

	return SortTexture;
}
