#pragma once // 预处理指令：确保本头文件在同一个编译单元中只被包含一次，防止结构体重复定义
#include <windows.h> // 引入 Windows API 核心头文件：提供 DWORD、WCHAR、BOOL 等基础类型

// ==================== 硬盘扫描模块 ====================
// 说明：通过 \\.\PhysicalDriveN 枚举真实物理硬盘（USB/SATA/NVMe 等），
//       查询盘号、容量、总线类型、型号、序列号与只读/可移动标记。

#define DISKSCAN_MAX_DISKS 32 // 一次扫描最多返回的物理硬盘数量（数组上限）

// 单盘标记位（Flags 字段的位掩码定义）
#define DISKSCAN_FLAG_READONLY   0x0001  // 只读盘（IOCTL_DISK_IS_WRITABLE 报告不可写）
#define DISKSCAN_FLAG_REMOVABLE  0x0002  // 可移动介质（USB/SD 等，来自存储描述符的 RemovableMedia）
#define DISKSCAN_FLAG_TOO_SMALL  0x0004  // 容量过小（< 512MB，通常不作为目标盘）

// 单个物理硬盘的扫描结果结构
typedef struct DiskScanEntry
{
    DWORD     DiskIndex;      // 物理盘号（\\.\PhysicalDriveN 的 N，也是后续部署的目标标识）
    ULONGLONG SizeBytes;      // 原始容量（字节），0 = 未知（打开成功但拿不到容量的设备被剔除）
    DWORD     BusType;        // 总线类型：STORAGE_BUS_TYPE 枚举值（USB/SATA/NVMe/SCSI...）
    DWORD     Flags;          // 组合标记位：DISKSCAN_FLAG_READONLY / REMOVABLE / TOO_SMALL
    DWORD     PartitionStyle; // 分区样式：PARTITION_STYLE_GPT / MBR / RAW（未初始化）
    DWORD     PartitionCount; // 当前分区数量（供 UI 展示与后续校验）
    WCHAR     Model[96];      // 型号（已去除尾部空白；缺失时兜底为 "Unknown disk"）
    WCHAR     Serial[64];     // 序列号（可能为空，部分设备不返回）
    WCHAR     Fingerprint[80]; // 磁盘指纹：GPT DiskId / MBR-XXXXXXXX / COMP-复合签名（用于部署前确认同一块盘）
} DiskScanEntry;

// 扫描所有物理硬盘；成功返回数量，无盘/失败返回 0
int DiskScanAll(DiskScanEntry* out, int maxOut);

// 总线类型 -> 显示文本（NVMe / SATA / USB ...），未知返回 L"Disk"
const wchar_t* DiskScanBusText(DWORD busType);
