#include "DiskScanner.h" // 引入本模块头文件：DiskScanEntry 结构与对外接口声明

#include <stdio.h>      // 标准 I/O：提供 swprintf 宽字符格式化
#include <stdlib.h>     // 标准库：malloc/free 动态内存
#include <string.h>     // 字符串处理：memcpy/memset
#include <winioctl.h>   // Windows I/O 控制码：IOCTL_DISK_* / IOCTL_STORAGE_* 定义
#include <ntddstor.h>   // 存储设备头：STORAGE_DEVICE_DESCRIPTOR、STORAGE_BUS_TYPE 等
#include <ntdddisk.h>   // 磁盘设备头：DRIVE_LAYOUT_INFORMATION_EX、GET_LENGTH_INFORMATION 等

// 兜底常量：某些 SDK 未把 STORAGE_BUS_TYPE 枚举值暴露为宏时使用
#ifndef BusTypeScsi
#define BusTypeScsi  1   // SCSI 总线类型编号（兜底定义）
#endif
#ifndef BusTypeAtapi
#define BusTypeAtapi 2   // ATAPI 总线类型编号
#endif
#ifndef BusTypeAta
#define BusTypeAta   3   // ATA（并口 IDE）总线类型编号
#endif
#ifndef BusTypeUsb
#define BusTypeUsb   7   // USB 总线类型编号
#endif
#ifndef BusTypeRAID
#define BusTypeRAID  8   // RAID 总线类型编号
#endif
#ifndef BusTypeSas
#define BusTypeSas   10  // SAS 总线类型编号
#endif
#ifndef BusTypeSata
#define BusTypeSata  11  // SATA 总线类型编号
#endif
#ifndef BusTypeSd
#define BusTypeSd    12  // SD 卡总线类型编号
#endif
#ifndef BusTypeMmc
#define BusTypeMmc   13  // eMMC 总线类型编号
#endif
#ifndef BusTypeVirtual
#define BusTypeVirtual 14 // 虚拟磁盘总线类型编号
#endif
#ifndef BusTypeNvme
#define BusTypeNvme  17  // NVMe 总线类型编号
#endif

#define DISKSCAN_MAX_TRY        128 // 最多尝试打开 PhysicalDrive0..127，防止死循环
#define DISKSCAN_MIN_TARGET_BYTES (512ULL * 1024 * 1024) // 512MB：低于此容量标为"过小"，不推荐做目标盘

// GUID -> 宽字符串（GPT DiskId 指纹）
static void GuidToWchar(const GUID* g, wchar_t* out, int outLen)
{
    swprintf(out, outLen,                          // 按标准 GUID 文本格式输出
        L"%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X",
        g->Data1, g->Data2, g->Data3,              // 前三段直接展开
        g->Data4[0], g->Data4[1], g->Data4[2], g->Data4[3], // 第四段前两个字节
        g->Data4[4], g->Data4[5], g->Data4[6], g->Data4[7]); // 第四段后六个字节
}

