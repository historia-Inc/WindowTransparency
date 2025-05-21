// WindowTransparencyHelper.cpp

#include "WindowTransparencyHelper.h"
#include "Engine/GameEngine.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Components/PrimitiveComponent.h"
#include "CollisionQueryParams.h"
#include "Engine/LocalPlayer.h"
#include "Layout/WidgetPath.h"
#include "GameFramework/Actor.h"


DEFINE_LOG_CATEGORY_STATIC(LogWindowHelper, Log, All);

#if PLATFORM_WINDOWS
#pragma comment(lib, "Dwmapi.lib") 
#endif

static APlayerController* GetFirstLocalPlayerController(const UObject* WorldContextObject)
{
    if (GEngine && GEngine->GameViewport)
    {
        UWorld* World = GEngine->GameViewport->GetWorld();
        if (World)
        {
            return World->GetFirstPlayerController();
        }
    }
    if (WorldContextObject)
    {
        UWorld* World = WorldContextObject->GetWorld();
        if (World) return World->GetFirstPlayerController();
    }
    return nullptr;
}

UWindowTransparencyHelper::UWindowTransparencyHelper()
    : bIsInitialized(false)
    , bIsBorderlessActive(false)
    , bIsClickThroughStateOS(false)
    , bIsTopmostActive(false)
    , bIsDWMTransparentActive(false)
#if PLATFORM_WINDOWS
    , GameHWnd(nullptr)
    , OriginalWindowStyle(0)
    , OriginalExWindowStyle(0)
    , bOriginalStylesStored(false)
#endif
    , bHitTestingGloballyEnabled(false)
    , CurrentHitTestTypeLogic(EWindowHitTestType::None)
    , GameRaycastTraceChannelLogic(ECollisionChannel::ECC_Visibility) // Default trace channel
    , bIsMouseOverOpaqueAreaLogic(true)
    , bCanHelperTick(false)
{
}

UWindowTransparencyHelper::~UWindowTransparencyHelper()
{
}

#if PLATFORM_WINDOWS
HWND UWindowTransparencyHelper::GetGameHWnd() const
{
    if (GEngine && GEngine->GameViewport && GEngine->GameViewport->GetWindow().IsValid())
    {
        TSharedPtr<SWindow> GameSWindow = GEngine->GameViewport->GetWindow();
        if (GameSWindow.IsValid() && GameSWindow->GetNativeWindow().IsValid())
        {
            void* Handle = GameSWindow->GetNativeWindow()->GetOSWindowHandle();
            return static_cast<HWND>(Handle);
        }
    }
    if (FSlateApplication::IsInitialized())
    {
        TSharedPtr<SWindow> ActiveWindow = FSlateApplication::Get().GetActiveTopLevelWindow();
        if (ActiveWindow.IsValid() && ActiveWindow->GetNativeWindow().IsValid())
        {
            void* Handle = ActiveWindow->GetNativeWindow()->GetOSWindowHandle();
            return static_cast<HWND>(Handle);
        }
    }
    UE_LOG(LogWindowHelper, Warning, TEXT("GetGameHWnd: Could not retrieve HWND."));
    return nullptr;
}

