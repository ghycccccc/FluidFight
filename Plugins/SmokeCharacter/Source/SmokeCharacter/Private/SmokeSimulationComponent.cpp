// Copyright Epic Games, Inc. All Rights Reserved.

#include "SmokeSimulationComponent.h"

#include "GameFramework/Actor.h"
#include "SmokeCharacter.h"

USmokeSimulationComponent::USmokeSimulationComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
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
