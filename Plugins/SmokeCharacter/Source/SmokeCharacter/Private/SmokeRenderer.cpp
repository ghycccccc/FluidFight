// Copyright Epic Games, Inc. All Rights Reserved.

#include "SmokeRenderer.h"

#include "GlobalShader.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderTargetPool.h"
#include "RHICommandList.h"
#include "SceneInterface.h"
#include "SceneView.h"
#include "SceneViewExtension.h"
#include "ScreenPass.h"
#include "ShaderParameterStruct.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogSmokeWorldRenderer, Log, All);

static TAutoConsoleVariable<int32> CVarSmokeWorldCompositeDebugStage(
	TEXT("r.SmokeCharacter.WorldCompositeDebugStage"),
	6,
	TEXT("0=disabled, 1=callback only, 2=inputs only, 3=density/output only, 4=params only, 5=shader ref only, 6=draw"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarSmokeWorldCompositeUseMinimalPS(
	TEXT("r.SmokeCharacter.WorldCompositeUseMinimalPS"),
	0,
	TEXT("Use a minimal pixel shader with only render target bindings for world composite PSO isolation."),
	ECVF_RenderThreadSafe);

namespace
{
FCriticalSection GSmokeWorldRenderStateLock;
TArray<FSmokeWorldRenderState> GSmokeWorldRenderStates;
TSharedPtr<FSceneViewExtensionBase, ESPMode::ThreadSafe> GSmokeWorldSceneViewExtension;
}

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

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

class FSmokeCompositePS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSmokeCompositePS);
	SHADER_USE_PARAMETER_STRUCT(FSmokeCompositePS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER(FIntVector, GridResolution)
		SHADER_PARAMETER(FVector3f, DomainWorldMin)
		SHADER_PARAMETER(FVector3f, DomainWorldMax)
		SHADER_PARAMETER(FVector3f, SmokeColor)
		SHADER_PARAMETER(float, DensityScale)
		SHADER_PARAMETER(float, Absorption)
		SHADER_PARAMETER(FVector3f, LightDirection)
		SHADER_PARAMETER(FVector3f, LightColor)
		SHADER_PARAMETER(float, LightIntensity)
		SHADER_PARAMETER(float, AmbientIntensity)
		SHADER_PARAMETER(int32, ViewStepCount)
		SHADER_PARAMETER(int32, LightStepCount)
		SHADER_PARAMETER(int32, bOccludeWithSceneDepth)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneColorTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, SceneColorSampler)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SceneDepthTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float>, DensityTexture)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

class FSmokeCompositeMinimalPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSmokeCompositeMinimalPS);
	SHADER_USE_PARAMETER_STRUCT(FSmokeCompositeMinimalPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FSmokeRaymarchCS, "/Plugin/SmokeCharacter/Private/SmokeRaymarch.usf", "RaymarchCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FSmokeCompositePS, "/Plugin/SmokeCharacter/Private/SmokeRaymarch.usf", "CompositePS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FSmokeCompositeMinimalPS, "/Plugin/SmokeCharacter/Private/SmokeRaymarch.usf", "CompositeMinimalPS", SF_Pixel);

namespace
{
bool FindSmokeWorldRenderState(uint32 WorldId, FSmokeWorldRenderState& OutState)
{
	FScopeLock Lock(&GSmokeWorldRenderStateLock);
	const FSmokeWorldRenderState* BestState = nullptr;
	for (const FSmokeWorldRenderState& State : GSmokeWorldRenderStates)
	{
		if (State.WorldId == WorldId && State.IsValid())
		{
			if (!BestState || State.FrameIndex >= BestState->FrameIndex)
			{
				BestState = &State;
			}
		}
	}

	if (!BestState)
	{
		return false;
	}

	OutState = *BestState;
	return true;
}

void LogSmokeTextureDesc(const TCHAR* Name, FRDGTextureRef Texture)
{
	if (!Texture)
	{
		UE_LOG(LogSmokeWorldRenderer, Warning, TEXT("%s=null"), Name);
		return;
	}

	const FRDGTextureDesc& Desc = Texture->Desc;
	UE_LOG(LogSmokeWorldRenderer, Log, TEXT("%s Format=%d Extent=%s NumSamples=%d Flags=0x%llx"),
		Name,
		static_cast<int32>(Desc.Format),
		*Desc.Extent.ToString(),
		Desc.NumSamples,
		static_cast<unsigned long long>(Desc.Flags));
}

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

class FSmokeWorldSceneViewExtension : public FSceneViewExtensionBase
{
public:
	FSmokeWorldSceneViewExtension(const FAutoRegister& AutoRegister)
		: FSceneViewExtensionBase(AutoRegister)
	{
	}

