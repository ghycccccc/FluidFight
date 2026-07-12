// Copyright Epic Games, Inc. All Rights Reserved.

#include "SmokeSimulationComponent.h"

#include "CanvasItem.h"
#include "Debug/DebugDrawService.h"
#include "DrawDebugHelpers.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GameFramework/Actor.h"
#include "SmokeCharacter.h"
#include "SmokeDebugRenderer.h"

USmokeSimulationComponent::USmokeSimulationComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
#if WITH_EDITOR
	bTickInEditor = true;
#endif
}

void USmokeSimulationComponent::OnRegister()
{
	Super::OnRegister();
	MarkGridResourcesDirty();
	RegisterDensitySliceDebugDraw();
	LogLifecycleEvent(TEXT("registered"));
}

void USmokeSimulationComponent::OnUnregister()
{
	LogLifecycleEvent(TEXT("unregistered"));
	UnregisterDensitySliceDebugDraw();
	Super::OnUnregister();
}

void USmokeSimulationComponent::BeginPlay()
{
	Super::BeginPlay();
	LogLifecycleEvent(TEXT("begin play"));
}

void USmokeSimulationComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	LogLifecycleEvent(TEXT("end play"));
	Super::EndPlay(EndPlayReason);
}

void USmokeSimulationComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const double UpdateStartSeconds = FPlatformTime::Seconds();

	UpdateDomainPreview();

	const UWorld* World = GetWorld();
	const bool bShouldDispatchGrid = bSimulationEnabled && World && (World->IsGameWorld() || DebugMode == ESmokeDebugMode::Timing || DebugMode == ESmokeDebugMode::DensitySlice);
	if (bShouldDispatchGrid)
	{
		if (bGridResourcesDirty)
		{
			ReinitializeGridResources();
		}

		DispatchSmokeSimulation(DeltaTime);
	}

	if (DebugMode == ESmokeDebugMode::Timing)
	{
		const double UpdateSeconds = FPlatformTime::Seconds() - UpdateStartSeconds;
		LogDomainTiming(DeltaTime, UpdateSeconds);
	}
}

void USmokeSimulationComponent::SetSimulationEnabled(bool bEnabled)
{
	if (bSimulationEnabled == bEnabled)
	{
		return;
	}

	bSimulationEnabled = bEnabled;
	if (bSimulationEnabled)
	{
		MarkGridResourcesDirty();
	}

	if (DebugMode == ESmokeDebugMode::Lifecycle)
	{
		UE_LOG(LogSmokeCharacter, Log, TEXT("Smoke simulation %s on %s."),
			bSimulationEnabled ? TEXT("enabled") : TEXT("disabled"),
			*GetNameSafe(GetOwner()));
	}
}

void USmokeSimulationComponent::ResetSimulation()
{
	MarkGridResourcesDirty();
	Solver.ResetResources();

	if (DebugMode == ESmokeDebugMode::Lifecycle)
	{
		UE_LOG(LogSmokeCharacter, Log, TEXT("Smoke simulation reset requested on %s. Resolution=%s DomainWorldSize=%s"),
			*GetNameSafe(GetOwner()),
			*GridResolution.ToString(),
			*DomainWorldSize.ToString());
	}
}

FIntVector USmokeSimulationComponent::GetEffectiveGridResolution() const
{
	switch (GridPreset)
	{
	case ESmokeGridPreset::Debug32:
		return FIntVector(32, 32, 48);
	case ESmokeGridPreset::Debug64:
		return FIntVector(64, 64, 96);
	case ESmokeGridPreset::Target96:
		return FIntVector(96, 96, 160);
	case ESmokeGridPreset::Custom:
	default:
		return FIntVector(
			FMath::Max(1, GridResolution.X),
			FMath::Max(1, GridResolution.Y),
			FMath::Max(1, GridResolution.Z));
	}
}

