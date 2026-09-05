#pragma once // 预处理指令：确保本头文件在同一个编译单元中只被包含一次
#include <windows.h> // 引入 Windows API 核心头文件：提供 DWORD、ULONGLONG、WCHAR、BOOL 等类型
#include "SchemeParser.h" // 引入分区方案结构（DeployJob 内嵌 SchemeInfo 作为成员）

// ==================== 部署引擎（分区 -> 应用镜像 -> 引导） ====================
// 说明：执行完整部署流水线，只做"事"，不碰 UI：
//   1. 依据分区方案生成并执行 diskpart 脚本（目标盘 clean + GPT 分区）
//   2. 在目标盘上回读 ESP/System 盘符
//   3. dism /Apply-Image 释放 install.wim/esd（.iso 自动临时挂载）
//   4. bcdboot 写入 UEFI/BIOS 引导
// 运行前务必由 UI 做"确认 + 非当前系统盘"校验。

// 一次完整部署所需的全部输入参数（由 UI 三张卡片收集后填充）
typedef struct DeployJob
{
    DWORD     DiskIndex;        // 目标物理盘号（\\.\PhysicalDriveN 的 N）
    ULONGLONG DiskBytes;        // 目标盘容量（字节；用于固定分区总和不超过盘容量的预检）
    WCHAR     DiskModel[96];    // 目标盘型号（日志与确认用）
    WCHAR     DiskSerial[64];   // 目标盘序列号（日志与确认用）
    WCHAR     Fingerprint[80];  // 目标盘指纹（GPT DiskId / MBR 签名，用于交叉确认）

    WCHAR     ImagePath[MAX_PATH]; // 镜像源路径：.wim / .esd / .iso
    int       ImageIndex;          // 要释放的 WIM 映像索引（从 1 开始）
    BOOL      DemoMode;            // TRUE = 演示：只生成脚本/模拟，不写盘
    SchemeInfo Scheme;             // 已选分区方案（定义分区顺序、大小、标签）
} DeployJob;

// 日志回调：部署引擎把过程信息逐行交给调用方（UI 线程转发到日志窗口/文件）
typedef void (*DeployLogFn)(const wchar_t* line, void* user);
// 进度回调：部署引擎把 0-100 的总体进度交给调用方（UI 线程更新进度条）
typedef void (*DeployProgressFn)(int pct, void* user);

// 执行完整部署。返回 0=成功；非 0=失败（错误原因会通过 log 输出）。
int DeployRun(const DeployJob* job, DeployLogFn log, DeployProgressFn progress,
              void* user);

// 安全校验：当前程序所在卷是否在目标盘上（防止把正在运行的 PE/系统盘清掉）
BOOL DeployIsCurrentBootDisk(DWORD diskIndex);