	virtual void PrePostProcessPass_RenderThread(
		FRDGBuilder& GraphBuilder,
		const FSceneView& InView,
		const FPostProcessingInputs& Inputs) override
	{
		const UWorld* World = InView.Family && InView.Family->Scene ? InView.Family->Scene->GetWorld() : nullptr;
		FSmokeWorldRenderState State;
		const bool bStateFound = World && FindSmokeWorldRenderState(World->GetUniqueID(), State);
		UE_LOG(LogSmokeWorldRenderer, Log, TEXT("Smoke world pre-postprocess. World=%s WorldId=%u StateFound=%s"),
			*GetNameSafe(World),
			World ? World->GetUniqueID() : 0,
			bStateFound ? TEXT("true") : TEXT("false"));
	}

	virtual void SubscribeToPostProcessingPass(
		EPostProcessingPass Pass,
		const FSceneView& InView,
		FPostProcessingPassDelegateArray& InOutPassCallbacks,
		bool bIsPassEnabled) override
	{
		if (Pass != EPostProcessingPass::BeforeDOF)
		{
			return;
		}

		const UWorld* World = InView.Family && InView.Family->Scene ? InView.Family->Scene->GetWorld() : nullptr;
		UE_LOG(LogSmokeWorldRenderer, Log, TEXT("Smoke world composite subscribe. World=%s WorldId=%u Pass=%d Enabled=%s"),
			*GetNameSafe(World),
			World ? World->GetUniqueID() : 0,
			static_cast<int32>(Pass),
			bIsPassEnabled ? TEXT("true") : TEXT("false"));

		InOutPassCallbacks.Add(FPostProcessingPassDelegate::CreateRaw(this, &FSmokeWorldSceneViewExtension::PostProcessPass_RenderThread));
	}

