// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FSmokeGridDesc
{
	FIntVector Resolution = FIntVector(96, 96, 160);
	FVector DomainWorldSize = FVector(120.0, 120.0, 220.0);
	FVector WorldOrigin = FVector::ZeroVector;
	FTransform DomainToWorld = FTransform::Identity;
	FTransform WorldToDomain = FTransform::Identity;
	FVector VoxelSize = FVector::OneVector;

	bool IsValid() const;
	FString ToLogString() const;
};

class SMOKECHARACTER_API FSmokeGrid
{
public:
	static FSmokeGridDesc BuildDesc(const FIntVector& Resolution, const FVector& DomainWorldSize, const FVector& WorldOrigin);
	static void DispatchSyntheticDensityPass(const FSmokeGridDesc& GridDesc, uint64 FrameIndex);

private:
	static FIntVector SanitizeResolution(const FIntVector& Resolution);
	static FVector SanitizeDomainWorldSize(const FVector& DomainWorldSize);
};