BOOL CALLBACK UWindowTransparencyHelper::EnumWindowsProc(HWND hwnd, LPARAM lParam)
{
    EnumWindowsCallbackData* Data = reinterpret_cast<EnumWindowsCallbackData*>(lParam);
    if (!Data || !Data->WindowsList)
    {
        return true; // Continue enumeration
    }

    // 自身のウィンドウはスキップ
    if (hwnd == Data->SelfHWnd)
    {
        return true; // Continue enumeration
    }

    // 非表示ウィンドウはスキップ
    if (!IsWindowVisible(hwnd))
    {
        return true;
    }

    // タイトルがないウィンドウはスキップ（多くのバックグラウンドウィンドウが該当）
    int TitleLength = GetWindowTextLength(hwnd);
    if (TitleLength == 0)
    {
        return true;
    }

    // 最小化されているウィンドウはスキップ
    if (IsIconic(hwnd))
    {
        return true;
    }

    // クラス名を取得してフィルタリング ("Progman" と "WorkerW" を除外)
    WCHAR ClassName[256];
    if (GetClassNameW(hwnd, ClassName, sizeof(ClassName) / sizeof(WCHAR)) > 0)
    {
        if (wcscmp(ClassName, L"Progman") == 0 || wcscmp(ClassName, L"WorkerW") == 0)
        {
            // UE_LOG(LogWindowHelper, Verbose, TEXT("EnumWindowsProc: Skipping Program Manager or WorkerW window (Class: %s, HWND: %p)"), FString(ClassName), hwnd);
            return true;
        }
    }

    // DWMによってクローク(隠蔽)されているウィンドウを除外 (UWPアプリのバックグラウンド状態などに対応)
    // このチェックはWindows Vista以降で有効
    BOOL bIsCloaked = false;
    HRESULT hr = ::DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &bIsCloaked, sizeof(bIsCloaked));

    if (SUCCEEDED(hr) && bIsCloaked)
    {
        // TCHAR title_debug[256]; GetWindowText(hwnd, title_debug, 255); // For debugging
        // UE_LOG(LogWindowHelper, Verbose, TEXT("EnumWindowsProc: Skipping cloaked window: %s (HWND: %p)"), FString(title_debug), hwnd);
        return true; // クロークされているウィンドウはスキップ
    }


    FOtherWindowInfo Info;
    TCHAR WindowTitleRaw[256]; // TCHAR for wider compatibility with GetWindowText
    GetWindowText(hwnd, WindowTitleRaw, sizeof(WindowTitleRaw) / sizeof(TCHAR));
    Info.WindowTitle = FString(WindowTitleRaw);

    RECT Rect;
    if (GetWindowRect(hwnd, &Rect))
    {
        Info.PosX = Rect.left;
        Info.PosY = Rect.top;
        Info.Width = Rect.right - Rect.left;
        Info.Height = Rect.bottom - Rect.top;

        // 幅または高さが0以下のウィンドウは実質的に表示されていないか特殊なものなのでスキップ
        if (Info.Width <= 0 || Info.Height <= 0)
        {
            return true;
        }
        Data->WindowsList->Add(Info);
    }
    return true; // Continue enumeration
}

TArray<FOtherWindowInfo> UWindowTransparencyHelper::GetOtherWindowsInformation(bool& bSuccess)
{
    bSuccess = false;
    TArray<FOtherWindowInfo> WindowsList;

    ReInitializeIfNeeded(); // Ensure GameHWnd is valid
    if (!GameHWnd)
    {
        UE_LOG(LogWindowHelper, Warning, TEXT("GetOtherWindowsInformation: GameHWnd is not valid. Cannot determine self."));
        return WindowsList;
    }

    EnumWindowsCallbackData CallbackData;
    CallbackData.WindowsList = &WindowsList;
    CallbackData.SelfHWnd = GameHWnd; // 自身のウィンドウハンドルを渡す

    if (::EnumWindows(UWindowTransparencyHelper::EnumWindowsProc, reinterpret_cast<LPARAM>(&CallbackData)))
    {
        bSuccess = true;
    }
    else
    {
        DWORD ErrorCode = GetLastError();
        UE_LOG(LogWindowHelper, Error, TEXT("GetOtherWindowsInformation: EnumWindows failed. Error code: %u"), ErrorCode);
    }

    return WindowsList;
}

#endif

void UWindowTransparencyHelper::StoreOriginalWindowStyles()
{
#if PLATFORM_WINDOWS
    if (GameHWnd && !bOriginalStylesStored)
    {
        OriginalWindowStyle = GetWindowLongPtr(GameHWnd, GWL_STYLE);
        OriginalExWindowStyle = GetWindowLongPtr(GameHWnd, GWL_EXSTYLE);
        bOriginalStylesStored = true;
        UE_LOG(LogWindowHelper, Log, TEXT("Stored original window styles. Style: 0x%p, ExStyle: 0x%p"), (void*)OriginalWindowStyle, (void*)OriginalExWindowStyle);
    }
#endif
}

void UWindowTransparencyHelper::ReInitializeIfNeeded()
{
#if PLATFORM_WINDOWS
    if (!GameHWnd || (GameHWnd && !IsWindow(GameHWnd)))
    {
        UE_LOG(LogWindowHelper, Log, TEXT("ReInitializeIfNeeded: GameHWnd %p is invalid or null. Attempting to re-initialize."), GameHWnd);
        GameHWnd = nullptr;
        bIsInitialized = false;
        bOriginalStylesStored = false;
        bCanHelperTick = false;
        Initialize();
    }
#endif
}

