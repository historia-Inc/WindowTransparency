#include "WindowTransparency.h"
#include "WindowTransparencyHelper.h"
#include "CoreGlobals.h"
#include "Engine/Engine.h"

DEFINE_LOG_CATEGORY_STATIC(LogWindowTransparency, Log, All);

#define LOCTEXT_NAMESPACE "FWindowTransparencyModule"

void FWindowTransparencyModule::StartupModule()
{
    UE_LOG(LogWindowTransparency, Log, TEXT("WindowTransparency module has started."));
    HelperInstance = nullptr;

#if PLATFORM_WINDOWS
    if (!IsRunningCommandlet() && !IsRunningDedicatedServer() && GEngine)
    {
        HelperInstance = NewObject<UWindowTransparencyHelper>();
        if (HelperInstance)
        {
            if (HelperInstance->Initialize())
            {
                UE_LOG(LogWindowTransparency, Log, TEXT("UWindowTransparencyHelper instance created and initialized."));
            }
            else
            {
                UE_LOG(LogWindowTransparency, Warning, TEXT("UWindowTransparencyHelper instance created but failed to initialize HWND."));
            }
        }
        else
        {
            UE_LOG(LogWindowTransparency, Error, TEXT("Failed to create UWindowTransparencyHelper instance."));
        }
    }
#else
    UE_LOG(LogWindowTransparency, Warning, TEXT("WindowTransparency module: Platform is not Windows. Functionality will be disabled."));
#endif
}

void FWindowTransparencyModule::ShutdownModule()
{
    UE_LOG(LogWindowTransparency, Log, TEXT("WindowTransparency module is shutting down."));

    if (IsValid(HelperInstance))
    {
#if PLATFORM_WINDOWS
        UE_LOG(LogWindowTransparency, Log, TEXT("HelperInstance is valid, attempting to restore settings."));
        HelperInstance->RestoreDefaultWindowSettings();
#endif
    }
    else
    {
        UE_LOG(LogWindowTransparency, Log, TEXT("HelperInstance was not valid or not set at ShutdownModule."));
    }

    UE_LOG(LogWindowTransparency, Log, TEXT("WindowTransparency module shutdown sequence finished."));
}

UWindowTransparencyHelper* FWindowTransparencyModule::GetHelper()
{
    FWindowTransparencyModule* Module = FModuleManager::GetModulePtr<FWindowTransparencyModule>("WindowTransparency");
    if (Module)
    {
#if PLATFORM_WINDOWS
        if (!Module->HelperInstance && !IsRunningCommandlet() && !IsRunningDedicatedServer() && GEngine)
        {
            UE_LOG(LogWindowTransparency, Log, TEXT("FWindowTransparencyModule::GetHelper(): HelperInstance is null, attempting lazy initialization."));
            Module->HelperInstance = NewObject<UWindowTransparencyHelper>();
            if (Module->HelperInstance)
            {
                Module->HelperInstance->AddToRoot();
                if (Module->HelperInstance->Initialize())
                {
                    UE_LOG(LogWindowTransparency, Log, TEXT("UWindowTransparencyHelper lazily initialized successfully."));
                }
                else
                {
                    UE_LOG(LogWindowTransparency, Warning, TEXT("UWindowTransparencyHelper lazy initialization: instance created but failed to initialize HWND."));
                }
            }
            else
            {
                UE_LOG(LogWindowTransparency, Error, TEXT("FWindowTransparencyModule::GetHelper(): Failed to create UWindowTransparencyHelper instance during lazy init."));
            }
        }
        return Module->HelperInstance;
#else
        // On non-Windows, HelperInstance should be nullptr from StartupModule.
        // If it's somehow not, log an error, but still return nullptr.
        if (Module->HelperInstance) {
            UE_LOG(LogWindowTransparency, Error, TEXT("FWindowTransparencyModule::GetHelper(): HelperInstance is unexpectedly non-null on a non-Windows platform. Correcting to nullptr."));
            // Potentially clean up Module->HelperInstance if it's in a bad state.
            // For now, just ensure nullptr is returned.
            return nullptr;
        }
        return nullptr; // Expected path for non-Windows
#endif
    }

#if PLATFORM_WINDOWS
    UE_LOG(LogWindowTransparency, Warning, TEXT("FWindowTransparencyModule::GetHelper(): Module 'WindowTransparency' not loaded. Cannot provide HelperInstance."));
#else
    // UE_LOG(LogWindowTransparency, Log, TEXT("FWindowTransparencyModule::GetHelper(): Module not loaded or platform not supported. HelperInstance is null."));
#endif
    return nullptr;
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FWindowTransparencyModule, WindowTransparency)