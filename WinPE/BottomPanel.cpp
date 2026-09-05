#include "BottomPanel.h"      // 引入本模块头：注册接口/步骤接口/日志接口声明
#include "ContentPanel.h"     // 引入内容面板头：收集部署 job / 锁定卡片接口
#include "GlobalResources.h"  // 引入全局资源：画刷池/字体/布局参数
#include "UIConfig/UIConfig.h" // 引入界面配置：底部面板内部控件布局参数
#include "StatusBarPanel.h"   // 引入状态栏头：英文消息框/锁定重启关机按钮
#include "Utils/DeployEngine.h" // 引入部署引擎头：DeployRun/DeployIsCurrentBootDisk
#include <windows.h>          // Windows API 核心头
#include <commctrl.h>         // 用于进度条控件（PROGRESS_CLASSW / InitCommonControlsEx）
#include <cwchar>             // swprintf
#pragma comment(lib, "comctl32.lib") // 链接公共控件库

// 定义子窗口 ID
#define IDC_LOG_WINDOW      1001 // 日志窗口 ID
#define IDC_STATUS_TEXT     1002 // 左侧：状态文本 ID
#define IDC_PROGRESS_BAR    1003 // 中间：进度条 ID
#define IDC_DEPLOY_BUTTON   1004 // 右侧：部署按钮 ID
#define IDC_DEMO_CHECK      1005 // 演示模式开关 ID

// 部署线程 -> UI 线程消息
#define BM_MSG_DEPLOY_LOG    (WM_APP + 30) // 部署日志消息
#define BM_MSG_DEPLOY_PROG   (WM_APP + 31) // 部署进度消息
#define BM_MSG_DEPLOY_DONE   (WM_APP + 32) // 部署完成消息

// ==================== 布局与调试配色 ====================
// 说明：底部面板内部子窗口统一留出与全局一致的边距（以主窗口客户区高度为基准，
//       与 RelayoutChildPanels 四周的 margin 像素一致），方便直接观察布局；
//       各区域用不同背景色区分（复用 Main.cpp 的全局画刷池）：
//         面板背景/边距间隙 = g_BrushPool[2] 蓝色
//         日志窗口           = g_BrushPool[3] 绿色
//         状态文本区         = g_BrushPool[5] 橙黄色
//         进度条背景         = 紫色
//         部署按钮           = 红色

// 子窗口句柄：保存后直接用于布局与着色，避免反复 GetDlgItem
static HWND g_hMainParent = NULL;      // 主父窗口句柄（用于取边距基准）
static HWND g_hLog = NULL;             // 日志编辑框句柄
static HWND g_hStatusText = NULL;      // 状态文本句柄
static HWND g_hProgress = NULL;        // 进度条句柄
static HWND g_hDeployBtn = NULL;       // 部署按钮句柄
static HWND g_hDemoCheck = NULL;       // 演示模式开关句柄
static HFONT g_hAppliedChildFont = NULL; // 最近一次下发给文本子控件的字体，用于 DPI 变化后自愈
static int  g_step = 0;                // 当前完成步骤：0/1/2/3
static BOOL g_deploying = FALSE;       // 部署线程是否运行中
static BOOL g_demoOn = FALSE;          // 演示模式开关状态

// 执行日志文件：自动创建在 exe 所在目录（WinPE_log_yyyyMMdd_HHmmss.log）
static HANDLE g_hLogFile = INVALID_HANDLE_VALUE; // 日志文件句柄
static wchar_t g_logFilePath[MAX_PATH] = L"";    // 日志文件完整路径

// 计算与全局一致的边距：以父（主）窗口客户区高度为基准，
// 与 RelayoutChildPanels 中主窗口四周的 margin 保持相同像素值。
static int GetLayoutMargin(HWND hWnd)
{
    RECT rc = { 0 };                        // 父客户区矩形
    HWND hParent = GetParent(hWnd);         // 取父窗口
    BOOL bOk = (hParent != NULL) && GetClientRect(hParent, &rc) && rc.bottom > 0; // 成功取到高度
    if (!bOk)                               // 失败
    {
        GetClientRect(hWnd, &rc); // 兜底：使用自身客户区高度 // 退回自身
    }
    int margin = rc.bottom * g_uiLayout[UI_WIN_MAIN].Percent / 100; // 按高度百分比计算（读配置）
    return (margin < 0) ? 0 : margin;       // 负值归零
}

// 给文本类子控件应用全局字体（DPI 变化重建全局字体后重新下发）
static void ApplyChildFonts()
{
    if (!g_GlobalFont) return;              // 无全局字体跳过
    if (g_hLog) SendMessageW(g_hLog, WM_SETFONT, (WPARAM)g_GlobalFont, TRUE); // 日志字体
    if (g_hStatusText) SendMessageW(g_hStatusText, WM_SETFONT, (WPARAM)g_GlobalFont, TRUE); // 状态字体
    if (g_hDemoCheck) SendMessageW(g_hDemoCheck, WM_SETFONT, (WPARAM)g_GlobalFont, TRUE); // 开关字体
    g_hAppliedChildFont = g_GlobalFont;     // 记录已应用
}

