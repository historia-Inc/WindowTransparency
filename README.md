# WindowTransparency
[![MIT License](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![UE Version](https://img.shields.io/badge/UE-5.5+-blue.svg)](https://www.unrealengine.com/)
[![Platform](https://img.shields.io/badge/Platform-Windows-lightgrey.svg)](#supported-environments)

[日本語 (Japanese)](./Docs/README.ja.md)

UE5 plugin for creating transparent window backgrounds on Windows (DX11), with dynamic click-through and borderless support.

A technical blog post explaining the details will be published at a later date.

![windowtranspercency2](https://github.com/user-attachments/assets/b6d375cc-7b6d-4801-8afa-19195b8180e7)

## Features

*   **Window Transparency (DWM Alpha Transparency):**
    *   Makes the window transparent based on the alpha channel of the rendering result, allowing you to see the desktop or other windows behind it.
*   **Borderless Window:**
    *   Hides the window's title bar and borders.
*   **Click-Through Control:**
    *   **OS-Level Click-Through:** Ignores all mouse input on the window, passing events to the windows behind it.
    *   **Pixel-Based Click-Through (Hit-Testing):** Determines in real-time whether there is UE content (3D objects or UI widgets) under the mouse cursor, and only allows click-through in transparent areas where there is no UE content.
        *   `GameRaycast` Mode: Performs a raycast to 3D scenes or UI widgets using a specified trace channel, determining opacity/transparency based on whether a hit occurs.
*   **Always on Top:**
    *   Keeps the window always in front of other windows.
*   **Desktop Background Mode:**
    *   Displays the UE window like a desktop wallpaper (parents it to the Windows `WorkerW` window).
*   **External Window Information Retrieval:**
    *   Retrieves information such as title, position, and size of other windows displayed on the system.

## Requirements

*   Unreal Engine: **UE5.5, UE5.6**
*   Operating System: **Windows** (Tested on Windows 11)
*   Rendering API: **DirectX 11**

## Installation

1.  Download the latest version of the plugin (Zip file) from the [Releases page](https://github.com/historia-Inc/WindowTransparency/releases)
2.  Create a `Plugins` folder in your Unreal Engine project's root folder (if it doesn't already exist).
3.  Extract the downloaded Zip file and copy the `WindowTransparency` folder into your project's `Plugins` folder.
    (e.g., `YourProject/Plugins/WindowTransparency`)
4.  Launch the Unreal Engine editor.
5.  From the main menu, select `Edit > Plugins`.
6.  Search for the `WindowTransparency` plugin and check the `Enabled` checkbox.
7.  If prompted to restart the editor, follow the instructions to restart.

## Setup (Project Settings)

Apply the following settings to your project.

1.  **Alpha Channel Support:**
    *   Open `Project Settings > Engine > Rendering`.
    *   In the `Default Settings` section, set `Enable Alpha Channel Support (Post Processing)` to `Allow through tonemapper`. (Note: The original Japanese mentioned `AlphaOutput` to `True`. If that's the precise setting, adjust this. `Enable Alpha Channel Support` is the common one for DWM transparency).
    *   *Correction based on Japanese original:* In the `Default Settings` section, set `AlphaOutput` to `True`.

2.  **Setting `r.D3D11.UseAllowTearing=0`:**
    *   Open your project's `Config/DefaultEngine.ini` file.
    *   Add or verify the following section and line. This is crucial.
        ```ini
        [/Script/Engine.RendererSettings]
        r.D3D11.UseAllowTearing=0
        ```

3.  **Custom Stencil Pass (Optional):**
    *   If you plan to use the stencil buffer to mask parts of the window (as in the `StencilMask_Demo` demo), the following setting is required.
    *   Open `Project Settings > Engine > Rendering`.
    *   In the `Postprocessing` category, set `Custom Depth-Stencil Pass` to `Enabled with Stencil`.


## Demos

Demo levels showcasing the plugin's features are available in the `/WindowTransparency/` folder.

*   **`/WindowTransparency/Demo`**
    *   The background becomes transparent, showing the desktop behind it.

*   **`/WindowTransparency/Maps/StencilMask_Demo`**
    *   A demo that uses a post-process material and stencil buffer to display only specific objects, making other areas transparent.

https://github.com/user-attachments/assets/d159b4a2-6507-403f-870f-85d53877cdb2

---

*   **`/WindowTransparency/Maps/WindowInteraction_Demo`**
    *   Demonstrates interaction with external windows, such as collision detection and occlusion.

https://github.com/user-attachments/assets/3acb76e1-5139-447d-b1b2-dcc7fb73eb90

---

*   **`/WindowTransparency/Maps/MouseInteraction_Demo`**
    *   A demo of click-through using pixel-based hit testing. Mouse operations are possible on UE 3D objects and UI widgets, while mouse events pass through to windows behind in other transparent areas.


https://github.com/user-attachments/assets/bbb4a8f5-cda0-4e9b-88c7-754510af0269

---

*   **`/WindowTransparency/Maps/ShadowMask_Demo`**
    *   A demo that renders objects in the scene and their shadows, making other parts transparent.


https://github.com/user-attachments/assets/4c42b417-7555-4ccd-b9af-b8ebc8eab072

---

*   **`/WindowTransparency/Maps/Wallpaper_Demo`**
    *   A demo using the `SetWindowAsDesktopBackground` feature to make the UE application behave like a live wallpaper.


https://github.com/user-attachments/assets/899f7d18-9379-440a-8438-dcc43ece1ea4

---

## License

This plugin is released under the [MIT License](LICENSE).