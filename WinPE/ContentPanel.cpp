#include "ContentPanel.h"      // 引入本模块头：面板注册接口与部署任务收集接口声明
#include "GlobalResources.h"   // 引入全局资源：画刷池 g_BrushPool、全局字体、布局参数
#include "UIConfig/UIConfig.h" // 引入界面配置：内容面板内部控件布局参数
#include "System/System.h"     // 引入系统层：GetWindowScaleFactor（统一 DPI 缩放）
#include "BottomPanel.h"       // 引入底部面板头：用于 BottomPanelSetStep/BottomPanelLog
#include "Utils/DeployEngine.h" // 引入部署引擎头：DeployJob 结构（用于收集部署参数）
#include "Utils/DiskScanner.h"  // 引入硬盘扫描头：DiskScanEntry/DiskScanAll
#include "Utils/ImageParser.h"  // 引入镜像解析头：ImgEntry/ImgParseFile
#include "Utils/SchemeParser.h" // 引入方案解析头：SchemeInfo/SchemeParseFileEx
#include <windows.h>            // Windows API 核心头
#include <commdlg.h>            // GetOpenFileNameW 文件选择对话框
#include <cwchar>               // swprintf
#include <wchar.h>              // wcsstr（型号容量判断）
#pragma comment(lib, "comdlg32.lib") // 链接通用对话框库
#pragma comment(lib, "shell32.lib")  // 链接 Shell 库
#pragma comment(lib, "ole32.lib")    // 链接 COM/OLE 库

// ==================== 三张卡片 ====================
// 说明：ContentPanel 作为容器，X 方向均分三张卡片；
//       卡片①目标硬盘、卡片②系统镜像、卡片③分区方案（交互与 HTML 原型一致）。
#define ID_CARD_1  3001 // 卡片① 窗口 ID：目标硬盘
#define ID_CARD_2  3002 // 卡片② 窗口 ID：系统镜像
#define ID_CARD_3  3003 // 卡片③ 窗口 ID：分区方案

// 卡片①控件
#define IDC1_BTN_SCAN  1101 // "Scan Disks" 按钮 ID
#define IDC1_LIST      1102 // 扫描结果只读列表 ID
#define IDC1_COMBO     1103 // 磁盘选择下拉框 ID
// 卡片②控件
#define IDC2_BTN_BROWSE 1201 // "Browse" 按钮 ID
#define IDC2_EDIT_PATH  1202 // 镜像路径只读输入框 ID
#define IDC2_LIST       1203 // 版本列表只读框 ID
#define IDC2_COMBO      1204 // 版本选择下拉框 ID
// 卡片③控件
#define IDC3_BTN_BROWSE 1301 // "Browse..." 按钮 ID
#define IDC3_EDIT_PATH  1302 // 方案文件路径只读框 ID
#define IDC3_LIST       1303 // 方案列表只读框 ID
#define IDC3_COMBO      1304 // 方案选择下拉框 ID
#define IDC3_DETAIL     1305 // 分区明细只读框 ID

// ==================== 下拉框占位提示 ====================
#define C1_PROMPT   L"Please select a disk"           // 卡片① 未选择提示
#define C2_PROMPT   L"Please select a system image"   // 卡片② 未选择提示
#define C3_PROMPT   L"Please select a partition scheme" // 卡片③ 未选择提示

// ==================== 本模块全局状态 ====================
static HWND g_hCard1 = NULL; // 卡片① 窗口句柄
static HWND g_hCard2 = NULL; // 卡片② 窗口句柄
static HWND g_hCard3 = NULL; // 卡片③ 窗口句柄

static HWND g_hC1BtnScan = NULL, g_hC1List = NULL, g_hC1Combo = NULL;   // 卡片① 三个控件句柄
static HWND g_hC2BtnBrowse = NULL, g_hC2EditPath = NULL, g_hC2List = NULL, g_hC2Combo = NULL; // 卡片② 控件句柄
static HWND g_hC3BtnBrowse = NULL, g_hC3EditPath = NULL, g_hC3List = NULL, g_hC3Combo = NULL, g_hC3Detail = NULL; // 卡片③ 控件句柄

// 卡片③：扫描到的分区方案缓存
static SchemeInfo g_schemes[SCHEME_MAX_ITEMS]; // 方案结构缓存数组
static int g_schemeCount = 0;                  // 缓存中方案数量

// 卡片①：扫描到的真实硬盘缓存（数组下标 = 下拉框项序号）
static DiskScanEntry g_disks[DISKSCAN_MAX_DISKS]; // 磁盘结构缓存数组
static int g_diskCount = 0;                       // 缓存中磁盘数量

// 卡片②：解析到的映像缓存（真实 WIM/ESD 索引）
#define IMG_MAX_ITEMS 32              // 最多缓存映像数
static ImgEntry g_images[IMG_MAX_ITEMS]; // 映像条目缓存
static int g_imageCount = 0;              // 缓存中映像数

// 部署状态（与 HTML 的 S 对象对应）
static struct                              // 匿名状态结构：记录三张卡片的选择进度
{
    int  disk;        // 选中的硬盘编号（真实物理盘号），-1 未选
    bool haveImage;   // 是否已成功解析镜像文件
    int  edition;     // 选中的版本序号（WIM 映像 Index），0 未选
    int  scheme;      // 选中的方案索引，-1 未选
    int  schemeData;  // 选中的方案数据索引，-1 未选
} g_state = { -1, false, 0, -1, -1 }; // 初始：全部未选

// ==================== 工具函数 ====================
// 向只读多行编辑框追加一行
static void AppendLine(HWND hEdit, const wchar_t* line)
{
    const int len = GetWindowTextLengthW(hEdit); // 取当前文本长度（用于定位插入点）
    SendMessageW(hEdit, EM_SETSEL, len, len);    // 把光标选中位置移到文末
    SendMessageW(hEdit, EM_REPLACESEL, FALSE, (LPARAM)line); // 在文末插入新行内容
    SendMessageW(hEdit, EM_REPLACESEL, FALSE, (LPARAM)L"\r\n"); // 追加换行
}

// 实际可用容量：向下取整的 GiB 文本（500GB 盘显示 465GB，1TB 盘显示 931GB）
static void FormatActualSize(ULONGLONG bytes, wchar_t* out, int outLen)
{
    const ULONGLONG gib = bytes / (1024ULL * 1024ULL * 1024ULL); // 字节换算成 GiB（向下取整）
    swprintf(out, outLen, L"%I64uGB", gib); // 输出如 "465GB"
}

// 型号文本里是否已带容量标记（500GB / 1TB ...），避免重复拼接
static BOOL ModelContainsCapacity(const wchar_t* model)
{
    return model && (wcsstr(model, L"GB") || wcsstr(model, L"TB") || // 型号含 GB/TB
                     wcsstr(model, L"MB") || wcsstr(model, L"PB"));  // 或 MB/PB 均视为已带容量
}