// 步骤 -> 状态区背景画刷下标（复用 Main.cpp 的画刷池）
// 0 保持原有橙黄色；1/2/3 分别对应卡片①②③的调试配色
static int GetStepBrushIndex(int step)
{
    switch (step)                           // 按步骤分发
    {
    case 1:  return 4; // 卡片①：浅蓝色     // 步骤1 蓝
    case 2:  return 5; // 卡片②：橙黄色     // 步骤2 琥珀
    case 3:  return 6; // 卡片③：紫色       // 步骤3 紫
    default: return 8; // 未完成步骤：中性深灰 // 默认深灰
    }
}

// 对外接口：由内容面板在步骤状态变化时调用
void BottomPanelSetStep(int step)
{
    if (step < 0) step = 0;                 // 下限保护
    if (step > 3) step = 3;                 // 上限保护
    const int oldStep = g_step;             // 记录旧步骤
    g_step = step;                          // 更新步骤

    wchar_t text[64];                       // 状态文本缓冲
    wsprintfW(text, L"Status: Step %d", step); // 生成文本
    if (g_hStatusText) SetWindowTextW(g_hStatusText, text); // 更新文本
    if (g_hStatusText) InvalidateRect(g_hStatusText, NULL, TRUE); // 重绘以刷新背景色

    // 步骤变化时打印分段日志
    if (oldStep != step)                    // 步骤确实变化
    {
        BottomPanelLog(L"");                // 空行分隔
        if (step > 0 && step > oldStep)     // 前进到更高步骤
        {
            const wchar_t* label =          // 步骤完成文案
                step == 1 ? L"Step 1 completed (Target Disk selected)"
                : step == 2 ? L"Step 2 completed (System Image selected)"
                : L"Step 3 completed (Partition Scheme selected)";
            wchar_t line[160];              // 日志行缓冲
            swprintf(line, 160, L"========== %ls ==========", label); // 分隔标题
            BottomPanelLog(line);           // 输出
            if (step == 3)                  // 全部完成
                BottomPanelLog(L"========== Deploy button enabled =========="); // 提示解锁
        }
        else                                // 回退步骤
        {
            wchar_t line[160];              // 日志行缓冲
            swprintf(line, 160, L"========== Selection cleared; back to Step %d ==========", step); // 提示回退
            BottomPanelLog(line);           // 输出
        }
    }

    // 只有三步全部完成才允许部署
    if (g_hDeployBtn && !g_deploying)       // 按钮存在且未在部署
        EnableWindow(g_hDeployBtn, (step == 3) ? TRUE : FALSE); // 步骤3 才解锁
}

// ==================== 部署日志 / 后台线程 ====================
static void AppendLogLine(const wchar_t* line) // 往日志框追加一行
{
    if (!g_hLog || !line) return;           // 无效跳过
    const int len = GetWindowTextLengthW(g_hLog); // 当前长度
    SendMessageW(g_hLog, EM_SETSEL, len, len); // 光标置末尾
    SendMessageW(g_hLog, EM_REPLACESEL, FALSE, (LPARAM)line); // 插入
    SendMessageW(g_hLog, EM_REPLACESEL, FALSE, (LPARAM)L"\r\n"); // 换行
}

// 查找卷标为 "CustomPE-LOG" 的固定磁盘（用于把部署日志持久化到虚拟磁盘）
static BOOL FindLogVolume(wchar_t* root, int rootLen)
{
    if (!root || rootLen <= 0) return FALSE; // 参数检查
    root[0] = L'\0';                        // 清空输出
    const DWORD mask = GetLogicalDrives();  // 已分配盘符位图
    for (int i = 0; i < 26; i++)            // 遍历 A-Z
    {
        if (!(mask & ((DWORD)1 << i))) continue; // 未分配跳过
        wchar_t r[8];                       // 盘根
        wsprintfW(r, L"%c:\\", L'A' + i);   // "X:\"
        if (_wcsicmp(r, L"X:\\") == 0) continue;            // PE RAM 盘跳过
        if (GetDriveTypeW(r) != DRIVE_FIXED) continue;      // 只找硬盘卷
        wchar_t vol[64] = L"";              // 卷标
        DWORD sn = 0, maxLen = 0, flags = 0; // 占位输出
        if (GetVolumeInformationW(r, vol, 64, &sn, &maxLen, &flags, NULL, 0) && // 读卷标
            _wcsicmp(vol, L"CustomPE-LOG") == 0) // 匹配
        {
            lstrcpynW(root, r, rootLen);    // 输出盘根
            return TRUE;                    // 找到
        }
    }
    return FALSE;                           // 未找到
}

