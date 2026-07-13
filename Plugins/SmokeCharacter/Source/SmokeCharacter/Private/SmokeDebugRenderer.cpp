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

class FSmokeDebugScalarSliceCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSmokeDebugScalarSliceCS);
	SHADER_USE_PARAMETER_STRUCT(FSmokeDebugScalarSliceCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntVector, GridResolution)
		SHADER_PARAMETER(FIntPoint, SliceDimensions)
		SHADER_PARAMETER(int32, SliceAxis)
		SHADER_PARAMETER(int32, SliceIndex)
		SHADER_PARAMETER(int32, FieldMode)
		SHADER_PARAMETER(uint32, bUseFalseColor)
		SHADER_PARAMETER(float, FieldScale)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float>, FieldTexture)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutSlice)
	END_SHADER_PARAMETER_STRUCT()
};

class FSmokeDebugVelocityMagnitudeSliceCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSmokeDebugVelocityMagnitudeSliceCS);
	SHADER_USE_PARAMETER_STRUCT(FSmokeDebugVelocityMagnitudeSliceCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntVector, GridResolution)
		SHADER_PARAMETER(FIntPoint, SliceDimensions)
		SHADER_PARAMETER(int32, SliceAxis)
		SHADER_PARAMETER(int32, SliceIndex)
		SHADER_PARAMETER(uint32, bUseFalseColor)
		SHADER_PARAMETER(float, FieldScale)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float4>, VelocityTexture)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutSlice)
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FSmokeDebugDensityCS, "/Plugin/SmokeCharacter/Private/SmokeDebugSlice.usf", "GenerateDensityCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FSmokeDebugScalarSliceCS, "/Plugin/SmokeCharacter/Private/SmokeDebugSlice.usf", "SliceScalarFieldCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FSmokeDebugVelocityMagnitudeSliceCS, "/Plugin/SmokeCharacter/Private/SmokeDebugSlice.usf", "SliceVelocityMagnitudeCS", SF_Compute);

namespace
{
float GetDebugFieldScale(ESmokeDebugField Field)
{
	switch (Field)
	{
	case ESmokeDebugField::Pressure:
		return 0.25f;
	case ESmokeDebugField::Divergence:
		return 2.0f;
	case ESmokeDebugField::VelocityMagnitude:
		return 1.5f;
	case ESmokeDebugField::Density:
	default:
		return 1.0f;
	}
}

FRDGTextureRef CreateSliceTexture(FRDGBuilder& GraphBuilder, const FIntPoint& SliceDimensions)
{
	const FRDGTextureDesc SliceDesc = FRDGTextureDesc::Create2D(
		SliceDimensions,
		PF_FloatRGBA,
		FClearValueBinding::Black,
		TexCreate_ShaderResource | TexCreate_UAV);

	return GraphBuilder.CreateTexture(SliceDesc, TEXT("SmokeCharacter.Debug.FieldSlice"));
}

void CopySliceToOutput(
	FRDGBuilder& GraphBuilder,
	const FIntPoint& SliceDimensions,
	FRDGTextureRef SliceTexture,
	FRHITexture* OutputTextureRHI)
{
	TRefCountPtr<IPooledRenderTarget> ExternalOutput = CreateRenderTarget(OutputTextureRHI, TEXT("SmokeCharacter.Debug.FieldSliceOutput"));
	FRDGTextureRef OutputTexture = GraphBuilder.RegisterExternalTexture(ExternalOutput);

	FRHICopyTextureInfo CopyInfo;
	CopyInfo.Size = FIntVector(SliceDimensions.X, SliceDimensions.Y, 1);
	AddCopyTexturePass(GraphBuilder, SliceTexture, OutputTexture, CopyInfo);
}
}

void FSmokeDebugRenderer::AddDensitySlicePass(
	FRDGBuilder& GraphBuilder,
	const FSmokeGridDesc& GridDesc,
	FRDGTexture* DensityTexture,
	int32 SliceAxis,
	int32 SliceIndex,
	bool bUseFalseColor,
	FRHITexture* OutputTextureRHI) const
{
	AddScalarFieldSlicePass(
		GraphBuilder,
		GridDesc,
		DensityTexture,
		ESmokeDebugField::Density,
		SliceAxis,
		SliceIndex,
		bUseFalseColor,
		OutputTextureRHI);
}

