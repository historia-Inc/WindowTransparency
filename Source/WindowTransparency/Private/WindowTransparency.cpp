#include "WindowTransparency.h"
#include "WindowTransparencyHelper.h"
#include "CoreGlobals.h"
#include "Engine/Engine.h"
#include "UObject/UObjectBase.h" // UObjectInitialized() — guards shutdown against the exit-time UObject teardown

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
            // Root the helper so it isn't garbage-collected before ShutdownModule runs (mirrors the
            // AddToRoot() already done in GetHelper()'s lazy-init path).
            HelperInstance->AddToRoot();
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

    // On engine exit the UObject system is torn down (AppPreExit -> StaticExit purges ALL objects, even
    // rooted ones, then sets UObjectInitialized() = false) BEFORE modules are unloaded
    // (LaunchEngineLoop.cpp calls AppPreExit() well before FModuleManager::UnloadModulesAtShutdown()).
    // So by the time this runs on exit, HelperInstance points at a freed object; calling IsValid() on it
    // reads a negative internal index and asserts (FUObjectArray::IndexToObject "Index >= 0"; fatal on
    // UE 5.x). Guarding on UObjectInitialized() skips that dangling access on exit (it's false once
    // StaticExit() has run), while still restoring the window on a live module unload / hot reload.
    if (UObjectInitialized() && IsValid(HelperInstance))
    {
#if PLATFORM_WINDOWS
        UE_LOG(LogWindowTransparency, Log, TEXT("HelperInstance is valid, attempting to restore settings."));
        HelperInstance->RestoreDefaultWindowSettings();
        HelperInstance->RemoveFromRoot(); // balance the AddToRoot() from StartupModule
#endif
    }
    else
    {
        UE_LOG(LogWindowTransparency, Log, TEXT("HelperInstance not valid / object system already shut down at ShutdownModule."));
    }

    HelperInstance = nullptr;
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