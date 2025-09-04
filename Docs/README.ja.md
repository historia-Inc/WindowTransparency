# WindowTransparency
[![MIT License](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![UE Version](https://img.shields.io/badge/UE-5.5+-blue.svg)](https://www.unrealengine.com/)
[![Platform](https://img.shields.io/badge/Platform-Windows-lightgrey.svg)](#supported-environments)


![windowtranspercency2](https://github.com/user-attachments/assets/b6d375cc-7b6d-4801-8afa-19195b8180e7)

UE5でウィンドウの背景を透過表示するためのプラグインです。

技術ブログによる解説はこちら：[https://historia.co.jp/archives/50391/](https://historia.co.jp/archives/50391/)

## 主な機能

*   **ウィンドウ透過 (DWM Alpha Transparency):**
    *   レンダリング結果のアルファチャンネルに基づいてウィンドウを透過させ、背後のデスクトップや他のウィンドウが見えるようにします。
*   **クリックスルー:**
    *   **OSレベルクリックスルー:** ウィンドウ全体のマウス入力を無視し、背後のウィンドウにイベントを渡します。
    *   **ピクセルベースクリックスルー (ヒットテスト):** マウスカーソル下のUEコンテンツ（3DオブジェクトやUIウィジェット）の有無をリアルタイムに判定し、UEコンテンツがない透明な領域のみクリックスルーさせます。
        *   `GameRaycast` : 指定したトレースチャンネルで3DシーンやUIウィジェットへのレイキャストを行い、ヒットの有無で不透明/透明を判定します。
*   **最前面表示:**
    *   ウィンドウを常に他のウィンドウより手前に表示します。
*   **デスクトップの壁紙:**
    *   UEウィンドウをデスクトップの壁紙のように表示します (Windows の `WorkerW` ウィンドウにペアレントします)。
*   **外部ウィンドウ情報取得:**
    *   システム上で表示されている他のウィンドウのタイトル、位置、サイズなどの情報を取得します。

## 要件

*   Unreal Engine: **UE5.5, UE5.6**
*   オペレーティングシステム: **Windows** (Windows 11 で動作確認済み)
*   レンダリング API: **DirectX 11**

## インストール

1.  [リリースページ](https://github.com/historia-Inc/WindowTransparency/releases)から最新バージョンのプラグイン (Zipファイル) をダウンロードします。
2.  Unreal Engine プロジェクトのルートフォルダに `Plugins` フォルダを作成します (まだ存在しない場合)。
3.  ダウンロードした Zip ファイルを展開し、`WindowTransparency` フォルダをプロジェクトの `Plugins` フォルダにコピーします。
    (例: `YourProject/Plugins/WindowTransparency`)
4.  Unreal Engine エディタを起動します。
5.  メインメニューから `編集 (Edit) > プラグイン (Plugins)` を選択します。
6.  `WindowTransparency` プラグインを検索し、`有効 (Enabled)` チェックボックスをオンにします。
7.  エディタの再起動を求められたら、指示に従って再起動します。

##  セットアップ

以下の設定をプロジェクトに適用してください。

1.  **アルファチャンネルサポート:**
    *   `プロジェクト設定 (Project Settings)` > `エンジン (Engine)` > `レンダリング (Rendering)` を開きます。
    *   `Default Settings` セクションにある `AlphaOutput` を `True` に設定します。

2.  **Default RHIの設定:**
    * `プロジェクト設定 (Project Settings)` > `プラットフォーム(Platforms)` > `Windows`を開きます。
    * `Targeted RHIs`セクションにある`Default RHI`をDirectX 11に変更します。

3.  **r.D3D11.UseAllowTearing=0 の設定:**
    *   プロジェクトの `Config/DefaultEngine.ini` ファイルを開きます。
    *   以下のセクションと行を追加または確認してください。これが最も重要です。
        ```ini
        [/Script/Engine.RendererSettings]
        r.D3D11.UseAllowTearing=0
        ```

4.  **カスタムステンシルパス (任意):**
    *   ステンシルバッファを使用してウィンドウの一部をマスクする場合（デモ `StencilMask_Demo` のようなケース）は、以下の設定が必要です。
    *   `プロジェクト設定 (Project Settings) > エンジン (Engine) > レンダリング (Rendering)` を開きます。
    *   `ポストプロセス (Postprocessing)` カテゴリ内の `カスタム深度ステンシルパス (Custom Depth-Stencil Pass)` 設定を `ステンシル付きで有効 (Enabled with Stencil)` にします。

## 注意事項

*   **動作確認:** このプラグインの機能は、Unreal EngineエディタのPIE (Play In Editor) モードでは正しく動作しません。動作確認はスタンドアローンゲームとして実行するか、パッケージ化したビルドで行ってください。


## デモ

プラグインの機能を具体的に示すデモレベルが `/WindowTransparency/` フォルダ以下に用意されています。

*   **`/WindowTransparency/Demo`**
    *   背景が透明になり、背後のデスクトップが見えます。

*   **`/WindowTransparency/Maps/StencilMask_Demo`**
    *   ポストプロセスマテリアルとステンシルバッファを使用して、特定のオブジェクトだけを表示し、それ以外の領域を透過させるデモです。

https://github.com/user-attachments/assets/d159b4a2-6507-403f-870f-85d53877cdb2

---
      
*   **`/WindowTransparency/Maps/WindowInteraction_Demo`**
    *   外部ウィンドウに当たり判定や遮蔽などインタラクションできます。

https://github.com/user-attachments/assets/3acb76e1-5139-447d-b1b2-dcc7fb73eb90

---

*   **`/WindowTransparency/Maps/MouseInteraction_Demo`**
    *   ピクセルベースのヒットテスト機能を使用したクリックスルーのデモです。UEの3DオブジェクトやUIウィジェット上ではマウス操作が可能で、それ以外の透明な部分ではマウスイベントが背後のウィンドウに通過します。
   

https://github.com/user-attachments/assets/bbb4a8f5-cda0-4e9b-88c7-754510af0269

---

*   **`/WindowTransparency/Maps/ShadowMask_Demo`**
    *   シーン内のオブジェクトとその影を描画し、それ以外の部分を透過させるデモです。


https://github.com/user-attachments/assets/4c42b417-7555-4ccd-b9af-b8ebc8eab072

---
      
*   **`/WindowTransparency/Maps/Wallpaper_Demo`**
    *   `SetWindowAsDesktopBackground` 機能を使用して、UEアプリケーションをライブ壁紙のように動作させるデモです。



https://github.com/user-attachments/assets/899f7d18-9379-440a-8438-dcc43ece1ea4

---

## ライセンス

このプラグインは [MITライセンス](LICENSE) の下で公開されています。



