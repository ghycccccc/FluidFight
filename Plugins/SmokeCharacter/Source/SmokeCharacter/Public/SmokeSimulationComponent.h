// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SmokeSimulationComponent.generated.h"

UENUM(BlueprintType)
enum class ESmokeDebugMode : uint8
{
	None UMETA(DisplayName = "None"),
	Lifecycle UMETA(DisplayName = "Lifecycle"),
	DomainBounds UMETA(DisplayName = "Domain Bounds"),
	Timing UMETA(DisplayName = "Timing")
};

UCLASS(ClassGroup = (Smoke), meta = (BlueprintSpawnableComponent))
class SMOKECHARACTER_API USmokeSimulationComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USmokeSimulationComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smoke|Simulation")
	bool bSimulationEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smoke|Simulation")
	FIntVector GridResolution = FIntVector(96, 96, 160);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smoke|Simulation", meta = (ClampMin = "1.0", ForceUnits = "cm"))
	FVector DomainWorldSize = FVector(120.0, 120.0, 220.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smoke|Debug")
	ESmokeDebugMode DebugMode = ESmokeDebugMode::Lifecycle;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smoke|Debug")
	bool bShowDomainPreview = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smoke|Debug")
	FColor DomainPreviewColor = FColor::Cyan;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smoke|Debug", meta = (ClampMin = "0.0"))
	float DomainPreviewThickness = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smoke|Debug", meta = (ClampMin = "0.0", ForceUnits = "s"))
	float DomainPreviewDuration = 0.0f;

	UFUNCTION(BlueprintCallable, Category = "Smoke")
	void SetSimulationEnabled(bool bEnabled);

	UFUNCTION(BlueprintCallable, Category = "Smoke")
	void ResetSimulation();

	UFUNCTION(BlueprintCallable, Category = "Smoke|Domain")
	void UpdateDomainPreview();

	UFUNCTION(BlueprintPure, Category = "Smoke|Domain")
	FVector GetDomainOrigin() const;

	UFUNCTION(BlueprintPure, Category = "Smoke|Domain")
	FBox GetDomainBounds() const;

	UFUNCTION(BlueprintPure, Category = "Smoke|Domain")
	FTransform GetDomainToWorldTransform() const;

protected:
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	void LogLifecycleEvent(const TCHAR* EventName) const;
	void LogDomainTiming(float DeltaTime, double UpdateSeconds) const;
	FVector GetDomainExtents() const;
};