bool UWindowTransparencyHelper::Initialize()
{
#if PLATFORM_WINDOWS
    if (bIsInitialized && GameHWnd && IsWindow(GameHWnd))
    {
        return true;
    }

    GameHWnd = GetGameHWnd();
    if (GameHWnd)
    {
        bIsInitialized = true;
        StoreOriginalWindowStyles();
        LONG_PTR CurrentExStyle = GetWindowLongPtr(GameHWnd, GWL_EXSTYLE);
        bIsClickThroughStateOS = (CurrentExStyle & WS_EX_TRANSPARENT) != 0;
        bCanHelperTick = true;
        UE_LOG(LogWindowHelper, Log, TEXT("WindowTransparencyHelper Initialized with HWND: %p. Initial OS ClickThrough: %s"), GameHWnd, bIsClickThroughStateOS ? TEXT("true") : TEXT("false"));
        return true;
    }
    else
    {
        bIsInitialized = false;
        bCanHelperTick = false;
        UE_LOG(LogWindowHelper, Warning, TEXT("WindowTransparencyHelper: Could not get game HWND during Initialize."));
        return false;
    }
#else
    bIsInitialized = false;
    bCanHelperTick = false;
    UE_LOG(LogWindowHelper, Log, TEXT("WindowTransparencyHelper: Platform is not Windows. Initialize is a no-op."));
    return false;
#endif
}

void UWindowTransparencyHelper::SetDWMTransparency(bool bEnable)
{
#if PLATFORM_WINDOWS
    ReInitializeIfNeeded();
    if (!IsInitialized() || !GameHWnd)
    {
        UE_LOG(LogWindowHelper, Warning, TEXT("SetDWMTransparency: Not initialized or HWND is null."));
        return;
    }
    if (bEnable == bIsDWMTransparentActive) return;

    ApplyDWMAlphaTransparency(bEnable);
    bIsDWMTransparentActive = bEnable;
    InvalidateRect(GameHWnd, NULL, true);
    UpdateWindow(GameHWnd);
    UE_LOG(LogWindowHelper, Log, TEXT("DWM Transparency set to: %s"), bEnable ? TEXT("true") : TEXT("false"));
#else
    UE_LOG(LogWindowHelper, Log, TEXT("SetDWMTransparency: Not supported on this platform."));
#endif
}

void UWindowTransparencyHelper::ApplyDWMAlphaTransparency(bool bEnable)
{
#if PLATFORM_WINDOWS
    if (!GameHWnd) return;
    MARGINS margins = bEnable ? MARGINS{ -1 } : MARGINS{ 0, 0, 0, 0 };
    HRESULT hr = DwmExtendFrameIntoClientArea(GameHWnd, &margins);
    if (!SUCCEEDED(hr))
    {
        UE_LOG(LogWindowHelper, Error, TEXT("DwmExtendFrameIntoClientArea %s failed with HRESULT: 0x%08lX"), bEnable ? TEXT("enable") : TEXT("disable"), hr);
    }
#endif
}

void UWindowTransparencyHelper::EnableBorderless(bool bEnable)
{
#if PLATFORM_WINDOWS
    ReInitializeIfNeeded();
    if (!IsInitialized() || !GameHWnd || !bOriginalStylesStored)
    {
        UE_LOG(LogWindowHelper, Warning, TEXT("EnableBorderless: Not initialized, HWND is null, or original styles not stored."));
        return;
    }
    LONG_PTR CurrentStyle = GetWindowLongPtr(GameHWnd, GWL_STYLE);
    bool bIsCurrentlyBorderless = !(CurrentStyle & WS_CAPTION) && !(CurrentStyle & WS_THICKFRAME);
    if (bEnable == bIsBorderlessActive && bEnable == bIsCurrentlyBorderless) return;

    LONG_PTR NewStyle = bEnable ? ((OriginalWindowStyle & ~WS_OVERLAPPEDWINDOW) | WS_POPUP) : OriginalWindowStyle;
    SetWindowLongPtr(GameHWnd, GWL_STYLE, NewStyle);
    bIsBorderlessActive = bEnable;
    SetWindowPos(GameHWnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_NOACTIVATE);
    InvalidateRect(GameHWnd, NULL, true);
    UpdateWindow(GameHWnd);
    UE_LOG(LogWindowHelper, Log, TEXT("Borderless mode set to: %s"), bEnable ? TEXT("true") : TEXT("false"));
#else
    UE_LOG(LogWindowHelper, Log, TEXT("EnableBorderless: Not supported on this platform."));
#endif
}

