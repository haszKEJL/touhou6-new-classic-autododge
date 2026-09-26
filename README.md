# Scarlet Assist

A C++ assist for **Touhou 6 New Classic** by **haszKEJL**.

Auto dodge adjusts movement around bullets and lasers. Autoplay adds shooting, item collection, focus movement and an emergency bomb. Each feature has its own key binding and toggle/hold mode. Autobomb attempts a bomb when no escape is predicted; the game decides whether one is available.

The in-game menu uses Dear ImGui with DirectX 11 and MinHook. Press **Insert** to open it. English, Polish and Russian are available; language and key bindings are saved between sessions.

## Download and run

Download the Windows x64 ZIP from [Releases](https://github.com/haszKEJL/touhou6-new-classic-autododge/releases), extract it, start `th06nc.exe`, then run `run.bat`. No compiler or Python required.

| Default key | Action |
| --- | --- |
| Insert | Open / close the menu |
| F5 | Autoplay |
| F6 | Autobomb |
| F7 | Item collection |
| F8 | Auto dodge |
| F9 | Disable all features |

<img width="758" height="657" alt="touhou" src="https://github.com/user-attachments/assets/59bcdd46-885f-4be5-992c-ba85ca98660e" />

All features start off. For dodge on Shift, choose **Hold**, bind Shift and enable **Armed**. Only physical key presses activate hold bindings.

Opening the panel pauses automation, not the game. Pause the stage first if you need time to change settings. Restart the game when updating the DLL.

## Compatibility

Windows x64 and DirectX 11. The known New Classic 1.03 build uses its verified legacy layout. Other New Classic builds are resolved from unique signatures in the executable instead of relying on fixed absolute offsets. When a signature is missing or ambiguous, the launcher stops without enabling input. This can cover updates that preserve the relevant code, but a future update that changes it still needs validation and possibly a resolver update. The original 2002 game is not supported. Game files and saves are not included.

Version 1.5.0 was tested with Steam build 25306795, SHA-256 `48630a42a2eb6762d0db2a7e0d151203efbe67220fef7d2f9d0d51857928ac82`. Player, bullets, lasers, pickups, enemies, pause flags and spell records are resolved together. Extra-stage routes use the game's frame counter and spell names as before.

Extra-stage routes are experimental. The bot can still get hit, miss items or use bombs too late; it does not guarantee a clear.

## Build

Requires Windows x64 and a C++20 compiler. Dependencies are included in `vendor/` with their licenses: Dear ImGui 1.91.9b and MinHook 1.3.4.

With MinGW-w64 GCC on `PATH`:

```bat
build.bat
```

Or with Visual Studio and CMake:

```bat
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The outputs are `scarlet_launcher.exe`, `scarlet_assist.dll` and the optional `dodge_assist.exe` diagnostic tool. Keep the launcher and DLL together.

`build.bat` runs the behavior tests. CMake also builds a headless layout test for all three languages, including Polish and Cyrillic glyphs.

To check a game update without launching or modifying it:

```bat
bin\dodge_assist.exe --verify-layout "C:\path\to\th06nc.exe"
```

This also tests a simulated address shift across all 18 resolved global fields. It confirms layout detection, not gameplay performance. Structural changes that fail these checks need a compatibility update.

To make a download ZIP after a MinGW build:

```powershell
.\package.ps1 -SkipBuild
```

Settings and the initialization log are stored in `%LOCALAPPDATA%\TouhouAssist\`.
