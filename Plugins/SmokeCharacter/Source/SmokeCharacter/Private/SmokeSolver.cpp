// Copyright Epic Games, Inc. All Rights Reserved.

#include "SmokeSolver.h"

#include "GlobalShader.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RHICommandList.h"
#include "ShaderParameterStruct.h"
#include "SmokeCharacter.h"
#include "SmokeDebugRenderer.h"
#include "Engine/TextureRenderTarget2D.h"

class FSmokeInitializeDensityCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSmokeInitializeDensityCS);
	SHADER_USE_PARAMETER_STRUCT(FSmokeInitializeDensityCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntVector, GridResolution)
		SHADER_PARAMETER(float, PatternSeed)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float>, OutDensity)
	END_SHADER_PARAMETER_STRUCT()
};

class FSmokeAdvectDensityCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSmokeAdvectDensityCS);
	SHADER_USE_PARAMETER_STRUCT(FSmokeAdvectDensityCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntVector, GridResolution)
		SHADER_PARAMETER(float, DeltaTime)
		SHADER_PARAMETER(float, TimeStepScale)
		SHADER_PARAMETER(float, DensityDissipation)
		SHADER_PARAMETER(float, PatternSeed)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float>, SourceDensity)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float>, OutDensity)
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FSmokeInitializeDensityCS, "/Plugin/SmokeCharacter/Private/SmokeAdvect.usf", "InitializeDensityCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FSmokeAdvectDensityCS, "/Plugin/SmokeCharacter/Private/SmokeAdvect.usf", "AdvectDensityCS", SF_Compute);

namespace
{
FRHITextureCreateDesc CreateSmokeDensityTextureDesc(const TCHAR* DebugName, const FIntVector& Resolution)
{
	return FRHITextureCreateDesc::Create3D(DebugName, Resolution, PF_R16F)
		.SetClearValue(FClearValueBinding::Black)
		.SetFlags(TexCreate_ShaderResource | TexCreate_UAV | TexCreate_3DTiling)
		.SetInitialState(ERHIAccess::Unknown);
}

void EnsureSmokeDensityResources(FRHICommandListImmediate& RHICmdList, FSmokeGridResources& Resources, const FSmokeGridDesc& GridDesc)
{
	if (Resources.IsValidFor(GridDesc))
	{
		return;
	}

	Resources.Reset();
	Resources.Desc = GridDesc;
	Resources.ActiveDensityIndex = 0;

	FTextureRHIRef DensityA = RHICmdList.CreateTexture(CreateSmokeDensityTextureDesc(TEXT("SmokeCharacter.Density.A"), GridDesc.Resolution));
	FTextureRHIRef DensityB = RHICmdList.CreateTexture(CreateSmokeDensityTextureDesc(TEXT("SmokeCharacter.Density.B"), GridDesc.Resolution));
	Resources.DensityTextures[0] = CreateRenderTarget(DensityA, TEXT("SmokeCharacter.Density.A"));
	Resources.DensityTextures[1] = CreateRenderTarget(DensityB, TEXT("SmokeCharacter.Density.B"));
}

void AddInitializeDensityPass(
	FRDGBuilder& GraphBuilder,
	const FSmokeGridDesc& GridDesc,
	FRDGTextureRef OutputDensity,
	float PatternSeed)
{
	FSmokeInitializeDensityCS::FParameters* Parameters = GraphBuilder.AllocParameters<FSmokeInitializeDensityCS::FParameters>();
	Parameters->GridResolution = GridDesc.Resolution;
	Parameters->PatternSeed = PatternSeed;
	Parameters->OutDensity = GraphBuilder.CreateUAV(OutputDensity);

	const FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(GridDesc.Resolution, FIntVector(8, 8, 8));
	const TShaderMapRef<FSmokeInitializeDensityCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("SmokeCharacter Initialize Density %dx%dx%d", GridDesc.Resolution.X, GridDesc.Resolution.Y, GridDesc.Resolution.Z),
		ComputeShader,
		Parameters,
		GroupCount);
}
}

void FSmokeSolver::ResetResources()
{
	bResetPending = true;
}