void UWindowTransparencyHelper::EnableClickThrough(bool bEnable)
{
#if PLATFORM_WINDOWS
    ReInitializeIfNeeded();
    if (!IsInitialized() || !GameHWnd)
    {
        UE_LOG(LogWindowHelper, Warning, TEXT("EnableClickThrough: Not initialized or HWND is null. Cannot set OS click-through state."));
        bIsClickThroughStateOS = bEnable;
        return;
    }

    LONG_PTR CurrentExStyle = GetWindowLongPtr(GameHWnd, GWL_EXSTYLE);
    bool bIsCurrentlyClickThroughOSLevel = (CurrentExStyle & WS_EX_TRANSPARENT) != 0;

    if (bIsClickThroughStateOS != bIsCurrentlyClickThroughOSLevel && bEnable != bIsCurrentlyClickThroughOSLevel) {
        UE_LOG(LogWindowHelper, Warning, TEXT("EnableClickThrough: Internal state bIsClickThroughStateOS (%s) differs from actual OS state (%s) before attempting to set to %s."),
            bIsClickThroughStateOS ? TEXT("true") : TEXT("false"),
            bIsCurrentlyClickThroughOSLevel ? TEXT("true") : TEXT("false"),
            bEnable ? TEXT("true") : TEXT("false"));
    }

    if (bEnable == bIsCurrentlyClickThroughOSLevel)
    {
        if (bIsClickThroughStateOS != bEnable)
        {
            UE_LOG(LogWindowHelper, Verbose, TEXT("EnableClickThrough: OS click-through already %s. Updating internal state bIsClickThroughStateOS from %s to %s."),
                bEnable ? TEXT("true") : TEXT("false"),
                bIsClickThroughStateOS ? TEXT("true") : TEXT("false"),
                bEnable ? TEXT("true") : TEXT("false"));
        }
        bIsClickThroughStateOS = bEnable;
        return;
    }

    LONG_PTR NewExStyle;
    if (bEnable)
    {
        NewExStyle = CurrentExStyle | WS_EX_LAYERED | WS_EX_TRANSPARENT;
    }
    else
    {
        if (bOriginalStylesStored)
        {
            NewExStyle = OriginalExWindowStyle & ~WS_EX_TRANSPARENT;
            if (!bIsDWMTransparentActive && !(OriginalExWindowStyle & WS_EX_LAYERED))
            {
                NewExStyle &= ~WS_EX_LAYERED;
            }
        }
        else
        {
            NewExStyle = CurrentExStyle & ~WS_EX_TRANSPARENT;
            if (!bIsDWMTransparentActive)
            {
                NewExStyle &= ~WS_EX_LAYERED;
            }
        }
    }

    if (NewExStyle != CurrentExStyle)
    {
        SetWindowLongPtr(GameHWnd, GWL_EXSTYLE, NewExStyle);
        LONG_PTR StyleAfterSet = GetWindowLongPtr(GameHWnd, GWL_EXSTYLE);
        bool bSetSuccessfully = (bEnable && (StyleAfterSet & WS_EX_TRANSPARENT)) || (!bEnable && !(StyleAfterSet & WS_EX_TRANSPARENT));

        UE_LOG(LogWindowHelper, Log, TEXT("EnableClickThrough: OS Click-Through set to %s. OldExStyle: 0x%p, Attempted NewExStyle: 0x%p, Actual StyleAfterSet: 0x%p. Success: %s"),
            bEnable ? TEXT("true") : TEXT("false"),
            (void*)CurrentExStyle,
            (void*)NewExStyle,
            (void*)StyleAfterSet,
            bSetSuccessfully ? TEXT("Yes") : TEXT("No"));

        if (bSetSuccessfully) {
            SetWindowPos(GameHWnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_NOACTIVATE);
        }
        else {
            UE_LOG(LogWindowHelper, Error, TEXT("EnableClickThrough: Failed to apply desired ExStyle change!"));
        }
    }
    else
    {
        UE_LOG(LogWindowHelper, Verbose, TEXT("EnableClickThrough: Calculated NewExStyle 0x%p is same as CurrentExStyle 0x%p. No OS call for %s. Current OS click-through: %s."),
            (void*)NewExStyle, (void*)CurrentExStyle, bEnable ? TEXT("true") : TEXT("false"), bIsCurrentlyClickThroughOSLevel ? TEXT("true") : TEXT("false"));
    }
    bIsClickThroughStateOS = bEnable;
#else
    bIsClickThroughStateOS = false;
#endif
}

