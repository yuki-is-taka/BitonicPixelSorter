// Copyright (c) 2026 yuki-is-taka. UE port of ruccho/BitonicPixelSorter (MIT). See LICENSE.

#include "BitonicPixelSorterRenderer.h"

#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "DataDrivenShaderPlatformInfo.h"
#include "HAL/IConsoleManager.h"

// META_THREADS_PER_GROUP in the .usf (META_LINES_PER_GROUP(8) * 2).
static constexpr uint32 kMetaThreadsPerGroup = 16;
// Sorted axis below this uses the original single-group, group-shared path (MAX_SIZE in the .usf).
// At or above it, the "wide" path moves the per-line working set into global scratch buffers so the
// sort axis is bounded only by scratch VRAM (see CVarMaxScratchMB) instead of by 2048.
static constexpr uint32 kFastMax = 2048;

// VRAM budget (MB) for the wide path's transient scratch (~10 * NumLines * SortAxis bytes). A view
// that would need more than this is left untouched. 0 = unlimited.
static TAutoConsoleVariable<int32> CVarMaxScratchMB(
	TEXT("r.BitonicPixelSorter.MaxScratchMB"), 1536,
	TEXT("Bitonic pixel sorter: VRAM budget (MB) for the wide >2048 sort-axis path. Views needing more scratch are skipped. 0 = unlimited."),
	ECVF_RenderThreadSafe);

// ---- MetaPass: marks per-line brightness ranges to be sorted, into metaTex ----
class FMetaPassCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FMetaPassCS);
	SHADER_USE_PARAMETER_STRUCT(FMetaPassCS, FGlobalShader);

	class FWideLine : SHADER_PERMUTATION_BOOL("WIDE_LINE");
	using FPermutationDomain = TShaderPermutationDomain<FWideLine>;

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
		SHADER_PARAMETER(uint32, sortKey)
		// Wide path only (null on the fast path): global inter-half cache, one uint2 per meta texel.
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FUintVector2>, metaScratch)
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

	class FWideLine : SHADER_PERMUTATION_BOOL("WIDE_LINE");
	using FPermutationDomain = TShaderPermutationDomain<FWideLine>;

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
		// Wide path only (null on the fast path): global per-line working set + per-position spans.
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, lineScratch)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, rangeScratch)
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

	// Bail when there is nothing to do (degenerate size, no strength, or an empty threshold window).
	if (SortAxis == 0 || NumLines == 0
		|| Params.Strength <= 0.0f || Params.ThresholdMin >= Params.ThresholdMax)
	{
		return SrcTexture;
	}

	// Below kFastMax: original single-group, group-shared path (bit-for-bit unchanged). At/above it,
	// the "wide" path keeps the per-line working set in global scratch and synchronizes the bitonic
	// levels with device-memory barriers, so the sort axis is bounded only by scratch VRAM.
	const bool bWide = SortAxis >= kFastMax;

	const uint32 ReducedSize = FMath::DivideAndRoundUp(SortAxis, 2u); // meta texels per line
	const uint32 LineStride = ReducedSize * 2u;                       // even; contains a line's xL/xR

	if (bWide)
	{
		// Predicted transient scratch: lineScratch (NumLines*LineStride*4) + rangeScratch
		// (NumLines*ReducedSize*4) + metaScratch (NumLines*ReducedSize*8) ~= 10 * NumLines * SortAxis.
		const uint64 ScratchBytes =
			(uint64)NumLines * ((uint64)LineStride + 3ull * (uint64)ReducedSize) * sizeof(uint32);
		const int32 BudgetMB = CVarMaxScratchMB.GetValueOnRenderThread();
		if (BudgetMB > 0 && ScratchBytes > (uint64)BudgetMB * 1024ull * 1024ull)
		{
			return SrcTexture; // larger than the configured budget; leave the view untouched
		}
	}

	// metaTex is our own intermediate, keyed by (position/2 along the line, line index).
	const FIntPoint MetaExtent((int32)ReducedSize, (int32)NumLines);

	FRDGTextureRef MetaTexture = GraphBuilder.CreateTexture(
		FRDGTextureDesc::Create2D(MetaExtent, PF_R32G32_UINT, FClearValueBinding::None,
			TexCreate_UAV | TexCreate_ShaderResource),
		TEXT("BitonicPixelSorter.Meta"));

	FRDGTextureRef SortTexture = GraphBuilder.CreateTexture(
		FRDGTextureDesc::Create2D(Extent, SrcTexture->Desc.Format, FClearValueBinding::None,
			TexCreate_UAV | TexCreate_ShaderResource | TexCreate_RenderTargetable),
		TEXT("BitonicPixelSorter.Sorted"));

	// Wide-path global scratch (one region per line); left null on the fast path.
	FRDGBufferRef MetaScratch = nullptr;
	FRDGBufferRef LineScratch = nullptr;
	FRDGBufferRef RangeScratch = nullptr;
	if (bWide)
	{
		// metaScratch gets a few pad rows so MetaPass's excess threads (2 per line, rounded up to the
		// group size) never write out of bounds; SortPass dispatches exactly NumLines groups.
		const uint32 MetaRows = NumLines + 8u;
		MetaScratch = GraphBuilder.CreateBuffer(
			FRDGBufferDesc::CreateStructuredDesc(sizeof(FUintVector2), MetaRows * ReducedSize),
			TEXT("BitonicPixelSorter.MetaScratch"));
		LineScratch = GraphBuilder.CreateBuffer(
			FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), NumLines * LineStride),
			TEXT("BitonicPixelSorter.LineScratch"));
		RangeScratch = GraphBuilder.CreateBuffer(
			FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), NumLines * ReducedSize),
			TEXT("BitonicPixelSorter.RangeScratch"));
	}

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
		P->sortKey = Params.SortKey;
		if (bWide)
		{
			P->metaScratch = GraphBuilder.CreateUAV(MetaScratch);
		}

		FMetaPassCS::FPermutationDomain Perm;
		Perm.Set<FMetaPassCS::FWideLine>(bWide);
		TShaderMapRef<FMetaPassCS> ComputeShader(ShaderMap, Perm);
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
		if (bWide)
		{
			P->lineScratch = GraphBuilder.CreateUAV(LineScratch);
			P->rangeScratch = GraphBuilder.CreateUAV(RangeScratch);
		}

		FSortPassCS::FPermutationDomain Perm;
		Perm.Set<FSortPassCS::FWideLine>(bWide);
		TShaderMapRef<FSortPassCS> ComputeShader(ShaderMap, Perm);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("BitonicPixelSorter.Sort"),
			ComputeShader, P, FIntVector(NumLines, 1, 1));
	}

	return SortTexture;
}
