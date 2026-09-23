# Scarlet Assist

A C++ assist for **Touhou 6 New Classic** by **haszKEJL**.

Auto dodge adjusts movement around bullets and lasers. Autoplay adds shooting, item collection, focus movement and an emergency bomb. Each feature has its own key binding and toggle/hold mode.

The in-game menu uses Dear ImGui with DirectX 11 and MinHook. Press **Insert** to open it. English, Polish and Russian are available; language and key bindings are saved between sessions.

## Download and run

Download the Windows x64 ZIP from [Releases](https://github.com/haszKEJL/scarlet-assist/releases), extract it, start `th06nc.exe`, then run `run.bat`. No compiler or Python required.

| Default key | Action |
| --- | --- |
| Insert | Open / close the menu |
| F5 | Autoplay |
| F6 | Autobomb |
| F7 | Item collection |
| F8 | Auto dodge |
| F9 | Disable all features |

All features start off. For dodge on Shift, choose **Hold**, bind Shift and enable **Armed**. Only physical key presses activate hold bindings.

Opening the panel pauses automation, not the game. Pause the stage first if you need time to change settings. Restart the game when updating the DLL.

## Compatibility

Windows x64, DirectX 11, **New Classic 1.03** with the default game controls. The supported executable has SHA-256:

```
07850c8c6e469c0e82c13423e6d0d096a88d693455bdacacbb44c0aa3bcce473
```

Other builds, including the original 2002 release, need different offsets. The launcher checks the executable before loading the DLL. Game files and saves are not included.

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

To make a download ZIP after a MinGW build:

```powershell
.\package.ps1 -SkipBuild
```

Settings and the initialization log are stored in `%LOCALAPPDATA%\TouhouAssist\`.