	FScreenPassTexture PostProcessPass_RenderThread(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		const FPostProcessMaterialInputs& Inputs)
	{
		const int32 DebugStage = CVarSmokeWorldCompositeDebugStage.GetValueOnRenderThread();
		const bool bUseMinimalPS = CVarSmokeWorldCompositeUseMinimalPS.GetValueOnRenderThread() != 0;
		const bool bDebugComposite = DebugStage != 6 || bUseMinimalPS;
		const UWorld* World = View.Family && View.Family->Scene ? View.Family->Scene->GetWorld() : nullptr;
		if (bDebugComposite)
		{
			UE_LOG(LogSmokeWorldRenderer, Log, TEXT("Smoke world composite debug. Stage=%d MinimalPS=%s World=%s WorldId=%u"),
				DebugStage,
				bUseMinimalPS ? TEXT("true") : TEXT("false"),
				*GetNameSafe(World),
				World ? World->GetUniqueID() : 0);
		}

		if (DebugStage <= 0)
		{
			if (bDebugComposite)
			{
				UE_LOG(LogSmokeWorldRenderer, Log, TEXT("Smoke world composite debug return. Stage=%d Reason=Disabled"), DebugStage);
			}
			return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
		}

		if (DebugStage <= 1)
		{
			if (bDebugComposite)
			{
				UE_LOG(LogSmokeWorldRenderer, Log, TEXT("Smoke world composite debug return. Stage=%d Reason=CallbackOnly"), DebugStage);
			}
			return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
		}

		FSmokeWorldRenderState State;
		if (!World || !FindSmokeWorldRenderState(World->GetUniqueID(), State))
		{
			UE_LOG(LogSmokeWorldRenderer, Log, TEXT("Smoke world composite skipped. World=%s WorldId=%u StateFound=false"),
				*GetNameSafe(World),
				World ? World->GetUniqueID() : 0);
			return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
		}

		FScreenPassTextureSlice SceneColorSlice = Inputs.GetInput(EPostProcessMaterialInput::SceneColor);
		if (!SceneColorSlice.IsValid())
		{
			UE_LOG(LogSmokeWorldRenderer, Log, TEXT("Smoke world composite skipped. World=%s WorldId=%u Reason=NoSceneColor"),
				*GetNameSafe(World),
				World->GetUniqueID());
			return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
		}

		FScreenPassTexture SceneColor(SceneColorSlice);
		FRDGTextureRef SceneDepthTexture = Inputs.SceneTextures.SceneTextures
			? Inputs.SceneTextures.SceneTextures->GetParameters()->SceneDepthTexture
			: nullptr;
		if (!SceneDepthTexture)
		{
			UE_LOG(LogSmokeWorldRenderer, Log, TEXT("Smoke world composite skipped. World=%s WorldId=%u Reason=NoSceneDepth"),
				*GetNameSafe(World),
				World->GetUniqueID());
			return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
		}

		if (bDebugComposite)
		{
			LogSmokeTextureDesc(TEXT("Smoke composite SceneColor"), SceneColor.Texture);
			LogSmokeTextureDesc(TEXT("Smoke composite SceneDepth"), SceneDepthTexture);
		}

		if (DebugStage <= 2)
		{
			if (bDebugComposite)
			{
				UE_LOG(LogSmokeWorldRenderer, Log, TEXT("Smoke world composite debug return. Stage=%d Reason=InputsOnly"), DebugStage);
			}
			return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
		}

		FRDGTextureRef DensityTexture = GraphBuilder.RegisterExternalTexture(State.DensityTarget);
		if (!DensityTexture)
		{
			UE_LOG(LogSmokeWorldRenderer, Log, TEXT("Smoke world composite skipped. World=%s WorldId=%u Reason=NoDensityTexture"),
				*GetNameSafe(World),
				World->GetUniqueID());
			return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
		}
		if (bDebugComposite)
		{
			LogSmokeTextureDesc(TEXT("Smoke composite Density"), DensityTexture);
		}

		FScreenPassRenderTarget Output = Inputs.OverrideOutput;
		if (!Output.IsValid())
		{
			Output = FScreenPassRenderTarget::CreateFromInput(GraphBuilder, SceneColor, View.GetOverwriteLoadAction(), TEXT("SmokeCharacter.WorldComposite"));
		}
		if (bDebugComposite)
		{
			LogSmokeTextureDesc(TEXT("Smoke composite Output"), Output.Texture);
		}

		if (DebugStage <= 3)
		{
			if (bDebugComposite)
			{
				UE_LOG(LogSmokeWorldRenderer, Log, TEXT("Smoke world composite debug return. Stage=%d Reason=DensityAndOutputOnly"), DebugStage);
			}
			return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
		}

		UE_LOG(LogSmokeWorldRenderer, Log, TEXT("Smoke world composite pass. World=%s WorldId=%u Frame=%llu Grid=%s DensityValid=%s SceneRect=%s"),
			*GetNameSafe(World),
			World->GetUniqueID(),
			static_cast<unsigned long long>(State.FrameIndex),
			*State.GridDesc.ToLogString(),
			State.DensityTarget.IsValid() ? TEXT("true") : TEXT("false"),
			*View.UnscaledViewRect.ToString());

		const FVector DomainExtents = State.GridDesc.DomainWorldSize * 0.5;
		const FVector DomainWorldMin = State.GridDesc.WorldOrigin - DomainExtents;
		const FVector DomainWorldMax = State.GridDesc.WorldOrigin + DomainExtents;
		const FVector LightDirection = State.Settings.LightDirection.GetSafeNormal();

		FSmokeCompositePS::FParameters* Parameters = GraphBuilder.AllocParameters<FSmokeCompositePS::FParameters>();
		Parameters->View = View.ViewUniformBuffer;
		Parameters->GridResolution = State.GridDesc.Resolution;
		Parameters->DomainWorldMin = FVector3f(DomainWorldMin);
		Parameters->DomainWorldMax = FVector3f(DomainWorldMax);
		Parameters->SmokeColor = FVector3f(State.Settings.SmokeColor.R, State.Settings.SmokeColor.G, State.Settings.SmokeColor.B);
		Parameters->DensityScale = FMath::Max(0.0f, State.Settings.DensityScale);
		Parameters->Absorption = FMath::Max(0.0f, State.Settings.Absorption);
		Parameters->LightDirection = FVector3f(LightDirection.IsNearlyZero() ? FVector(-0.4, -0.3, -0.85) : LightDirection);
		Parameters->LightColor = FVector3f(State.Settings.LightColor.R, State.Settings.LightColor.G, State.Settings.LightColor.B);
		Parameters->LightIntensity = FMath::Max(0.0f, State.Settings.LightIntensity);
		Parameters->AmbientIntensity = FMath::Max(0.0f, State.Settings.AmbientIntensity);
		Parameters->ViewStepCount = FMath::Clamp(State.Settings.ViewStepCount, 8, 256);
		Parameters->LightStepCount = FMath::Clamp(State.Settings.LightStepCount, 0, 64);
		Parameters->bOccludeWithSceneDepth = State.Settings.bOccludeWithSceneDepth ? 1 : 0;
		Parameters->SceneColorTexture = SceneColor.Texture;
		Parameters->SceneColorSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		Parameters->SceneDepthTexture = SceneDepthTexture;
		Parameters->DensityTexture = DensityTexture;
		Parameters->RenderTargets[0] = Output.GetRenderTargetBinding();

		if (DebugStage <= 4)
		{
			if (bDebugComposite)
			{
				UE_LOG(LogSmokeWorldRenderer, Log, TEXT("Smoke world composite debug return. Stage=%d Reason=ParamsOnly"), DebugStage);
			}
			return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
		}

		const FScreenPassTextureViewport InputViewport(SceneColor);
		const FScreenPassTextureViewport OutputViewport(Output);
		if (bUseMinimalPS)
		{
			FSmokeCompositeMinimalPS::FParameters* MinimalParameters = GraphBuilder.AllocParameters<FSmokeCompositeMinimalPS::FParameters>();
			MinimalParameters->RenderTargets[0] = Output.GetRenderTargetBinding();
			const TShaderMapRef<FSmokeCompositeMinimalPS> PixelShader(GetGlobalShaderMap(View.GetFeatureLevel()));
			if (bDebugComposite)
			{
				UE_LOG(LogSmokeWorldRenderer, Log, TEXT("Smoke world composite minimal shader. Valid=%s FeatureLevel=%d"),
					PixelShader.IsValid() ? TEXT("true") : TEXT("false"),
					static_cast<int32>(View.GetFeatureLevel()));
			}

			if (DebugStage <= 5)
			{
				if (bDebugComposite)
				{
					UE_LOG(LogSmokeWorldRenderer, Log, TEXT("Smoke world composite debug return. Stage=%d Reason=MinimalShaderRefOnly"), DebugStage);
				}
				return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
			}

			AddDrawScreenPass(
				GraphBuilder,
				RDG_EVENT_NAME("SmokeCharacter World Composite Minimal"),
				View,
				OutputViewport,
				InputViewport,
				PixelShader,
				MinimalParameters);
		}
		else
		{
			const TShaderMapRef<FSmokeCompositePS> PixelShader(GetGlobalShaderMap(View.GetFeatureLevel()));
			if (bDebugComposite)
			{
				UE_LOG(LogSmokeWorldRenderer, Log, TEXT("Smoke world composite full shader. Valid=%s FeatureLevel=%d"),
					PixelShader.IsValid() ? TEXT("true") : TEXT("false"),
					static_cast<int32>(View.GetFeatureLevel()));
			}

			if (DebugStage <= 5)
			{
				if (bDebugComposite)
				{
					UE_LOG(LogSmokeWorldRenderer, Log, TEXT("Smoke world composite debug return. Stage=%d Reason=FullShaderRefOnly"), DebugStage);
				}
				return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
			}

			AddDrawScreenPass(
				GraphBuilder,
				RDG_EVENT_NAME("SmokeCharacter World Composite"),
				View,
				OutputViewport,
				InputViewport,
				PixelShader,
				Parameters);
		}

		return MoveTemp(Output);
	}
};
}

