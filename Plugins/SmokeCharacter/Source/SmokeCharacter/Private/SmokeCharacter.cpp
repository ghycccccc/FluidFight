// Copyright Epic Games, Inc. All Rights Reserved.

#include "SmokeCharacter.h"

#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogSmokeCharacter);

class FSmokeCharacterModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		UE_LOG(LogSmokeCharacter, Log, TEXT("SmokeCharacter module started."));
	}

	virtual void ShutdownModule() override
	{
		UE_LOG(LogSmokeCharacter, Log, TEXT("SmokeCharacter module shut down."));
	}
};

IMPLEMENT_MODULE(FSmokeCharacterModule, SmokeCharacter)
