// Copyright Epic Games, Inc. All Rights Reserved.

#include "SmokeGrid.h"

#include "GlobalShader.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RHICommandList.h"
#include "ShaderParameterStruct.h"
#include "SmokeCharacter.h"

class FSmokeGridPatternCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSmokeGridPatternCS);
	SHADER_USE_PARAMETER_STRUCT(FSmokeGridPatternCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntVector, GridResolution)
		SHADER_PARAMETER(float, PatternSeed)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float>, OutDensity)
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FSmokeGridPatternCS, "/Plugin/SmokeCharacter/Private/SmokeGridPattern.usf", "MainCS", SF_Compute);

bool FSmokeGridDesc::IsValid() const
{
	return Resolution.X > 0
		&& Resolution.Y > 0
		&& Resolution.Z > 0
		&& DomainWorldSize.X > 0.0
		&& DomainWorldSize.Y > 0.0
		&& DomainWorldSize.Z > 0.0;
}

FString FSmokeGridDesc::ToLogString() const
{
	return FString::Printf(
		TEXT("Resolution=%s DomainWorldSize=%s WorldOrigin=%s VoxelSize=%s"),
		*Resolution.ToString(),
		*DomainWorldSize.ToString(),
		*WorldOrigin.ToString(),
		*VoxelSize.ToString());
}

FSmokeGridDesc FSmokeGrid::BuildDesc(const FIntVector& Resolution, const FVector& DomainWorldSize, const FVector& WorldOrigin)
{
	FSmokeGridDesc Desc;
	Desc.Resolution = SanitizeResolution(Resolution);
	Desc.DomainWorldSize = SanitizeDomainWorldSize(DomainWorldSize);
	Desc.WorldOrigin = WorldOrigin;
	Desc.VoxelSize = FVector(
		Desc.DomainWorldSize.X / static_cast<double>(Desc.Resolution.X),
		Desc.DomainWorldSize.Y / static_cast<double>(Desc.Resolution.Y),
		Desc.DomainWorldSize.Z / static_cast<double>(Desc.Resolution.Z));

	Desc.DomainToWorld = FTransform(FQuat::Identity, Desc.WorldOrigin, Desc.DomainWorldSize);
	Desc.WorldToDomain = Desc.DomainToWorld.Inverse();
	return Desc;
}

void FSmokeGrid::DispatchSyntheticDensityPass(const FSmokeGridDesc& GridDesc, uint64 FrameIndex)
{
	if (!GridDesc.IsValid())
	{
		UE_LOG(LogSmokeCharacter, Warning, TEXT("Skipped smoke grid RDG dispatch for invalid grid. %s"), *GridDesc.ToLogString());
		return;
	}

	ENQUEUE_RENDER_COMMAND(SmokeGridPattern)(
		[GridDesc, FrameIndex](FRHICommandListImmediate& RHICmdList)
		{
			FRDGBuilder GraphBuilder(RHICmdList);

			const FRDGTextureDesc DensityDesc = FRDGTextureDesc::Create3D(
				GridDesc.Resolution,
				PF_R16F,
				FClearValueBinding::Black,
				TexCreate_ShaderResource | TexCreate_UAV);

			FRDGTextureRef DensityTexture = GraphBuilder.CreateTexture(DensityDesc, TEXT("SmokeCharacter.Density.Pattern"));
			FRDGTextureUAVRef DensityUAV = GraphBuilder.CreateUAV(DensityTexture);

			FSmokeGridPatternCS::FParameters* Parameters = GraphBuilder.AllocParameters<FSmokeGridPatternCS::FParameters>();
			Parameters->GridResolution = GridDesc.Resolution;
			Parameters->PatternSeed = static_cast<float>(FrameIndex % 1024);
			Parameters->OutDensity = DensityUAV;

			const FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(GridDesc.Resolution, FIntVector(8, 8, 8));
			const TShaderMapRef<FSmokeGridPatternCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("SmokeCharacter Synthetic Density %dx%dx%d", GridDesc.Resolution.X, GridDesc.Resolution.Y, GridDesc.Resolution.Z),
				ComputeShader,
				Parameters,
				GroupCount);

			GraphBuilder.Execute();
		});
}

FIntVector FSmokeGrid::SanitizeResolution(const FIntVector& Resolution)
{
	return FIntVector(
		FMath::Max(1, Resolution.X),
		FMath::Max(1, Resolution.Y),
		FMath::Max(1, Resolution.Z));
}

FVector FSmokeGrid::SanitizeDomainWorldSize(const FVector& DomainWorldSize)
{
	return FVector(
		FMath::Max(1.0, DomainWorldSize.X),
		FMath::Max(1.0, DomainWorldSize.Y),
		FMath::Max(1.0, DomainWorldSize.Z));
}