// 打开物理盘句柄：优先 GENERIC_READ，失败时退化为 0 访问权限（IOCTL 仍可用）
static HANDLE OpenPhysicalDisk(DWORD diskIndex)
{
    wchar_t path[64];                             // 设备路径缓冲
    swprintf(path, 64, L"\\\\.\\PhysicalDrive%lu", diskIndex); // 构造 "\\.\PhysicalDriveN"

    HANDLE h = CreateFileW(path, GENERIC_READ,    // 先尝试以"可读"权限打开
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE)                // 可读打开失败（常见于无盘符/被占用的盘）
    {
        h = CreateFileW(path, 0,                  // 退化为 0 访问权限重开
            FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    }
    return h;                                     // 返回句柄（可能仍是 INVALID_HANDLE_VALUE）
}

// 把 STORAGE_DEVICE_DESCRIPTOR 中的 ANSI 字符串字段转为宽字符并去除尾部空白
static void AnsiFieldToWide(const BYTE* base, DWORD totalBytes, DWORD offset,
                            wchar_t* out, int outLen)
{
    out[0] = L'\0';                               // 先清空输出
    if (!outLen || !base || !offset || offset >= totalBytes) return; // 参数越界直接返回

    const char* s = (const char*)(base + offset); // 定位到字段实际起始字节
    const DWORD avail = totalBytes - offset;      // 该字段可安全读取的最大字节数
    int n = 0;                                    // 有效字符长度计数器
    while (n < (int)avail - 1 && n < 255 && s[n]) n++; // 统计到 NUL 为止的非空长度（上限 255）
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r')) n--; // 去掉尾部空白
    if (n <= 0) return;                           // 没有有效内容

    const int wn = MultiByteToWideChar(CP_ACP, 0, s, n, NULL, 0); // 查询 ANSI -> UTF-16 所需长度
    if (wn <= 0 || wn >= outLen) return;          // 转换失败或超出缓冲则放弃
    MultiByteToWideChar(CP_ACP, 0, s, n, out, wn);// 实际转换
    out[wn] = L'\0';                              // 补结尾符
}

// 查询分区布局：GPT/MBR 样式、分区数量、磁盘指纹（供后续部署校验）
static void FillPartitionInfo(DiskScanEntry* e, HANDLE h)
{
    // PARTITION_STYLE_MBR == 0，所以查询前必须先标记为 RAW，避免误判
    e->PartitionStyle = PARTITION_STYLE_RAW;      // 默认视为 RAW/未初始化
    BOOL gotLayout = FALSE;                       // 是否已成功取得布局

    DWORD need = 0, returned = 0;                 // 输出缓冲大小与实际返回字节数
    DRIVE_LAYOUT_INFORMATION_EX* lo = NULL;       // 新版扩展布局结构指针

    if (DeviceIoControl(h, IOCTL_DISK_GET_DRIVE_LAYOUT_EX, NULL, 0, // 先问缓冲区大小
                        NULL, 0, &need, NULL) && need)              // 返回需要的字节数
    {
        lo = (DRIVE_LAYOUT_INFORMATION_EX*)malloc(need);            // 按需分配
        if (lo &&
            DeviceIoControl(h, IOCTL_DISK_GET_DRIVE_LAYOUT_EX, NULL, 0, // 真正查询布局
                            lo, need, &returned, NULL))
        {
            e->PartitionStyle = lo->PartitionStyle;   // 记录 GPT/MBR
            e->PartitionCount = lo->PartitionCount;   // 记录分区数量

            if (lo->PartitionStyle == PARTITION_STYLE_GPT)  // GPT 盘
                GuidToWchar(&lo->Gpt.DiskId, e->Fingerprint, 80); // 用 GPT DiskId 做指纹
            else if (lo->PartitionStyle == PARTITION_STYLE_MBR) // MBR 盘
                swprintf(e->Fingerprint, 80, L"MBR-%08X", lo->Mbr.Signature); // 用 MBR 签名做指纹
            gotLayout = TRUE;                        // 标记布局已取得
        }
        else if (lo)                                 // 第二次调用失败：释放已分配内存
        {
            free(lo);
            lo = NULL;
        }
    }

    // 老式 MBR 布局查询（EX 不可用时的兜底）
    if (!gotLayout)                                 // EX 查询失败时才尝试老式接口
    {
        DWORD need2 = 0;                            // 老式结构所需大小
        if (DeviceIoControl(h, IOCTL_DISK_GET_DRIVE_LAYOUT, NULL, 0, // 问大小
                            NULL, 0, &need2, NULL) && need2)
        {
            DRIVE_LAYOUT_INFORMATION* lo2 =         // 老式布局结构指针
                (DRIVE_LAYOUT_INFORMATION*)malloc(need2);
            if (lo2 &&
                DeviceIoControl(h, IOCTL_DISK_GET_DRIVE_LAYOUT, NULL, 0, // 查询 MBR 布局
                                lo2, need2, &returned, NULL))
            {
                e->PartitionStyle = PARTITION_STYLE_MBR; // 老式接口只可能返回 MBR
                e->PartitionCount = lo2->PartitionCount; // 记录分区数
                swprintf(e->Fingerprint, 80, L"MBR-%08X", lo2->Signature); // MBR 签名指纹
                gotLayout = TRUE;                   // 标记成功
            }
            free(lo2);                              // 无论成败都释放
        }
    }

    if (lo) free(lo);                               // 释放扩展布局结构

    // 完全无布局（RAW/未初始化）或查询失败：用复合签名兜底
    if (!e->Fingerprint[0])                         // 指纹仍为空
    {
        swprintf(e->Fingerprint, 80, L"COMP-%ls-%ls-%I64u", // 型号+序列号+容量拼复合指纹
                 e->Model, e->Serial, e->SizeBytes);
    }
}

// 通过 IOCTL 填充单盘信息（身份 / 容量 / 总线 / 标记 / 分区布局）
static void FillDiskEntry(DiskScanEntry* e, HANDLE h)
{
    // 容量：先查容量（与稳定工作的探针顺序一致；部分驱动对后续 IOCTL 较敏感）
    DWORD returned = 0;                             // 返回字节数
    GET_LENGTH_INFORMATION li;                      // 容量信息结构
    memset(&li, 0, sizeof(li));                     // 清零
    if (DeviceIoControl(h, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0, // 查询物理介质长度
                        &li, sizeof(li), &returned, NULL))
    {
        e->SizeBytes = (ULONGLONG)li.Length.QuadPart; // 记录原始字节容量
    }

    // 身份：型号 / 序列号 / 总线类型 / 可移动标记
    STORAGE_PROPERTY_QUERY q;                       // 存储属性查询请求
    memset(&q, 0, sizeof(q));                       // 清零
    q.PropertyId = StorageDeviceProperty;           // 请求设备描述属性
    q.QueryType = PropertyStandardQuery;            // 标准查询方式

    STORAGE_DESCRIPTOR_HEADER hdr;                  // 描述符头部（含总长度）
    memset(&hdr, 0, sizeof(hdr));                   // 清零

    // 先只读描述符头部拿到真实长度；部分驱动不支持“NULL 缓冲区问长度”的两段式写法
    if (DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &q, sizeof(q), // 查询头部
                        &hdr, sizeof(hdr), &returned, NULL) &&
        hdr.Size >= sizeof(STORAGE_DEVICE_DESCRIPTOR) && // 长度至少覆盖完整描述符
        hdr.Size <= 1024 * 1024)                    // 长度上限保护，防止异常驱动返回超大值
    {
        BYTE* buf = (BYTE*)malloc(hdr.Size);        // 按实际长度分配
        if (buf &&
            DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &q, sizeof(q), // 完整查询
                            buf, hdr.Size, &returned, NULL))
        {
            STORAGE_DEVICE_DESCRIPTOR* d = (STORAGE_DEVICE_DESCRIPTOR*)buf; // 强转为描述符
            e->BusType = d->BusType;                // 总线类型
            if (d->RemovableMedia) e->Flags |= DISKSCAN_FLAG_REMOVABLE; // 可移动标记
            AnsiFieldToWide(buf, hdr.Size, d->ProductIdOffset, e->Model, 64); // 型号字段
            AnsiFieldToWide(buf, hdr.Size, d->SerialNumberOffset, e->Serial, 64); // 序列号字段
        }
        free(buf);                                  // 释放描述符缓冲
    }

    // 分区布局与磁盘指纹（依赖上面读到的型号/序列号/容量做复合兜底）
    FillPartitionInfo(e, h);

    // 只读标记（失败默认可写）
    BOOL writable = TRUE;                           // 默认假设可写
    if (DeviceIoControl(h, IOCTL_DISK_IS_WRITABLE, NULL, 0, // 查询是否可写
                        &writable, sizeof(writable), &returned, NULL))
    {
        if (!writable) e->Flags |= DISKSCAN_FLAG_READONLY; // 不可写则打只读标记
    }
}

