#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class UWindowTransparencyHelper;

class WINDOWTRANSPARENCY_API FWindowTransparencyModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

    static UWindowTransparencyHelper* GetHelper();

private:
    UWindowTransparencyHelper* HelperInstance;
};