# Magnifier

A Windows screen magnification overlay built on the Windows Magnification API. Creates a transparent overlay window centered on screen that magnifies the area around the screen center in real time.

## Table of Contents

- [Features](#features)
- [Dependencies](#dependencies)
- [Build](#build)
  - [Option 1 — Developer Command Prompt / PowerShell for VS 2022](#option-1--developer-command-prompt--powershell-for-vs-2022)
  - [Option 2 — Any Command Prompt (cmd.exe), full MSBuild path](#option-2--any-command-prompt-cmdexe-full-msbuild-path)
  - [Option 3 — CMake](#option-3--cmake)
- [Run](#run)
- [Release Process](#release-process)
- [CI](#ci)

## Features

- Always-on-top transparent overlay with configurable frame rate
- Global keyboard hotkeys (work regardless of focused window):
  - **Y** — toggle visibility on/off
  - **Up Arrow** — increase magnification
  - **Down Arrow** — decrease magnification
- System tray icon — double-click to open Settings, right-click to exit
- Settings window: hotkeys, magnification step, frame rate, overlay size, monitor selection, Y-offset (includes Hunt: Showdown preset)
- Settings persisted to `settings.ini` next to the executable

## Dependencies

- Windows 8.1 or later
- Windows SDK (`magnification.h`, `windows.h`, `wincodec.h`) — no third-party libraries
- Visual Studio 2022 (or 2013+) for building from source

### Linux / macOS note

This application uses the Windows Magnification API and Win32 UI, so it does **not** run on Linux/macOS.

However, the repository is set up to **compile** on non-Windows platforms via CMake using a small stub `main()`.
This is mainly for cross-platform development tooling and CI.

## Build

### Option 1 — Developer Command Prompt / PowerShell for VS 2022

Open **Developer Command Prompt for VS 2022** (or **Developer PowerShell for VS 2022**) from the Start menu, then run from the repo root:

**Debug:**
```
msbuild magnifier.vcxproj /p:Configuration=Debug /p:Platform=x64
```

**Release:**
```
msbuild magnifier.vcxproj /p:Configuration=Release /p:Platform=x64
```

### Option 2 — Any Command Prompt (cmd.exe), full MSBuild path

```
"C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" magnifier.vcxproj /p:Configuration=Debug /p:Platform=x64
```

```
"C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" magnifier.vcxproj /p:Configuration=Release /p:Platform=x64
```

> **Note:** The `&` syntax for calling executables is PowerShell-specific. In `cmd.exe` (including the Claude Code terminal), use double quotes around the path instead.

### Option 3 — CMake

```
cmake -B build
cmake --build build
```

On Linux/macOS you can also use:

```bash
chmod +x ./scripts/build.sh
./scripts/build.sh
```

Output: `x64\Debug\magnifier.exe` / `x64\Release\magnifier.exe`

## Run

### Pre-built executable

A pre-built release binary is included at the repo root:

```
magnifier.exe
```

### Debug build

```powershell
x64\Debug\magnifier.exe
```

### Release build

```powershell
x64\Release\magnifier.exe
```

### Linux/macOS

The built executable prints a message and exits (stub build).

## Release Process

Releases are triggered by pushing a version tag on a commit that is on `main`. GitHub Actions builds the Release binary, packages it, and publishes a GitHub Release with auto-generated notes.

```bash
git tag v1.2.3
git push origin v1.2.3
```

**Versioning:** `v<major>.<minor>.<patch>`
- `patch` — bug fixes
- `minor` — new features (backwards compatible)
- `major` — breaking changes or major rewrites

Each release contains:
- `magnifier-vX.Y.Z-win-x64.exe` — the executable
- `SHA256SUMS.txt` — SHA-256 checksums of the files above
- a signed build provenance attestation (GitHub artifact attestation)

### Verifying a download

```powershell
Get-FileHash .\magnifier-v1.2.3-win-x64.exe -Algorithm SHA256   # compare with SHA256SUMS.txt
gh attestation verify magnifier-v1.2.3-win-x64.exe --repo MMGeri/magnifier
```

The attestation proves the file was built by this repository's release workflow from the tagged commit.

## CI

- **Every PR** — builds Debug and Release (x64) to catch build failures before merge
- **Tag push** (`v*.*.*`) — verifies the tag is on `main`, builds Release, then publishes a GitHub Release with the exe, checksums and build provenance attestation