// 厂商容量标签：缺省型号按十进制习惯补（<1000GB 显示 GB，否则显示 TB）
static void AppendMarketingSize(ULONGLONG bytes, wchar_t* out, int outLen)
{
    const double gb = (double)bytes / 1000000000.0; // 字节按 10 进制换算成 GB
    if (gb >= 1000.0)                              // 达到 1000GB
        swprintf(out, outLen, L"%.0fTB", gb / 1000.0); // 显示 TB
    else                                           // 未到 1000GB
        swprintf(out, outLen, L"%.0fGB", gb);      // 显示 GB
}

// 构造标题：DiskN + 型号（型号缺容量时自动补厂商容量）
static void BuildDiskTitle(const DiskScanEntry* d, wchar_t* out, int outLen)
{
    if (ModelContainsCapacity(d->Model))           // 型号自带容量
    {
        swprintf(out, outLen, L"Disk%lu %s", d->DiskIndex, d->Model); // 直接拼接
    }
    else                                           // 型号无容量
    {
        wchar_t cap[24] = L"";                     // 容量文本缓冲
        AppendMarketingSize(d->SizeBytes, cap, 24); // 生成厂商容量
        swprintf(out, outLen, L"Disk%lu %s %s", d->DiskIndex, d->Model, cap); // 标题补容量
    }
}

// 卡片标题
static const wchar_t* GetCardTitle(int id)
{
    if (id == ID_CARD_1) return L"① Target Disk"; // 卡片① 标题
    if (id == ID_CARD_2) return L"② System Image"; // 卡片② 标题
    return L"③ Partition Scheme";                  // 卡片③ 标题
}

// 重置卡片②（镜像）内容与状态
static void ResetCard2()
{
    g_imageCount = 0;                              // 清空映像缓存计数
    SetWindowTextW(g_hC2EditPath, L"");            // 清空路径框
    SetWindowTextW(g_hC2List, L"");                // 清空列表
    SendMessageW(g_hC2Combo, CB_RESETCONTENT, 0, 0); // 清空下拉内容
    SendMessageW(g_hC2Combo, CB_ADDSTRING, 0, (LPARAM)C2_PROMPT); // 放回占位提示
    SendMessageW(g_hC2Combo, CB_SETCURSEL, 0, 0); // 默认选中提示项
    EnableWindow(g_hC2BtnBrowse, FALSE);           // 禁用浏览按钮
    EnableWindow(g_hC2Combo, FALSE);               // 禁用版本下拉框
}

// 重置卡片③（方案）内容与状态
static void ResetCard3()
{
    g_schemeCount = 0;                             // 清空方案缓存计数
    SetWindowTextW(g_hC3EditPath, L"");            // 清空路径框
    SetWindowTextW(g_hC3List, L"");                // 清空列表
    SetWindowTextW(g_hC3Detail, L"");              // 清空明细
    SendMessageW(g_hC3Combo, CB_RESETCONTENT, 0, 0); // 清空下拉
    SendMessageW(g_hC3Combo, CB_ADDSTRING, 0, (LPARAM)C3_PROMPT); // 占位提示
    SendMessageW(g_hC3Combo, CB_SETCURSEL, 0, 0); // 默认选中提示
    EnableWindow(g_hC3BtnBrowse, FALSE);           // 禁用浏览按钮
    EnableWindow(g_hC3Combo, FALSE);               // 禁用方案下拉框
}

// 联动状态同步（与 HTML 的 sync() 对应）
static void SyncCards()
{
    const bool s1 = g_state.disk >= 0;                 // 步骤①完成：已选硬盘
    const bool s2 = s1 && g_state.edition > 0;         // 步骤②完成：已选版本
    const bool s3 = s2 && g_state.schemeData >= 0;     // 步骤③完成：已选方案

    EnableWindow(g_hCard2, s1 ? TRUE : FALSE); // 没选硬盘，卡片②锁定
    EnableWindow(g_hCard3, s2 ? TRUE : FALSE); // 没选版本，卡片③锁定

    EnableWindow(g_hC2BtnBrowse, s1 ? TRUE : FALSE);        // 镜像浏览按钮随步骤①解锁
    EnableWindow(g_hC2Combo, g_state.haveImage ? TRUE : FALSE); // 有解析结果才允许选版本
    EnableWindow(g_hC3BtnBrowse, s2 ? TRUE : FALSE);        // 方案浏览按钮随步骤②解锁
    EnableWindow(g_hC3Combo, s2 ? TRUE : FALSE);            // 方案下拉随步骤②解锁

    // 底部状态区同步：已完成几步 + 对应卡片颜色
    BottomPanelSetStep(s3 ? 3 : (s2 ? 2 : (s1 ? 1 : 0))); // 按完成阶段通知底部面板
}

// ==================== 卡片① 目标硬盘 ====================
static void ScanDisks()
{
    BottomPanelLog(L"Disk scan: requested");     // 记录扫描请求
    SetWindowTextW(g_hC1List, L"");              // 清空列表
    SendMessageW(g_hC1Combo, CB_RESETCONTENT, 0, 0); // 清空下拉

    // 真实扫描物理硬盘（\\.\PhysicalDriveN）
    g_diskCount = DiskScanAll(g_disks, DISKSCAN_MAX_DISKS); // 调用工具层枚举物理盘

    if (g_diskCount <= 0)                        // 没有扫到任何盘
    {
        BottomPanelLog(L"Disk scan: no physical disks found"); // 记录
        AppendLine(g_hC1List, L"No physical disks found.");   // 提示
        AppendLine(g_hC1List, L"Check that the disk is connected and accessible."); // 排查建议
        SendMessageW(g_hC1Combo, CB_ADDSTRING, 0, (LPARAM)C1_PROMPT); // 下拉只剩提示
        SendMessageW(g_hC1Combo, CB_SETCURSEL, 0, 0); // 选中提示
    }
    else                                         // 扫描到磁盘
    {
        wchar_t logline[256];                    // 日志缓冲
        swprintf(logline, 256, L"Disk scan: %d disk(s) found", g_diskCount); // 记录数量
        BottomPanelLog(logline);                 // 输出

        // 默认项始终保留"请选择硬盘"，真实硬盘从索引 1 开始
        SendMessageW(g_hC1Combo, CB_ADDSTRING, 0, (LPARAM)C1_PROMPT); // 第 0 项提示

        wchar_t title[160] = L"", sizeText[32] = L"", line[360]; // 标题/容量/行缓冲
        for (int i = 0; i < g_diskCount; i++)    // 遍历每块盘
        {
            const DiskScanEntry* d = &g_disks[i]; // 当前盘条目
            BuildDiskTitle(d, title, 160);        // 构造标题
            FormatActualSize(d->SizeBytes, sizeText, 32); // 实际容量文本

            // 列表明细：DiskN 型号 (总线·DiskN·实际容量)
            swprintf(line, 360, L"%s (%s·Disk%lu·%s)", // 组装明细行
                title, DiskScanBusText(d->BusType), d->DiskIndex, sizeText);
            AppendLine(g_hC1List, line);          // 追加到列表

            // 下拉框：DiskN 型号
            SendMessageW(g_hC1Combo, CB_ADDSTRING, 0, (LPARAM)title); // 添加为选项
        }
        SendMessageW(g_hC1Combo, CB_SETCURSEL, 0, 0); // 默认停在提示项
    }

    g_state.disk = -1;                           // 未选盘
    g_state.haveImage = false;                   // 清除镜像
    g_state.edition = 0;                         // 清除版本
    g_state.scheme = -1;                         // 清除方案
    g_state.schemeData = -1;                     // 清除方案数据索引
    ResetCard2();                                // 重置卡片②
    ResetCard3();                                // 重置卡片③
    SyncCards();                                 // 同步状态
}