void UWindowTransparencyHelper::SetWindowTopmost(bool bTopmost)
{
#if PLATFORM_WINDOWS
    ReInitializeIfNeeded();
    if (!IsInitialized() || !GameHWnd)
    {
        UE_LOG(LogWindowHelper, Warning, TEXT("SetWindowTopmost: Not initialized or HWND is null."));
        return;
    }
    LONG_PTR CurrentExStyle = GetWindowLongPtr(GameHWnd, GWL_EXSTYLE);
    bool bIsCurrentlyTopmostOS = (CurrentExStyle & WS_EX_TOPMOST) != 0;
    if (bTopmost == bIsTopmostActive && bTopmost == bIsCurrentlyTopmostOS) return;

    HWND HwndInsertAfter = bTopmost ? HWND_TOPMOST : HWND_NOTOPMOST;
    SetWindowPos(GameHWnd, HwndInsertAfter, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    bIsTopmostActive = bTopmost;
    UE_LOG(LogWindowHelper, Log, TEXT("Window topmost set to: %s"), bTopmost ? TEXT("true") : TEXT("false"));
#else
    UE_LOG(LogWindowHelper, Log, TEXT("SetWindowTopmost: Not supported on this platform."));
#endif
}

FVector2D UWindowTransparencyHelper::GetMousePositionInWindow(bool& bSuccess)
{
    bSuccess = false;
#if PLATFORM_WINDOWS
    if (!GameHWnd && bIsInitialized) {
        GameHWnd = GetGameHWnd();
    }
    if (!GameHWnd) return FVector2D::ZeroVector;

    POINT CursorPosScreen;
    if (::GetCursorPos(&CursorPosScreen))
    {
        RECT WindowRect;
        if (::GetWindowRect(GameHWnd, &WindowRect))
        {
            bSuccess = true;
            return FVector2D(static_cast<float>(CursorPosScreen.x - WindowRect.left), static_cast<float>(CursorPosScreen.y - WindowRect.top));
        }
        else
        {
            DWORD ErrorCode = GetLastError();
            UE_LOG(LogWindowHelper, Error, TEXT("GetMousePositionInWindow: GetWindowRect failed for HWND %p. Error code: %u"), GameHWnd, ErrorCode);
            if (ErrorCode == ERROR_INVALID_WINDOW_HANDLE)
            {
                GameHWnd = nullptr;
                bIsInitialized = false;
                bOriginalStylesStored = false;
                bCanHelperTick = false;
            }
        }
    }
#endif
    return FVector2D::ZeroVector;
}

void UWindowTransparencyHelper::RestoreDefaultWindowSettings()
{
#if PLATFORM_WINDOWS
    UE_LOG(LogWindowHelper, Log, TEXT("Attempting to restore default window settings..."));
    if (!GameHWnd || !IsWindow(GameHWnd))
    {
        UE_LOG(LogWindowHelper, Warning, TEXT("Cannot restore default settings: HWND is null or invalid."));
        bIsBorderlessActive = false;
        bIsClickThroughStateOS = false;
        bIsTopmostActive = false;
        bIsDWMTransparentActive = false;
        bHitTestingGloballyEnabled = false;
        return;
    }

    bHitTestingGloballyEnabled = false;
    bool bRestoredSomething = false;

    if (bOriginalStylesStored)
    {
        if (GetWindowLongPtr(GameHWnd, GWL_EXSTYLE) != OriginalExWindowStyle)
        {
            SetWindowLongPtr(GameHWnd, GWL_EXSTYLE, OriginalExWindowStyle);
            UE_LOG(LogWindowHelper, Log, TEXT("Restored OriginalExWindowStyle."));
            bRestoredSomething = true;
        }
    }
    else
    {
        if (bIsClickThroughStateOS)
        {
            LONG_PTR CurrentExStyle = GetWindowLongPtr(GameHWnd, GWL_EXSTYLE);
            LONG_PTR NewExStyle = CurrentExStyle & ~(WS_EX_TRANSPARENT | WS_EX_LAYERED);
            if (NewExStyle != CurrentExStyle)
            {
                SetWindowLongPtr(GameHWnd, GWL_EXSTYLE, NewExStyle);
                UE_LOG(LogWindowHelper, Log, TEXT("Removed WS_EX_TRANSPARENT and WS_EX_LAYERED (no original style)."));
                bRestoredSomething = true;
            }
        }
    }
    bIsClickThroughStateOS = (GetWindowLongPtr(GameHWnd, GWL_EXSTYLE) & WS_EX_TRANSPARENT) != 0;


    if (bIsDWMTransparentActive)
    {
        ApplyDWMAlphaTransparency(false);
        bIsDWMTransparentActive = false;
        bRestoredSomething = true;
        UE_LOG(LogWindowHelper, Log, TEXT("Disabled DWM Transparency."));
    }

    if (bIsBorderlessActive)
    {
        if (bOriginalStylesStored)
        {
            if (GetWindowLongPtr(GameHWnd, GWL_STYLE) != OriginalWindowStyle)
            {
                SetWindowLongPtr(GameHWnd, GWL_STYLE, OriginalWindowStyle);
                UE_LOG(LogWindowHelper, Log, TEXT("Restored OriginalWindowStyle."));
                bRestoredSomething = true;
            }
        }
        else
        {
            LONG_PTR CurrentStyle = GetWindowLongPtr(GameHWnd, GWL_STYLE);
            LONG_PTR NewStyle = CurrentStyle | WS_OVERLAPPEDWINDOW;
            if (NewStyle != CurrentStyle)
            {
                SetWindowLongPtr(GameHWnd, GWL_STYLE, NewStyle);
                UE_LOG(LogWindowHelper, Log, TEXT("Applied WS_OVERLAPPEDWINDOW (no original style)."));
                bRestoredSomething = true;
            }
        }
        bIsBorderlessActive = false;
    }

    if (bIsTopmostActive)
    {
        SetWindowPos(GameHWnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        bIsTopmostActive = false;
        bRestoredSomething = true;
        UE_LOG(LogWindowHelper, Log, TEXT("Set window to HWND_NOTOPMOST."));
    }

    if (bRestoredSomething)
    {
        SetWindowPos(GameHWnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_NOACTIVATE);
        InvalidateRect(GameHWnd, NULL, true);
        UpdateWindow(GameHWnd);
        UE_LOG(LogWindowHelper, Log, TEXT("Window settings restoration commands issued."));
    }
    else
    {
        UE_LOG(LogWindowHelper, Log, TEXT("No specific modifications by this helper were flagged for restoration, or original styles were already in place."));
    }
#else
    UE_LOG(LogWindowHelper, Log, TEXT("RestoreDefaultWindowSettings: Not supported on this platform."));
#endif
}

TStatId UWindowTransparencyHelper::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UWindowTransparencyHelper, STATGROUP_Tickables);
}

