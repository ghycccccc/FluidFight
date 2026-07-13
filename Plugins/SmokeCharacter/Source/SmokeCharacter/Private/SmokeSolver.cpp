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
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float>, SourceDensity)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float4>, SourceVelocity)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float>, OutDensity)
	END_SHADER_PARAMETER_STRUCT()
};

class FSmokeInitializeVelocityCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSmokeInitializeVelocityCS);
	SHADER_USE_PARAMETER_STRUCT(FSmokeInitializeVelocityCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntVector, GridResolution)
		SHADER_PARAMETER(float, PatternSeed)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, OutVelocity)
	END_SHADER_PARAMETER_STRUCT()
};

class FSmokeClearScalarCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSmokeClearScalarCS);
	SHADER_USE_PARAMETER_STRUCT(FSmokeClearScalarCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntVector, GridResolution)
		SHADER_PARAMETER(float, ClearValue)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float>, OutScalar)
	END_SHADER_PARAMETER_STRUCT()
};

class FSmokeAdvectVelocityCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSmokeAdvectVelocityCS);
	SHADER_USE_PARAMETER_STRUCT(FSmokeAdvectVelocityCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntVector, GridResolution)
		SHADER_PARAMETER(float, DeltaTime)
		SHADER_PARAMETER(float, TimeStepScale)
		SHADER_PARAMETER(float, VelocityDissipation)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float4>, SourceVelocity)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, OutVelocity)
	END_SHADER_PARAMETER_STRUCT()
};

class FSmokeComputeDivergenceCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSmokeComputeDivergenceCS);
	SHADER_USE_PARAMETER_STRUCT(FSmokeComputeDivergenceCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntVector, GridResolution)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float4>, SourceVelocity)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float>, OutScalar)
	END_SHADER_PARAMETER_STRUCT()
};

class FSmokeJacobiPressureCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSmokeJacobiPressureCS);
	SHADER_USE_PARAMETER_STRUCT(FSmokeJacobiPressureCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntVector, GridResolution)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float>, SourcePressure)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float>, DivergenceTexture)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float>, OutScalar)
	END_SHADER_PARAMETER_STRUCT()
};

class FSmokeProjectVelocityCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSmokeProjectVelocityCS);
	SHADER_USE_PARAMETER_STRUCT(FSmokeProjectVelocityCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntVector, GridResolution)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float4>, SourceVelocity)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float>, SourcePressure)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, OutVelocity)
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FSmokeInitializeDensityCS, "/Plugin/SmokeCharacter/Private/SmokeAdvect.usf", "InitializeDensityCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FSmokeAdvectDensityCS, "/Plugin/SmokeCharacter/Private/SmokeAdvect.usf", "AdvectDensityCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FSmokeInitializeVelocityCS, "/Plugin/SmokeCharacter/Private/SmokeFluidSolve.usf", "InitializeVelocityCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FSmokeClearScalarCS, "/Plugin/SmokeCharacter/Private/SmokeFluidSolve.usf", "ClearScalarCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FSmokeAdvectVelocityCS, "/Plugin/SmokeCharacter/Private/SmokeFluidSolve.usf", "AdvectVelocityCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FSmokeComputeDivergenceCS, "/Plugin/SmokeCharacter/Private/SmokeFluidSolve.usf", "ComputeDivergenceCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FSmokeJacobiPressureCS, "/Plugin/SmokeCharacter/Private/SmokeFluidSolve.usf", "JacobiPressureCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FSmokeProjectVelocityCS, "/Plugin/SmokeCharacter/Private/SmokeFluidSolve.usf", "ProjectVelocityCS", SF_Compute);