static void PickDisk()
{
    const LRESULT sel = SendMessageW(g_hC1Combo, CB_GETCURSEL, 0, 0); // 当前选中项索引
    // 索引 0 = 提示项（未选择）；真实盘从索引 1 开始
    // 保存真实物理盘号（数组下标可能因缺号设备与实际盘号不一致）
    g_state.disk = (sel > 0 && (sel - 1) < g_diskCount) // 有效真实盘
        ? (int)g_disks[sel - 1].DiskIndex        // 存真实物理盘号
        : -1;                                    // 否则视为未选
    if (g_state.disk >= 0)                       // 选择了盘
    {
        wchar_t logline[160];                    // 日志缓冲
        swprintf(logline, 160, L"Target disk selected: Disk%lu %s", // 记录
                 g_disks[sel - 1].DiskIndex, g_disks[sel - 1].Model);
        BottomPanelLog(logline);                 // 输出
    }
    else                                         // 取消选择
    {
        BottomPanelLog(L"Target disk selection cleared"); // 记录清除
    }

    g_state.haveImage = false;                   // 清除镜像状态
    g_state.edition = 0;                         // 清除版本
    g_state.scheme = -1;                         // 清除方案
    g_state.schemeData = -1;                     // 清除数据索引
    ResetCard2();                                // 重置卡片②
    ResetCard3();                                // 重置卡片③
    SyncCards();                                 // 同步
}

// ==================== 卡片② 系统镜像 ====================
// 镜像解析回调上下文：把解析结果写入卡片②的列表与下拉框
struct ImgFillCtx
{
    HWND list;   // 列表控件句柄
    HWND combo;  // 下拉控件句柄
};

static BOOL OnImageFound(const ImgEntry* e, void* pv) // 每解析出一个映像被回调
{
    ImgFillCtx* c = (ImgFillCtx*)pv;           // 取上下文
    if (!c) return FALSE;                      // 无效上下文停止
    if (g_imageCount < IMG_MAX_ITEMS)          // 缓存未满
        g_images[g_imageCount++] = *e;         // 缓存该映像

    wchar_t line[1024];                        // 列表行缓冲
    if (e->FileVersion[0])                     // 有版本号
        swprintf(line, 1024, L"[%d] %s  (%s %s)", e->Index, e->DisplayName, e->Arch, e->FileVersion); // 含版本
    else                                       // 无版本号
        swprintf(line, 1024, L"[%d] %s  (%s)", e->Index, e->DisplayName, e->Arch); // 只含架构
    AppendLine(c->list, line);                 // 追加到列表

    swprintf(line, 1024, L"[%d] %s", e->Index, e->DisplayName); // 下拉项文本
    SendMessageW(c->combo, CB_ADDSTRING, 0, (LPARAM)line); // 加入下拉
    return TRUE; // 继续解析下一个映像 // 返回 TRUE 表示继续
}

static void PickImage()                        // 浏览并解析镜像文件
{
    wchar_t path[MAX_PATH] = L"";              // 文件路径缓冲
    OPENFILENAMEW ofn = { sizeof(ofn) };       // 打开对话框结构（自动填大小）
    ofn.hwndOwner = g_hCard2;                  // owner=所在卡片（与备份版本一致）
    ofn.lpstrFilter = L"Image Files (*.wim;*.esd;*.iso)\0*.wim;*.esd;*.iso\0All Files (*.*)\0*.*\0"; // 扩展名过滤
    ofn.lpstrFile = path;                      // 输出路径缓冲
    ofn.nMaxFile = MAX_PATH;                   // 缓冲大小
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST; // 与初始版本完全一致
    if (!GetOpenFileNameW(&ofn)) return;       // 用户取消则返回

    BottomPanelLog(L"Image browse: parsing selected file..."); // 记录解析开始
    SetWindowTextW(g_hC2EditPath, path);       // 路径框显示所选文件
    SetWindowTextW(g_hC2List, L"");            // 清空版本列表
    SendMessageW(g_hC2Combo, CB_RESETCONTENT, 0, 0); // 清空下拉
    // 默认项：请选择系统镜像（真实版本由回调追加到其后，索引从 1 开始）
    SendMessageW(g_hC2Combo, CB_ADDSTRING, 0, (LPARAM)C2_PROMPT); // 放提示
    SendMessageW(g_hC2Combo, CB_SETCURSEL, 0, 0); // 选中提示

    // 调用 Utils 模块真实解析 .wim/.esd/.iso
    ImgFillCtx ctx = { g_hC2List, g_hC2Combo }; // 回调上下文
    wchar_t err[512] = { 0 };                  // 错误缓冲
    const int n = ImgParseFile(path, OnImageFound, &ctx, err, 512); // 解析镜像

    if (n <= 0)                                // 解析失败
    {
        wchar_t logline[900];                  // 日志缓冲
        swprintf(logline, 900, L"Image parse FAILED: %ls", // 组装错误日志
                 err[0] ? err : L"unknown error");
        BottomPanelLog(logline);               // 输出
        // 解析失败：列表显示原因，下拉恢复默认提示
        SetWindowTextW(g_hC2List, err[0] ? err : L"Failed to parse image."); // 显示原因
        SendMessageW(g_hC2Combo, CB_RESETCONTENT, 0, 0); // 重置下拉
        SendMessageW(g_hC2Combo, CB_ADDSTRING, 0, (LPARAM)C2_PROMPT); // 提示
        SendMessageW(g_hC2Combo, CB_SETCURSEL, 0, 0); // 选中提示
        g_state.haveImage = false;             // 标记无可用镜像
    }
    else                                       // 解析成功
    {
        wchar_t logline[900];                  // 日志缓冲
        swprintf(logline, 900, L"Image parse OK: %d edition(s) from %ls", n, path); // 记录数量
        BottomPanelLog(logline);               // 输出
        // 保持默认提示为当前显示项，未选择任何具体版本
        g_state.haveImage = true;              // 标记已有镜像
    }
    g_state.edition = 0;                       // 版本未选
    g_state.scheme = -1;                       // 方案清除
    g_state.schemeData = -1;                   // 数据索引清除
    ResetCard3();                              // 重置卡片③
    SyncCards();                               // 同步
}