void UWindowTransparencyHelper::Tick(float DeltaTime)
{
#if PLATFORM_WINDOWS
    if (!bCanHelperTick || !bIsInitialized || !GameHWnd || !IsWindow(GameHWnd))
    {
        ReInitializeIfNeeded();
        if (!bCanHelperTick || !bIsInitialized || !GameHWnd || !IsWindow(GameHWnd))
        {
            return;
        }
    }
#else
    if (!bCanHelperTick || !bIsInitialized)
    {
        return;
    }
#endif
    if (!bHitTestingGloballyEnabled || CurrentHitTestTypeLogic == EWindowHitTestType::None)
    {
        if (!bIsMouseOverOpaqueAreaLogic)
        {
#if PLATFORM_WINDOWS
            UE_LOG(LogWindowHelper, Verbose, TEXT("Tick: Hit testing disabled/None. Setting bIsMouseOverOpaqueAreaLogic to true. OS click-through state (%s) is not changed by Tick."), bIsClickThroughStateOS ? TEXT("true") : TEXT("false"));
#else
            UE_LOG(LogWindowHelper, Verbose, TEXT("Tick: Hit testing disabled/None. Setting bIsMouseOverOpaqueAreaLogic to true."));
#endif
        }
        bIsMouseOverOpaqueAreaLogic = true;
        return;
    }

    UpdateHitDetectionLogic(DeltaTime);
#if PLATFORM_WINDOWS
    bool bShouldBeClickThroughLogically = bIsDWMTransparentActive && !bIsMouseOverOpaqueAreaLogic;

    if (bIsClickThroughStateOS != bShouldBeClickThroughLogically)
    {
        UE_LOG(LogWindowHelper, Verbose, TEXT("Tick: Logic dictates click-through: %s. OS state is: %s. Updating OS state."),
            bShouldBeClickThroughLogically ? TEXT("true") : TEXT("false"),
            bIsClickThroughStateOS ? TEXT("true") : TEXT("false"));
        EnableClickThrough(bShouldBeClickThroughLogically);
    }
#endif
}