namespace
{
FRHITextureCreateDesc CreateSmokeDensityTextureDesc(const TCHAR* DebugName, const FIntVector& Resolution)
{
	return FRHITextureCreateDesc::Create3D(DebugName, Resolution, PF_R16F)
		.SetClearValue(FClearValueBinding::Black)
		.SetFlags(TexCreate_ShaderResource | TexCreate_UAV | TexCreate_3DTiling)
		.SetInitialState(ERHIAccess::Unknown);
}

FRHITextureCreateDesc CreateSmokeScalarTextureDesc(const TCHAR* DebugName, const FIntVector& Resolution)
{
	return FRHITextureCreateDesc::Create3D(DebugName, Resolution, PF_R16F)
		.SetClearValue(FClearValueBinding::Black)
		.SetFlags(TexCreate_ShaderResource | TexCreate_UAV | TexCreate_3DTiling)
		.SetInitialState(ERHIAccess::Unknown);
}

FRHITextureCreateDesc CreateSmokeVelocityTextureDesc(const TCHAR* DebugName, const FIntVector& Resolution)
{
	return FRHITextureCreateDesc::Create3D(DebugName, Resolution, PF_FloatRGBA)
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
	Resources.ActiveVelocityIndex = 0;
	Resources.ActivePressureIndex = 0;

	FTextureRHIRef DensityA = RHICmdList.CreateTexture(CreateSmokeDensityTextureDesc(TEXT("SmokeCharacter.Density.A"), GridDesc.Resolution));
	FTextureRHIRef DensityB = RHICmdList.CreateTexture(CreateSmokeDensityTextureDesc(TEXT("SmokeCharacter.Density.B"), GridDesc.Resolution));
	FTextureRHIRef VelocityA = RHICmdList.CreateTexture(CreateSmokeVelocityTextureDesc(TEXT("SmokeCharacter.Velocity.A"), GridDesc.Resolution));
	FTextureRHIRef VelocityB = RHICmdList.CreateTexture(CreateSmokeVelocityTextureDesc(TEXT("SmokeCharacter.Velocity.B"), GridDesc.Resolution));
	FTextureRHIRef PressureA = RHICmdList.CreateTexture(CreateSmokeScalarTextureDesc(TEXT("SmokeCharacter.Pressure.A"), GridDesc.Resolution));
	FTextureRHIRef PressureB = RHICmdList.CreateTexture(CreateSmokeScalarTextureDesc(TEXT("SmokeCharacter.Pressure.B"), GridDesc.Resolution));
	FTextureRHIRef Divergence = RHICmdList.CreateTexture(CreateSmokeScalarTextureDesc(TEXT("SmokeCharacter.Divergence"), GridDesc.Resolution));
	Resources.DensityTextures[0] = CreateRenderTarget(DensityA, TEXT("SmokeCharacter.Density.A"));
	Resources.DensityTextures[1] = CreateRenderTarget(DensityB, TEXT("SmokeCharacter.Density.B"));
	Resources.VelocityTextures[0] = CreateRenderTarget(VelocityA, TEXT("SmokeCharacter.Velocity.A"));
	Resources.VelocityTextures[1] = CreateRenderTarget(VelocityB, TEXT("SmokeCharacter.Velocity.B"));
	Resources.PressureTextures[0] = CreateRenderTarget(PressureA, TEXT("SmokeCharacter.Pressure.A"));
	Resources.PressureTextures[1] = CreateRenderTarget(PressureB, TEXT("SmokeCharacter.Pressure.B"));
	Resources.DivergenceTexture = CreateRenderTarget(Divergence, TEXT("SmokeCharacter.Divergence"));
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

void AddInitializeVelocityPass(
	FRDGBuilder& GraphBuilder,
	const FSmokeGridDesc& GridDesc,
	FRDGTextureRef OutputVelocity,
	float PatternSeed)
{
	FSmokeInitializeVelocityCS::FParameters* Parameters = GraphBuilder.AllocParameters<FSmokeInitializeVelocityCS::FParameters>();
	Parameters->GridResolution = GridDesc.Resolution;
	Parameters->PatternSeed = PatternSeed;
	Parameters->OutVelocity = GraphBuilder.CreateUAV(OutputVelocity);

	const FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(GridDesc.Resolution, FIntVector(8, 8, 8));
	const TShaderMapRef<FSmokeInitializeVelocityCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("SmokeCharacter Initialize Velocity %dx%dx%d", GridDesc.Resolution.X, GridDesc.Resolution.Y, GridDesc.Resolution.Z),
		ComputeShader,
		Parameters,
		GroupCount);
}

void AddClearScalarPass(
	FRDGBuilder& GraphBuilder,
	const FSmokeGridDesc& GridDesc,
	FRDGTextureRef OutputScalar,
	float ClearValue,
	const TCHAR* EventName)
{
	FSmokeClearScalarCS::FParameters* Parameters = GraphBuilder.AllocParameters<FSmokeClearScalarCS::FParameters>();
	Parameters->GridResolution = GridDesc.Resolution;
	Parameters->ClearValue = ClearValue;
	Parameters->OutScalar = GraphBuilder.CreateUAV(OutputScalar);

	const FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(GridDesc.Resolution, FIntVector(8, 8, 8));
	const TShaderMapRef<FSmokeClearScalarCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("%s %dx%dx%d", EventName, GridDesc.Resolution.X, GridDesc.Resolution.Y, GridDesc.Resolution.Z),
		ComputeShader,
		Parameters,
		GroupCount);
}