static void PickEdition()                      // 用户选择版本
{
    const LRESULT sel = SendMessageW(g_hC2Combo, CB_GETCURSEL, 0, 0); // 选中索引
    // 索引 0 = 提示项（未选版本）；真实版本从索引 1 开始
    g_state.edition = (sel > 0 && sel <= g_imageCount) // 有效范围
        ? g_images[sel - 1].Index              // 取真实 WIM 索引
        : 0;                                   // 无效为 0（未选）
    if (g_state.edition > 0)                   // 已选
    {
        wchar_t logline[640];                  // 日志缓冲
        swprintf(logline, 640, L"Image edition selected: index %d (%s)", // 记录
                 g_images[sel - 1].Index, g_images[sel - 1].DisplayName);
        BottomPanelLog(logline);               // 输出
    }
    else                                       // 清除选择
    {
        BottomPanelLog(L"Image edition selection cleared"); // 记录
    }

    g_state.scheme = -1;                       // 方案清除
    g_state.schemeData = -1;                   // 数据索引清除
    ResetCard3();                              // 重置卡片③
    SyncCards();                               // 同步
}

// ==================== 卡片③ 分区方案 ====================
static void ScanSchemes(); // 前向声明（BrowseSchemes 会调用） // 供 BrowseSchemes 提前声明

// 浏览并选择分区方案文件（与卡片②同款经典 GetOpenFileNameW 对话框，WinPE 可用）；
// 选中后直接从文件里解析其中的 1..N 个分区方案。
static void BrowseSchemes()
{
    wchar_t file[MAX_PATH] = L"";              // 选择结果路径
    wchar_t initDir[MAX_PATH] = L"";           // 初始目录
    GetWindowTextW(g_hC3EditPath, initDir, MAX_PATH); // 取当前方案文件路径作初始目录线索

    // 对话框初始目录 = 当前方案文件所在目录；没有则用当前工作目录
    wchar_t* lastSlash = initDir[0] ? wcsrchr(initDir, L'\\') : NULL; // 找最后一个分隔符
    if (lastSlash) *lastSlash = L'\0';         // 截成目录
    else GetCurrentDirectoryW(MAX_PATH, initDir); // 无文件时用当前目录

    OPENFILENAMEW ofn = { sizeof(ofn) };       // 打开对话框结构
    ofn.hwndOwner = g_hCard3;                  // owner=所在卡片（与备份版本一致）
    ofn.lpstrFilter = L"Partition Scheme Files (*.xml)\0*.xml\0All Files (*.*)\0*.*\0"; // XML 过滤
    ofn.lpstrFile = file;              // 返回值：所选文件的完整路径
    ofn.nMaxFile = MAX_PATH;                   // 缓冲大小
    ofn.lpstrInitialDir = initDir;             // 初始目录
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY; // 与初始版本完全一致
    if (!GetOpenFileNameW(&ofn))               // 用户取消
    {
        BottomPanelLog(L"Scheme browse: canceled"); // 记录取消
        return;                                // 返回
    }

    SetWindowTextW(g_hC3EditPath, file); // 显示所选方案文件 // 路径框更新
    BottomPanelLog(L"Scheme browse: parsing selected file..."); // 记录
    ScanSchemes();                       // 从文件里直接解析分区方案 // 解析所选文件
}

static void ScanSchemes()                      // 解析路径框中的方案文件
{
    wchar_t file[MAX_PATH] = L"";              // 路径缓冲
    GetWindowTextW(g_hC3EditPath, file, MAX_PATH); // 取路径框内容
    if (!file[0]) return; // 尚未选择文件，直接返回 // 无文件直接返回

    // 真实解析所选 XML（合并文件内可含 1..N 个方案）
    g_schemeCount = SchemeParseFileEx(file, g_schemes, SCHEME_MAX_ITEMS); // 解析方案
    wchar_t logline[320];                      // 日志缓冲
    swprintf(logline, 320, L"Scheme scan: %d scheme(s) from %ls", // 记录结果
             g_schemeCount, file);
    BottomPanelLog(logline);                   // 输出

    SetWindowTextW(g_hC3List, L"");            // 清空列表
    SendMessageW(g_hC3Combo, CB_RESETCONTENT, 0, 0); // 清空下拉
    // 默认项始终保留"请选择分区方案"
    SendMessageW(g_hC3Combo, CB_ADDSTRING, 0, (LPARAM)C3_PROMPT); // 放提示

    if (g_schemeCount == 0)                    // 未解析出方案
    {
        AppendLine(g_hC3List, L"No partition scheme found in the file."); // 提示
        SendMessageW(g_hC3Combo, CB_SETCURSEL, 0, 0); // 选中提示
    }
    else                                       // 有方案
    {
        wchar_t line[320];                     // 行缓冲
        for (int i = 0; i < g_schemeCount; i++) // 遍历方案
        {
            swprintf(line, 320, L"%s  (%d partitions)", // 列表文本
                g_schemes[i].Name, g_schemes[i].PartCount);
            AppendLine(g_hC3List, line);       // 追加到列表
            SendMessageW(g_hC3Combo, CB_ADDSTRING, 0, (LPARAM)g_schemes[i].Name); // 下拉选项
        }
        SendMessageW(g_hC3Combo, CB_SETCURSEL, 0, 0); // 停在提示项
    }

    g_state.scheme = -1;                       // 方案未选
    g_state.schemeData = -1;                   // 数据索引未选
    SetWindowTextW(g_hC3Detail, L"");          // 清空明细
    SyncCards();                               // 同步
}

static void PickScheme()                       // 用户选择方案
{
    const LRESULT sel = SendMessageW(g_hC3Combo, CB_GETCURSEL, 0, 0); // 选中索引
    // 索引 0 = 提示项（未选择）；真实方案从索引 1 开始
    if (sel > 0 && (sel - 1) < g_schemeCount)  // 有效范围
    {
        g_state.scheme = (int)(sel - 1);       // 方案索引
        g_state.schemeData = (int)(sel - 1);   // 数据索引（两者当前一致）
        wchar_t logline[320];                  // 日志缓冲
        swprintf(logline, 320, L"Partition scheme selected: %s (%d partitions)", // 记录
                 g_schemes[sel - 1].Name, g_schemes[sel - 1].PartCount);
        BottomPanelLog(logline);               // 输出
        wchar_t detail[4096] = { 0 };          // 明细缓冲
        SchemeFormatDetail(&g_schemes[sel - 1], detail, 4096); // 生成分区明细
        SetWindowTextW(g_hC3Detail, detail);   // 显示明细
    }
    else                                       // 回到提示项
    {
        BottomPanelLog(L"Partition scheme selection cleared"); // 记录
        g_state.scheme = -1;                   // 方案清除
        g_state.schemeData = -1;               // 数据索引清除
        SetWindowTextW(g_hC3Detail, L"");      // 清空明细
    }
    SyncCards();                               // 同步
}