FSmokeGridDesc USmokeSimulationComponent::BuildGridDesc() const
{
	return FSmokeGrid::BuildDesc(GetEffectiveGridResolution(), DomainWorldSize, GetDomainOrigin());
}

void USmokeSimulationComponent::MarkGridResourcesDirty()
{
	bGridResourcesDirty = true;
	Solver.ResetResources();
}

void USmokeSimulationComponent::ReinitializeGridResources()
{
	CurrentGridDesc = BuildGridDesc();
	ClampDensitySliceIndex();
	Solver.ResetResources();
	bGridResourcesDirty = false;

	if (DebugMode == ESmokeDebugMode::Lifecycle || DebugMode == ESmokeDebugMode::Timing)
	{
		UE_LOG(LogSmokeCharacter, Log, TEXT("Smoke grid resources initialized. %s Velocity=PF_A16B16G16R16F Density=PF_R16F Pressure=PF_R16F Divergence=PF_R16F SDF=PF_R16F BoundaryVelocity=PF_A16B16G16R16F"),
			*CurrentGridDesc.ToLogString());
	}
}

void USmokeSimulationComponent::UpdateDomainPreview()
{
	if (!bShowDomainPreview || DebugMode != ESmokeDebugMode::DomainBounds)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	DrawDebugBox(
		World,
		GetDomainOrigin(),
		GetDomainExtents(),
		FQuat::Identity,
		DomainPreviewColor,
		false,
		DomainPreviewDuration,
		0,
		DomainPreviewThickness);
}

FVector USmokeSimulationComponent::GetDomainOrigin() const
{
	const AActor* Owner = GetOwner();
	return Owner ? Owner->GetActorLocation() : FVector::ZeroVector;
}

FBox USmokeSimulationComponent::GetDomainBounds() const
{
	const FVector Origin = GetDomainOrigin();
	const FVector Extents = GetDomainExtents();
	return FBox(Origin - Extents, Origin + Extents);
}

FTransform USmokeSimulationComponent::GetDomainToWorldTransform() const
{
	return FTransform(FQuat::Identity, GetDomainOrigin(), DomainWorldSize);
}

void USmokeSimulationComponent::LogLifecycleEvent(const TCHAR* EventName) const
{
	if (DebugMode != ESmokeDebugMode::Lifecycle)
	{
		return;
	}

	UE_LOG(LogSmokeCharacter, Log, TEXT("SmokeSimulationComponent %s on %s. Resolution=%s DomainWorldSize=%s Enabled=%s"),
		EventName,
		*GetNameSafe(GetOwner()),
		*GridResolution.ToString(),
		*DomainWorldSize.ToString(),
		bSimulationEnabled ? TEXT("true") : TEXT("false"));
}

void USmokeSimulationComponent::LogDomainTiming(float DeltaTime, double UpdateSeconds) const
{
	const FVector Extents = GetDomainExtents();
	const FVector VoxelSize(
		GridResolution.X > 0 ? DomainWorldSize.X / static_cast<double>(GridResolution.X) : 0.0,
		GridResolution.Y > 0 ? DomainWorldSize.Y / static_cast<double>(GridResolution.Y) : 0.0,
		GridResolution.Z > 0 ? DomainWorldSize.Z / static_cast<double>(GridResolution.Z) : 0.0);

	UE_LOG(LogSmokeCharacter, Log, TEXT("Smoke domain timing on %s. DeltaTime=%.4f UpdateMs=%.4f Origin=%s Extents=%s VoxelSize=%s Resolution=%s"),
		*GetNameSafe(GetOwner()),
		DeltaTime,
		UpdateSeconds * 1000.0,
		*GetDomainOrigin().ToString(),
		*Extents.ToString(),
		*VoxelSize.ToString(),
		*GridResolution.ToString());
}