bool FSmokeWorldRenderState::IsValid() const
{
	return WorldId != 0
		&& GridDesc.IsValid()
		&& DensityTarget.IsValid()
		&& Settings.bEnableWorldSpaceRender;
}

void FSmokeRenderer::InitializeWorldRenderer()
{
	if (!GEngine || !GEngine->ViewExtensions.IsValid())
	{
		UE_LOG(LogSmokeWorldRenderer, Verbose, TEXT("Smoke world renderer initialization deferred until GEngine and view extensions are available."));
		return;
	}

	bool bNeedsRegister = !GSmokeWorldSceneViewExtension.IsValid();
	if (!bNeedsRegister)
	{
		FSceneViewExtensionContext Context;
		const TArray<FSceneViewExtensionRef> ActiveExtensions = GEngine->ViewExtensions->GatherActiveExtensions(Context);
		bNeedsRegister = !ActiveExtensions.ContainsByPredicate([](const FSceneViewExtensionRef& Extension)
		{
			return &Extension.Get() == GSmokeWorldSceneViewExtension.Get();
		});
	}

	if (bNeedsRegister)
	{
		GSmokeWorldSceneViewExtension.Reset();
		GSmokeWorldSceneViewExtension = FSceneViewExtensions::NewExtension<FSmokeWorldSceneViewExtension>();
		UE_LOG(LogSmokeWorldRenderer, Log, TEXT("Smoke world renderer initialized."));
	}
}