void FSmokeDebugRenderer::AddScalarFieldSlicePass(
	FRDGBuilder& GraphBuilder,
	const FSmokeGridDesc& GridDesc,
	FRDGTexture* FieldTexture,
	ESmokeDebugField Field,
	int32 SliceAxis,
	int32 SliceIndex,
	bool bUseFalseColor,
	FRHITexture* OutputTextureRHI) const
{
	if (!GridDesc.IsValid() || !FieldTexture || !OutputTextureRHI)
	{
		return;
	}

	const int32 ClampedAxis = FMath::Clamp(SliceAxis, 0, 2);
	const int32 ClampedSliceIndex = FSmokeGrid::ClampSliceIndex(GridDesc.Resolution, ClampedAxis, SliceIndex);
	const FIntPoint SliceDimensions = FSmokeGrid::GetSliceDimensions(GridDesc.Resolution, ClampedAxis);
	FRDGTextureRef SliceTexture = CreateSliceTexture(GraphBuilder, SliceDimensions);

	FSmokeDebugScalarSliceCS::FParameters* SliceParameters = GraphBuilder.AllocParameters<FSmokeDebugScalarSliceCS::FParameters>();
	SliceParameters->GridResolution = GridDesc.Resolution;
	SliceParameters->SliceDimensions = SliceDimensions;
	SliceParameters->SliceAxis = ClampedAxis;
	SliceParameters->SliceIndex = ClampedSliceIndex;
	SliceParameters->FieldMode = static_cast<int32>(Field);
	SliceParameters->bUseFalseColor = bUseFalseColor ? 1u : 0u;
	SliceParameters->FieldScale = GetDebugFieldScale(Field);
	SliceParameters->FieldTexture = FieldTexture;
	SliceParameters->OutSlice = GraphBuilder.CreateUAV(SliceTexture);

	const FIntVector SliceGroupCount(
		FMath::DivideAndRoundUp(SliceDimensions.X, 8),
		FMath::DivideAndRoundUp(SliceDimensions.Y, 8),
		1);
	const TShaderMapRef<FSmokeDebugScalarSliceCS> SliceShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("SmokeCharacter Scalar Field Slice Mode=%d Axis=%d Index=%d", static_cast<int32>(Field), ClampedAxis, ClampedSliceIndex),
		SliceShader,
		SliceParameters,
		SliceGroupCount);

	CopySliceToOutput(GraphBuilder, SliceDimensions, SliceTexture, OutputTextureRHI);
}

void FSmokeDebugRenderer::AddVelocityMagnitudeSlicePass(
	FRDGBuilder& GraphBuilder,
	const FSmokeGridDesc& GridDesc,
	FRDGTexture* VelocityTexture,
	int32 SliceAxis,
	int32 SliceIndex,
	bool bUseFalseColor,
	FRHITexture* OutputTextureRHI) const
{
	if (!GridDesc.IsValid() || !VelocityTexture || !OutputTextureRHI)
	{
		return;
	}

	const int32 ClampedAxis = FMath::Clamp(SliceAxis, 0, 2);
	const int32 ClampedSliceIndex = FSmokeGrid::ClampSliceIndex(GridDesc.Resolution, ClampedAxis, SliceIndex);
	const FIntPoint SliceDimensions = FSmokeGrid::GetSliceDimensions(GridDesc.Resolution, ClampedAxis);
	FRDGTextureRef SliceTexture = CreateSliceTexture(GraphBuilder, SliceDimensions);

	FSmokeDebugVelocityMagnitudeSliceCS::FParameters* SliceParameters = GraphBuilder.AllocParameters<FSmokeDebugVelocityMagnitudeSliceCS::FParameters>();
	SliceParameters->GridResolution = GridDesc.Resolution;
	SliceParameters->SliceDimensions = SliceDimensions;
	SliceParameters->SliceAxis = ClampedAxis;
	SliceParameters->SliceIndex = ClampedSliceIndex;
	SliceParameters->bUseFalseColor = bUseFalseColor ? 1u : 0u;
	SliceParameters->FieldScale = GetDebugFieldScale(ESmokeDebugField::VelocityMagnitude);
	SliceParameters->VelocityTexture = VelocityTexture;
	SliceParameters->OutSlice = GraphBuilder.CreateUAV(SliceTexture);

	const FIntVector SliceGroupCount(
		FMath::DivideAndRoundUp(SliceDimensions.X, 8),
		FMath::DivideAndRoundUp(SliceDimensions.Y, 8),
		1);
	const TShaderMapRef<FSmokeDebugVelocityMagnitudeSliceCS> SliceShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("SmokeCharacter Velocity Magnitude Slice Axis=%d Index=%d", ClampedAxis, ClampedSliceIndex),
		SliceShader,
		SliceParameters,
		SliceGroupCount);

	CopySliceToOutput(GraphBuilder, SliceDimensions, SliceTexture, OutputTextureRHI);
}

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

	ENQUEUE_RENDER_COMMAND(SmokeDebugDensitySlice)(
		[GridDesc, FrameIndex, ClampedAxis, ClampedSliceIndex, bUseFalseColor, OutputResource](FRHICommandListImmediate& RHICmdList)
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

			FSmokeDebugRenderer().AddDensitySlicePass(
				GraphBuilder,
				GridDesc,
				DensityTexture,
				ClampedAxis,
				ClampedSliceIndex,
				bUseFalseColor,
				OutputTextureRHI);

			GraphBuilder.Execute();
		});
}
