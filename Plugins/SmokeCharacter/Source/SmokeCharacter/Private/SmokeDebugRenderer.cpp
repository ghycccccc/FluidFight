// Copyright Epic Games, Inc. All Rights Reserved.

#include "SmokeDebugRenderer.h"

#include "GlobalShader.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderTargetPool.h"
#include "RHICommandList.h"
#include "ShaderParameterStruct.h"
#include "SmokeCharacter.h"
#include "Engine/TextureRenderTarget2D.h"

class FSmokeDebugDensityCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSmokeDebugDensityCS);
	SHADER_USE_PARAMETER_STRUCT(FSmokeDebugDensityCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntVector, GridResolution)
		SHADER_PARAMETER(float, PatternSeed)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float>, OutDensity)
	END_SHADER_PARAMETER_STRUCT()
};

class FSmokeDebugSliceCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSmokeDebugSliceCS);
	SHADER_USE_PARAMETER_STRUCT(FSmokeDebugSliceCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntVector, GridResolution)
		SHADER_PARAMETER(FIntPoint, SliceDimensions)
		SHADER_PARAMETER(int32, SliceAxis)
		SHADER_PARAMETER(int32, SliceIndex)
		SHADER_PARAMETER(uint32, bUseFalseColor)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float>, DensityTexture)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutSlice)
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FSmokeDebugDensityCS, "/Plugin/SmokeCharacter/Private/SmokeDebugSlice.usf", "GenerateDensityCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FSmokeDebugSliceCS, "/Plugin/SmokeCharacter/Private/SmokeDebugSlice.usf", "SliceDensityCS", SF_Compute);

void FSmokeDebugRenderer::DispatchDensitySlice(
	const FSmokeGridDesc& GridDesc,
	uint64 FrameIndex,
	int32 SliceAxis,
	int32 SliceIndex,
	bool bUseFalseColor,
	UTextureRenderTarget2D* OutputRenderTarget) const
{
	if (!GridDesc.IsValid() || !OutputRenderTarget)
	{
		return;
	}

	FTextureRenderTargetResource* OutputResource = OutputRenderTarget->GameThread_GetRenderTargetResource();
	if (!OutputResource)
	{
		return;
	}

	const int32 ClampedAxis = FMath::Clamp(SliceAxis, 0, 2);
	const int32 ClampedSliceIndex = FSmokeGrid::ClampSliceIndex(GridDesc.Resolution, ClampedAxis, SliceIndex);
	const FIntPoint SliceDimensions = FSmokeGrid::GetSliceDimensions(GridDesc.Resolution, ClampedAxis);

	ENQUEUE_RENDER_COMMAND(SmokeDebugDensitySlice)(
		[GridDesc, FrameIndex, ClampedAxis, ClampedSliceIndex, SliceDimensions, bUseFalseColor, OutputResource](FRHICommandListImmediate& RHICmdList)
		{
			FTextureRHIRef OutputTextureRHI = OutputResource->GetRenderTargetTexture();
			if (!OutputTextureRHI.IsValid())
			{
				return;
			}

			FRDGBuilder GraphBuilder(RHICmdList);

			const FRDGTextureDesc DensityDesc = FRDGTextureDesc::Create3D(
				GridDesc.Resolution,
				PF_R16F,
				FClearValueBinding::Black,
				TexCreate_ShaderResource | TexCreate_UAV);

			FRDGTextureRef DensityTexture = GraphBuilder.CreateTexture(DensityDesc, TEXT("SmokeCharacter.Debug.Density"));
			FRDGTextureUAVRef DensityUAV = GraphBuilder.CreateUAV(DensityTexture);

			FSmokeDebugDensityCS::FParameters* DensityParameters = GraphBuilder.AllocParameters<FSmokeDebugDensityCS::FParameters>();
			DensityParameters->GridResolution = GridDesc.Resolution;
			DensityParameters->PatternSeed = static_cast<float>(FrameIndex % 1024);
			DensityParameters->OutDensity = DensityUAV;

			const FIntVector DensityGroupCount = FComputeShaderUtils::GetGroupCount(GridDesc.Resolution, FIntVector(8, 8, 8));
			const TShaderMapRef<FSmokeDebugDensityCS> DensityShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("SmokeCharacter Debug Density %dx%dx%d", GridDesc.Resolution.X, GridDesc.Resolution.Y, GridDesc.Resolution.Z),
				DensityShader,
				DensityParameters,
				DensityGroupCount);

			const FRDGTextureDesc SliceDesc = FRDGTextureDesc::Create2D(
				SliceDimensions,
				PF_FloatRGBA,
				FClearValueBinding::Black,
				TexCreate_ShaderResource | TexCreate_UAV);

			FRDGTextureRef SliceTexture = GraphBuilder.CreateTexture(SliceDesc, TEXT("SmokeCharacter.Debug.DensitySlice"));
			FRDGTextureUAVRef SliceUAV = GraphBuilder.CreateUAV(SliceTexture);

			FSmokeDebugSliceCS::FParameters* SliceParameters = GraphBuilder.AllocParameters<FSmokeDebugSliceCS::FParameters>();
			SliceParameters->GridResolution = GridDesc.Resolution;
			SliceParameters->SliceDimensions = SliceDimensions;
			SliceParameters->SliceAxis = ClampedAxis;
			SliceParameters->SliceIndex = ClampedSliceIndex;
			SliceParameters->bUseFalseColor = bUseFalseColor ? 1u : 0u;
			SliceParameters->DensityTexture = DensityTexture;
			SliceParameters->OutSlice = SliceUAV;

			const FIntVector SliceGroupCount(
				FMath::DivideAndRoundUp(SliceDimensions.X, 8),
				FMath::DivideAndRoundUp(SliceDimensions.Y, 8),
				1);
			const TShaderMapRef<FSmokeDebugSliceCS> SliceShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("SmokeCharacter Density Slice Axis=%d Index=%d", ClampedAxis, ClampedSliceIndex),
				SliceShader,
				SliceParameters,
				SliceGroupCount);

			TRefCountPtr<IPooledRenderTarget> ExternalOutput = CreateRenderTarget(OutputTextureRHI, TEXT("SmokeCharacter.Debug.DensitySliceOutput"));
			FRDGTextureRef OutputTexture = GraphBuilder.RegisterExternalTexture(ExternalOutput);

			FRHICopyTextureInfo CopyInfo;
			CopyInfo.Size = FIntVector(SliceDimensions.X, SliceDimensions.Y, 1);
			AddCopyTexturePass(GraphBuilder, SliceTexture, OutputTexture, CopyInfo);

			GraphBuilder.Execute();
		});
}