void FSmokeRenderer::ShutdownWorldRenderer()
{
	GSmokeWorldSceneViewExtension.Reset();
	FScopeLock Lock(&GSmokeWorldRenderStateLock);
	GSmokeWorldRenderStates.Reset();
	UE_LOG(LogSmokeWorldRenderer, Log, TEXT("Smoke world renderer shut down."));
}

void FSmokeRenderer::PublishWorldRenderState_RenderThread(const FSmokeWorldRenderState& State)
{
	if (!State.IsValid())
	{
		UE_LOG(LogSmokeWorldRenderer, Log, TEXT("Smoke world render state rejected. WorldId=%u Frame=%llu GridValid=%s DensityValid=%s WorldSpaceRender=%s"),
			State.WorldId,
			static_cast<unsigned long long>(State.FrameIndex),
			State.GridDesc.IsValid() ? TEXT("true") : TEXT("false"),
			State.DensityTarget.IsValid() ? TEXT("true") : TEXT("false"),
			State.Settings.bEnableWorldSpaceRender ? TEXT("true") : TEXT("false"));
		return;
	}

	FScopeLock Lock(&GSmokeWorldRenderStateLock);
	for (FSmokeWorldRenderState& ExistingState : GSmokeWorldRenderStates)
	{
		if (ExistingState.WorldId == State.WorldId)
		{
			ExistingState = State;
			UE_LOG(LogSmokeWorldRenderer, Log, TEXT("Smoke world render state updated. WorldId=%u Frame=%llu Grid=%s"),
				State.WorldId,
				static_cast<unsigned long long>(State.FrameIndex),
				*State.GridDesc.ToLogString());
			return;
		}
	}

	GSmokeWorldRenderStates.Add(State);
	UE_LOG(LogSmokeWorldRenderer, Log, TEXT("Smoke world render state published. WorldId=%u Frame=%llu Grid=%s"),
		State.WorldId,
		static_cast<unsigned long long>(State.FrameIndex),
		*State.GridDesc.ToLogString());
}

void FSmokeRenderer::RemoveWorldRenderState(uint32 WorldId)
{
	FScopeLock Lock(&GSmokeWorldRenderStateLock);
	const int32 RemovedCount = GSmokeWorldRenderStates.RemoveAll([WorldId](const FSmokeWorldRenderState& State)
	{
		return State.WorldId == WorldId;
	});
	UE_LOG(LogSmokeWorldRenderer, Log, TEXT("Smoke world render state removed. WorldId=%u Removed=%d"), WorldId, RemovedCount);
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
