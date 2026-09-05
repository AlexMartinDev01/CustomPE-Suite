#pragma once // 预处理指令：确保本头文件在同一个编译单元中只被包含一次，防止结构体重复定义
#include <windows.h> // 引入 Windows API 核心头文件：提供 BOOL、WCHAR、MAX_PATH 等类型与常量

// ==================== 分区方案解析模块 ====================
// 说明：扫描文件夹下的 *.xml 分区方案（xNappHddSetting 格式），
//       解析 ESP/MSR/OS/WinRE 等分区的类型、大小、文件系统、标签。

#define SCHEME_MAX_PARTS 8   // 单个分区方案最多允许的分区数量（ESP/MSR/OS/WinRE 四种键名 + 余量）
#define SCHEME_MAX_ITEMS 32  // 单个 XML 文件（合并格式）最多可解析出的独立方案数量

// 单个分区（Partition）的数据结构：描述分区表中一个分区的全部属性
typedef struct SchemePart
{
    wchar_t   Key[16];   // 分区键名：ESP / MSR / OS / WinRE（决定该分区在部署中的角色）
    wchar_t   Label[32]; // 标签（如 System / Recovery）：用作卷标显示或后续卷匹配
    wchar_t   Type[16];  // 分区类型关键字：efi / msr / primary（XML 中的 <Type>）
    wchar_t   Fs[16];    // 文件系统：FAT32 / NTFS，可为空（MSR 等无文件系统分区留空）
    long long SizeMB;    // 分区大小（单位 MB）；-1 表示"占满剩余空间"（XML 中 <Size> 为空）
    wchar_t   Id[80];    // GPT 分区类型 GUID（如 WinRE 的 de94bba4-...），用于 set id 命令
    ULONGLONG Attributes;// GPT 分区属性位掩码（如 0x8000000000000001），用于 gpt attributes 命令
    wchar_t   Letter[8]; // 期望盘符（可空；部署时仅 ESP/System 实际分配盘符）
} SchemePart;

// 一个完整分区方案（Scheme）的数据结构：方案名 + 源文件路径 + 分区数组
typedef struct SchemeInfo
{
    wchar_t    Name[160];          // 方案名（XML 文件名去扩展名，合并格式则取块内 <Name>）
    wchar_t    File[MAX_PATH];     // 该方案所在 XML 文件的完整路径（部署/回读时用于定位）
    int        PartCount;          // 实际解析出的分区数量（<= SCHEME_MAX_PARTS）
    SchemePart Parts[SCHEME_MAX_PARTS]; // 分区明细数组，下标 0..PartCount-1
} SchemeInfo;

// 解析单个 XML 文件里的 1..N 个方案（合并文件格式 <PartitionScheme> 块，
// 也兼容传统单方案文件）；返回解析成功的方案数
int SchemeParseFileEx(const wchar_t* path, SchemeInfo* out, int maxOut);

// 把方案格式化为多行明细文本（供卡片③展示）
void SchemeFormatDetail(const SchemeInfo* s, wchar_t* out, int outLen);
