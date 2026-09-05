# CustomPE - 自定义 Windows PE 构建工具

**作者：** AlexMartin · **最后更新：** 2026-08-08

构建用于 OEM 部署的可启动 WinPE ISO：注入硬件驱动、添加可选组件，并通过普通脚本控制部署行为 —— 无需重建 boot.wim。

> **核心思路** —— ISO 分两层：
> - `boot.wim`（只构建一次）：WinPE 环境，包含组件、驱动和注册表设置。
> - `AutoRun.cmd`（可随时编辑）：ISO 根目录的部署入口。改部署行为不用重新打包镜像。

## 目录

1. [环境要求](#1-环境要求)
2. [快速开始](#2-快速开始)
3. [自定义](#3-自定义)
4. [构建配置](#4-构建配置)
5. [启动流程](#5-启动流程)
6. [构建步骤](#6-构建步骤)
7. [测试](#7-测试)
8. [常见问题](#8-常见问题)
9. [文件清单](#9-文件清单)

---

## 1. 环境要求

### 软件

| 组件 | 版本 | 用途 |
|------|------|------|
| Windows ADK | 10.0.26100.x（Win11 24H2） | 部署工具 |
| WinPE 加载项 | amd64，与 ADK 匹配 | WinPE 文件 |
| 操作系统 | Windows 10/11 x64 | 构建主机 |
| 权限 | 管理员 | DISM 需要 |

> ADK 自动检测（依次探测 `C:\Program Files (x86)\Windows Kits\`、`C:\Program Files\Windows Kits\`、`D:\Windows Kits\`、`E:\Windows Kits\`）。若安装位置特殊，可在 `Build-WinPE.conf` 中用 `CustomPE_OCSPATH` 指定 `WinPE_OCs` 目录。

### ADK 安装

- Windows ADK：https://learn.microsoft.com/zh-cn/windows-hardware/get-started/adk-install
- WinPE 加载项：https://learn.microsoft.com/zh-cn/windows-hardware/get-started/adk-install#winpe

### 驱动（可选）

驱动包放入 `Build-WinPE.conf` 中 `CustomPE_DRVREPO` 指定的目录（默认 `E:\DriversRepo\`）。构建脚本会递归扫描该目录，自动注入每个直接包含 `.inf` 驱动包的文件夹——不依赖固定文件夹名，支持多层嵌套：

```
E:\DriversRepo\
├── IntelRST\         # Intel RST/VMD 存储驱动
└── IntelSerialIO\    # Intel Serial IO 驱动
```

---

## 2. 快速开始

1. **打开 ADK 环境**：开始菜单 > Windows Kits > 部署和映像工具环境，右键以管理员身份运行。
2. **在项目目录运行构建**：
   ```cmd
   cd /d E:\WinPE
   Build-WinPE.cmd
   ```
   `Build-WinPE.cmd` 是纯入口，实际构建由唯一的实现 `Build-WinPE.ps1` 完成（也可直接运行：`powershell -ExecutionPolicy Bypass -File .\Build-WinPE.ps1`）
3. **获取 ISO**：`E:\WinPE-Build\ISO\%CustomPE_NAME%.iso`（ISO 名称来自 `Build-WinPE.conf`）
4. **在虚拟机中测试**：在 Hyper-V、VMware 或 VirtualBox 中启动 ISO。

> 每次构建都会删除并重建工作目录 `E:\WinPE-Build`，需要保留的东西请放到别处。

---

## 3. 自定义

### 3.1 部署逻辑（AutoRun.cmd）

ISO 根目录的 `AutoRun.cmd` 是部署入口。编辑它即可加入自己的逻辑，无需重建 boot.wim：

```cmd
call "%SRC%\%CustomPE_NAME%\Scripts\Test_Components.cmd"
```

### 3.2 脚本和工具

- `%CustomPE_NAME%\Scripts\` 下的脚本和 `%CustomPE_NAME%\Tools\` 下的工具会自动打进 ISO。

### 3.3 WinPE 组件

组件列表在 `Build-WinPE.conf`（`COMPONENTS_51` ... `COMPONENTS_54`），共 15 个核心包 + 6 个字体/语言包：

```
5.1 核心管理：  WMI、StorageWMI、Scripting、HTA
5.2 运行时：    .NET Framework、PowerShell
5.3 部署工具：  EnhancedStorage、SecureStartup、WDS-Tools、PlatformId、MDAC、DismCmdlets、Dot3Svc、RNDIS、SecureBootCmdlets
5.4 字体：      Legacy、ZH-CN/HK/TW、JA-JP、KO-KR
```

---

## 4. 构建配置

### 4.1 全局配置（Build-WinPE.conf）

项目级设置集中在项目根目录的单个文件 `Build-WinPE.conf` 中：

```ini
CustomPE_NAME=CustomPE
CustomPE_AUTHOR=AlexMartin
CustomPE_VERSION=1.0
CustomPE_WORKDIR=E:\WinPE-Build
```

- `CustomPE_NAME` 是项目名称：它决定 ISO 文件名、打进 ISO 的工具文件夹名，以及 `startnet.cmd`、`AutoRun.cmd`、`Test_Components.cmd` 的横幅文字。
- 改项目名只需编辑 `Build-WinPE.conf` 一个文件，无需改任何脚本。构建会优先使用以 `CustomPE_NAME` 命名的源文件夹，不存在时回退到 `CustomPE` 文件夹；日志目录同样跟随项目名（PE/媒体日志统一为 `<CustomPE_NAME>\Logs`，构建日志随实际使用的源文件夹）。
- 配置文件会自动拷贝到 ISO 根目录，PE 启动时读取。
- `CustomPE_WORKDIR` 是构建工作目录（默认 `E:\WinPE-Build\`），ISO 输出到 `%CustomPE_WORKDIR%\ISO\%CustomPE_NAME%.iso`。
- ISO 输出路径会自动跟随项目名，任何地方都没有写死的 ISO 路径。

### 4.2 构建脚本变量

构建行为由 `Build-WinPE.conf` 控制（两个构建脚本都读取它）：

| 配置项 | 默认值 | 说明 |
|--------|--------|------|
| `CustomPE_NAME` | `CustomPE` | 项目名；用于 ISO 文件名和媒体目录名 |
| `CustomPE_WORKDIR` | `E:\WinPE-Build` | 构建工作目录，每次构建重建 |
| `CustomPE_DRVREPO` | `E:\DriversRepo` | 驱动源目录（递归扫描） |
| `CustomPE_OCSPATH` | （自动） | `WinPE_OCs` 目录；留空则从 ADK 自动检测 |
| ISO 输出 | `%CustomPE_WORKDIR%\ISO\%CustomPE_NAME%.iso` | 由以上配置推导 |

### 构建前检查

- [ ] 已安装 Windows ADK + WinPE 加载项
- [ ] 已安装 ADK（自动检测；特殊位置可在 `Build-WinPE.conf` 设置 `CustomPE_OCSPATH`）
- [ ] 驱动（如有）已放入 `CustomPE_DRVREPO` 指定目录（默认 `E:\DriversRepo\`）

---

## 5. 启动流程

```
开机
  └─ UEFI/BIOS 加载 bootx64.efi 或 bootmgr
       └─ BCD 将 boot.wim 加载到内存
            └─ startnet.cmd（boot.wim 内部）
                 ├─ wpeinit
                 ├─ 遍历 C-Z 盘查找 AutoRun.cmd
                 │    ├─ 找到      -> 执行
                 │    └─ 未找到    -> 显示 [ERROR]，进入 CMD
                 └─ cmd /k

AutoRun.cmd（U盘/ISO 根目录）：
  ├─ 检测源盘符 (%~d0)
  └─ 执行部署脚本
```

分层的意义：`boot.wim` 里的内容在构建时固定，`AutoRun.cmd` 则可随时编辑 —— 这就是"改部署不用重建"的关键。

---

## 6. 构建步骤

| # | 步骤 | 说明 |
|---|------|------|
| 1 | 清理 | DISM 清理；删除旧 `WORK_DIR` |
| 2 | 基础 | `copype.cmd amd64` 生成基础 WinPE |
| 2b | 结构 | 语言目录移入 `Boot\`（OEM 布局） |
| 3 | 挂载 | DISM 挂载 `boot.wim` |
| 4 | 组件 | 添加 21 个可选包（15 核心 + 6 字体） |
| 5 | 驱动 | 注入驱动（动态扫描） |
| 6 | 注册表 | 应用 8 项离线注册表设置 |
| 7 | startnet.cmd | 注入自定义启动脚本 |
| 8 | 提交 | 组件清理；卸载并提交 |
| 9 | ISO | 用 `MakeWinPEMedia` 生成 ISO |

### 离线注册表设置

| 键 | 值 |
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

## 7. 测试

启动 ISO 后运行：

```cmd
X:\> %CustomPE_NAME%\Scripts\Test_Components.cmd
```

脚本会验证 WMI、VBScript、HTA、.NET、PowerShell、DISM、网络、注册表设置、磁盘枚举，以及注入的可选组件（WDS-Tools、MDAC、Dot3Svc、RNDIS、EnhancedStorage、SecureStartup、PlatformID、多语言字体包），共 32 项检查，最后输出 Pass / Fail / Skip 汇总。

### 日志

日志路径跟随媒体文件夹名 `<CustomPE_NAME>`，不再硬编码 `CustomPE\Logs`；所有脚本都**实时**写入控制台与日志文件：

- 构建机：`<项目根>\<CustomPE_NAME>\Logs` → `Build.log`（唯一构建日志，由 `Build-WinPE.ps1` 写入；源码没有以 `CustomPE_NAME` 命名的文件夹时回退到 `<项目根>\CustomPE\Logs`，都缺失时回退 `<项目根>\Logs`）
- PE 实体机运行时：`<启动盘>\<CustomPE_NAME>\Logs` → `Startnet.log`、`wpeinit.log`、`AutoRun.log`、`Test_Components.log`（启动盘只读时回退到 RAM 盘 `X:`）

每次“进入”（PE 启动或重新构建）时自动清空该目录，本次会话内所有 CMD 的日志结果都会累积存放在这里。

---


## 8. 常见问题

| 现象 | 可能原因 | 解决方法 |
|------|----------|----------|
| 提示需要管理员权限 | 非管理员运行 | 以管理员身份运行 |
| 找不到 `boot.wim` | `copype.cmd` 失败 | 检查 ADK 与 WinPE 加载项版本一致 |
| 挂载失败 | `boot.wim` 缺失 | 第 2b 步不能重命名 `sources` |
| 找不到 `WinPE_OCs` | 未安装 WinPE 加载项 | 安装 WinPE 加载项 |
| `AutoRun.cmd` 未执行 | 不在 ISO/U 盘根目录 | 放到根目录 |

---

## 9. 文件清单

```
E:\WinPE\
├── Build-WinPE.cmd / .ps1      # 构建脚本
├── AutoRun.cmd                 # 外部部署接口
├── Build-WinPE.conf                  # 全局配置 - 改 CustomPE_NAME 即可改名
├── README.md                   # 本文档（英文）
├── README.zh-CN.md             # 本文档（中文）
├── Scripts\
│   └── startnet.cmd            # 注入 boot.wim 的启动脚本
└── CustomPE\                   # 拷贝到 ISO 中，文件夹名取 <CustomPE_NAME>
    ├── Scripts\
    │   └── Test_Components.cmd # 组件验证（在 WinPE 中运行）
    ├── Logs\                   # 实时日志，首次进入自动清空
    ├── Tools\                  # 工具目录
    └── Drivers\                # 驱动目录
```

> 上图的 `CustomPE` 是默认源码文件夹。改名时把源码文件夹与 `CustomPE_NAME` 保持一致（或让构建自动回退），日志统一写入 `<CustomPE_NAME>\Logs`。

## 许可证

MIT