void USmokeSimulationComponent::DispatchSyntheticGridPattern()
{
	if (!CurrentGridDesc.IsValid())
	{
		ReinitializeGridResources();
	}

	FSmokeGrid::DispatchSyntheticDensityPass(CurrentGridDesc, GridDispatchFrameIndex++);

	if (DebugMode == ESmokeDebugMode::Timing)
	{
		const FIntVector GroupCount(
			FMath::DivideAndRoundUp(CurrentGridDesc.Resolution.X, 8),
			FMath::DivideAndRoundUp(CurrentGridDesc.Resolution.Y, 8),
			FMath::DivideAndRoundUp(CurrentGridDesc.Resolution.Z, 8));

		UE_LOG(LogSmokeCharacter, Log, TEXT("Smoke grid synthetic density pass dispatched. Groups=%s Frame=%llu"),
			*GroupCount.ToString(),
			static_cast<unsigned long long>(GridDispatchFrameIndex));
	}
}

void USmokeSimulationComponent::DispatchSmokeSimulation(float DeltaTime)
{
	if (!CurrentGridDesc.IsValid())
	{
		ReinitializeGridResources();
	}

	CurrentGridDesc = BuildGridDesc();
	ClampDensitySliceIndex();
	FSmokeDensitySliceRequest SliceRequest;
	FSmokeDensitySliceRequest* SliceRequestPtr = nullptr;
	if (DebugMode == ESmokeDebugMode::DensitySlice)
	{
		EnsureDensitySliceRenderTarget();
		SliceRequest.SliceAxis = static_cast<int32>(SliceAxis);
		SliceRequest.SliceIndex = SliceIndex;
		SliceRequest.bUseFalseColor = bUseDensitySliceFalseColor;
		SliceRequest.OutputRenderTarget = DensitySlicePreview;
		SliceRequest.DebugRenderer = &DebugRenderer;
		SliceRequestPtr = &SliceRequest;
	}

	const uint64 FrameIndex = GridDispatchFrameIndex++;
	FSmokeSolverSettings SolverSettings;
	SolverSettings.DeltaTime = DeltaTime;
	SolverSettings.TimeStepScale = TimeStepScale;
	SolverSettings.DensityDissipation = DensityDissipation;
	SolverSettings.bVerboseLogging = DebugMode == ESmokeDebugMode::Timing;
	Solver.DispatchSimulation(
		CurrentGridDesc,
		SolverSettings,
		FrameIndex,
		SliceRequestPtr);

	if (DebugMode == ESmokeDebugMode::DensitySlice)
	{
		const FIntPoint SliceDimensions = FSmokeGrid::GetSliceDimensions(CurrentGridDesc.Resolution, static_cast<int32>(SliceAxis));
		UE_LOG(LogSmokeCharacter, Verbose, TEXT("Smoke advected density slice dispatched. Axis=%d Index=%d Size=%s Frame=%llu"),
			static_cast<int32>(SliceAxis),
			SliceIndex,
			*SliceDimensions.ToString(),
			static_cast<unsigned long long>(FrameIndex));
	}
}

void USmokeSimulationComponent::ClampDensitySliceIndex()
{
	const FIntVector EffectiveResolution = GetEffectiveGridResolution();
	SliceIndex = FSmokeGrid::ClampSliceIndex(EffectiveResolution, static_cast<int32>(SliceAxis), SliceIndex);
}

void USmokeSimulationComponent::EnsureDensitySliceRenderTarget()
{
	const FIntPoint SliceDimensions = FSmokeGrid::GetSliceDimensions(GetEffectiveGridResolution(), static_cast<int32>(SliceAxis));
	const bool bNeedsNewTarget = !DensitySlicePreview
		|| DensitySlicePreview->SizeX != SliceDimensions.X
		|| DensitySlicePreview->SizeY != SliceDimensions.Y;

	if (!bNeedsNewTarget)
	{
		return;
	}

	DensitySlicePreview = NewObject<UTextureRenderTarget2D>(this, TEXT("SmokeDensitySlicePreview"), RF_Transient);
	DensitySlicePreview->RenderTargetFormat = RTF_RGBA16f;
	DensitySlicePreview->ClearColor = FLinearColor::Black;
	DensitySlicePreview->bAutoGenerateMips = false;
	DensitySlicePreview->InitCustomFormat(SliceDimensions.X, SliceDimensions.Y, PF_FloatRGBA, false);
	DensitySlicePreview->UpdateResourceImmediate(true);
}

