// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SmokeGrid.h"

class FSmokeDebugRenderer;
class UTextureRenderTarget2D;

struct FSmokeSolverSettings
{
	float DeltaTime = 0.0f;
	float TimeStepScale = 1.0f;
	float DensityDissipation = 0.995f;
	bool bVerboseLogging = false;
};

struct FSmokeDensitySliceRequest
{
	int32 SliceAxis = 2;
	int32 SliceIndex = 0;
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
