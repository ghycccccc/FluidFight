// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PooledRenderTarget.h"
#include "SmokeGrid.h"

class FRDGBuilder;
class FRDGTexture;
class FRHITexture;
class UWorld;

struct FSmokeRenderSettings
{
	FIntPoint OutputDimensions = FIntPoint(512, 512);
	FVector CameraWorldPosition = FVector::ZeroVector;
	FVector CameraForward = FVector::ForwardVector;
	FVector CameraRight = FVector::RightVector;
	FVector CameraUp = FVector::UpVector;
	float VerticalFovDegrees = 60.0f;
	FLinearColor SmokeColor = FLinearColor(0.72f, 0.82f, 0.88f, 1.0f);
	float DensityScale = 2.5f;
	float Absorption = 1.2f;
	FVector LightDirection = FVector(-0.4f, -0.3f, -0.85f);
	FLinearColor LightColor = FLinearColor::White;
	float LightIntensity = 1.0f;
	float AmbientIntensity = 0.18f;
	int32 ViewStepCount = 96;
	int32 LightStepCount = 12;
	bool bEnableWorldSpaceRender = false;
	bool bEnablePreviewRender = true;
	bool bOccludeWithSceneDepth = false;
};

struct FSmokeWorldRenderState
{
	uint32 WorldId = 0;
	uint64 FrameIndex = 0;
	FSmokeGridDesc GridDesc;
	FSmokeRenderSettings Settings;
	TRefCountPtr<IPooledRenderTarget> DensityTarget;

	bool IsValid() const;
};

class SMOKECHARACTER_API FSmokeRenderer
{
public:
	static void InitializeWorldRenderer();
	static void ShutdownWorldRenderer();
	static void PublishWorldRenderState_RenderThread(const FSmokeWorldRenderState& State);
	static void RemoveWorldRenderState(uint32 WorldId);

	void AddVolumeRenderPass(
		FRDGBuilder& GraphBuilder,
		const FSmokeGridDesc& GridDesc,
		FRDGTexture* DensityTexture,
		const FSmokeRenderSettings& Settings,
		FRHITexture* OutputTextureRHI) const;
};
