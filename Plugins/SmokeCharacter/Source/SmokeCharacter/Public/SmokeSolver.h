// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SmokeDebugRenderer.h"
#include "SmokeGrid.h"

class FSmokeDebugRenderer;
class UTextureRenderTarget2D;

struct FSmokeSolverSettings
{
	float DeltaTime = 0.0f;
	float TimeStepScale = 1.0f;
	float DensityDissipation = 0.995f;
	float VelocityDissipation = 0.995f;
	int32 PressureIterations = 20;
	bool bVerboseLogging = false;
};

struct FSmokeDensitySliceRequest
{
	int32 SliceAxis = 2;
	int32 SliceIndex = 0;
	ESmokeDebugField Field = ESmokeDebugField::Density;
	bool bUseFalseColor = false;
	UTextureRenderTarget2D* OutputRenderTarget = nullptr;
	const FSmokeDebugRenderer* DebugRenderer = nullptr;
};

class SMOKECHARACTER_API FSmokeSolver
{
public:
	void ResetResources();
	void DispatchSimulation(
		const FSmokeGridDesc& GridDesc,
		const FSmokeSolverSettings& Settings,
		uint64 FrameIndex,
		const FSmokeDensitySliceRequest* SliceRequest);

private:
	FSmokeGridResources GridResources;
	bool bResetPending = true;
};