void UWindowTransparencyHelper::SetHitTestEnabled(bool bEnable)
{
    bHitTestingGloballyEnabled = bEnable;
    if (!bEnable)
    {
#if PLATFORM_WINDOWS
        if (bIsClickThroughStateOS)
        {
            UE_LOG(LogWindowHelper, Log, TEXT("Hit Test Disabled. Window was click-through, setting to interactive."));
            EnableClickThrough(false);
        }
#endif
        bIsMouseOverOpaqueAreaLogic = true;
    }
    else { UE_LOG(LogWindowHelper, Log, TEXT("Hit Test Enabled: %s"), bEnable ? TEXT("true") : TEXT("false")); }
}

void UWindowTransparencyHelper::SetHitTestType(EWindowHitTestType NewType)
{
    if (CurrentHitTestTypeLogic != NewType)
    {
        CurrentHitTestTypeLogic = NewType;
        UE_LOG(LogWindowHelper, Log, TEXT("Hit Test Type set to: %s"), *UEnum::GetValueAsString(NewType));
    }
}

void UWindowTransparencyHelper::SetGameRaycastTraceChannel(ECollisionChannel NewChannel)
{
    if (GameRaycastTraceChannelLogic != NewChannel)
    {
        GameRaycastTraceChannelLogic = NewChannel;
        const UEnum* EnumPtr = StaticEnum<ECollisionChannel>();
        FString ChannelName = EnumPtr ? EnumPtr->GetNameStringByValue(static_cast<int64>(NewChannel)) : FString::FromInt(static_cast<int32>(NewChannel));
        UE_LOG(LogWindowHelper, Log, TEXT("Game Raycast Trace Channel set to: %s"), *ChannelName);
    }
}

void UWindowTransparencyHelper::UpdateHitDetectionLogic(float DeltaTime)
{
#if PLATFORM_WINDOWS
    bool bMousePosSuccess;
    FVector2D MousePosInWindow = GetMousePositionInWindow(bMousePosSuccess);

    if (!bMousePosSuccess)
    {
        bIsMouseOverOpaqueAreaLogic = false;
        UE_LOG(LogWindowHelper, Verbose, TEXT("UpdateHitDetectionLogic: Mouse position not retrieved. Assuming transparent area."));
        return;
    }

    switch (CurrentHitTestTypeLogic)
    {
    case EWindowHitTestType::GameRaycast:
    {
        bIsMouseOverOpaqueAreaLogic = PerformGameRaycastUnderMouse(MousePosInWindow);
        const UEnum* EnumPtr = StaticEnum<ECollisionChannel>();
        FString ChannelName = EnumPtr ? EnumPtr->GetNameStringByValue(static_cast<int64>(GameRaycastTraceChannelLogic)) : FString::FromInt(static_cast<int32>(GameRaycastTraceChannelLogic));
        UE_LOG(LogWindowHelper, Log, TEXT("GameRaycastTest Result: bIsMouseOverOpaqueAreaLogic = %s at Pos: %s (Channel: %s)"),
            bIsMouseOverOpaqueAreaLogic ? TEXT("true (Opaque)") : TEXT("false (Transparent)"), *MousePosInWindow.ToString(), *ChannelName);
        break;
    }
    case EWindowHitTestType::None:
    default:
        bIsMouseOverOpaqueAreaLogic = true;
        break;
    }
#endif
}

