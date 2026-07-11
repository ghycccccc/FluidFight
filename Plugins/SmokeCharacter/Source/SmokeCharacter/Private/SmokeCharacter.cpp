// Copyright Epic Games, Inc. All Rights Reserved.

#include "SmokeCharacter.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ShaderCore.h"

DEFINE_LOG_CATEGORY(LogSmokeCharacter);

class FSmokeCharacterModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("SmokeCharacter"));
		if (Plugin.IsValid())
		{
			const FString ShaderDirectory = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders"));
			AddShaderSourceDirectoryMapping(TEXT("/Plugin/SmokeCharacter"), ShaderDirectory);
			UE_LOG(LogSmokeCharacter, Log, TEXT("SmokeCharacter shader directory mapped to %s."), *ShaderDirectory);
		}

		UE_LOG(LogSmokeCharacter, Log, TEXT("SmokeCharacter module started."));
	}

	virtual void ShutdownModule() override
	{
		UE_LOG(LogSmokeCharacter, Log, TEXT("SmokeCharacter module shut down."));
	}
};

IMPLEMENT_MODULE(FSmokeCharacterModule, SmokeCharacter)