int DiskScanAll(DiskScanEntry* out, int maxOut)
{
    if (!out || maxOut <= 0) return 0;              // 参数检查

    int count = 0;                                  // 已收集磁盘数
    for (DWORD n = 0; n < DISKSCAN_MAX_TRY && count < maxOut; n++) // 遍历物理盘号直到上限
    {
        HANDLE h = OpenPhysicalDisk(n);             // 尝试打开第 n 块盘
        if (h == INVALID_HANDLE_VALUE) continue;    // 打不开说明该盘号不存在或不可访问，跳过

        DiskScanEntry* e = &out[count];             // 输出数组当前空位
        memset(e, 0, sizeof(*e));                   // 清零
        e->DiskIndex = n;                           // 记录真实物理盘号
        FillDiskEntry(e, h);                        // 填充容量/型号/序列号/布局等
        CloseHandle(h);                             // 用完立即关闭

        // 打开成功但拿不到容量的设备（如部分虚拟/异常设备）不列入列表
        if (e->SizeBytes == 0) continue;            // 容量未知则跳过
        if (e->SizeBytes < DISKSCAN_MIN_TARGET_BYTES) // 小于 512MB
            e->Flags |= DISKSCAN_FLAG_TOO_SMALL;    // 标记过小
        if (!e->Model[0])                           // 型号为空
            lstrcpynW(e->Model, L"Unknown disk", 96); // 显示兜底文案
        count++;                                    // 有效盘计数 +1
    }
    return count;                                   // 返回扫描到的总盘数
}

const wchar_t* DiskScanBusText(DWORD busType)
{
    switch (busType)                                // 按总线枚举值分发
    {
    case BusTypeScsi:   return L"SCSI";             // SCSI
    case BusTypeAtapi:  return L"ATAPI";            // ATAPI（光驱等）
    case BusTypeAta:    return L"ATA";              // ATA/IDE
    case BusTypeUsb:    return L"USB";              // USB
    case BusTypeRAID:   return L"RAID";             // RAID
    case BusTypeSas:    return L"SAS";              // SAS
    case BusTypeSata:   return L"SATA";             // SATA
    case BusTypeSd:     return L"SD";               // SD 卡
    case BusTypeMmc:    return L"MMC";              // eMMC
    case BusTypeVirtual: return L"Virtual";         // 虚拟磁盘（VM 常见）
    case BusTypeNvme:   return L"NVMe";             // NVMe
    default:            return L"Disk";             // 未知类型兜底
    }
}