void USmokeSimulationComponent::RegisterDensitySliceDebugDraw()
{
	if (DensitySliceDebugDrawHandle.IsValid())
	{
		return;
	}

	DensitySliceDebugDrawHandle = UDebugDrawService::Register(
		TEXT("Game"),
		FDebugDrawDelegate::CreateUObject(this, &USmokeSimulationComponent::DrawDensitySliceOverlay));
}

void USmokeSimulationComponent::UnregisterDensitySliceDebugDraw()
{
	if (!DensitySliceDebugDrawHandle.IsValid())
	{
		return;
	}

	UDebugDrawService::Unregister(DensitySliceDebugDrawHandle);
	DensitySliceDebugDrawHandle.Reset();
}

void USmokeSimulationComponent::DrawDensitySliceOverlay(UCanvas* Canvas, APlayerController* PlayerController)
{
	if (DebugMode != ESmokeDebugMode::DensitySlice || !DensitySlicePreview || !Canvas)
	{
		return;
	}

	FTexture* RenderTargetTexture = DensitySlicePreview->GetResource();
	if (!RenderTargetTexture)
	{
		return;
	}

	const FIntPoint SliceDimensions = FSmokeGrid::GetSliceDimensions(GetEffectiveGridResolution(), static_cast<int32>(SliceAxis));
	const float Scale = FMath::Max(1.0f, DensitySliceOverlayScale);
	const FVector2D DrawPosition(24.0f, 56.0f);
	const FVector2D DrawSize(
		static_cast<float>(SliceDimensions.X) * Scale,
		static_cast<float>(SliceDimensions.Y) * Scale);

	FCanvasTileItem TileItem(DrawPosition, RenderTargetTexture, DrawSize, FLinearColor::White);
	TileItem.BlendMode = SE_BLEND_Opaque;
	Canvas->DrawItem(TileItem);

	if (GEngine && GEngine->GetSmallFont())
	{
		const FString Label = FString::Printf(
			TEXT("Smoke Density Slice  Axis=%d  Index=%d  Resolution=%s"),
			static_cast<int32>(SliceAxis),
			SliceIndex,
			*GetEffectiveGridResolution().ToString());

		FCanvasTextItem TextItem(DrawPosition + FVector2D(0.0f, -20.0f), FText::FromString(Label), GEngine->GetSmallFont(), FLinearColor::White);
		TextItem.EnableShadow(FLinearColor::Black);
		Canvas->DrawItem(TextItem);
	}
}

FVector USmokeSimulationComponent::GetDomainExtents() const
{
	return DomainWorldSize * 0.5;
}

#if WITH_EDITOR
void USmokeSimulationComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = PropertyChangedEvent.Property ? PropertyChangedEvent.Property->GetFName() : NAME_None;
	if (PropertyName == GET_MEMBER_NAME_CHECKED(USmokeSimulationComponent, GridPreset)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(USmokeSimulationComponent, GridResolution)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(USmokeSimulationComponent, DomainWorldSize))
	{
		MarkGridResourcesDirty();
	}

	const bool bSliceShapeChanged = PropertyName == GET_MEMBER_NAME_CHECKED(USmokeSimulationComponent, SliceAxis)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(USmokeSimulationComponent, GridPreset)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(USmokeSimulationComponent, GridResolution);
	if (bSliceShapeChanged)
	{
		SliceIndex = FSmokeGrid::GetSliceAxisExtent(GetEffectiveGridResolution(), static_cast<int32>(SliceAxis)) / 2;
		DensitySlicePreview = nullptr;
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(USmokeSimulationComponent, SliceIndex))
	{
		ClampDensitySliceIndex();
	}
}
#endif
