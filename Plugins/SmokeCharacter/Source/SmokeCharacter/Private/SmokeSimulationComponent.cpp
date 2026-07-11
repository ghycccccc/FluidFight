// Copyright Epic Games, Inc. All Rights Reserved.

#include "SmokeSimulationComponent.h"

#include "DrawDebugHelpers.h"
#include "GameFramework/Actor.h"
#include "SmokeCharacter.h"

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
	LogLifecycleEvent(TEXT("registered"));
}

void USmokeSimulationComponent::OnUnregister()
{
	LogLifecycleEvent(TEXT("unregistered"));
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
	const bool bShouldDispatchGrid = bSimulationEnabled && World && (World->IsGameWorld() || DebugMode == ESmokeDebugMode::Timing);
	if (bShouldDispatchGrid)
	{
		if (bGridResourcesDirty)
		{
			ReinitializeGridResources();
		}

		DispatchSyntheticGridPattern();
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
}

void USmokeSimulationComponent::ReinitializeGridResources()
{
	CurrentGridDesc = BuildGridDesc();
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
}
#endif
