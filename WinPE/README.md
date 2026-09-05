# WinPE - Windows Deployment UI

Win32/GDI desktop application (C++20) for OEM-style Windows PE deployment.
The UI is built from native Win32 window classes (no WinUI/XAML), with
minimal owner-draw kept only where a colored state button is required.

## Directory layout

```
Main.cpp                  Thin entry: delegates to WinPEMain (20 lines)
App.cpp / App.h           Application lifecycle orchestration
System/                   Platform layer
  System.h                Public interfaces
  Dpi.cpp                 DPI awareness / DPI query / scale factor
  Elevation.cpp           Administrator check + runas relaunch
  GdiResources.cpp        Global brush pool and shared font
UIConfig/                 UI configuration layer (C-style, saved as .cpp)
  UIConfig.h              Geometry/theme/font types + per-window tables
  UIConfig_Init.cpp       Screen + per-window default configuration
  UIConfig_Layout.cpp     Layout calculation + relayout entry
  UIConfig_Theme.cpp      Theme/palette application (brush pool)
  UIConfig_Register.cpp   Unified window-class registration
  UIConfig.cpp            Master config dispatcher UI_ConfigAll()
MainWindow.cpp            Main window class/creation
StatusBarPanel.cpp        Child window 1: status bar (model/clock/buttons)
ContentPanel.cpp          Child window 2: three selection cards
BottomPanel.cpp           Child window 3: log/progress/deploy/demo
Utils/                    Deployment back end (no UI)
  DiskScanner.cpp         Physical disk enumeration (IOCTL)
  ImageParser.cpp         WIM/ESD/ISO image parsing (wimgapi/virtdisk)
  SchemeParser.cpp        Partition scheme XML parser
  DeployEngine.cpp        diskpart -> DISM -> bcdboot pipeline
OEM_Partition_Schemes.xml Sample partition schemes (card 3 runtime data)
```

## Startup flow

1. `Main.cpp` calls `WinPEMain()`.
2. `App.cpp` orchestrates:
   elevation -> DPI -> `UI_ConfigAll()` -> `UI_RegisterAllClasses()`
   -> create main window -> create child windows -> message loop -> cleanup.
3. `UI_ConfigAll()` precomputes every window layout/theme once.
4. `UI_RelayoutChildren()` recalculates child geometry only when the
   main window size or DPI changes, then applies and redraws.

## Build

Requirements:

- Visual Studio 2026 / MSVC toolset `v145`
- Windows 10/11 SDK
- Desktop development with C++ workload

Steps:

1. Open `WinPE.slnx` (or `WinPE.vcxproj`).
2. Build `Debug|x64` or `Release|x64`.
3. Run `x64\Debug\WinPE.exe` as administrator (the app requests elevation
   automatically; in WinPE it already runs as SYSTEM).

## Runtime data and safety

- `OEM_Partition_Schemes.xml` is auto-loaded by card 3 when the app's
  current directory is the project folder; otherwise use Browse.
- Real deployment erases the target disk. Use virtual/test machines or
  enable `Demo Mode` first.
