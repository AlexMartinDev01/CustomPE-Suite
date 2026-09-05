# CustomPE Suite（整包备份，可交给任何人直接使用）

这是一个完整的 CustomPE 工程备份：包含 PE 构建工程、全部驱动、
部署 UI 源码和辅助工具源码。所有路径都按本文件夹相对解析，
拷贝到任意电脑、任意盘符都能直接运行。

## 文件夹内容

| 名称 | 作用 |
| --- | --- |
| CustomPE | PE 构建工程：Build-WinPE.ps1 / .cmd / .conf + PE 内容树 |
| DriversRepo | 注入 PE 的驱动仓库（Intel RST 8–15 代 + Serial IO） |
| WinPE | 部署 UI 源码（Visual Studio 工程） |
| MinimizeWindow | 最小化辅助工具源码 |
| Release | 已经打好的可直接使用的 PE ISO |
| Build-PE.cmd | 双击即可重新打包 PE（自动请求管理员权限） |

## 最省事的使用方式（不重新打包）

直接用 `Release\CustomPE.iso`：

- 写进 U 盘 / Ventoy / 虚拟机启动；
- 启动后进入 PE，桌面/自动运行的是部署 UI。

## 需要重新打包时

电脑前置要求（一次性）：

- Windows 10/11 x64；
- 安装 Windows ADK + WinPE 加载项（10.0.26100 对应 Win11 24H2）；
- 有管理员权限。

步骤：

1. 双击 `Build-PE.cmd`；
2. 弹 UAC 时点“是”；
3. 等待构建完成；
4. 新 ISO 输出在 `Build-WorkDir\ISO\CustomPE.iso`。

## 路径为什么不需要改

- 驱动仓库：自动找本文件夹下的 `DriversRepo`；
- 构建工作目录：自动建在本文件夹下的 `Build-WorkDir`（可整个删除，不影响源码）；
- ADK：构建脚本自动探测常见安装位置。

如果某台电脑 ADK 装在不常见位置，编辑
`CustomPE\Build-WinPE.ps1` 顶部参数或直接给构建命令传 `-ADKPath`。
