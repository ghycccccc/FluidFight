// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SmokeGrid.h"

class UTextureRenderTarget2D;

class SMOKECHARACTER_API FSmokeDebugRenderer
{
public:
	void DispatchDensitySlice(
		const FSmokeGridDesc& GridDesc,
		uint64 FrameIndex,
		int32 SliceAxis,
		int32 SliceIndex,
		bool bUseFalseColor,
		UTextureRenderTarget2D* OutputRenderTarget) const;
};