// ==================== 部署信息收集 ====================
BOOL ContentPanelBuildDeployJob(DeployJob* job, wchar_t* err, int errLen) // 收集部署参数
{
    if (err && errLen > 0) err[0] = L'\0';     // 错误缓冲清空
    if (!job) return FALSE;                    // 输出为空直接失败
    ZeroMemory(job, sizeof(*job));             // 清空 job

    if (g_state.disk < 0)                      // 未选盘
    {
        if (err) lstrcpynW(err, L"Target disk is not selected.", errLen); // 报错
        return FALSE;                          // 失败
    }
    if (!g_state.haveImage || g_state.edition <= 0) // 未选镜像版本
    {
        if (err) lstrcpynW(err, L"System image edition is not selected.", errLen); // 报错
        return FALSE;                          // 失败
    }
    if (g_state.schemeData < 0 || g_state.schemeData >= g_schemeCount) // 未选方案
    {
        if (err) lstrcpynW(err, L"Partition scheme is not selected.", errLen); // 报错
        return FALSE;                          // 失败
    }

    // 目标盘：按真实物理盘号找回缓存条目
    BOOL found = FALSE;                        // 是否找到
    for (int i = 0; i < g_diskCount; i++)      // 遍历磁盘缓存
    {
        if ((int)g_disks[i].DiskIndex == g_state.disk) // 盘号匹配
        {
            job->DiskIndex = g_disks[i].DiskIndex;   // 盘号
            job->DiskBytes = g_disks[i].SizeBytes;   // 容量
            lstrcpynW(job->DiskModel, g_disks[i].Model, 96); // 型号
            lstrcpynW(job->DiskSerial, g_disks[i].Serial, 64); // 序列号
            lstrcpynW(job->Fingerprint, g_disks[i].Fingerprint, 80); // 指纹
            found = TRUE;                      // 标记找到
            break;                             // 跳出
        }
    }
    if (!found)                                // 盘已不存在
    {
        if (err) lstrcpynW(err, L"Selected disk is no longer available.", errLen); // 报错
        return FALSE;                          // 失败
    }

    GetWindowTextW(g_hC2EditPath, job->ImagePath, MAX_PATH); // 取镜像路径
    if (!job->ImagePath[0])                    // 路径为空
    {
        if (err) lstrcpynW(err, L"Image file path is empty.", errLen); // 报错
        return FALSE;                          // 失败
    }
    job->ImageIndex = g_state.edition; // 真实 WIM 映像索引 // 写映像索引
    job->Scheme = g_schemes[g_state.schemeData]; // 拷贝所选方案
    return TRUE;                               // 成功
}

void ContentPanelSetEnabled(BOOL enabled)      // 部署期间锁定/恢复卡片
{
    if (g_hCard1) EnableWindow(g_hCard1, enabled ? TRUE : FALSE); // 卡片①
    if (g_hCard2) EnableWindow(g_hCard2, enabled ? TRUE : FALSE); // 卡片②
    if (g_hCard3) EnableWindow(g_hCard3, enabled ? TRUE : FALSE); // 卡片③
    if (g_hC1BtnScan) EnableWindow(g_hC1BtnScan, enabled ? TRUE : FALSE); // Scan 按钮
    if (g_hC1List) EnableWindow(g_hC1List, enabled ? TRUE : FALSE); // 列表
    if (g_hC1Combo) EnableWindow(g_hC1Combo, enabled ? TRUE : FALSE); // 下拉
    if (g_hC2BtnBrowse) EnableWindow(g_hC2BtnBrowse, enabled ? TRUE : FALSE); // 浏览按钮
    if (g_hC2EditPath) EnableWindow(g_hC2EditPath, enabled ? TRUE : FALSE); // 路径框
    if (g_hC2List) EnableWindow(g_hC2List, enabled ? TRUE : FALSE); // 列表
    if (g_hC2Combo) EnableWindow(g_hC2Combo, enabled ? TRUE : FALSE); // 下拉
    if (g_hC3BtnBrowse) EnableWindow(g_hC3BtnBrowse, enabled ? TRUE : FALSE); // 浏览按钮
    if (g_hC3EditPath) EnableWindow(g_hC3EditPath, enabled ? TRUE : FALSE); // 路径框
    if (g_hC3List) EnableWindow(g_hC3List, enabled ? TRUE : FALSE); // 列表
    if (g_hC3Combo) EnableWindow(g_hC3Combo, enabled ? TRUE : FALSE); // 下拉
    if (g_hC3Detail) EnableWindow(g_hC3Detail, enabled ? TRUE : FALSE); // 明细框

    if (enabled) SyncCards(); // 恢复锁定卡片②③的正常解锁状态 // 恢复后按进度联动
}

// ==================== 卡片自适应布局 ====================
// 说明：与项目其他模块一致，按 DPI 缩放自动计算尺寸；
//       控件全部从标题栏下方开始排列，杜绝标题与按钮重叠；
//       按钮宽度按文字实际宽度自动计算，避免文字被截断。
static float g_fScaleFactor = 1.0f;  // 当前 DPI 缩放因子
static HFONT g_hCtrlFont = NULL;     // 上次应用到控件的全局字体（用于 DPI 重建后刷新）

// 用全局字体测量文本宽度（供按钮等控件自动计算宽度）
static int MeasureTextWidth(HWND hWnd, const wchar_t* text)
{
    HDC hdc = GetDC(hWnd);                       // 取窗口 DC
    HFONT hOld = (HFONT)SelectObject(hdc,        // 选入字体并保存旧字体
        g_GlobalFont ? g_GlobalFont : (HFONT)GetStockObject(DEFAULT_GUI_FONT)); // 无全局字体用默认
    SIZE sz = { 0, 0 };                          // 文本尺寸
    GetTextExtentPoint32W(hdc, text, lstrlenW(text), &sz); // 测量
    SelectObject(hdc, hOld);                     // 还原旧字体
    ReleaseDC(hWnd, hdc);                        // 释放 DC
    return sz.cx;                                // 返回像素宽度
}

// 给所有控件统一应用全局字体（DPI 变化重建 g_GlobalFont 后自动刷新）
static void ApplyControlFonts()
{
    if (!g_GlobalFont) return;                   // 无字体则跳过
    const HWND controls[] = {                    // 全部控件数组
        g_hC1BtnScan, g_hC1List, g_hC1Combo,
        g_hC2BtnBrowse, g_hC2EditPath, g_hC2List, g_hC2Combo,
        g_hC3BtnBrowse, g_hC3EditPath, g_hC3List, g_hC3Combo, g_hC3Detail
    };
    for (HWND hCtrl : controls)                  // 遍历（C++20 range-for）
    {
        if (hCtrl) SendMessageW(hCtrl, WM_SETFONT, (WPARAM)g_GlobalFont, TRUE); // 下发字体
    }
    g_hCtrlFont = g_GlobalFont;                  // 记录已应用字体
}

