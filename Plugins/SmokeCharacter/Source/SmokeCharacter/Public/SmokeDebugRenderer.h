// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SmokeGrid.h"

class FRDGBuilder;
class FRDGTexture;
class FRHITexture;
class UTextureRenderTarget2D;

enum class ESmokeDebugField : uint8
{
	Density = 0,
	VelocityMagnitude = 1,
	Pressure = 2,
	Divergence = 3
};

class SMOKECHARACTER_API FSmokeDebugRenderer
{
public:
	void AddDensitySlicePass(
		FRDGBuilder& GraphBuilder,
		const FSmokeGridDesc& GridDesc,
		FRDGTexture* DensityTexture,
		int32 SliceAxis,
		int32 SliceIndex,
		bool bUseFalseColor,
		FRHITexture* OutputTextureRHI) const;

	void AddScalarFieldSlicePass(
		FRDGBuilder& GraphBuilder,
		const FSmokeGridDesc& GridDesc,
		FRDGTexture* FieldTexture,
		ESmokeDebugField Field,
		int32 SliceAxis,
		int32 SliceIndex,
		bool bUseFalseColor,
		FRHITexture* OutputTextureRHI) const;

	void AddVelocityMagnitudeSlicePass(
		FRDGBuilder& GraphBuilder,
		const FSmokeGridDesc& GridDesc,
		FRDGTexture* VelocityTexture,
		int32 SliceAxis,
		int32 SliceIndex,
		bool bUseFalseColor,
		FRHITexture* OutputTextureRHI) const;

	void DispatchDensitySlice(
		const FSmokeGridDesc& GridDesc,
		uint64 FrameIndex,
		int32 SliceAxis,
		int32 SliceIndex,
		bool bUseFalseColor,
		UTextureRenderTarget2D* OutputRenderTarget) const;
};
