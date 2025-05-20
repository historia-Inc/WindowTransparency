// WindowTransparencyHelper.h

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Math/Vector2D.h"
#include "Tickable.h"
#include "Engine/EngineTypes.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <dwmapi.h>
#include <WinUser.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

#include "WindowTransparencyHelper.generated.h"

// 当たり判定の種類
UENUM(BlueprintType)
enum class EWindowHitTestType : uint8
{
    None            UMETA(DisplayName = "None"),
    GameRaycast     UMETA(DisplayName = "Game Raycast")
};

UCLASS()
class WINDOWTRANSPARENCY_API UWindowTransparencyHelper : public UObject, public FTickableGameObject
{
    GENERATED_BODY()

public:
    UWindowTransparencyHelper();
    ~UWindowTransparencyHelper();

    bool Initialize();
    void SetDWMTransparency(bool bEnable);
    void EnableBorderless(bool bEnable);
    void EnableClickThrough(bool bEnable);
    void SetWindowTopmost(bool bTopmost);
    FVector2D GetMousePositionInWindow(bool& bSuccess);
    void RestoreDefaultWindowSettings();
    bool IsInitialized() const { return bIsInitialized; }

#if PLATFORM_WINDOWS
    HWND GetGameHWnd() const;
#endif

    // --- Hit Test関連の公開メソッド ---
    void SetHitTestEnabled(bool bEnable);
    void SetHitTestType(EWindowHitTestType NewType);
    void SetGameRaycastTraceChannel(ECollisionChannel NewChannel);

    UFUNCTION(BlueprintPure, Category = "Window Transparency|HitTest")
    bool IsMouseConsideredOverOpaqueArea() const { return bIsMouseOverOpaqueAreaLogic; }

    virtual void Tick(float DeltaTime) override;
    virtual ETickableTickType GetTickableTickType() const override { return ETickableTickType::Always; }
    virtual TStatId GetStatId() const override;
    virtual bool IsTickableWhenPaused() const override { return true; }
    virtual bool IsTickableInEditor() const override { return false; }

private:
    void ApplyDWMAlphaTransparency(bool bEnable);
    void StoreOriginalWindowStyles();
    void ReInitializeIfNeeded();
    bool bIsInitialized;

    bool bIsBorderlessActive;
    bool bIsClickThroughStateOS;
    bool bIsTopmostActive;
    bool bIsDWMTransparentActive;

#if PLATFORM_WINDOWS
    HWND GameHWnd;
    LONG_PTR OriginalWindowStyle;
    LONG_PTR OriginalExWindowStyle;
    bool bOriginalStylesStored;
#endif

    bool bHitTestingGloballyEnabled;
    EWindowHitTestType CurrentHitTestTypeLogic;
    ECollisionChannel GameRaycastTraceChannelLogic;
    bool bIsMouseOverOpaqueAreaLogic;

    bool bCanHelperTick;

    void UpdateHitDetectionLogic(float DeltaTime);
    bool PerformGameRaycastUnderMouse(FVector2D MousePosInWindow);
};