// ==================== 卡片窗口过程 ====================
static void LayoutCardControls(HWND hCard)       // 计算卡片内控件布局
{
    const int id = GetDlgCtrlID(hCard);          // 卡片 ID
    RECT rc;                                     // 客户区矩形
    GetClientRect(hCard, &rc);                   // 取客户区
    const int w = rc.right;                      // 宽
    const int h = rc.bottom;                     // 高
    if (w <= 0 || h <= 0) return; // 防御性检查 // 尺寸无效返回

    // 与项目一致：尺寸随 DPI 自动缩放
    g_fScaleFactor = GetWindowScaleFactor(hCard); // 取当前缩放
    const float s = g_fScaleFactor;              // 别名

    // 全局字体变化（DPI 重建）时刷新控件字体
    if (g_GlobalFont != g_hCtrlFont) ApplyControlFonts(); // 字体自愈

    const int m = (int)(g_uiContentParams.CardMargin * s);       // 卡片内边距（读配置）
    const int pad = (int)(g_uiContentParams.CardPad * s);        // 控件间距（读配置）
    const int titleH = (int)(g_uiContentParams.TitleH * s);      // 标题栏高度（读配置）
    const int rowH = (int)(g_uiContentParams.RowH * s);          // 按钮/输入行高度（读配置）
    const int comboH = (int)(g_uiContentParams.ComboH * s);      // 下拉框可见高度（读配置）
    const int dropH = (int)(g_uiContentParams.DropH * s);        // 下拉列表展开高度（读配置）

    if (id == ID_CARD_1)                         // 卡片① 布局
    {
        // Scan 按钮：宽度按文字自动计算，放在按钮行右侧
        int btnW = MeasureTextWidth(hCard, L"Scan Disks") + (int)(24 * s); // 文字宽 + 边距
        const int maxW = w - m * 2;              // 可用宽度
        if (btnW > maxW) btnW = maxW;            // 超宽截断
        if (btnW < (int)(g_uiContentParams.MinScanButtonW * s)) btnW = (int)(g_uiContentParams.MinScanButtonW * s); // 最小宽度（读配置）
        MoveWindow(g_hC1BtnScan, w - m - btnW, titleH + pad, btnW, rowH, TRUE); // 右上角

        // 列表：按钮行下方到下拉框上方
        const int listTop = titleH + pad + rowH + pad; // 列表顶边
        const int comboTop = h - m - comboH;     // 下拉框顶边
        const int listH = (comboTop > listTop) ? (comboTop - listTop) : 0; // 列表高度
        MoveWindow(g_hC1List, m, listTop, w - m * 2, listH, TRUE); // 列表铺满中间
        MoveWindow(g_hC1Combo, m, comboTop, w - m * 2, comboH + dropH, TRUE); // 下拉贴底
    }
    else if (id == ID_CARD_2)                    // 卡片② 布局
    {
        // Browse 按钮 + 路径输入框（同一行，按钮宽度按文字自适应）
        int btnW = MeasureTextWidth(hCard, L"Browse") + (int)(24 * s); // 按钮宽
        int pathW = w - m * 2 - pad - btnW;      // 路径框宽
        if (pathW < (int)(g_uiContentParams.MinPathW * s))       // 太窄时压缩按钮
        {
            btnW = w - m * 2 - pad - (int)(g_uiContentParams.MinPathW * s); // 按钮压缩
            pathW = (int)(g_uiContentParams.MinPathW * s);       // 路径框最小宽
        }
        if (btnW < (int)(g_uiContentParams.MinBrowseButtonW * s)) btnW = (int)(g_uiContentParams.MinBrowseButtonW * s); // 按钮最小宽（读配置）
        MoveWindow(g_hC2EditPath, m, titleH + pad, pathW, rowH, TRUE); // 路径框在左
        MoveWindow(g_hC2BtnBrowse, m + pathW + pad, titleH + pad, btnW, rowH, TRUE); // 按钮在右

        const int listTop = titleH + pad + rowH + pad; // 列表顶边
        const int comboTop = h - m - comboH;     // 下拉顶边
        const int listH = (comboTop > listTop) ? (comboTop - listTop) : 0; // 列表高
        MoveWindow(g_hC2List, m, listTop, w - m * 2, listH, TRUE); // 列表
        MoveWindow(g_hC2Combo, m, comboTop, w - m * 2, comboH + dropH, TRUE); // 下拉
    }
    else if (id == ID_CARD_3)                    // 卡片③ 布局
    {
        // Scan 按钮 + 路径输入框（同一行）
        int btnW = MeasureTextWidth(hCard, L"Browse...") + (int)(24 * s); // 按钮宽
        int pathW = w - m * 2 - pad - btnW;      // 路径框宽
        if (pathW < (int)(g_uiContentParams.MinPathW * s))       // 太窄
        {
            btnW = w - m * 2 - pad - (int)(g_uiContentParams.MinPathW * s); // 压缩按钮
            pathW = (int)(g_uiContentParams.MinPathW * s);       // 最小路径宽
        }
        if (btnW < (int)(g_uiContentParams.MinBrowseButtonW * s)) btnW = (int)(g_uiContentParams.MinBrowseButtonW * s); // 最小按钮宽（读配置）
        MoveWindow(g_hC3EditPath, m, titleH + pad, pathW, rowH, TRUE); // 路径框
        MoveWindow(g_hC3BtnBrowse, m + pathW + pad, titleH + pad, btnW, rowH, TRUE); // 按钮

        // NOTE: scheme combo is pinned to the card bottom (same style as Card1/2)
        const int comboTop = h - m - comboH;     // 下拉贴底
        MoveWindow(g_hC3Combo, m, comboTop, w - m * 2, comboH + dropH, TRUE); // 下拉

        const int top = titleH + pad + rowH + pad; // 列表顶边
        int middleH = comboTop - pad - top;      // 列表+明细总高
        if (middleH < 0) middleH = 0;            // 防御
        int schemeH = (int)(middleH * g_uiContentParams.ListRatio / 1000); // 方案列表占比（读配置）
        if (schemeH < (int)(40 * s)) schemeH = (int)(40 * s); // 最小列表高
        if (schemeH > middleH - (int)(40 * s)) schemeH = middleH - (int)(40 * s); // 给明细留空间
        if (schemeH < 0) schemeH = 0;            // 防御
        MoveWindow(g_hC3List, m, top, w - m * 2, schemeH, TRUE); // 方案列表

        const int detailTop = top + schemeH + pad; // 明细顶边
        int detailH = comboTop - pad - detailTop; // 明细高度
        if (detailH < 0) detailH = 0;            // 防御
        MoveWindow(g_hC3Detail, m, detailTop, w - m * 2, detailH, TRUE); // 明细框
    }
}

