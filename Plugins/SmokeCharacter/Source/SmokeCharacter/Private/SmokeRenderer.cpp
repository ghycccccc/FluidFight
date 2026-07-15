// Copyright Epic Games, Inc. All Rights Reserved.

#include "SmokeRenderer.h"

#include "GlobalShader.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderTargetPool.h"
#include "RHICommandList.h"
#include "ShaderParameterStruct.h"

class FSmokeRaymarchCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSmokeRaymarchCS);
	SHADER_USE_PARAMETER_STRUCT(FSmokeRaymarchCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntVector, GridResolution)
		SHADER_PARAMETER(FIntPoint, OutputDimensions)
		SHADER_PARAMETER(FVector3f, DomainWorldMin)
		SHADER_PARAMETER(FVector3f, DomainWorldMax)
		SHADER_PARAMETER(FVector3f, CameraWorldPosition)
		SHADER_PARAMETER(FVector3f, CameraForward)
		SHADER_PARAMETER(FVector3f, CameraRight)
		SHADER_PARAMETER(FVector3f, CameraUp)
		SHADER_PARAMETER(float, TanHalfVerticalFov)
		SHADER_PARAMETER(float, AspectRatio)
		SHADER_PARAMETER(FVector3f, SmokeColor)
		SHADER_PARAMETER(float, DensityScale)
		SHADER_PARAMETER(float, Absorption)
		SHADER_PARAMETER(FVector3f, LightDirection)
		SHADER_PARAMETER(FVector3f, LightColor)
		SHADER_PARAMETER(float, LightIntensity)
		SHADER_PARAMETER(float, AmbientIntensity)
		SHADER_PARAMETER(int32, ViewStepCount)
		SHADER_PARAMETER(int32, LightStepCount)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float>, DensityTexture)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutVolume)
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FSmokeRaymarchCS, "/Plugin/SmokeCharacter/Private/SmokeRaymarch.usf", "RaymarchCS", SF_Compute);

namespace
{
FRDGTextureRef CreateVolumeTexture(FRDGBuilder& GraphBuilder, const FIntPoint& OutputDimensions)
{
	const FRDGTextureDesc VolumeDesc = FRDGTextureDesc::Create2D(
		OutputDimensions,
		PF_FloatRGBA,
		FClearValueBinding::Black,
		TexCreate_ShaderResource | TexCreate_UAV);

	return GraphBuilder.CreateTexture(VolumeDesc, TEXT("SmokeCharacter.Render.VolumePreview"));
}

void CopyVolumeToOutput(
	FRDGBuilder& GraphBuilder,
	const FIntPoint& OutputDimensions,
	FRDGTextureRef VolumeTexture,
	FRHITexture* OutputTextureRHI)
{
	TRefCountPtr<IPooledRenderTarget> ExternalOutput = CreateRenderTarget(OutputTextureRHI, TEXT("SmokeCharacter.Render.VolumePreviewOutput"));
	FRDGTextureRef OutputTexture = GraphBuilder.RegisterExternalTexture(ExternalOutput);

	FRHICopyTextureInfo CopyInfo;
	CopyInfo.Size = FIntVector(OutputDimensions.X, OutputDimensions.Y, 1);
	AddCopyTexturePass(GraphBuilder, VolumeTexture, OutputTexture, CopyInfo);
}
}

void FSmokeRenderer::AddVolumeRenderPass(
	FRDGBuilder& GraphBuilder,
	const FSmokeGridDesc& GridDesc,
	FRDGTexture* DensityTexture,
	const FSmokeRenderSettings& Settings,
	FRHITexture* OutputTextureRHI) const
{
	if (!GridDesc.IsValid() || !DensityTexture || !OutputTextureRHI)
	{
		return;
	}

	const FIntPoint OutputDimensions(
		FMath::Max(1, Settings.OutputDimensions.X),
		FMath::Max(1, Settings.OutputDimensions.Y));
	const FVector DomainExtents = GridDesc.DomainWorldSize * 0.5;
	const FVector DomainWorldMin = GridDesc.WorldOrigin - DomainExtents;
	const FVector DomainWorldMax = GridDesc.WorldOrigin + DomainExtents;
	const float VerticalFovRadians = FMath::DegreesToRadians(FMath::Clamp(Settings.VerticalFovDegrees, 5.0f, 170.0f));
	const float AspectRatio = static_cast<float>(OutputDimensions.X) / static_cast<float>(OutputDimensions.Y);

	FRDGTextureRef VolumeTexture = CreateVolumeTexture(GraphBuilder, OutputDimensions);

	FSmokeRaymarchCS::FParameters* Parameters = GraphBuilder.AllocParameters<FSmokeRaymarchCS::FParameters>();
	Parameters->GridResolution = GridDesc.Resolution;
	Parameters->OutputDimensions = OutputDimensions;
	Parameters->DomainWorldMin = FVector3f(DomainWorldMin);
	Parameters->DomainWorldMax = FVector3f(DomainWorldMax);
	Parameters->CameraWorldPosition = FVector3f(Settings.CameraWorldPosition);
	const FVector CameraForward = Settings.CameraForward.GetSafeNormal();
	const FVector CameraRight = Settings.CameraRight.GetSafeNormal();
	const FVector CameraUp = Settings.CameraUp.GetSafeNormal();
	Parameters->CameraForward = FVector3f(CameraForward.IsNearlyZero() ? FVector::ForwardVector : CameraForward);
	Parameters->CameraRight = FVector3f(CameraRight.IsNearlyZero() ? FVector::RightVector : CameraRight);
	Parameters->CameraUp = FVector3f(CameraUp.IsNearlyZero() ? FVector::UpVector : CameraUp);
	Parameters->TanHalfVerticalFov = FMath::Tan(VerticalFovRadians * 0.5f);
	Parameters->AspectRatio = AspectRatio;
	Parameters->SmokeColor = FVector3f(Settings.SmokeColor.R, Settings.SmokeColor.G, Settings.SmokeColor.B);
	Parameters->DensityScale = FMath::Max(0.0f, Settings.DensityScale);
	Parameters->Absorption = FMath::Max(0.0f, Settings.Absorption);
	const FVector NormalizedLightDirection = Settings.LightDirection.GetSafeNormal();
	Parameters->LightDirection = FVector3f(NormalizedLightDirection.IsNearlyZero() ? FVector(-0.4, -0.3, -0.85) : NormalizedLightDirection);
	Parameters->LightColor = FVector3f(Settings.LightColor.R, Settings.LightColor.G, Settings.LightColor.B);
	Parameters->LightIntensity = FMath::Max(0.0f, Settings.LightIntensity);
	Parameters->AmbientIntensity = FMath::Max(0.0f, Settings.AmbientIntensity);
	Parameters->ViewStepCount = FMath::Clamp(Settings.ViewStepCount, 8, 256);
	Parameters->LightStepCount = FMath::Clamp(Settings.LightStepCount, 0, 64);
	Parameters->DensityTexture = DensityTexture;
	Parameters->OutVolume = GraphBuilder.CreateUAV(VolumeTexture);

	const FIntVector GroupCount(
		FMath::DivideAndRoundUp(OutputDimensions.X, 8),
		FMath::DivideAndRoundUp(OutputDimensions.Y, 8),
		1);
	const TShaderMapRef<FSmokeRaymarchCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("SmokeCharacter Raymarch Volume %dx%d", OutputDimensions.X, OutputDimensions.Y),
		ComputeShader,
		Parameters,
		GroupCount);

	CopyVolumeToOutput(GraphBuilder, OutputDimensions, VolumeTexture, OutputTextureRHI);
}