bool UWindowTransparencyHelper::PerformGameRaycastUnderMouse(FVector2D MousePosInWindow)
{
    APlayerController* PC = GetFirstLocalPlayerController(this);
    if (!PC)
    {
        UE_LOG(LogWindowHelper, Warning, TEXT("GameRaycastTest: PlayerController not found. Assuming no hit (transparent)."));
        return false;
    }

    FHitResult HitResult3D;
    FCollisionQueryParams CollisionParams3D(SCENE_QUERY_STAT(WindowTransparencyRaycast3D), true);

    bool bHit3D = PC->GetHitResultAtScreenPosition(
        MousePosInWindow,
        this->GameRaycastTraceChannelLogic,
        CollisionParams3D,
        HitResult3D
    );

    if (bHit3D && HitResult3D.GetActor())
    {
        AActor* HitActor = HitResult3D.GetActor();
        const UEnum* EnumPtr = StaticEnum<ECollisionChannel>();
        FString ChannelName = EnumPtr ? EnumPtr->GetNameStringByValue(static_cast<int64>(this->GameRaycastTraceChannelLogic)) : FString::FromInt(static_cast<int32>(this->GameRaycastTraceChannelLogic));
        UE_LOG(LogWindowHelper, Verbose, TEXT("GameRaycastTest: Hit 3D Actor: %s (Component: %s) using Channel %s"),
            *HitActor->GetName(),
            HitResult3D.GetComponent() ? *HitResult3D.GetComponent()->GetName() : TEXT("None"),
            *ChannelName);
        return true;
    }

    if (FSlateApplication::IsInitialized() && GEngine && GEngine->GameViewport)
    {
        TSharedPtr<SWindow> GameSWindow = GEngine->GameViewport->GetWindow();
        if (GameSWindow.IsValid())
        {
            TArray<TSharedRef<SWindow>> WindowsToSearch;
            WindowsToSearch.Add(GameSWindow.ToSharedRef());

            FWidgetPath WidgetPath = FSlateApplication::Get().LocateWindowUnderMouse(
                MousePosInWindow,
                WindowsToSearch,
                false, /*bAllowDisabledWidgets*/
                PC->GetLocalPlayer() ? PC->GetLocalPlayer()->GetControllerId() : 0
            );

            if (WidgetPath.IsValid() && WidgetPath.Widgets.Num() > 0)
            {
                const FArrangedWidget& LastWidgetArranged = WidgetPath.Widgets.Last();
                TSharedRef<SWidget> HitWidget = LastWidgetArranged.Widget;
                FString WidgetType = HitWidget->GetTypeAsString();
                FString WidgetDesc = HitWidget->ToString();

                UE_LOG(LogWindowHelper, Verbose, TEXT("GameRaycastTest: UI Hit Candidate: Type: %s, Desc: %s, Visible: %d, Enabled: %d, PathLen: %d"),
                    *WidgetType, *WidgetDesc, HitWidget->GetVisibility().IsVisible(), HitWidget->IsEnabled(), WidgetPath.Widgets.Num());

                if (HitWidget->GetVisibility().IsVisible() && HitWidget->IsEnabled())
                {
                    if (WidgetType == TEXT("SWindow") ||
                        WidgetType == TEXT("SGameLayerManager") ||
                        WidgetType == TEXT("SViewport") ||
                        (WidgetType == TEXT("SBorder") && WidgetPath.Widgets.Num() <= 2) ||
                        (WidgetType == TEXT("SOverlay") && WidgetPath.Widgets.Num() <= 2) ||
                        (WidgetType == TEXT("SScaleBox") && WidgetPath.Widgets.Num() <= 2) ||
                        (WidgetType == TEXT("SCanvasPanel") && WidgetPath.Widgets.Num() <= 2) ||
                        (WidgetType == TEXT("SObjectWidget") && WidgetPath.Widgets.Num() <= 1)
                        )
                    {
                        UE_LOG(LogWindowHelper, Verbose, TEXT("GameRaycastTest: UI Hit on %s (%s), but considering it non-blocking/transparent due to type/context."), *WidgetType, *WidgetDesc);
                    }
                    else
                    {
                        UE_LOG(LogWindowHelper, Verbose, TEXT("GameRaycastTest: Hit blocking UI Widget: %s (%s)"), *WidgetType, *WidgetDesc);
                        return true;
                    }
                }
            }
        }
    }
    const UEnum* EnumPtr = StaticEnum<ECollisionChannel>();
    FString ChannelName = EnumPtr ? EnumPtr->GetNameStringByValue(static_cast<int64>(this->GameRaycastTraceChannelLogic)) : FString::FromInt(static_cast<int32>(this->GameRaycastTraceChannelLogic));
    UE_LOG(LogWindowHelper, Verbose, TEXT("GameRaycastTest: No blocking hit found on 3D (using Channel %s) or UI. Assuming transparent."), *ChannelName);
    return false;
}