static LRESULT CALLBACK CardWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) // 卡片窗口过程
{
    switch (msg)                                 // 消息分发
    {
    case WM_CREATE:                              // 创建卡片
    {
        HINSTANCE hInst = ((LPCREATESTRUCTW)lParam)->hInstance; // 实例句柄
        const int id = GetDlgCtrlID(hWnd);       // 卡片 ID

        if (id == ID_CARD_1)                     // 创建卡片① 控件
        {
            g_hC1BtnScan = CreateWindowExW(0, L"BUTTON", L"Scan Disks", // 扫描按钮
                WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hWnd, (HMENU)IDC1_BTN_SCAN, hInst, NULL);
            g_hC1List = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", // 结果列表
                WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,
                0, 0, 0, 0, hWnd, (HMENU)IDC1_LIST, hInst, NULL);
            g_hC1Combo = CreateWindowExW(0, L"COMBOBOX", L"", // 磁盘下拉
                WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                0, 0, 0, 0, hWnd, (HMENU)IDC1_COMBO, hInst, NULL);
        }
        else if (id == ID_CARD_2)                // 创建卡片② 控件
        {
            g_hC2BtnBrowse = CreateWindowExW(0, L"BUTTON", L"Browse", // 浏览按钮
                WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hWnd, (HMENU)IDC2_BTN_BROWSE, hInst, NULL);
            g_hC2EditPath = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", // 路径框
                WS_CHILD | WS_VISIBLE | ES_READONLY,
                0, 0, 0, 0, hWnd, (HMENU)IDC2_EDIT_PATH, hInst, NULL);
            g_hC2List = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", // 版本列表
                WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,
                0, 0, 0, 0, hWnd, (HMENU)IDC2_LIST, hInst, NULL);
            g_hC2Combo = CreateWindowExW(0, L"COMBOBOX", L"", // 版本下拉
                WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                0, 0, 0, 0, hWnd, (HMENU)IDC2_COMBO, hInst, NULL);
        }
        else if (id == ID_CARD_3)                // 创建卡片③ 控件
        {
            g_hC3BtnBrowse = CreateWindowExW(0, L"BUTTON", L"Browse...", // 浏览按钮
                WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hWnd, (HMENU)IDC3_BTN_BROWSE, hInst, NULL);
            g_hC3EditPath = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", // 方案路径框
                WS_CHILD | WS_VISIBLE | ES_READONLY, // 只读：显示 Browse 选择的文件夹
                0, 0, 0, 0, hWnd, (HMENU)IDC3_EDIT_PATH, hInst, NULL);
            g_hC3List = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", // 方案列表
                WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,
                0, 0, 0, 0, hWnd, (HMENU)IDC3_LIST, hInst, NULL);
            g_hC3Combo = CreateWindowExW(0, L"COMBOBOX", L"", // 方案下拉
                WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                0, 0, 0, 0, hWnd, (HMENU)IDC3_COMBO, hInst, NULL);
            g_hC3Detail = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", // 分区明细
                WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,
                0, 0, 0, 0, hWnd, (HMENU)IDC3_DETAIL, hInst, NULL);
        }

        // 统一控件字体（与卡片自绘标题一致）
        ApplyControlFonts();                     // 应用全局字体

        // 卡片①：初始未扫描时下拉框显示占位提示
        if (id == ID_CARD_1)                     // 仅卡片①
        {
            SendMessageW(g_hC1Combo, CB_RESETCONTENT, 0, 0); // 清空
            SendMessageW(g_hC1Combo, CB_ADDSTRING, 0, (LPARAM)C1_PROMPT); // 提示
            SendMessageW(g_hC1Combo, CB_SETCURSEL, 0, 0); // 选中
        }

        // 初始状态：重置下游卡片并同步锁定
        ResetCard2();                            // 重置卡片②
        ResetCard3();                            // 重置卡片③
        SyncCards();                             // 同步

        // 卡片③：默认定位到当前目录下的合并方案文件（OEM_Partition_Schemes.xml）
        if (id == ID_CARD_3)                     // 仅卡片③
        {
            wchar_t cwd[MAX_PATH] = L"";         // 当前目录
            wchar_t def[MAX_PATH] = L"";         // 默认文件路径
            if (GetCurrentDirectoryW(MAX_PATH, cwd)) // 取当前目录
            {
                swprintf(def, MAX_PATH, L"%s\\OEM_Partition_Schemes.xml", cwd); // 拼默认路径
                if (GetFileAttributesW(def) != INVALID_FILE_ATTRIBUTES) // 文件存在
                {
                    SetWindowTextW(g_hC3EditPath, def); // 填入路径框
                    ScanSchemes(); // 直接解析合并文件中的方案 // 自动解析默认方案文件
                }
            }
        }
        return 0;                                // 处理完成
    }

    case WM_SIZE:                                // 尺寸变化
        LayoutCardControls(hWnd);                // 重排控件
        return 0;                                // 完成

    case WM_DPICHANGED:                          // DPI 变化
    case WM_DPICHANGED_AFTERPARENT:              // 父链 DPI 变化（PMv2）
        // PMv2 下子窗口实际收到 AFTERPARENT；两种入口都按新 DPI 重算控件布局并刷新字体
        LayoutCardControls(hWnd); // 按新 DPI 重新计算控件布局
        InvalidateRect(hWnd, NULL, TRUE);        // 强制重绘
        return 0;                                // 完成

    case WM_COMMAND:                             // 控件命令
    {
        const int card = GetDlgCtrlID(hWnd);     // 来源卡片
        const int id = LOWORD(wParam);           // 控件 ID
        const int code = HIWORD(wParam);         // 通知码

        if (card == ID_CARD_1)                   // 卡片① 命令
        {
            if (id == IDC1_BTN_SCAN && code == BN_CLICKED) ScanDisks(); // 扫描按钮
            else if (id == IDC1_COMBO && code == CBN_SELCHANGE) PickDisk(); // 选盘
        }
        else if (card == ID_CARD_2)              // 卡片② 命令
        {
            if (id == IDC2_BTN_BROWSE && code == BN_CLICKED) PickImage(); // 浏览镜像
            else if (id == IDC2_COMBO && code == CBN_SELCHANGE) PickEdition(); // 选版本
        }
        else if (card == ID_CARD_3)              // 卡片③ 命令
        {
            if (id == IDC3_BTN_BROWSE && code == BN_CLICKED) BrowseSchemes(); // 浏览方案
            else if (id == IDC3_COMBO && code == CBN_SELCHANGE) PickScheme(); // 选方案
        }
        return 0;                                // 完成
    }

    case WM_PAINT:                               // 自绘卡片
    {
        PAINTSTRUCT ps;                          // 绘制结构
        HDC hdc = BeginPaint(hWnd, &ps);         // 开始绘制
        RECT rc;                                 // 客户区
        GetClientRect(hWnd, &rc);                // 取区域

        const int id = GetDlgCtrlID(hWnd);       // 卡片 ID
        HBRUSH hBrush = g_BrushPool[6];          // 默认紫色（卡片③）
        if (id == ID_CARD_1) hBrush = g_BrushPool[4]; // 卡片① 蓝色
        else if (id == ID_CARD_2) hBrush = g_BrushPool[5]; // 卡片② 琥珀
        FillRect(hdc, &rc, hBrush);              // 填充背景

        SetBkMode(hdc, TRANSPARENT);             // 文字透明背景
        // 卡片标题/提示文字与其它面板文字一致：统一纯白（不再随锁定状态变灰）
        SetTextColor(hdc, g_uiTheme[UI_WIN_CONTENT].Foreground); // 文字色（读配置）
        if (g_GlobalFont) SelectObject(hdc, g_GlobalFont); // 全局字体
        // 标题占用独立的标题栏高度，控件从标题栏下方开始，避免重叠
        const float s = GetWindowScaleFactor(hWnd); // 缩放因子
        const int titleH = (int)(g_uiContentParams.TitleH * s);  // 标题栏高（读配置）
        RECT rcTitle = { (int)(g_uiContentParams.CardMargin * s), 0, rc.right - (int)(g_uiContentParams.CardMargin * s), titleH }; // 标题矩形（读配置）
        DrawTextW(hdc, GetCardTitle(id), -1, &rcTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE); // 画标题

        EndPaint(hWnd, &ps);                     // 结束绘制
        return 0;                                // 完成
    }

    case WM_ERASEBKGND:                          // 擦背景
        return 1; // 防止闪烁                    // 已由 WM_PAINT 覆盖
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam); // 未处理消息交给系统
}

