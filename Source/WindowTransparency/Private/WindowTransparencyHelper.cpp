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
    , DefaultParentHwnd(nullptr)
    , bIsDesktopBackgroundActive(false)
    , CurrentWorkerW(nullptr)
    , TrueOriginalParentHwnd(nullptr)
    , TrueOriginalWindowStyle(0)
    , TrueOriginalExWindowStyle(0)
    , bTrueOriginalStateStored(false)
#endif
    , bHitTestingGloballyEnabled(false)
    , CurrentHitTestTypeLogic(EWindowHitTestType::None)
    , GameRaycastTraceChannelLogic(ECollisionChannel::ECC_Visibility)
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
    // 保持している SWindow から優先的に取得
    if (GameSWindowPtr.IsValid()) {
        TSharedPtr<SWindow> StrongSWindow = GameSWindowPtr.Pin();
        if (StrongSWindow.IsValid() && StrongSWindow->GetNativeWindow().IsValid()) {
            void* Handle = StrongSWindow->GetNativeWindow()->GetOSWindowHandle();
            UE_LOG(LogWindowHelper, Verbose, TEXT("GetGameHWnd: Returning HWND from cached GameSWindowPtr: %p"), Handle);
            return static_cast<HWND>(Handle);
        }
    }

    // SWindow が無効なら従来のロジックで取得し、キャッシュも試みる
    if (GEngine && GEngine->GameViewport && GEngine->GameViewport->GetWindow().IsValid())
    {
        TSharedPtr<SWindow> GameSWindow = GEngine->GameViewport->GetWindow();
        // const_cast は通常避けるべきだが、ここではメンバ変数を更新するために一時的に使用
        const_cast<UWindowTransparencyHelper*>(this)->GameSWindowPtr = GameSWindow;
        if (GameSWindow.IsValid() && GameSWindow->GetNativeWindow().IsValid())
        {
            void* Handle = GameSWindow->GetNativeWindow()->GetOSWindowHandle();
            UE_LOG(LogWindowHelper, Verbose, TEXT("GetGameHWnd: Returning HWND from GEngine->GameViewport and caching SWindow: %p"), Handle);
            return static_cast<HWND>(Handle);
        }
    }
    if (FSlateApplication::IsInitialized())
    {
        TSharedPtr<SWindow> ActiveWindow = FSlateApplication::Get().GetActiveTopLevelWindow();
        if (ActiveWindow.IsValid() && ActiveWindow->GetNativeWindow().IsValid())
        {
            // これがゲームウィンドウである保証は薄いが、フォールバックとして
            // const_cast<UWindowTransparencyHelper*>(this)->GameSWindowPtr = ActiveWindow; // トップレベルなのでキャッシュ対象としては微妙
            void* Handle = ActiveWindow->GetNativeWindow()->GetOSWindowHandle();
            UE_LOG(LogWindowHelper, Verbose, TEXT("GetGameHWnd: Returning HWND from GetActiveTopLevelWindow (fallback): %p"), Handle);
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
    Info.WindowHandleStr = FString::Printf(TEXT("%llu"), reinterpret_cast<uint64>(hwnd));

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
    if (GameHWnd && !bOriginalStylesStored) // 通常の bOriginalStylesStored をまずチェック
    {
        OriginalWindowStyle = GetWindowLongPtr(GameHWnd, GWL_STYLE);
        OriginalExWindowStyle = GetWindowLongPtr(GameHWnd, GWL_EXSTYLE);
        bOriginalStylesStored = true;
        UE_LOG(LogWindowHelper, Log, TEXT("Stored current window styles. Style: 0x%p, ExStyle: 0x%p"), (void*)OriginalWindowStyle, (void*)OriginalExWindowStyle);

        // 真のオリジナルスタイルがまだ保存されていなければ、現在のスタイルを真のオリジナルとして保存
        if (!bTrueOriginalStateStored)
        {
            TrueOriginalWindowStyle = OriginalWindowStyle;
            TrueOriginalExWindowStyle = OriginalExWindowStyle;
            // TrueOriginalParentHwnd は Initialize で設定済みのはず
            bTrueOriginalStateStored = true;
            UE_LOG(LogWindowHelper, Log, TEXT("Stored TRUE original window styles from current. Style: 0x%p, ExStyle: 0x%p"), (void*)TrueOriginalWindowStyle, (void*)TrueOriginalExWindowStyle);
        }
    }
#endif
}

void UWindowTransparencyHelper::ReInitializeIfNeeded()
{
#if PLATFORM_WINDOWS
    if (bIsDesktopBackgroundActive)
    {
        // デスクトップ背景モード中は、GameHWnd の生存確認のみ行う
        if (!GameHWnd || !IsWindow(GameHWnd))
        {
            UE_LOG(LogWindowHelper, Error, TEXT("ReInitializeIfNeeded: GameHWnd (%p) became invalid during Desktop Background mode! Forcing mode disable and full re-init."), GameHWnd);

            // バックグラウンドモード関連のフラグをリセット
            bIsDesktopBackgroundActive = false;
            CurrentWorkerW = nullptr;
            // GameHWnd が無効なので、親ウィンドウやスタイルを安全に戻せない可能性がある
            // 強制的にフラグを落とし、通常の再初期化フローへ
            GameHWnd = nullptr;
            GameSWindowPtr.Reset();
            bIsInitialized = false;
            bOriginalStylesStored = false; // これにより Initialize で Original~ が再取得される
            bCanHelperTick = false;
            Initialize(); // 通常の Initialize を試みる
        }
        // GameHWnd が有効なら、SWindow 関連の不整合は許容し、再初期化しない
        return;
    }

    // 通常の再初期化ロジック (変更なし)
    bool bNeedsReinit = false;
    if (!GameHWnd || (GameHWnd && !IsWindow(GameHWnd))) {
        UE_LOG(LogWindowHelper, Log, TEXT("ReInitializeIfNeeded: GameHWnd %p is invalid or null."), GameHWnd);
        bNeedsReinit = true;
    }
    if (!GameSWindowPtr.IsValid()) {
        UE_LOG(LogWindowHelper, Log, TEXT("ReInitializeIfNeeded: GameSWindowPtr is invalid."));
        bNeedsReinit = true;
    }
    else {
        TSharedPtr<SWindow> StrongSWindow = GameSWindowPtr.Pin();
        if (!StrongSWindow.IsValid() || !StrongSWindow->GetNativeWindow().IsValid() ||
            (GameHWnd && StrongSWindow->GetNativeWindow()->GetOSWindowHandle() != GameHWnd)) {
            UE_LOG(LogWindowHelper, Log, TEXT("ReInitializeIfNeeded: GameSWindowPtr valid but native window invalid or HWND mismatch."));
            bNeedsReinit = true;
        }
    }

    if (bNeedsReinit) {
        UE_LOG(LogWindowHelper, Log, TEXT("ReInitializeIfNeeded: Attempting to re-initialize all window handles and states (not in desktop background mode)."));
        GameHWnd = nullptr;
        GameSWindowPtr.Reset();
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
    if (bIsInitialized && GameHWnd && IsWindow(GameHWnd) && !bIsDesktopBackgroundActive) // デスクトップ背景モード中は再評価が必要な場合がある
    {
        return true;
    }

    if (GEngine && GEngine->GameViewport && GEngine->GameViewport->GetWindow().IsValid()) {
        GameSWindowPtr = GEngine->GameViewport->GetWindow();
    }

    HWND TempHWnd = GetGameHWnd();
    if (TempHWnd)
    {
        GameHWnd = TempHWnd; // GameHWnd を設定
        bIsInitialized = true;

        if (!bTrueOriginalStateStored) // まだ真のオリジナルが保存されていなければ
        {
            TrueOriginalWindowStyle = GetWindowLongPtr(GameHWnd, GWL_STYLE);
            TrueOriginalExWindowStyle = GetWindowLongPtr(GameHWnd, GWL_EXSTYLE);
            TrueOriginalParentHwnd = ::GetParent(GameHWnd);
            bTrueOriginalStateStored = true;
            UE_LOG(LogWindowHelper, Log, TEXT("Stored TRUE original state. Parent: %p, Style: 0x%p, ExStyle: 0x%p"),
                TrueOriginalParentHwnd, (void*)TrueOriginalWindowStyle, (void*)TrueOriginalExWindowStyle);

            // 通常のOriginalもこれで初期化
            OriginalWindowStyle = TrueOriginalWindowStyle;
            OriginalExWindowStyle = TrueOriginalExWindowStyle;
            DefaultParentHwnd = TrueOriginalParentHwnd;
            bOriginalStylesStored = true; // これも true に
        }
        else if (!bOriginalStylesStored && !bIsDesktopBackgroundActive)
        {
            // TrueOriginalは保存済みだが、Originalが未保存で、デスクトップ背景モードでない場合
            // (例: デスクトップ背景解除後の再初期化など)
            OriginalWindowStyle = GetWindowLongPtr(GameHWnd, GWL_STYLE);
            OriginalExWindowStyle = GetWindowLongPtr(GameHWnd, GWL_EXSTYLE);
            DefaultParentHwnd = ::GetParent(GameHWnd);
            bOriginalStylesStored = true;
        }

        LONG_PTR CurrentExStyle = GetWindowLongPtr(GameHWnd, GWL_EXSTYLE);
        bIsClickThroughStateOS = (CurrentExStyle & WS_EX_TRANSPARENT) != 0;
        bCanHelperTick = true;
        UE_LOG(LogWindowHelper, Log, TEXT("WindowTransparencyHelper Initialized. GameHWnd: %p, GameSWindow valid: %s, Current Parent: %p."),
            GameHWnd, GameSWindowPtr.IsValid() ? TEXT("true") : TEXT("false"), ::GetParent(GameHWnd));
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
    if (bIsDesktopBackgroundActive) {
        return;
    }
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

#if PLATFORM_WINDOWS
// Helper struct for EnumWindowsProcWorkerW
struct WorkerWEnumData {
    HWND WorkerW_Handle = nullptr;
    HWND Progman_Handle = nullptr;
    int MonitorCount = 0; // マルチモニター対応のためのヒント
};

// Callback for EnumWindows to find the correct WorkerW
static BOOL CALLBACK EnumWindowsProcFindWorkerW(HWND hwnd, LPARAM lParam)
{
    WorkerWEnumData* pData = reinterpret_cast<WorkerWEnumData*>(lParam);
    WCHAR className[256];
    WCHAR windowTitle[256]; // デバッグ用

    if (GetClassNameW(hwnd, className, sizeof(className) / sizeof(WCHAR)) && wcscmp(className, L"WorkerW") == 0)
    {
        GetWindowText(hwnd, windowTitle, sizeof(windowTitle) / sizeof(WCHAR)); // デバッグ用
        bool bIsVisible = IsWindowVisible(hwnd);
        HWND parentHwnd = GetParent(hwnd);
        RECT rcClient;
        GetClientRect(hwnd, &rcClient); // クライアント領域のサイズを取得


        // SHELLDLL_DefView を子に持つか確認 (これを持つものはアイコン表示用なので除外)
        HWND defView = FindWindowEx(hwnd, NULL, L"SHELLDLL_DefView", NULL);
        if (defView == NULL)
        {
            UE_LOG(LogWindowHelper, Log, TEXT("  - WorkerW %p does NOT have SHELLDLL_DefView child. Candidate."), hwnd);

            // 条件1: 表示されている (IsWindowVisible が true)
            // 条件2: クライアント領域が妥当なサイズ (幅 > 0 かつ 高さ > 0)
            // 条件3: 親が Progman であるか、トップレベルウィンドウである
            //        (一部環境では Progman の子ではなく、Progman の兄弟ウィンドウとして存在する)
            if (bIsVisible && (rcClient.right - rcClient.left > 0) && (rcClient.bottom - rcClient.top > 0))
            {
                // マルチモニター環境では複数の WorkerW が存在する可能性がある。
                // 通常はプライマリモニターに対応するものが使われるが、より確実なのは
                // Progman と同じモニター上にある WorkerW を探すこと。
                // ここでは簡略化のため、最初に見つかった条件を満たすものを採用。

                pData->WorkerW_Handle = hwnd;
                UE_LOG(LogWindowHelper, Log, TEXT("  - WorkerW %p SELECTED as target (Visible, Valid Size, No DefView)."), hwnd);
                return false; // 発見したので列挙を停止
            }
            else
            {
                FString skipReason;
                if (!bIsVisible) skipReason += TEXT("NotVisible; ");
                if (rcClient.right - rcClient.left <= 0) skipReason += TEXT("ZeroWidth; ");
                if (rcClient.bottom - rcClient.top <= 0) skipReason += TEXT("ZeroHeight; ");
                UE_LOG(LogWindowHelper, Log, TEXT("  - WorkerW %p (No DefView) SKIPPED. Reason: %s"), hwnd, *skipReason);
            }
        }
        else
        {
            UE_LOG(LogWindowHelper, Log, TEXT("  - WorkerW %p HAS SHELLDLL_DefView child (%p). Skipping."), hwnd, defView);
        }
    }
    return true; // 列挙を続行
}

HWND UWindowTransparencyHelper::FindTargetWorkerW()
{
    HWND progman = FindWindowW(L"Progman", NULL);
    if (!progman)
    {
        UE_LOG(LogWindowHelper, Warning, TEXT("FindTargetWorkerW: Progman window not found."));
        return nullptr;
    }
    UE_LOG(LogWindowHelper, Log, TEXT("FindTargetWorkerW: Found Progman: %p"), progman);

    ULONG_PTR result;
    UE_LOG(LogWindowHelper, Log, TEXT("FindTargetWorkerW: Sending 0x052C message to Progman %p."), progman);
    SendMessageTimeout(progman, 0x052C, 0x0000000D, 0, SMTO_NORMAL, 1000, &result);
    SendMessageTimeout(progman, 0x052C, 0, 0, SMTO_NORMAL, 1000, &result);

    // メッセージ送信後、WorkerWが準備されるのを少し待つ (荒療治だが効果がある場合がある)
    // FPlatformProcess::Sleep(0.2f); // 200ms待機 -> いきなりSleepは避ける。まずはログで確認

    WorkerWEnumData data;
    data.WorkerW_Handle = nullptr;
    data.Progman_Handle = progman;
    data.MonitorCount = GetSystemMetrics(SM_CMONITORS); // 参考情報

    UE_LOG(LogWindowHelper, Log, TEXT("FindTargetWorkerW: Strategy 1 - EnumWindows for top-level WorkerW. Monitor count: %d"), data.MonitorCount);
    EnumWindows(EnumWindowsProcFindWorkerW, reinterpret_cast<LPARAM>(&data));

    if (data.WorkerW_Handle)
    {
        UE_LOG(LogWindowHelper, Log, TEXT("FindTargetWorkerW: Strategy 1 - Found target WorkerW: %p"), data.WorkerW_Handle);
        return data.WorkerW_Handle;
    }

    UE_LOG(LogWindowHelper, Warning, TEXT("FindTargetWorkerW: Strategy 1 failed. Strategy 2 - EnumChildWindows on Progman (%p)."), progman);
    data.WorkerW_Handle = nullptr;
    EnumChildWindows(progman, EnumWindowsProcFindWorkerW, reinterpret_cast<LPARAM>(&data));

    if (data.WorkerW_Handle)
    {
        UE_LOG(LogWindowHelper, Log, TEXT("FindTargetWorkerW: Strategy 2 - Found target WorkerW (%p) as child of Progman."), data.WorkerW_Handle);
        return data.WorkerW_Handle;
    }

    UE_LOG(LogWindowHelper, Error, TEXT("FindTargetWorkerW: All strategies failed. No suitable WorkerW found."));
    return nullptr;
}
#endif // PLATFORM_WINDOWS

void UWindowTransparencyHelper::SetAsDesktopBackground(bool bEnable)
{
#if PLATFORM_WINDOWS
    if (bEnable)
    {

        if (!bIsInitialized || !GameHWnd || !IsWindow(GameHWnd)) {
            UE_LOG(LogWindowHelper, Log, TEXT("SetAsDesktopBackground(Enable): Helper not initialized or GameHWnd invalid. Calling Initialize()."));
            if (!Initialize()) {
                UE_LOG(LogWindowHelper, Error, TEXT("SetAsDesktopBackground(Enable): Initialize() failed. Cannot proceed."));
                return;
            }
        }

        CurrentWorkerW = FindTargetWorkerW();
        if (!CurrentWorkerW)
        {
            UE_LOG(LogWindowHelper, Error, TEXT("SetAsDesktopBackground: Failed to find WorkerW."));
            return;
        }

        LONG_PTR DesktopBackgroundStyle = (GetWindowLongPtr(GameHWnd, GWL_STYLE) & ~(WS_CAPTION | WS_THICKFRAME | WS_SYSMENU)) | WS_POPUP;
        if (!bIsBorderlessActive) { // まだボーダレスでなければ適用
            SetWindowLongPtr(GameHWnd, GWL_STYLE, DesktopBackgroundStyle);
        }


        LONG_PTR CurrentExStyle = GetWindowLongPtr(GameHWnd, GWL_EXSTYLE);
        SetWindowLongPtr(GameHWnd, GWL_EXSTYLE, CurrentExStyle | WS_EX_LAYERED | WS_EX_TRANSPARENT);
        // SetWindowPos でスタイルの変更を適用
        SetWindowPos(GameHWnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_NOACTIVATE);


        UE_LOG(LogWindowHelper, Log, TEXT("SetAsDesktopBackground: About to call SetParent. GameHWnd: %p, WorkerW: %p"), GameHWnd, CurrentWorkerW);
        if (::SetParent(GameHWnd, CurrentWorkerW) == NULL) {
            DWORD lastError = GetLastError();
            UE_LOG(LogWindowHelper, Error, TEXT("SetAsDesktopBackground: SetParent of GameHWnd %p to WorkerW %p failed. Error: %d."), GameHWnd, CurrentWorkerW, lastError);
            // 親の設定に失敗した場合、スタイルを元に戻す試み
            if (!bIsBorderlessActive) { // 元々ボーダレスでなかった場合のみ戻す
                SetWindowLongPtr(GameHWnd, GWL_STYLE, GetWindowLongPtr(GameHWnd, GWL_STYLE) & ~WS_POPUP | (TrueOriginalWindowStyle & (WS_CAPTION | WS_THICKFRAME | WS_SYSMENU))); // 大まかな復元
            }
            SetWindowLongPtr(GameHWnd, GWL_EXSTYLE, CurrentExStyle); // SetParent 前の ExStyle に戻す
            SetWindowPos(GameHWnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_NOACTIVATE);
            CurrentWorkerW = nullptr;
            return;
        }
        UE_LOG(LogWindowHelper, Log, TEXT("SetAsDesktopBackground: Successfully set parent of GameHWnd %p to WorkerW %p."), GameHWnd, CurrentWorkerW);

        RECT rcWorker;
        if (GetClientRect(CurrentWorkerW, &rcWorker))
        {
            if (rcWorker.right - rcWorker.left > 0 && rcWorker.bottom - rcWorker.top > 0) {
                SetWindowPos(GameHWnd, NULL, rcWorker.left, rcWorker.top, rcWorker.right - rcWorker.left, rcWorker.bottom - rcWorker.top, SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
            }
            else { /* ... */ }
        }
        SetWindowPos(GameHWnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        // SetWindowTopmost(false); // HWND_BOTTOM にしているので不要、かつ Topmost 状態は TrueOriginalExStyle に基づくべき

        bIsDesktopBackgroundActive = true;
        // SWindowポインタは無効になっている可能性があるのでクリア
        GameSWindowPtr.Reset();
        bIsClickThroughStateOS = true; // OSレベルではクリックスルーにした

        UE_LOG(LogWindowHelper, Log, TEXT("Window set as desktop background. GameHWnd: %p"), GameHWnd);
    }
    else // bEnable is false (デスクトップ背景モード解除)
    {
        if (!bIsDesktopBackgroundActive) return;

        // GameHWnd が有効か確認。無効ならどうしようもない場合がある。
        if (!GameHWnd || !IsWindow(GameHWnd)) {
            UE_LOG(LogWindowHelper, Error, TEXT("SetAsDesktopBackground(Disable): GameHWnd is invalid. Cannot restore properly. Resetting flags."));
            bIsDesktopBackgroundActive = false;
            CurrentWorkerW = nullptr;
            bIsInitialized = false; // 次のTickでReInitializeIfNeeded -> Initializeを促す
            bOriginalStylesStored = false; // Original スタイルも再取得させる
            return;
        }

        UE_LOG(LogWindowHelper, Log, TEXT("SetAsDesktopBackground: Disabling desktop background mode. GameHWnd for restore: %p"), GameHWnd);

        // 親ウィンドウを「真の」初期の親に戻す
        HWND TargetParent = bTrueOriginalStateStored ? TrueOriginalParentHwnd : NULL; // フォールバックでトップレベルに
        if (::SetParent(GameHWnd, TargetParent) == NULL && TargetParent != NULL) {
            // DefaultParentHwnd (おそらくNULL) へのSetParentが失敗した場合、最後の手段としてNULLを試す
            if (::SetParent(GameHWnd, NULL) == NULL) { // トップレベルウィンドウに戻す
                UE_LOG(LogWindowHelper, Error, TEXT("SetAsDesktopBackground(Disable): SetParent to TrueOriginalParentHwnd/NULL failed. Error: %d"), GetLastError());
            }
            else {
                UE_LOG(LogWindowHelper, Log, TEXT("SetAsDesktopBackground(Disable): Restored parent to NULL (top-level)."));
            }
        }
        else {
            UE_LOG(LogWindowHelper, Log, TEXT("SetAsDesktopBackground(Disable): Restored parent to TrueOriginalParentHwnd: %p."), TargetParent);
        }

        // スタイルを「真の」初期スタイルに戻す
        if (bTrueOriginalStateStored)
        {
            SetWindowLongPtr(GameHWnd, GWL_STYLE, TrueOriginalWindowStyle);
            SetWindowLongPtr(GameHWnd, GWL_EXSTYLE, TrueOriginalExWindowStyle);
            UE_LOG(LogWindowHelper, Log, TEXT("SetAsDesktopBackground(Disable): Restored TRUE original styles. Style: 0x%p, ExStyle: 0x%p"), (void*)TrueOriginalWindowStyle, (void*)TrueOriginalExWindowStyle);
        }
        else {
            UE_LOG(LogWindowHelper, Warning, TEXT("SetAsDesktopBackground(Disable): True original styles not stored. Attempting restore with potentially current Original styles (if any)."));
            if (bOriginalStylesStored) { // フォールバック
                SetWindowLongPtr(GameHWnd, GWL_STYLE, OriginalWindowStyle);
                SetWindowLongPtr(GameHWnd, GWL_EXSTYLE, OriginalExWindowStyle);
            }
        }
        bIsClickThroughStateOS = (GetWindowLongPtr(GameHWnd, GWL_EXSTYLE) & WS_EX_TRANSPARENT) != 0;


        SetWindowPos(GameHWnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW); // SWP_SHOWWINDOW で表示を確実にする
        InvalidateRect(GameHWnd, NULL, true);
        UpdateWindow(GameHWnd);

        bIsDesktopBackgroundActive = false;
        CurrentWorkerW = nullptr;
        bIsBorderlessActive = (GetWindowLongPtr(GameHWnd, GWL_STYLE) & WS_POPUP) != 0 && !((GetWindowLongPtr(GameHWnd, GWL_STYLE) & (WS_CAPTION | WS_THICKFRAME)));
        bIsTopmostActive = (GetWindowLongPtr(GameHWnd, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0; //Topmost状態を再評価


        // 通常状態に戻ったので、Slate との関連を再確立するため再初期化を促す
        bIsInitialized = false;
        bOriginalStylesStored = false; // Initialize で Original~ を再取得させる
        GameSWindowPtr.Reset();    // Slateウィンドウポインタもリセット
        UE_LOG(LogWindowHelper, Log, TEXT("Window removed from desktop background. Triggering re-initialization. Final GameHWnd: %p"), GameHWnd);
        // 次のTickで ReInitializeIfNeeded が呼ばれ、Initialize が実行されることを期待
    }
#else
    UE_LOG(LogWindowHelper, Log, TEXT("SetAsDesktopBackground: Not supported on this platform."));
#endif
}