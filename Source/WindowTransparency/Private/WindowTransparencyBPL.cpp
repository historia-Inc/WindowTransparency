// WindowTransparencyBPL.cpp

#include "WindowTransparencyBPL.h"
#include "WindowTransparency.h" // For FWindowTransparencyModule
#include "WindowTransparencyHelper.h"

DEFINE_LOG_CATEGORY_STATIC(LogWindowBPL, Log, All);

bool UWindowTransparencyBPL::InitializeWindowTransparency()
{
#if PLATFORM_WINDOWS
    UWindowTransparencyHelper* Helper = FWindowTransparencyModule::GetHelper();
    if (Helper)
    {
        if (Helper->IsInitialized())
        {
            return true;
        }
        return Helper->Initialize();
    }
    UE_LOG(LogWindowBPL, Warning, TEXT("InitializeWindowTransparency: Could not get WindowTransparencyHelper instance. System may not be available."));
    return false;
#else
    UE_LOG(LogWindowBPL, Log, TEXT("InitializeWindowTransparency: Window Transparency is not supported on this platform."));
    return false;
#endif
}

void UWindowTransparencyBPL::ConfigureAdvancedWindowFeatures(bool bEnableDWMTransparency, bool bEnableBorderless, bool bEnableClickThrough, bool bSetTopmost)
{
#if PLATFORM_WINDOWS
    UWindowTransparencyHelper* Helper = FWindowTransparencyModule::GetHelper();
    if (Helper)
    {
        Helper->EnableBorderless(bEnableBorderless);
        Helper->SetDWMTransparency(bEnableDWMTransparency);
        Helper->EnableClickThrough(bEnableClickThrough);
        Helper->SetWindowTopmost(bSetTopmost);
    }
    else
    {
        UE_LOG(LogWindowBPL, Warning, TEXT("ConfigureAdvancedWindowFeatures: Could not get WindowTransparencyHelper instance. System may not be available."));
    }
#else
    UE_LOG(LogWindowBPL, Log, TEXT("ConfigureAdvancedWindowFeatures: Window Transparency features are not supported on this platform."));
#endif
}

FVector2D UWindowTransparencyBPL::GetMousePositionInGameWindow(bool& bSuccess)
{
    bSuccess = false;
#if PLATFORM_WINDOWS
    UWindowTransparencyHelper* Helper = FWindowTransparencyModule::GetHelper();
    if (Helper)
    {
        return Helper->GetMousePositionInWindow(bSuccess);
    }
    else
    {
        UE_LOG(LogWindowBPL, Warning, TEXT("GetMousePositionInGameWindow: Could not get WindowTransparencyHelper instance. System may not be available."));
    }
#else
    UE_LOG(LogWindowBPL, Log, TEXT("GetMousePositionInGameWindow: Window Transparency features are not supported on this platform."));
#endif
    return FVector2D::ZeroVector;
}

void UWindowTransparencyBPL::RestoreDefaultWindow()
{
#if PLATFORM_WINDOWS
    UWindowTransparencyHelper* Helper = FWindowTransparencyModule::GetHelper();
    if (Helper)
    {
        Helper->RestoreDefaultWindowSettings();
    }
    else
    {
        UE_LOG(LogWindowBPL, Warning, TEXT("RestoreDefaultWindow: Could not get WindowTransparencyHelper instance. System may not be available."));
    }
#else
    UE_LOG(LogWindowBPL, Log, TEXT("RestoreDefaultWindow: Window Transparency features are not supported on this platform."));
#endif
}

void UWindowTransparencyBPL::SetHitTestEnabled(bool bEnable)
{
#if PLATFORM_WINDOWS
    UWindowTransparencyHelper* Helper = FWindowTransparencyModule::GetHelper();
    if (Helper)
    {
        Helper->SetHitTestEnabled(bEnable);
    }
#else
    UE_LOG(LogWindowBPL, Log, TEXT("SetHitTestEnabled: Not supported on this platform."));
#endif
}

void UWindowTransparencyBPL::SetHitTestType(EWindowHitTestType HitType)
{
#if PLATFORM_WINDOWS
    UWindowTransparencyHelper* Helper = FWindowTransparencyModule::GetHelper();
    if (Helper)
    {
        Helper->SetHitTestType(HitType);
    }
#else
    UE_LOG(LogWindowBPL, Log, TEXT("SetHitTestType: Not supported on this platform."));
#endif
}

void UWindowTransparencyBPL::SetGameRaycastTraceChannel(ECollisionChannel TraceChannel)
{
#if PLATFORM_WINDOWS
    UWindowTransparencyHelper* Helper = FWindowTransparencyModule::GetHelper();
    if (Helper)
    {
        Helper->SetGameRaycastTraceChannel(TraceChannel);
    }
#else
    UE_LOG(LogWindowBPL, Log, TEXT("SetGameRaycastTraceChannel: Not supported on this platform."));
#endif
}

bool UWindowTransparencyBPL::GetIsMouseOverOpaqueArea(bool& bIsOverOpaqueArea)
{
    bIsOverOpaqueArea = true; // Default to true (interactive) if helper unavailable
#if PLATFORM_WINDOWS
    UWindowTransparencyHelper* Helper = FWindowTransparencyModule::GetHelper();
    if (Helper)
    {
        bIsOverOpaqueArea = Helper->IsMouseConsideredOverOpaqueArea();
        return true; // Success
    }
    return false; // Helper not available
#else
    UE_LOG(LogWindowBPL, Log, TEXT("GetIsMouseOverOpaqueArea: Not supported on this platform."));
    return false;
#endif
}

TArray<FOtherWindowInfo> UWindowTransparencyBPL::GetOtherWindowsInfo(bool& bSuccess)
{
    bSuccess = false;
#if PLATFORM_WINDOWS
    UWindowTransparencyHelper* Helper = FWindowTransparencyModule::GetHelper();
    if (Helper)
    {
        // Ensure helper is initialized as GetOtherWindowsInformation needs the game's HWND
        if (!Helper->IsInitialized())
        {
            if (!Helper->Initialize()) // Attempt to initialize
            {
                UE_LOG(LogWindowBPL, Warning, TEXT("GetOtherWindowsInfo: Helper failed to initialize. Cannot get other windows info."));
                return TArray<FOtherWindowInfo>();
            }
        }
        return Helper->GetOtherWindowsInformation(bSuccess);
    }
    else
    {
        UE_LOG(LogWindowBPL, Warning, TEXT("GetOtherWindowsInfo: Could not get WindowTransparencyHelper instance."));
    }
#else
    UE_LOG(LogWindowBPL, Log, TEXT("GetOtherWindowsInfo: Not supported on this platform."));
#endif
    return TArray<FOtherWindowInfo>();
}

void UWindowTransparencyBPL::SetWindowAsDesktopBackground(bool bEnable)
{
#if PLATFORM_WINDOWS
    UWindowTransparencyHelper* Helper = FWindowTransparencyModule::GetHelper();
    if (Helper)
    {
        Helper->SetAsDesktopBackground(bEnable);
    }
    else
    {
        UE_LOG(LogWindowBPL, Warning, TEXT("SetWindowAsDesktopBackground: Could not get WindowTransparencyHelper instance. System may not be available."));
    }
#else
    UE_LOG(LogWindowBPL, Log, TEXT("SetWindowAsDesktopBackground: Window Transparency features are not supported on this platform."));
#endif
}