void AddAdvectVelocityPass(
	FRDGBuilder& GraphBuilder,
	const FSmokeGridDesc& GridDesc,
	const FSmokeSolverSettings& SolverSettings,
	FRDGTextureRef SourceVelocity,
	FRDGTextureRef OutputVelocity)
{
	FSmokeAdvectVelocityCS::FParameters* Parameters = GraphBuilder.AllocParameters<FSmokeAdvectVelocityCS::FParameters>();
	Parameters->GridResolution = GridDesc.Resolution;
	Parameters->DeltaTime = FMath::Clamp(SolverSettings.DeltaTime, 0.0f, 1.0f / 30.0f);
	Parameters->TimeStepScale = FMath::Max(0.0f, SolverSettings.TimeStepScale);
	Parameters->VelocityDissipation = FMath::Clamp(SolverSettings.VelocityDissipation, 0.0f, 1.0f);
	Parameters->SourceVelocity = SourceVelocity;
	Parameters->OutVelocity = GraphBuilder.CreateUAV(OutputVelocity);

	const FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(GridDesc.Resolution, FIntVector(8, 8, 8));
	const TShaderMapRef<FSmokeAdvectVelocityCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("SmokeCharacter Advect Velocity %dx%dx%d", GridDesc.Resolution.X, GridDesc.Resolution.Y, GridDesc.Resolution.Z),
		ComputeShader,
		Parameters,
		GroupCount);
}

void AddComputeDivergencePass(
	FRDGBuilder& GraphBuilder,
	const FSmokeGridDesc& GridDesc,
	FRDGTextureRef SourceVelocity,
	FRDGTextureRef OutputDivergence)
{
	FSmokeComputeDivergenceCS::FParameters* Parameters = GraphBuilder.AllocParameters<FSmokeComputeDivergenceCS::FParameters>();
	Parameters->GridResolution = GridDesc.Resolution;
	Parameters->SourceVelocity = SourceVelocity;
	Parameters->OutScalar = GraphBuilder.CreateUAV(OutputDivergence);

	const FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(GridDesc.Resolution, FIntVector(8, 8, 8));
	const TShaderMapRef<FSmokeComputeDivergenceCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("SmokeCharacter Compute Divergence %dx%dx%d", GridDesc.Resolution.X, GridDesc.Resolution.Y, GridDesc.Resolution.Z),
		ComputeShader,
		Parameters,
		GroupCount);
}