// ==================== 容器布局 ====================
// 基于 ContentPanel 客户区计算三张卡片的位置和大小（X 方向等分，四周与卡片之间保留全局边距）
static void LayoutCards(HWND hContainer)         // 布局三张卡片
{
    RECT rc;                                     // 容器客户区
    GetClientRect(hContainer, &rc);              // 取区域
    const int clientW = rc.right;                // 宽
    const int clientH = rc.bottom;               // 高
    if (clientW <= 0 || clientH <= 0) return;    // 防御

    int margin = clientH * g_uiLayout[UI_WIN_MAIN].Percent / 100; // 边距按高度百分比（读配置）
    if (margin < 0) margin = 0;                  // 防御

    const int usableW = clientW - margin * 2;    // 左右留边后的可用宽
    int cardH = clientH - margin * 2;            // 上下留边后的卡片高
    int cardW = (usableW - margin * 2) / 3;      // 三张卡片等分（含卡片间两个边距）
    if (cardW < 0) cardW = 0;                    // 防御
    if (cardH < 0) cardH = 0;                    // 防御

    const int y = margin;                        // 纵向起始
    if (g_hCard1) MoveWindow(g_hCard1, margin, y, cardW, cardH, TRUE); // 卡片① 最左
    if (g_hCard2) MoveWindow(g_hCard2, margin + cardW + margin, y, cardW, cardH, TRUE); // 卡片② 中间
    if (g_hCard3) MoveWindow(g_hCard3, margin + (cardW + margin) * 2, y, cardW, cardH, TRUE); // 卡片③ 最右
}

static LRESULT CALLBACK ContentPanelProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) // 内容面板窗口过程
{
    switch (message)                             // 消息分发
    {
    case WM_CREATE:                              // 创建面板
    {
        HINSTANCE hInst = ((LPCREATESTRUCTW)lParam)->hInstance; // 实例句柄

        WNDCLASSEXW wc = { sizeof(wc) };         // 卡片类结构
        if (!GetClassInfoExW(hInst, L"CardWindowClass", &wc)) // 未注册才注册
        {
            wc.lpfnWndProc = CardWndProc;        // 卡片窗口过程
            wc.hInstance = hInst;                // 实例
            wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH); // 无刷子
            wc.hCursor = LoadCursorW(NULL, IDC_ARROW); // 箭头光标
            wc.lpszClassName = L"CardWindowClass"; // 类名
            RegisterClassExW(&wc);               // 注册
        }

        g_hCard1 = CreateWindowExW(0, L"CardWindowClass", NULL, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0, 0, 0, hWnd, (HMENU)ID_CARD_1, hInst, NULL); // 卡片①
        g_hCard2 = CreateWindowExW(0, L"CardWindowClass", NULL, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0, 0, 0, hWnd, (HMENU)ID_CARD_2, hInst, NULL); // 卡片②
        g_hCard3 = CreateWindowExW(0, L"CardWindowClass", NULL, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0, 0, 0, hWnd, (HMENU)ID_CARD_3, hInst, NULL); // 卡片③

        LayoutCards(hWnd);                       // 初始布局
        return 0;                                // 完成
    }

    case WM_SIZE:                                // 尺寸变化
        LayoutCards(hWnd);                       // 重排卡片
        return 0;                                // 完成

    default:                                     // 其余消息
        return DefWindowProcW(hWnd, message, wParam, lParam); // 交给系统
    }
}

BOOL ContentPanelRegisterClass(HINSTANCE hInstance) // 内容面板注册类赋值函数
{
    const wchar_t CLASS_NAME[] = L"ContentPanelClass"; // 面板类名
    WNDCLASSEXW probe = { sizeof(probe) };        // 探测结构
    if (!GetClassInfoExW(hInstance, CLASS_NAME, &probe)) // 面板类未注册
    {
        WNDCLASSEXW wc = { 0 };                   // 类结构
        wc.cbSize = sizeof(WNDCLASSEXW);          // 结构大小
        wc.lpfnWndProc = ContentPanelProc;        // 窗口过程
        wc.hInstance = hInstance;                 // 实例
        wc.hCursor = LoadCursorW(NULL, IDC_ARROW); // 光标
        wc.hbrBackground = g_BrushPool[3];        // 内容面板背景画刷（来自 UIConfig）
        wc.lpszClassName = CLASS_NAME;            // 类名
        RegisterClassExW(&wc);                    // 注册
    }

    WNDCLASSEXW cprobe = { sizeof(cprobe) };      // 卡片类探测
    if (!GetClassInfoExW(hInstance, L"CardWindowClass", &cprobe)) // 卡片类未注册
    {
        WNDCLASSEXW wc = { sizeof(wc) };          // 类结构
        wc.lpfnWndProc = CardWndProc;             // 卡片窗口过程
        wc.hInstance = hInstance;                 // 实例
        wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH); // 无刷（自绘）
        wc.hCursor = LoadCursorW(NULL, IDC_ARROW); // 光标
        wc.lpszClassName = L"CardWindowClass";    // 类名
        RegisterClassExW(&wc);                    // 注册
    }
    return TRUE;                                  // 注册完成
}

HWND RegisterContentPanel(HWND hwndParent, const WindowLayoutConfig& layout, HINSTANCE hInstance) // 创建内容面板
{
    ContentPanelRegisterClass(hInstance);         // 类注册（面板类+卡片类，重复自动跳过）

    return CreateWindowExW(0, L"ContentPanelClass", L"", // 创建面板窗口
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, // 子窗口可见并裁剪子级防闪烁
        layout.StartX, layout.StartY, layout.Width, layout.Height, // 初始位置大小
        hwndParent, NULL, hInstance, NULL);      // 父窗口/无菜单/实例/无附加参数
}