void FSmokeSolver::DispatchSimulation(
	const FSmokeGridDesc& GridDesc,
	const FSmokeSolverSettings& Settings,
	uint64 FrameIndex,
	const FSmokeDensitySliceRequest* SliceRequest)
{
	if (!GridDesc.IsValid())
	{
		UE_LOG(LogSmokeCharacter, Warning, TEXT("Skipped smoke solver dispatch for invalid grid. %s"), *GridDesc.ToLogString());
		return;
	}

	FSmokeGridResources* Resources = &GridResources;
	const FSmokeSolverSettings SolverSettings = Settings;
	const bool bResetRequested = bResetPending;
	bResetPending = false;
	const FSmokeDensitySliceRequest SliceRequestCopy = SliceRequest ? *SliceRequest : FSmokeDensitySliceRequest();
	const bool bRenderSlice = SliceRequest
		&& SliceRequest->DebugRenderer
		&& SliceRequest->OutputRenderTarget;
	FTextureRenderTargetResource* SliceOutputResource = bRenderSlice
		? SliceRequest->OutputRenderTarget->GameThread_GetRenderTargetResource()
		: nullptr;

	ENQUEUE_RENDER_COMMAND(SmokeSolverDensityAdvection)(
		[Resources, GridDesc, SolverSettings, FrameIndex, SliceRequestCopy, bRenderSlice, SliceOutputResource, bResetRequested](FRHICommandListImmediate& RHICmdList)
		{
			if (bResetRequested)
			{
				Resources->Reset();
			}

			EnsureSmokeDensityResources(RHICmdList, *Resources, GridDesc);

			FRDGBuilder GraphBuilder(RHICmdList);
			FRDGTextureRef CurrentDensity = GraphBuilder.RegisterExternalTexture(Resources->DensityTextures[Resources->ActiveDensityIndex]);
			FRDGTextureRef NextDensity = GraphBuilder.RegisterExternalTexture(Resources->DensityTextures[1 - Resources->ActiveDensityIndex]);

			if (!Resources->bInitialized)
			{
				AddInitializeDensityPass(GraphBuilder, GridDesc, CurrentDensity, static_cast<float>(FrameIndex % 1024));
				AddInitializeDensityPass(GraphBuilder, GridDesc, NextDensity, static_cast<float>((FrameIndex + 17) % 1024));
				Resources->bInitialized = true;
			}

			FSmokeAdvectDensityCS::FParameters* Parameters = GraphBuilder.AllocParameters<FSmokeAdvectDensityCS::FParameters>();
			Parameters->GridResolution = GridDesc.Resolution;
			Parameters->DeltaTime = FMath::Max(0.0f, SolverSettings.DeltaTime);
			Parameters->TimeStepScale = FMath::Max(0.0f, SolverSettings.TimeStepScale);
			Parameters->DensityDissipation = FMath::Clamp(SolverSettings.DensityDissipation, 0.0f, 1.0f);
			Parameters->PatternSeed = static_cast<float>(FrameIndex % 4096);
			Parameters->SourceDensity = CurrentDensity;
			Parameters->OutDensity = GraphBuilder.CreateUAV(NextDensity);

			const FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(GridDesc.Resolution, FIntVector(8, 8, 8));
			const TShaderMapRef<FSmokeAdvectDensityCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("SmokeCharacter Advect Density %dx%dx%d", GridDesc.Resolution.X, GridDesc.Resolution.Y, GridDesc.Resolution.Z),
				ComputeShader,
				Parameters,
				GroupCount);

			if (bRenderSlice && SliceOutputResource && SliceRequestCopy.DebugRenderer)
			{
				FTextureRHIRef SliceOutputRHI = SliceOutputResource->GetRenderTargetTexture();
				if (SliceOutputRHI.IsValid())
				{
					SliceRequestCopy.DebugRenderer->AddDensitySlicePass(
						GraphBuilder,
						GridDesc,
						NextDensity,
						SliceRequestCopy.SliceAxis,
						SliceRequestCopy.SliceIndex,
						SliceRequestCopy.bUseFalseColor,
						SliceOutputRHI);
				}
			}

			Resources->SwapDensityBuffers();
			GraphBuilder.Execute();
		});

	if (SolverSettings.bVerboseLogging)
	{
		const FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(GridDesc.Resolution, FIntVector(8, 8, 8));
		UE_LOG(LogSmokeCharacter, Log, TEXT("Smoke solver dispatched. Groups=%s DeltaTime=%.4f TimeStepScale=%.3f DensityDissipation=%.3f Frame=%llu"),
			*GroupCount.ToString(),
			SolverSettings.DeltaTime,
			SolverSettings.TimeStepScale,
			SolverSettings.DensityDissipation,
			static_cast<unsigned long long>(FrameIndex));
	}
}
