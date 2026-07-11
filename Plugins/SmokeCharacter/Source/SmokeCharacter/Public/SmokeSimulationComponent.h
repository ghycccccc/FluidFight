// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SmokeDebugRenderer.h"
#include "SmokeGrid.h"
#include "SmokeSimulationComponent.generated.h"

class UCanvas;
class UTextureRenderTarget2D;

UENUM(BlueprintType)
enum class ESmokeDebugMode : uint8
{
	None UMETA(DisplayName = "None"),
	Lifecycle UMETA(DisplayName = "Lifecycle"),
	DomainBounds UMETA(DisplayName = "Domain Bounds"),
	Timing UMETA(DisplayName = "Timing"),
	DensitySlice UMETA(DisplayName = "Density Slice")
};

UENUM(BlueprintType)
enum class ESmokeGridPreset : uint8
{
	Custom UMETA(DisplayName = "Custom"),
	Debug32 UMETA(DisplayName = "32 x 32 x 48"),
	Debug64 UMETA(DisplayName = "64 x 64 x 96"),
	Target96 UMETA(DisplayName = "96 x 96 x 160")
};

UENUM(BlueprintType)
enum class ESmokeSliceAxis : uint8
{
	X UMETA(DisplayName = "X"),
	Y UMETA(DisplayName = "Y"),
	Z UMETA(DisplayName = "Z")
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
	ESmokeGridPreset GridPreset = ESmokeGridPreset::Target96;

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smoke|Debug|Density Slice")
	ESmokeSliceAxis SliceAxis = ESmokeSliceAxis::Z;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smoke|Debug|Density Slice", meta = (ClampMin = "0"))
	int32 SliceIndex = 80;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smoke|Debug|Density Slice")
	bool bUseDensitySliceFalseColor = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smoke|Debug|Density Slice", meta = (ClampMin = "1.0"))
	float DensitySliceOverlayScale = 3.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Smoke|Debug|Density Slice")
	TObjectPtr<UTextureRenderTarget2D> DensitySlicePreview = nullptr;

	UFUNCTION(BlueprintCallable, Category = "Smoke")
	void SetSimulationEnabled(bool bEnabled);

	UFUNCTION(BlueprintCallable, Category = "Smoke")
	void ResetSimulation();

	UFUNCTION(BlueprintPure, Category = "Smoke|Grid")
	FIntVector GetEffectiveGridResolution() const;

	FSmokeGridDesc BuildGridDesc() const;

	UFUNCTION(BlueprintCallable, Category = "Smoke|Grid")
	void MarkGridResourcesDirty();

	UFUNCTION(BlueprintCallable, Category = "Smoke|Grid")
	void ReinitializeGridResources();

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
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	void LogLifecycleEvent(const TCHAR* EventName) const;
	void LogDomainTiming(float DeltaTime, double UpdateSeconds) const;
	void DispatchSyntheticGridPattern();
	void DispatchDensitySliceDebug();
	void ClampDensitySliceIndex();
	void EnsureDensitySliceRenderTarget();
	void RegisterDensitySliceDebugDraw();
	void UnregisterDensitySliceDebugDraw();
	void DrawDensitySliceOverlay(UCanvas* Canvas, class APlayerController* PlayerController);
	FVector GetDomainExtents() const;

	FSmokeGridDesc CurrentGridDesc;
	FSmokeDebugRenderer DebugRenderer;
	FDelegateHandle DensitySliceDebugDrawHandle;
	bool bGridResourcesDirty = true;
	uint64 GridDispatchFrameIndex = 0;
};
