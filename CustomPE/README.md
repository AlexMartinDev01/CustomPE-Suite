# CustomPE - Custom Windows PE Builder

**Author:** AlexMartin · **Last updated:** 2026-08-08

Build a bootable Windows PE (WinPE) ISO for OEM-style deployment: inject hardware drivers, add optional components, and control deployment with plain scripts - no boot.wim rebuild required.

> **Key idea** - the ISO is split into two layers:
> - `boot.wim` (built once) - the WinPE environment: components, drivers, and registry settings.
> - `AutoRun.cmd` (edit freely) - the deployment entry point on the ISO root. Change deployment behavior anytime without rebuilding the image.

## Contents

1. [Prerequisites](#1-prerequisites)
2. [Quick Start](#2-quick-start)
3. [Customization](#3-customization)
4. [Build Configuration](#4-build-configuration)
5. [Boot Chain](#5-boot-chain)
6. [Build Steps](#6-build-steps)
7. [Testing](#7-testing)
8. [Troubleshooting](#8-troubleshooting)
9. [File Map](#9-file-map)

---

## 1. Prerequisites

### Software

| Component | Version | Purpose |
|-----------|---------|---------|
| Windows ADK | 10.0.26100.x (Win11 24H2) | Deployment tools |
| WinPE Add-on | amd64, matching ADK | WinPE files |
| OS | Windows 10/11 x64 | Build host |
| Privileges | Administrator | Required by DISM |

> The ADK is auto-detected (probes `C:\Program Files (x86)\Windows Kits\`, `C:\Program Files\Windows Kits\`, `D:\Windows Kits\`, `E:\Windows Kits\`). Set `CustomPE_OCSPATH` in `Build-WinPE.conf` to override the `WinPE_OCs` folder if your installation is elsewhere.

### ADK Installation

- Windows ADK: https://learn.microsoft.com/en-us/windows-hardware/get-started/adk-install
- WinPE Add-on: https://learn.microsoft.com/en-us/windows-hardware/get-started/adk-install#winpe

### Drivers (Optional)

Put driver packages in the folder set by `CustomPE_DRVREPO` in `Build-WinPE.conf` (default `E:\DriversRepo\`). The build scans this folder recursively and injects every folder that directly contains `.inf` packages - no fixed folder names; nested subfolders are supported:

```
E:\DriversRepo\
├── IntelRST\         # Intel RST/VMD storage drivers
└── IntelSerialIO\    # Intel Serial IO drivers
```

---

## 2. Quick Start

1. **Open the ADK environment** - Start Menu > Windows Kits > Deployment and Imaging Tools Environment, run as Administrator.
2. **Run the build** from the project folder:
   ```cmd
   cd /d E:\WinPE
   Build-WinPE.cmd
   ```
   `Build-WinPE.cmd` is a thin launcher; it delegates to the single implementation `Build-WinPE.ps1` (run it directly with `powershell -ExecutionPolicy Bypass -File .\Build-WinPE.ps1`)
3. **Get the ISO** - `E:\WinPE-Build\ISO\%CustomPE_NAME%.iso` (ISO name comes from `Build-WinPE.conf`)
4. **Test in a VM** - boot the ISO in Hyper-V, VMware, or VirtualBox.

> The build deletes and recreates the workspace `E:\WinPE-Build` on every run, so keep anything you need elsewhere.

---

## 3. Customization

### 3.1 Deployment Logic (AutoRun.cmd)

`AutoRun.cmd` on the ISO root is the deployment entry point. Edit it to add your own logic - no boot.wim rebuild needed:

```cmd
call "%SRC%\%CustomPE_NAME%\Scripts\Test_Components.cmd"
```

### 3.2 Scripts and Tools

- Scripts in `%CustomPE_NAME%\Scripts\` and tools in `%CustomPE_NAME%\Tools\` are copied into the ISO automatically.

### 3.3 WinPE Components

The component list lives in `Build-WinPE.conf` (`COMPONENTS_51` ... `COMPONENTS_54`) - 15 core packages plus 6 font/language packages:

```
5.1 Core management:  WMI, StorageWMI, Scripting, HTA
5.2 Runtime:          .NET Framework, PowerShell
5.3 Deployment tools: EnhancedStorage, SecureStartup, WDS-Tools, PlatformId, MDAC, DismCmdlets, Dot3Svc, RNDIS, SecureBootCmdlets
5.4 Fonts:            Legacy, ZH-CN/HK/TW, JA-JP, KO-KR
```

---

## 4. Build Configuration

### 4.1 Global Configuration (`Build-WinPE.conf`)

Project-wide settings live in a single file, `Build-WinPE.conf`, in the project root:

```ini
CustomPE_NAME=CustomPE
CustomPE_AUTHOR=AlexMartin
CustomPE_VERSION=1.0
CustomPE_WORKDIR=E:\WinPE-Build
```

- `CustomPE_NAME` is the project name: it controls the ISO file name, the tools folder copied into the ISO, and the banner text in `startnet.cmd`, `AutoRun.cmd`, and `Test_Components.cmd`.
- Rename the project by editing only `Build-WinPE.conf` - no script changes needed. The build uses the folder named after `CustomPE_NAME` (falling back to the `CustomPE` folder if that name is absent), and log paths follow the same name too: PE/media logs go to `<CustomPE_NAME>\Logs` and the build log follows the source folder actually used.
- The config file is copied into the ISO root automatically and read by the PE at boot.
- `CustomPE_WORKDIR` is the build workspace (default `E:\WinPE-Build\`); the ISO is written to `%CustomPE_WORKDIR%\ISO\%CustomPE_NAME%.iso`.
- The ISO output path follows the project name automatically - no hardcoded ISO path anywhere.

### 4.2 Build Script Variables

Build behavior is controlled by `Build-WinPE.conf` (read by both build scripts):

| Setting | Default | Description |
|---------|---------|-------------|
| `CustomPE_NAME` | `CustomPE` | Project name; used for the ISO file and the media folder |
| `CustomPE_WORKDIR` | `E:\WinPE-Build` | Build workspace; recreated on every build |
| `CustomPE_DRVREPO` | `E:\DriversRepo` | Driver source folder (scanned recursively) |
| `CustomPE_OCSPATH` | *(auto)* | `WinPE_OCs` folder; empty = auto-detect from the ADK |
| ISO output | `%CustomPE_WORKDIR%\ISO\%CustomPE_NAME%.iso` | Derived from the settings above |

### Before Building

- [ ] Windows ADK + WinPE Add-on installed
- [ ] ADK installed (auto-detected; or set `CustomPE_OCSPATH` in `Build-WinPE.conf`)
- [ ] Drivers (if any) placed in the folder set by `CustomPE_DRVREPO` (default `E:\DriversRepo\`)

---

## 5. Boot Chain

```
Power on
  └─ UEFI/BIOS loads bootx64.efi or bootmgr
       └─ BCD loads boot.wim into RAM
            └─ startnet.cmd (inside boot.wim)
                 ├─ wpeinit
                 ├─ Scan drives C-Z for AutoRun.cmd
                 │    ├─ Found      -> execute it
                 │    └─ Not found  -> show [ERROR], drop to CMD
                 └─ cmd /k

AutoRun.cmd (on USB/ISO root):
  ├─ Detect source drive (%~d0)
  └─ Run your deployment scripts
```

The split is deliberate: everything inside `boot.wim` is fixed at build time, while `AutoRun.cmd` can be edited at any time. That is what makes deployment changes possible without rebuilding.

---

## 6. Build Steps

| # | Step | What it does |
|---|------|--------------|
| 1 | Clean | DISM cleanup; delete old `WORK_DIR` |
| 2 | Base | `copype.cmd amd64` generates the base WinPE |
| 2b | Structure | Move locale folders into `Boot\` (OEM layout) |
| 3 | Mount | DISM mount `boot.wim` |
| 4 | Components | Add 21 optional packages (15 core + 6 fonts) |
| 5 | Drivers | Inject drivers (dynamic scan) |
| 6 | Registry | Apply 8 offline registry settings |
| 7 | startnet.cmd | Inject the custom boot script |
| 8 | Commit | Component cleanup; unmount with commit |
| 9 | ISO | Build the ISO with `MakeWinPEMedia` |

### Offline Registry Settings

| Key | Value |
|-----|-------|
| PowerShell ExecutionPolicy | Unrestricted |
| MS Shell Dlg | Microsoft YaHei UI |
| ThemeActive | 1 |
| Microsoft YaHei (TrueType) | msyh.ttc |
| Microsoft YaHei Bold (TrueType) | msyhbd.ttc |
| Segoe UI (TrueType) | segoeui.ttf |
| ICU DefaultLanguage | en-us |
| ICU DefaultLanguageGroup | en-us |

---

## 7. Testing

Boot the ISO in a VM and run:

```cmd
X:\> %CustomPE_NAME%\Scripts\Test_Components.cmd
```

The script verifies WMI, VBScript, HTA, .NET, PowerShell, DISM, networking, registry settings, disk enumeration, and the injected optional components (WDS-Tools, MDAC, Dot3Svc, RNDIS, EnhancedStorage, SecureStartup, PlatformID, localized font packages) - 32 checks in total - then prints a Pass / Fail / Skip summary.

### Logging

Log paths follow the media folder name `<CustomPE_NAME>` instead of a hardcoded `CustomPE`:

- Build machine: `<project>\<CustomPE_NAME>\Logs` - `Build.log` (single build log written by `Build-WinPE.ps1`; falls back to `<project>\CustomPE\Logs`, or `<project>\Logs` when no content folder exists)
- PE runtime: `<boot drive>\<CustomPE_NAME>\Logs` - `Startnet.log`, `wpeinit.log`, `AutoRun.log`, `Test_Components.log` (falls back to the RAM disk `X:` if the media is read-only)

The folder is auto-cleared on the first entry of each session (PE boot or a new build), then all CMD logs of that session accumulate there.

---


## 8. Troubleshooting

| Symptom | Likely cause | Fix |
|---------|--------------|-----|
| "Administrator privileges required" | Not running as admin | Run as Administrator |
| `boot.wim` not found | `copype.cmd` failed | Check ADK and WinPE Add-on versions match |
| Mount failed | Missing `boot.wim` | Step 2b must not rename `sources` |
| `WinPE_OCs` not found | WinPE Add-on missing | Install the WinPE Add-on |
| `AutoRun.cmd` not executed | Not on the ISO/USB root | Put it in the root directory |

---

## 9. File Map

```
E:\WinPE\
├── Build-WinPE.cmd / .ps1      # Build scripts
├── AutoRun.cmd                 # External deployment interface
├── Build-WinPE.conf                  # Global config - edit CustomPE_NAME to rename
├── README.md                   # This document (English)
├── README.zh-CN.md             # This document (Chinese)
├── Scripts\
│   └── startnet.cmd            # Boot script injected into boot.wim
└── CustomPE\                   # Copied to the ISO under <CustomPE_NAME>\
    ├── Scripts\
    │   └── Test_Components.cmd # Component verification (run in WinPE)
    ├── Logs\                   # Real-time logs, auto-cleared on first entry
    ├── Tools\                  # Your tools
    └── Drivers\                # Your drivers
```

> The `CustomPE` folder above is the default source folder. If you rename the project, keep `CustomPE_NAME` and the source folder name in sync (or let the build fall back), and all logs are written to `<CustomPE_NAME>\Logs`.

## License

MIT