// 目录是否可写（创建 .probe 试探）
static BOOL DirWritable(const wchar_t* dir)
{
    CreateDirectoryW(dir, NULL);            // 确保目录存在
    wchar_t probe[MAX_PATH];                // 探针文件路径
    swprintf(probe, MAX_PATH, L"%s.probe", dir); // 目录名+.probe
    HANDLE h = CreateFileW(probe, GENERIC_WRITE, 0, NULL, // 创建探针
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return FALSE; // 不可写
    CloseHandle(h);                         // 关闭
    DeleteFileW(probe);                     // 删除探针
    return TRUE;                            // 可写
}

// 日志目录优先级：
//   1) CustomPE-LOG 卷的 \CustomPE\Logs（持久化日志盘）
//   2) 启动盘媒体文件夹的 Logs（exe 位于 <盘>\<CustomPE_NAME>\Tools\DeployUI）
//   3) PE RAM 盘 X:\CustomPE\Logs
//   4) exe 所在目录
static void GetLogDir(wchar_t* dir, int dirLen)
{
    dir[0] = L'\0';                        // 清空输出
    wchar_t root[8] = L"";                 // 日志盘根
    if (FindLogVolume(root, 8))            // 找到持久日志盘
    {
        wchar_t p1[MAX_PATH], p2[MAX_PATH]; // 逐级目录
        swprintf(p1, MAX_PATH, L"%sCustomPE", root); // <盘>:\CustomPE
        CreateDirectoryW(p1, NULL);        // 创建
        swprintf(p2, MAX_PATH, L"%sCustomPE\\Logs", root); // ...\Logs
        CreateDirectoryW(p2, NULL);        // 创建
        swprintf(dir, dirLen, L"%sCustomPE\\Logs\\", root); // 输出带尾斜杠
        return;                            // 返回
    }

    wchar_t exe[MAX_PATH] = L"";           // exe 路径
    GetModuleFileNameW(NULL, exe, MAX_PATH); // 取路径
    wchar_t* slash = wcsrchr(exe, L'\\');  // 最后一个分隔符
    if (slash) slash[1] = L'\0'; // exeDir: ...\Tools\DeployUI\ // 截成目录

    /* 媒体日志目录：<exeDir>\..\..\Logs（仅当路径确实是 Tools\DeployUI 结构） */
    wchar_t work[MAX_PATH];                // 工作副本
    lstrcpynW(work, exe, MAX_PATH);        // 复制
    size_t wl = wcslen(work);              // 长度
    if (wl > 0 && work[wl - 1] == L'\\') work[--wl] = L'\0'; // 去尾斜杠
    slash = wcsrchr(work, L'\\');          // 取末段
    if (slash && _wcsicmp(slash + 1, L"DeployUI") == 0) // 末段是 DeployUI
    {
        *slash = L'\0';                    // 上溯一级
        slash = wcsrchr(work, L'\\');      // 再取末段
        if (slash && _wcsicmp(slash + 1, L"Tools") == 0) // 是 Tools
        {
            *slash = L'\0'; // work = <盘>\<CustomPE_NAME> // 上溯到媒体根
            wchar_t cand[MAX_PATH];        // 候选日志目录
            swprintf(cand, MAX_PATH, L"%s\\Logs\\", work); // <媒体根>\Logs
            if (DirWritable(cand))         // 可写
            {
                lstrcpynW(dir, cand, dirLen); // 使用
                return;                    // 返回
            }
        }
    }

    /* PE RAM 盘 X: */
    if (GetDriveTypeW(L"X:\\") == DRIVE_RAMDISK) // 处于 PE
    {
        wchar_t cand[MAX_PATH];            // 候选
        swprintf(cand, MAX_PATH, L"X:\\CustomPE\\Logs\\"); // RAM 盘日志目录
        if (DirWritable(cand))             // 可写
        {
            lstrcpynW(dir, cand, dirLen);  // 使用
            return;                        // 返回
        }
    }

    /* 最后：exe 所在目录 */
    lstrcpynW(dir, exe, dirLen);           // 兜底
}

static void EnsureLogFile()                 // 确保日志文件已打开
{
    if (g_hLogFile != INVALID_HANDLE_VALUE) return; // 已打开

    wchar_t dir[MAX_PATH] = L"";           // 日志目录
    GetLogDir(dir, MAX_PATH);              // 计算目录

    SYSTEMTIME st;                         // 系统时间
    GetLocalTime(&st);                     // 取本地时间
    swprintf(g_logFilePath, MAX_PATH,      // 生成文件名（带时间戳）
             L"%lsWinPE_log_%04u%02u%02u_%02u%02u%02u.log",
             dir, st.wYear, st.wMonth, st.wDay,
             st.wHour, st.wMinute, st.wSecond);
    g_hLogFile = CreateFileW(g_logFilePath, FILE_APPEND_DATA, // 追加模式打开/创建
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, NULL);
}

static void WriteUtf8Line(HANDLE h, const wchar_t* line) // 以 UTF-8 写一行
{
    char utf8[4096] = "";                  // UTF-8 缓冲
    int len = WideCharToMultiByte(CP_UTF8, 0, line, -1, // 转换
                                  utf8, sizeof(utf8) - 2, NULL, NULL);
    if (len > 1) len--; // 去掉结尾 NUL // 去掉结尾符
    if (len > 0)                            // 有内容
    {
        DWORD written = 0;                 // 写入计数
        WriteFile(h, utf8, (DWORD)len, &written, NULL); // 写内容
        WriteFile(h, "\r\n", 2, &written, NULL); // 写换行
        FlushFileBuffers(h); // 实时落盘，便于运行中分析 // 立即刷盘
    }
}

static void WriteLogFile(const wchar_t* line) // 写入运行日志文件
{
    EnsureLogFile();                       // 确保文件打开
    if (g_hLogFile != INVALID_HANDLE_VALUE) WriteUtf8Line(g_hLogFile, line); // 写一行
}

/* ============ 独立部署日志：<Logs>\Deploy.log ============ */
static HANDLE g_hDeployLogFile = INVALID_HANDLE_VALUE; // 部署日志句柄
static wchar_t g_deployLogPath[MAX_PATH] = L"";        // 部署日志路径

static void OpenDeployLog()                 // 打开独立部署日志（覆盖旧文件）
{
    if (g_hDeployLogFile != INVALID_HANDLE_VALUE) return; // 已打开
    wchar_t dir[MAX_PATH] = L"";           // 日志目录
    GetLogDir(dir, MAX_PATH);              // 计算
    swprintf(g_deployLogPath, MAX_PATH, L"%lsDeploy.log", dir); // 固定名 Deploy.log
    g_hDeployLogFile = CreateFileW(g_deployLogPath, FILE_APPEND_DATA, // 追加打开
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, // 覆盖创建
        FILE_ATTRIBUTE_NORMAL, NULL);
}

static void WriteDeployLog(const wchar_t* line) // 写部署日志
{
    if (g_hDeployLogFile == INVALID_HANDLE_VALUE) return; // 未打开
    WriteUtf8Line(g_hDeployLogFile, line);  // 写行
}

static void CloseDeployLog()                // 关闭部署日志
{
    if (g_hDeployLogFile != INVALID_HANDLE_VALUE) // 有效
    {
        FlushFileBuffers(g_hDeployLogFile); // 刷盘
        CloseHandle(g_hDeployLogFile);      // 关闭
        g_hDeployLogFile = INVALID_HANDLE_VALUE; // 置无效
    }
}

void BottomPanelLog(const wchar_t* line)    // 统一日志入口（时间戳+三路输出）
{
    if (!line) return;                      // 空行保护
    SYSTEMTIME st;                          // 时间
    GetLocalTime(&st);                      // 取时间
    wchar_t fmt[1200];                      // 格式化缓冲
    swprintf(fmt, 1200, L"[%02u:%02u:%02u] %ls", // 前缀时间戳
             st.wHour, st.wMinute, st.wSecond, line);
    AppendLogLine(fmt);                     // 写日志窗口
    WriteLogFile(fmt);                      // 写运行日志文件
    WriteDeployLog(fmt);                    // 写部署日志文件
}

static void CloseLogFile()                  // 关闭运行日志文件
{
    if (g_hLogFile != INVALID_HANDLE_VALUE) // 有效
    {
        FlushFileBuffers(g_hLogFile);       // 刷盘
        CloseHandle(g_hLogFile);            // 关闭
        g_hLogFile = INVALID_HANDLE_VALUE;  // 置无效
    }
}

typedef struct DeployThreadCtx              // 部署线程上下文
{
    HWND      hwnd;                         // 接收消息的窗口句柄
    DeployJob job;                          // 部署任务副本
} DeployThreadCtx;

static void DeployLogCallback(const wchar_t* line, void* user) // 引擎日志回调（工作线程）
{
    DeployThreadCtx* ctx = (DeployThreadCtx*)user; // 上下文
    if (!ctx || !ctx->hwnd || !line) return; // 无效
    size_t len = wcslen(line) + 1;          // 长度含结尾符
    wchar_t* copy = (wchar_t*)malloc(len * sizeof(wchar_t)); // 分配副本
    if (!copy) return;                      // 失败
    wcscpy_s(copy, len, line);              // 复制
    PostMessageW(ctx->hwnd, BM_MSG_DEPLOY_LOG, (WPARAM)copy, 0); // 投递到 UI 线程
}

static void DeployProgressCallback(int pct, void* user) // 引擎进度回调（工作线程）
{
    DeployThreadCtx* ctx = (DeployThreadCtx*)user; // 上下文
    if (ctx && ctx->hwnd)                   // 有效
        PostMessageW(ctx->hwnd, BM_MSG_DEPLOY_PROG, (WPARAM)pct, 0); // 投递进度
}

static DWORD WINAPI DeployWorkerThread(LPVOID param) // 部署工作线程入口
{
    DeployThreadCtx* ctx = (DeployThreadCtx*)param; // 上下文
    const int rc = DeployRun(&ctx->job, DeployLogCallback, // 执行部署
                             DeployProgressCallback, ctx);
    PostMessageW(ctx->hwnd, BM_MSG_DEPLOY_DONE, (WPARAM)rc, (LPARAM)ctx); // 通知完成
    return (DWORD)rc;                       // 返回结果
}

static void StartDeploy(HWND hWnd)          // 启动部署
{
    if (g_deploying) return;                // 已在部署则忽略

    DeployJob job;                          // 部署任务
    wchar_t err[256] = L"";                 // 错误缓冲
    if (!ContentPanelBuildDeployJob(&job, err, 256)) // 收集参数失败
    {
        ShowEnglishMessageBox(hWnd,         // 英文提示框
            err[0] ? err : L"Deployment prerequisites are not complete.", // 原因
            L"Deploy System", MB_OK | MB_ICONWARNING); // 标题/样式
        return;                             // 返回
    }

    const BOOL demoMode = g_demoOn;         // 取演示开关
    job.DemoMode = demoMode ? TRUE : FALSE; // 写入任务

    if (!demoMode && DeployIsCurrentBootDisk(job.DiskIndex)) // 非演示且目标是启动盘
    {
        ShowEnglishMessageBox(hWnd,         // 拦截提示
            L"This disk currently holds the running PE/Windows system.\n"
            L"Deployment is blocked to protect the boot media.",
            L"Deploy System", MB_OK | MB_ICONWARNING);
        return;                             // 返回
    }
    const wchar_t* confirmText = demoMode  // 确认文案按模式区分
        ? L"Demo mode: NO disk changes will be made.\n"
          L"The partition script will be generated for review, then "
          L"deployment steps will be simulated.\n\nContinue?"
        : L"WARNING: This operation will ERASE the entire target disk "
          L"and install Windows onto it.\n\n"
          L"Make sure all important data has been backed up.\n"
          L"Continue deployment?";
    if (ShowEnglishMessageBox(hWnd, confirmText, // 用户确认
            L"Deploy System", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
        return;                             // 未确认返回

    g_deploying = TRUE;                     // 标记部署中
    SetWindowTextW(g_hLog, L"");            // 清空日志窗口
    if (g_hProgress) SendMessageW(g_hProgress, PBM_SETPOS, 0, 0); // 进度归零
    EnableWindow(g_hDeployBtn, FALSE);      // 禁用部署按钮
    if (g_hDemoCheck) EnableWindow(g_hDemoCheck, FALSE); // 禁用演示开关
    OpenDeployLog(); // 独立部署日志 Deploy.log（覆盖上一次） // 打开部署日志
    BottomPanelLog(L"");                    // 空行
    BottomPanelLog(demoMode                 // 记录开始
        ? L"==================== Deployment START (DEMO MODE) ===================="
        : L"==================== Deployment START (REAL MODE) ====================");
    if (demoMode)                           // 演示模式附加提示
        BottomPanelLog(L"Demo mode check: ON -> real deployment skipped, simulating only");

    DeployThreadCtx* ctx = (DeployThreadCtx*)malloc(sizeof(DeployThreadCtx)); // 分配上下文
    if (!ctx)                               // 分配失败
    {
        g_deploying = FALSE;                // 复位状态
        return;                             // 返回
    }
    ctx->hwnd = hWnd;                       // 记录窗口
    ctx->job = job;                         // 复制任务

    HANDLE hThread = CreateThread(NULL, 0, DeployWorkerThread, ctx, 0, NULL); // 创建线程
    if (!hThread)                           // 创建失败
    {
        free(ctx);                          // 释放上下文
        g_deploying = FALSE;                // 复位
        ContentPanelSetEnabled(TRUE);       // 恢复卡片
        StatusBarPanelSetEnabled(TRUE);     // 恢复状态栏按钮
        if (g_hDemoCheck) EnableWindow(g_hDemoCheck, TRUE); // 恢复开关
        EnableWindow(g_hDeployBtn, (g_step == 3) ? TRUE : FALSE); // 恢复按钮
        return;                             // 返回
    }
    ContentPanelSetEnabled(FALSE); // 部署期间锁定三张卡片 // 锁定卡片
    StatusBarPanelSetEnabled(FALSE);        // 锁定重启/关机
    CloseHandle(hThread);                   // 关闭线程句柄（线程继续运行）
}

/**
 * @brief 底部面板窗口过程
 */
static LRESULT CALLBACK BottomPanelProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) // 窗口过程
{
    switch (message)                        // 消息分发
    {
    case WM_CREATE:                         // 创建面板
    {
        CREATESTRUCTW* pCreate = (CREATESTRUCTW*)lParam; // 创建参数
        g_hMainParent = pCreate->hwndParent; // 记录父窗口
        HINSTANCE hInst = pCreate->hInstance; // 实例句柄

        // 进度条是 comctl32 公共控件，先注册其窗口类
        INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_PROGRESS_CLASS }; // 初始化结构
        InitCommonControlsEx(&icc);         // 注册公共控件

        // --- 1. 日志窗口（上方区域）：绿色背景，通过 WM_CTLCOLOR* 着色 ---
        g_hLog = CreateWindowExW(           // 创建日志编辑框
            WS_EX_CLIENTEDGE,
            L"EDIT",
            L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | // 子窗口/滚动/多行
            ES_READONLY | ES_AUTOVSCROLL | ES_WANTRETURN, // 只读/自动滚动/回车换行
            0, 0, 0, 0,
            hWnd,                           // 父窗口
            (HMENU)IDC_LOG_WINDOW,          // 控件 ID
            hInst,
            NULL
        );

        // --- 2. 下方控制区子控件 ---

        // 2.1 左侧：状态文本（橙黄色背景，通过 WM_CTLCOLOR* 着色）
        g_hStatusText = CreateWindowExW(    // 状态文本控件
            0, L"STATIC", L"Status: Step 0", // 初始文本
            WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE | SS_CENTER, // 水平垂直居中
            0, 0, 0, 0,
            hWnd, (HMENU)IDC_STATUS_TEXT, hInst, NULL
        );

        // 2.2 中间：进度条（紫色背景 + 白色填充，便于看清进度条实际区域）
        g_hProgress = CreateWindowExW(      // 创建进度条
            0, PROGRESS_CLASSW, L"",
            WS_CHILD | WS_VISIBLE | PBS_SMOOTH, // 平滑进度
            0, 0, 0, 0,
            hWnd, (HMENU)IDC_PROGRESS_BAR, hInst, NULL
        );
        if (g_hProgress)                    // 成功
        {
            // 扁平进度条：深色轨道 + 高对比绿色填充
            SendMessageW(g_hProgress, PBM_SETBKCOLOR, 0, (LPARAM)g_uiPalette.ProgressTrack); // 轨道色（读配置）
            SendMessageW(g_hProgress, PBM_SETBARCOLOR, 0, (LPARAM)g_uiPalette.ProgressFill); // 填充色（读配置）
        }

        // 2.3 右侧：部署按钮（原生 BUTTON，系统绘制）
        g_hDeployBtn = CreateWindowExW(     // 创建部署按钮
            0, L"BUTTON", L"Deploy System",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, // owner-draw 彩色反馈
            0, 0, 0, 0,
            hWnd, (HMENU)IDC_DEPLOY_BUTTON, hInst, NULL
        );

        // 2.4 演示模式（Dry-Run）：点击切换开/关（原生 BUTTON）
        g_hDemoCheck = CreateWindowExW(     // 创建演示开关
            0, L"BUTTON", L"Demo Mode",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, // owner-draw 彩色反馈
            0, 0, 0, 0,
            hWnd, (HMENU)IDC_DEMO_CHECK, hInst, NULL
        );

        ApplyChildFonts();                  // 应用全局字体
        return 0;                           // 完成
    }

    case WM_SIZE:                           // 尺寸变化
    {
        RECT rc;                            // 客户区
        GetClientRect(hWnd, &rc);           // 取区域
        const int width = rc.right;         // 宽
        const int height = rc.bottom;       // 高
        if (width <= 0 || height <= 0) return 0; // 防御

        // DPI 变化时顶层会先重建全局字体再触发父链 WM_SIZE，
        // 这里做一次"字体自愈"，避免文本控件继续持有已被删除的旧字体句柄
        if (g_GlobalFont != g_hAppliedChildFont) ApplyChildFonts(); // 字体自愈

        // 边距与全局一致（由父窗口客户区高度推导）
        const int margin = GetLayoutMargin(hWnd); // 取边距

        // 垂直布局：上边距 + 日志区 + 间隙 + 控制区 + 下边距
        // 共扣除 3 个 margin（上、中、下）
        int availH = height - margin * 3;   // 可用高度
        if (availH < 0) availH = 0;         // 防御

        // 控制行（状态文本/进度条/部署按钮）与顶部状态栏等高：
        // 二者同为单行工具条，视觉上保持一致；日志区占据其余全部高度
        int ctrlH = g_uiLayout[UI_WIN_STATUS].Rect.Height; // 控制行高度=状态栏高度（读配置表）
        if (ctrlH < 0) ctrlH = 0;           // 防御
        if (ctrlH > availH) ctrlH = availH; // 窗口过小时防止日志区高度为负 // 上限
        const int logH = availH - ctrlH;    // 日志区吸收剩余高度
        const int bodyW = width - margin * 2; // 扣除左右边距 // 内容宽度
        if (bodyW < 0) return 0;            // 防御

        // 日志窗口：上/左/右留边距
        if (g_hLog) MoveWindow(g_hLog, margin, margin, bodyW, logH, TRUE); // 布局日志

        // 控制行：与日志区间隔一个 margin
        const int ctrlY = margin + logH + margin; // 控制行 Y
        const int gap = margin; // 行内三个控件之间的间隙与全局边距一致 // 控件间距
        const int innerW = bodyW - gap * 3; // 扣除行内三个间隙 // 控件总宽
        if (innerW < 0) return 0;           // 防御

        const int statusW = innerW * g_uiBottomParams.StatusPct / 100;   // 状态文本占比（读配置）
        const int progressW = innerW * g_uiBottomParams.ProgressPct / 100; // 进度条占比（读配置）
        const int demoW = innerW * g_uiBottomParams.DemoPct / 100;         // 演示开关占比（读配置）
        const int btnW = innerW - statusW - progressW - demoW;             // 余量给部署按钮

        int x = margin;                     // 横向游标
        if (g_hStatusText) MoveWindow(g_hStatusText, x, ctrlY, statusW, ctrlH, TRUE); // 状态
        x += statusW + gap;                 // 前进
        if (g_hProgress) MoveWindow(g_hProgress, x, ctrlY, progressW, ctrlH, TRUE); // 进度条
        x += progressW + gap;               // 前进
        if (g_hDemoCheck) MoveWindow(g_hDemoCheck, x, ctrlY, demoW, ctrlH, TRUE); // 开关
        x += demoW + gap;                   // 前进
        if (g_hDeployBtn) MoveWindow(g_hDeployBtn, x, ctrlY, btnW, ctrlH, TRUE); // 部署按钮
        return 0;                           // 完成
    }

    // 日志 / 状态文本等子控件绘制前请求背景色
    case WM_CTLCOLOREDIT:                   // 编辑框取色
    case WM_CTLCOLORSTATIC:                 // 静态文本取色
    {
        const int id = GetDlgCtrlID((HWND)lParam); // 子控件 ID
        HBRUSH hb = NULL;                   // 返回画刷
        if (id == IDC_LOG_WINDOW)           // 日志框
        {
            hb = g_BrushPool[7]; // 日志区：终端黑 // 黑色背景
            HDC hdc = (HDC)wParam;          // 子控件 DC
            // 必须用不透明背景：否则只读 EDIT 滚动时不擦除旧文字，造成重叠
            SetBkMode(hdc, OPAQUE);         // 不透明背景
            SetBkColor(hdc, g_uiPalette.LogBg);          // 背景色（读配置）
            SetTextColor(hdc, g_uiPalette.LogFg);        // 前景色（读配置）
            return (LRESULT)hb;             // 返回画刷
        }
        else if (id == IDC_STATUS_TEXT)     // 状态文本
        {
            hb = g_BrushPool[GetStepBrushIndex(g_step)]; // 状态区：随完成步骤取卡片颜色
            HDC hdc = (HDC)wParam;          // DC
            SetBkMode(hdc, TRANSPARENT);    // 透明背景
            SetTextColor(hdc, g_uiTheme[UI_WIN_BOTTOM].Foreground); // 文字色（读配置）
            return (LRESULT)hb;             // 返回画刷
        }

        return DefWindowProcW(hWnd, message, wParam, lParam); // 其余交给系统
    }

    // 演示/部署按钮自绘：颜色来自 UIConfig 调色板，按下/禁用都有状态色
    case WM_DRAWITEM:                       // owner-draw 绘制
    {
        DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lParam; // 绘制信息
        if (dis && dis->CtlID == IDC_DEMO_CHECK) // 演示开关
        {
            HDC hdc = dis->hDC;             // DC
            RECT rc = dis->rcItem;          // 控件矩形
            const COLORREF face = (dis->itemState & ODS_DISABLED) // 禁用
                ? (g_demoOn ? g_uiPalette.DemoDisabledOn : g_uiPalette.DemoDisabledOff)
                : (g_demoOn ? g_uiPalette.DemoOn : g_uiPalette.DemoOff); // 开绿/关灰
            HBRUSH hb = CreateSolidBrush(face); // 建画刷
            FillRect(hdc, &rc, hb ? hb : g_BrushPool[1]); // 填充
            if (hb) DeleteObject(hb);       // 释放
            SetBkMode(hdc, TRANSPARENT);    // 透明背景
            SetTextColor(hdc, g_uiTheme[UI_WIN_BOTTOM].Foreground); // 文字色
            if (g_GlobalFont) SelectObject(hdc, g_GlobalFont); // 全局字体
            DrawTextW(hdc, L"Demo Mode", -1, &rc, // 文字
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            return TRUE;                    // 已绘制
        }
        if (dis && dis->CtlID == IDC_DEPLOY_BUTTON) // 部署按钮
        {
            HDC hdc = dis->hDC;             // DC
            RECT rc = dis->rcItem;          // 矩形
            const COLORREF face = (dis->itemState & ODS_DISABLED) // 禁用灰
                ? g_uiPalette.DeployDisabled
                : ((dis->itemState & ODS_SELECTED) ? g_uiPalette.DeployPressed // 按下深红
                                                   : g_uiPalette.DeployNormal); // 正常红
            HBRUSH hb = CreateSolidBrush(face); // 建画刷
            FillRect(hdc, &rc, hb ? hb : g_BrushPool[1]); // 填充
            if (hb) DeleteObject(hb);       // 释放
            SetBkMode(hdc, TRANSPARENT);    // 透明背景
            SetTextColor(hdc, g_uiTheme[UI_WIN_BOTTOM].Foreground); // 文字色
            if (g_GlobalFont) SelectObject(hdc, g_GlobalFont); // 字体
            wchar_t text[64];               // 文本缓冲
            GetWindowTextW(dis->hwndItem, text, 64); // 取文字
            RECT rcText = rc;               // 文本矩形
            if (dis->itemState & ODS_SELECTED) OffsetRect(&rcText, 1, 1); // 按下 1px 反馈
            DrawTextW(hdc, text, -1, &rcText, // 画文字
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            return TRUE;                    // 已绘制
        }
        return DefWindowProcW(hWnd, message, wParam, lParam); // 其余交系统
    }

    case BM_MSG_DEPLOY_LOG:                 // 部署日志消息（UI 线程）
    {
        wchar_t* line = (wchar_t*)wParam;   // 工作线程分配的副本
        if (line)                           // 有效
        {
            BottomPanelLog(line);           // 写入三路日志
            free(line);                     // 释放副本
        }
        return 0;                           // 完成
    }

    case BM_MSG_DEPLOY_PROG:                // 部署进度消息
        if (g_hProgress)                    // 进度条存在
            SendMessageW(g_hProgress, PBM_SETPOS, (WPARAM)wParam, 0); // 设置位置
        return 0;                           // 完成

    case BM_MSG_DEPLOY_DONE:                // 部署完成消息
    {
        const int rc = (int)wParam;         // 结果码
        DeployThreadCtx* ctx = (DeployThreadCtx*)lParam; // 上下文
        g_deploying = FALSE;                // 复位部署中
        ContentPanelSetEnabled(TRUE); // 恢复卡片（含按步骤解锁） // 恢复卡片
        StatusBarPanelSetEnabled(TRUE);     // 恢复状态栏按钮
        if (g_hDemoCheck) EnableWindow(g_hDemoCheck, TRUE); // 恢复开关
        EnableWindow(g_hDeployBtn, (g_step == 3) ? TRUE : FALSE); // 恢复部署按钮
        BottomPanelLog(L"");                // 空行
        BottomPanelLog(rc == 0              // 结果日志
            ? L"==================== Deployment COMPLETE ===================="
            : L"==================== Deployment FAILED (see log above) ====================");
        CloseDeployLog(); // Deploy.log 收尾 // 关闭部署日志
        ShowEnglishMessageBox(hWnd,         // 完成提示框
            rc == 0
                ? L"Deployment completed successfully.\nYou can restart the computer now."
                : L"Deployment failed. See the log for details.",
            L"Deploy System",               // 标题
            rc == 0 ? (MB_OK | MB_ICONINFORMATION) // 成功图标
                    : (MB_OK | MB_ICONERROR)); // 失败图标
        if (ctx) free(ctx);                 // 释放线程上下文
        return 0;                           // 完成
    }

    case WM_COMMAND:                        // 控件命令
    {
        // 处理按钮点击事件
        if (LOWORD(wParam) == IDC_DEPLOY_BUTTON && // 部署按钮
            HIWORD(wParam) == BN_CLICKED)
        {
            StartDeploy(hWnd);              // 启动部署
        }
        else if (LOWORD(wParam) == IDC_DEMO_CHECK && // 演示开关
                 HIWORD(wParam) == BN_CLICKED && !g_deploying) // 非部署中
        {
            g_demoOn = !g_demoOn;           // 切换开关
            BottomPanelLog(g_demoOn         // 记录状态
                ? L"Demo mode: ON (no disk writes)"
                : L"Demo mode: OFF (real deployment)");
            if (g_hDemoCheck) InvalidateRect(g_hDemoCheck, NULL, TRUE); // 重绘开关
        }
        return 0;                           // 完成
    }

    case WM_DESTROY:                        // 销毁
        CloseLogFile();                     // 关闭日志文件
        return 0;                           // 完成

    case WM_DPICHANGED:                     // DPI 变化
    case WM_DPICHANGED_AFTERPARENT:         // 父链 DPI 变化（PMv2）
        // PMv2 下子窗口实际收到 AFTERPARENT；两种入口都统一刷新子控件字体
        ApplyChildFonts();                  // 刷新字体
        InvalidateRect(hWnd, NULL, TRUE);   // 重绘
        return 0;                           // 完成

    default:                                // 其余消息
        return DefWindowProcW(hWnd, message, wParam, lParam); // 交给系统
    }
}

BOOL BottomPanelRegisterClass(HINSTANCE hInstance) // 底部面板注册类赋值函数
{
    const wchar_t CLASS_NAME[] = L"BottomPanelClass"; // 类名
    WNDCLASSEXW probe = { sizeof(probe) };        // 探测结构
    if (GetClassInfoExW(hInstance, CLASS_NAME, &probe)) return TRUE; // 已注册

    WNDCLASSEXW wc = { 0 };                       // 类结构
    wc.cbSize = sizeof(WNDCLASSEXW);              // 大小
    wc.lpfnWndProc = BottomPanelProc;             // 窗口过程
    wc.hInstance = hInstance;                     // 实例
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);    // 光标
    // 面板背景：蓝色（子窗口四周的边距间隙显示为蓝色）
    wc.hbrBackground = g_BrushPool[2];            // 背景画刷（来自 UIConfig 主题）
    wc.lpszClassName = CLASS_NAME;                // 类名
    return RegisterClassExW(&wc) != 0;            // 注册并返回结果
}

HWND RegisterBottomPanel(HWND hwndParent, const WindowLayoutConfig& layout, HINSTANCE hInstance) // 创建底部面板
{
    BottomPanelRegisterClass(hInstance);          // 类注册（重复调用自动跳过）

    return CreateWindowExW(0, L"BottomPanelClass", L"", // 创建窗口
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, // 添加 WS_CLIPCHILDREN 防止重绘闪烁
        layout.StartX, layout.StartY, layout.Width, layout.Height, // 位置大小
        hwndParent, NULL, hInstance, NULL); // 父窗口等
}
