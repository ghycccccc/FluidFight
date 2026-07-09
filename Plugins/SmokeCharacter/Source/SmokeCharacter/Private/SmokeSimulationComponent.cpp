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

	if (DebugMode == ESmokeDebugMode::Lifecycle)
	{
		UE_LOG(LogSmokeCharacter, Log, TEXT("Smoke simulation %s on %s."),
			bSimulationEnabled ? TEXT("enabled") : TEXT("disabled"),
			*GetNameSafe(GetOwner()));
	}
}

void USmokeSimulationComponent::ResetSimulation()
{
	if (DebugMode == ESmokeDebugMode::Lifecycle)
	{
		UE_LOG(LogSmokeCharacter, Log, TEXT("Smoke simulation reset requested on %s. Resolution=%s DomainWorldSize=%s"),
			*GetNameSafe(GetOwner()),
			*GridResolution.ToString(),
			*DomainWorldSize.ToString());
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

FVector USmokeSimulationComponent::GetDomainExtents() const
{
	return DomainWorldSize * 0.5;
}
