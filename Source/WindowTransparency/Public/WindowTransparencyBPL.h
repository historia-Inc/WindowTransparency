// WindowTransparencyBPL.h

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Math/Vector2D.h"
#include "Engine/EngineTypes.h" // For ECollisionChannel
#include "WindowTransparencyHelper.h" // For EWindowHitTestType
#include "WindowTransparencyBPL.generated.h"

UCLASS()
class WINDOWTRANSPARENCY_API UWindowTransparencyBPL : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /**
     * Initializes the Window Transparency system. Recommended to call early, e.g., on BeginPlay.
     * Though most functions attempt to auto-initialize, calling this explicitly ensures readiness.
     * @return True if initialization was successful (or already initialized), false otherwise.
     */
    UFUNCTION(BlueprintCallable, Category = "Window Transparency", meta = (DisplayName = "Initialize Window Transparency System"))
    static bool InitializeWindowTransparency();

    /**
     * Configures multiple window properties at once: DWM transparency, borderless, click-through, and topmost.
     * @param bEnableDWMTransparency True to enable DWM alpha transparency.
     * @param bEnableBorderless True for borderless window.
     * @param bEnableClickThrough True to enable click-through.
     * @param bSetTopmost True to make the window topmost.
     */
    UFUNCTION(BlueprintCallable, Category = "Window Transparency", meta = (DisplayName = "Configure Advanced Window Features"))
    static void ConfigureAdvancedWindowFeatures(bool bEnableDWMTransparency, bool bEnableBorderless, bool bEnableClickThrough, bool bSetTopmost);

    /**
     * Gets the current mouse position relative to the top-left corner of the game window.
     * This works even when click-through is enabled.
     * @param bSuccess Outputs true if the position was successfully retrieved, false otherwise.
     * @return Mouse position as FVector2D (X, Y). Returns (0,0) on failure.
     */
    UFUNCTION(BlueprintCallable, Category = "Window Transparency", meta = (DisplayName = "Get Mouse Position in Window"))
    static FVector2D GetMousePositionInGameWindow(bool& bSuccess);

    /**
     * Restores any modified window settings (border, transparency, click-through, topmost) back to their original state
     * before this plugin made changes. Call this if you want to revert all effects.
     * This is also called automatically when the plugin module shuts down.
     */
    UFUNCTION(BlueprintCallable, Category = "Window Transparency", meta = (DisplayName = "Restore Default Window Settings"))
    static void RestoreDefaultWindow();

    /** Enables or disables the automatic hit-testing feature for click-through control. */
    UFUNCTION(BlueprintCallable, Category = "Window Transparency|HitTest", meta = (DisplayName = "Set Hit-Test Enabled"))
    static void SetHitTestEnabled(bool bEnable);

    /** Sets the method used for automatic hit-testing. */
    UFUNCTION(BlueprintCallable, Category = "Window Transparency|HitTest", meta = (DisplayName = "Set Hit-Test Type"))
    static void SetHitTestType(EWindowHitTestType HitType);

    /** Sets the trace channel to be used by GameRaycast hit-testing. Only effective if Hit-Test Type is GameRaycast. */
    UFUNCTION(BlueprintCallable, Category = "Window Transparency|HitTest", meta = (DisplayName = "Set Game Raycast Trace Channel"))
    static void SetGameRaycastTraceChannel(ECollisionChannel TraceChannel);

    /** Gets the last determined state of whether the mouse is over an 'opaque' area based on hit testing. */
    UFUNCTION(BlueprintPure, Category = "Window Transparency|HitTest", meta = (DisplayName = "Is Mouse Over Opaque Area (HitTest)"))
    static bool GetIsMouseOverOpaqueArea(bool& bIsOverOpaqueArea);

    /**
     * Gets information about other visible, non-minimized windows on the system.
     * @param bSuccess Outputs true if the information was successfully retrieved, false otherwise.
     * @return An array of FOtherWindowInfo structures containing title and geometry for each window.
     */
    UFUNCTION(BlueprintCallable, Category = "Window Transparency|External Windows", meta = (DisplayName = "Get Other Windows Info"))
    static TArray<FOtherWindowInfo> GetOtherWindowsInfo(bool& bSuccess);
};