void AddJacobiPressurePass(
	FRDGBuilder& GraphBuilder,
	const FSmokeGridDesc& GridDesc,
	FRDGTextureRef SourcePressure,
	FRDGTextureRef DivergenceTexture,
	FRDGTextureRef OutputPressure,
	int32 IterationIndex)
{
	FSmokeJacobiPressureCS::FParameters* Parameters = GraphBuilder.AllocParameters<FSmokeJacobiPressureCS::FParameters>();
	Parameters->GridResolution = GridDesc.Resolution;
	Parameters->SourcePressure = SourcePressure;
	Parameters->DivergenceTexture = DivergenceTexture;
	Parameters->OutScalar = GraphBuilder.CreateUAV(OutputPressure);

	const FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(GridDesc.Resolution, FIntVector(8, 8, 8));
	const TShaderMapRef<FSmokeJacobiPressureCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("SmokeCharacter Jacobi Pressure Iteration=%d", IterationIndex),
		ComputeShader,
		Parameters,
		GroupCount);
}

void AddProjectVelocityPass(
	FRDGBuilder& GraphBuilder,
	const FSmokeGridDesc& GridDesc,
	FRDGTextureRef SourceVelocity,
	FRDGTextureRef SourcePressure,
	FRDGTextureRef OutputVelocity)
{
	FSmokeProjectVelocityCS::FParameters* Parameters = GraphBuilder.AllocParameters<FSmokeProjectVelocityCS::FParameters>();
	Parameters->GridResolution = GridDesc.Resolution;
	Parameters->SourceVelocity = SourceVelocity;
	Parameters->SourcePressure = SourcePressure;
	Parameters->OutVelocity = GraphBuilder.CreateUAV(OutputVelocity);

	const FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(GridDesc.Resolution, FIntVector(8, 8, 8));
	const TShaderMapRef<FSmokeProjectVelocityCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("SmokeCharacter Project Velocity %dx%dx%d", GridDesc.Resolution.X, GridDesc.Resolution.Y, GridDesc.Resolution.Z),
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
			FRDGTextureRef CurrentVelocity = GraphBuilder.RegisterExternalTexture(Resources->VelocityTextures[Resources->ActiveVelocityIndex]);
			FRDGTextureRef NextVelocity = GraphBuilder.RegisterExternalTexture(Resources->VelocityTextures[1 - Resources->ActiveVelocityIndex]);
			FRDGTextureRef PressureA = GraphBuilder.RegisterExternalTexture(Resources->PressureTextures[0]);
			FRDGTextureRef PressureB = GraphBuilder.RegisterExternalTexture(Resources->PressureTextures[1]);
			FRDGTextureRef DivergenceTexture = GraphBuilder.RegisterExternalTexture(Resources->DivergenceTexture);

			if (!Resources->bInitialized)
			{
				AddInitializeDensityPass(GraphBuilder, GridDesc, CurrentDensity, static_cast<float>(FrameIndex % 1024));
				AddInitializeDensityPass(GraphBuilder, GridDesc, NextDensity, static_cast<float>((FrameIndex + 17) % 1024));
				AddInitializeVelocityPass(GraphBuilder, GridDesc, CurrentVelocity, static_cast<float>(FrameIndex % 4096));
				AddInitializeVelocityPass(GraphBuilder, GridDesc, NextVelocity, static_cast<float>((FrameIndex + 31) % 4096));
				Resources->bInitialized = true;
			}

			AddAdvectVelocityPass(GraphBuilder, GridDesc, SolverSettings, CurrentVelocity, NextVelocity);
			AddComputeDivergencePass(GraphBuilder, GridDesc, NextVelocity, DivergenceTexture);
			AddClearScalarPass(GraphBuilder, GridDesc, PressureA, 0.0f, TEXT("SmokeCharacter Clear Pressure A"));
			AddClearScalarPass(GraphBuilder, GridDesc, PressureB, 0.0f, TEXT("SmokeCharacter Clear Pressure B"));

			FRDGTextureRef PressureRead = PressureA;
			FRDGTextureRef PressureWrite = PressureB;
			const int32 PressureIterations = FMath::Clamp(SolverSettings.PressureIterations, 0, 80);
			int32 FinalPressureIndex = 0;
			for (int32 IterationIndex = 0; IterationIndex < PressureIterations; ++IterationIndex)
			{
				AddJacobiPressurePass(GraphBuilder, GridDesc, PressureRead, DivergenceTexture, PressureWrite, IterationIndex);
				Swap(PressureRead, PressureWrite);
				FinalPressureIndex = 1 - FinalPressureIndex;
			}

			AddProjectVelocityPass(GraphBuilder, GridDesc, NextVelocity, PressureRead, CurrentVelocity);
			AddComputeDivergencePass(GraphBuilder, GridDesc, CurrentVelocity, DivergenceTexture);

			FSmokeAdvectDensityCS::FParameters* Parameters = GraphBuilder.AllocParameters<FSmokeAdvectDensityCS::FParameters>();
			Parameters->GridResolution = GridDesc.Resolution;
			Parameters->DeltaTime = FMath::Clamp(SolverSettings.DeltaTime, 0.0f, 1.0f / 30.0f);
			Parameters->TimeStepScale = FMath::Max(0.0f, SolverSettings.TimeStepScale);
			Parameters->DensityDissipation = FMath::Clamp(SolverSettings.DensityDissipation, 0.0f, 1.0f);
			Parameters->SourceDensity = CurrentDensity;
			Parameters->SourceVelocity = CurrentVelocity;
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
					if (SliceRequestCopy.Field == ESmokeDebugField::Density)
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
					else if (SliceRequestCopy.Field == ESmokeDebugField::VelocityMagnitude)
					{
						SliceRequestCopy.DebugRenderer->AddVelocityMagnitudeSlicePass(
							GraphBuilder,
							GridDesc,
							CurrentVelocity,
							SliceRequestCopy.SliceAxis,
							SliceRequestCopy.SliceIndex,
							SliceRequestCopy.bUseFalseColor,
							SliceOutputRHI);
					}
					else if (SliceRequestCopy.Field == ESmokeDebugField::Pressure)
					{
						SliceRequestCopy.DebugRenderer->AddScalarFieldSlicePass(
							GraphBuilder,
							GridDesc,
							PressureRead,
							ESmokeDebugField::Pressure,
							SliceRequestCopy.SliceAxis,
							SliceRequestCopy.SliceIndex,
							SliceRequestCopy.bUseFalseColor,
							SliceOutputRHI);
					}
					else if (SliceRequestCopy.Field == ESmokeDebugField::Divergence)
					{
						SliceRequestCopy.DebugRenderer->AddScalarFieldSlicePass(
							GraphBuilder,
							GridDesc,
							DivergenceTexture,
							ESmokeDebugField::Divergence,
							SliceRequestCopy.SliceAxis,
							SliceRequestCopy.SliceIndex,
							SliceRequestCopy.bUseFalseColor,
							SliceOutputRHI);
					}
				}
			}

			Resources->ActivePressureIndex = FinalPressureIndex;
			Resources->SwapDensityBuffers();
			GraphBuilder.Execute();
		});

	if (SolverSettings.bVerboseLogging)
	{
		const FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(GridDesc.Resolution, FIntVector(8, 8, 8));
		UE_LOG(LogSmokeCharacter, Log, TEXT("Smoke solver dispatched. Groups=%s DeltaTime=%.4f TimeStepScale=%.3f DensityDissipation=%.3f VelocityDissipation=%.3f PressureIterations=%d Frame=%llu"),
			*GroupCount.ToString(),
			SolverSettings.DeltaTime,
			SolverSettings.TimeStepScale,
			SolverSettings.DensityDissipation,
			SolverSettings.VelocityDissipation,
			FMath::Clamp(SolverSettings.PressureIterations, 0, 80),
			static_cast<unsigned long long>(FrameIndex));
	}
}
