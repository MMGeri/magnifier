# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

A Windows desktop overlay that magnifies the screen center in real time using the Windows Magnification API. Single source file: `src/magnifier.cpp`.

## Build

**MSBuild (Visual Studio) — from repo root:**
```powershell
# Debug
msbuild magnifier.vcxproj /p:Configuration=Debug /p:Platform=x64

# Release
msbuild magnifier.vcxproj /p:Configuration=Release /p:Platform=x64
```

Full path if MSBuild is not on PATH (use in cmd.exe — `&` is PowerShell-only and breaks in bash/cmd):
```
"C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" magnifier.vcxproj /p:Configuration=Debug /p:Platform=x64
```

Output: `x64\Debug\magnifier.exe` / `x64\Release\magnifier.exe`

**CMake:**
```bash
cmake -B build && cmake --build build
```

## Architecture

`src/magnifier.cpp` — entire implementation in one file:

- `WinMain` — loads config, enumerates monitors, initializes Magnification API, creates the host window, starts a configurable-rate timer, installs a `WH_KEYBOARD_LL` hook, then runs the message loop.
- `SetupMagnifier` — creates a `WS_EX_LAYERED | WS_EX_TRANSPARENT` popup window, embeds a `WC_MAGNIFIER` child control, sets initial 3× transform. Size and position come from `g_config`.
- `UpdateMagWindow` — timer callback at `1000/frameRate` ms; computes source rect centered on the configured monitor, calls `MagSetWindowSource`, repaints. Skips work when hidden. Skips `SetWindowPos(HWND_TOPMOST)` while settings window is open (prevents combo dropdown dismissal).
- `LowLevelKeyboardProc` — global hook handling toggle/mag-up/mag-down keys from `g_config`.
- `OpenSettingsWindow` — creates a programmatic Win32 settings dialog (no .rc). Key-capture Edit controls are subclassed via `KeyEditProc`. Settings apply live on every change (`EN_CHANGE` + `CBN_SELCHANGE`). Guard at top of `ApplyFromControls` prevents partial-state writes during window construction.
- `Config` / `LoadConfig` / `SaveConfig` — persists all settings to `settings.ini` next to the EXE via `GetPrivateProfileString` / `WritePrivateProfileString`.

**Key globals:** `g_config` (settings), `g_timerId` (timer handle for restart on FPS change), `MAGFACTOR` (current zoom), `visible` (overlay shown/hidden), `g_hwndSettings` (NULL when closed).

The `huntBullshit` offset (`SM_CYSCREEN * 0.092592`) compensates for a crosshair position offset in Hunt: Showdown.

No third-party libraries — pure Windows SDK (`magnification.h`, `windows.h`, `wincodec.h`, `shlwapi.h`).

## Verification Steps

After building, run `x64\Debug\magnifier.exe` and verify:

1. **Tray icon** appears in the system tray (bottom-right).
2. **Toggle** — press **Y**: overlay becomes visible (400×400 window at screen center). Press **Y** again: hidden.
3. **Magnification** — while visible, press **Up Arrow**: zoom increases. **Down Arrow**: zoom decreases (floor 1×).
4. **Settings window** — double-click tray icon: settings window opens. Right-click tray → Settings also works.
5. **Frame rate** — change FPS field (e.g. 30): overlay update rate slows noticeably. Change back to 60.
6. **Step** — change Step to 0.5, close settings. Up/Down arrows now increment by 0.5×.
7. **Monitor** — change monitor dropdown: overlay repositions to the center of that monitor immediately.
8. **Y Offset** — select "Hunt: Showdown" preset: value field auto-fills and locks. Select "Custom": field becomes editable.
9. **Persistence** — close and reopen the app: all settings reload from `settings.ini`.
10. **Exit** — right-click tray → Exit: app quits. Or click "Exit App" in settings.

## Project Files

```
src/magnifier.cpp       — source
magnifier.vcxproj       — MSBuild project (x64 Debug/Release)
magnifier.sln           — Visual Studio solution
CMakeLists.txt          — CMake alternative
.github/workflows/
  ci.yml                — builds Debug+Release on every PR
  release.yml           — builds Release, publishes GitHub Release on v*.*.* tag
```
