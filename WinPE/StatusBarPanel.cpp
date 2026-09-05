#include "StatusBarPanel.h"     // 引入本模块头：注册接口/锁定接口/英文消息框声明
#include "WindowLayoutConfig.h" // 引入布局配置结构（函数参数类型）
#include "GlobalResources.h"    // 引入全局资源：画刷池/全局字体
#include "UIConfig/UIConfig.h"  // 引入界面配置：状态栏内部控件布局参数
#include "System/System.h"      // 引入系统层：GetWindowScaleFactor（统一 DPI 缩放）
#include <windows.h>            // Windows API 核心头
#include <dwmapi.h>             // DwmSetWindowAttribute：消息框沉浸式深色模式
#pragma comment(lib, "dwmapi.lib") // 链接 DWM 库

// 旧版 SDK 可能未定义该常量（Windows 10 1809 为 19，2004+ 为 20）
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20 // 沉浸式深色模式属性号
#endif

// ==================== ID 定义 ====================
#define ID_PANEL_LEFT      2001 // 左侧面板（设备型号）ID
#define ID_PANEL_CENTER    2002 // 中间面板（日期时间）ID
#define ID_PANEL_RIGHT     2003 // 右侧面板（按钮区）ID
#define ID_BTN_REBOOT      1001 // 重启按钮 ID
#define ID_BTN_SHUTDOWN    1002 // 关机按钮 ID
#define STATUS_BAR_TIMER   3001 // 时钟刷新定时器（与按钮 ID 分开，避免混淆）

// ==================== 本模块全局状态 ====================
static HWND g_hStatusBar = NULL;   // 状态栏窗口句柄
static HWND g_hPanelLeft = NULL;   // 左侧面板句柄
static HWND g_hPanelCenter = NULL; // 中间面板句柄
static HWND g_hPanelRight = NULL;  // 右侧面板句柄
static HWND g_hBtnReboot = NULL;   // 重启按钮句柄
static HWND g_hBtnShutdown = NULL; // 关机按钮句柄
static HFONT g_hSymbolFont = NULL; // 按钮符号字体（DPI 自适应）
static float g_fScaleFactor = 1.0f; // 当前 DPI 缩放因子

// ==================== 关机/重启特权与兜底 ====================
static void EnableShutdownPrivilege()            // 启用关机特权
{
    HANDLE hToken = NULL;                        // 进程令牌句柄
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) // 打开令牌
    {
        TOKEN_PRIVILEGES tkp = { 0 };            // 特权结构
        tkp.PrivilegeCount = 1;                  // 一个特权
        tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED; // 启用
        if (LookupPrivilegeValueW(NULL, SE_SHUTDOWN_NAME, &tkp.Privileges[0].Luid)) // 查关机特权 LUID
        {
            AdjustTokenPrivileges(hToken, FALSE, &tkp, 0, NULL, NULL); // 调整令牌特权
        }
        CloseHandle(hToken);                     // 关闭令牌
    }
}

// WinPE 原生关机/重启工具（wpeutil.exe 只在 WinPE 里存在）；
// 完整 Windows 中没有该工具时返回 FALSE，由调用方走 ExitWindowsEx 兜底。
static BOOL RunWpeUtil(const wchar_t* action)    // 运行 wpeutil
{
    wchar_t sysDir[MAX_PATH] = L"";              // 系统目录
    if (!GetSystemDirectoryW(sysDir, MAX_PATH)) return FALSE; // 失败

    wchar_t exePath[MAX_PATH];                   // wpeutil 路径
    wsprintfW(exePath, L"%s\\wpeutil.exe", sysDir); // 拼路径
    if (GetFileAttributesW(exePath) == INVALID_FILE_ATTRIBUTES) return FALSE; // 不存在

    wchar_t cmdLine[MAX_PATH];                   // 命令行
    wsprintfW(cmdLine, L"wpeutil %s", action);   // 如 "wpeutil reboot"

    STARTUPINFOW si = { sizeof(si) };            // 启动信息
    PROCESS_INFORMATION pi = { 0 };              // 进程信息
    si.dwFlags = STARTF_USESHOWWINDOW;           // 使用窗口显示
    si.wShowWindow = SW_HIDE;                    // 隐藏
    if (!CreateProcessW(exePath, cmdLine, NULL, NULL, FALSE, // 创建进程
                        CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
        return FALSE;                            // 失败

    CloseHandle(pi.hThread);                     // 关线程句柄
    CloseHandle(pi.hProcess);                    // 关进程句柄
    return TRUE;                                 // 成功
}

// ==================== 英文 MessageBox（保持原生弹窗的位置和大小不变） ====================
// 说明：标准 MessageBox 的 Yes/No 按钮文字由系统按语言显示（中文系统为"是/否"）。
//       这里用线程级 CBT 钩子，在弹窗激活的瞬间把按钮文字替换为 "Yes"/"No"，
//       弹窗本身的位置、大小、样式与原生 MessageBox 完全一致。
static HHOOK g_hMsgBoxHook = NULL;               // 消息框 CBT 钩子句柄

static LRESULT CALLBACK MsgBoxCbtProc(int nCode, WPARAM wParam, LPARAM lParam) // CBT 钩子过程
{
    if (nCode == HCBT_ACTIVATE)                  // 弹窗即将激活
    {
        HWND hDlg = (HWND)wParam; // 即将被激活的 MessageBox 对话框 // 对话框句柄

        // 1. 让系统消息框使用沉浸式深色模式，与应用整体深色主题一致
        typedef HRESULT(WINAPI* PFN_DwmSetWindowAttribute)(HWND, DWORD, LPCVOID, DWORD); // 函数指针
        HMODULE hDwm = LoadLibraryW(L"dwmapi.dll"); // 加载 DWM
        if (hDwm)                                // 成功
        {
            PFN_DwmSetWindowAttribute pfn =      // 取函数
                (PFN_DwmSetWindowAttribute)GetProcAddress(hDwm, "DwmSetWindowAttribute");
            if (pfn)                             // 有函数
            {
                BOOL dark = TRUE;                // 深色开关
                if (FAILED(pfn(hDlg, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark)))) // 新版属性
                    pfn(hDlg, 19, &dark, sizeof(dark)); // 兼容旧版本属性号
            }
            FreeLibrary(hDwm);                   // 释放模块
        }

        // 2. 按钮文字统一为英文
        HWND hOk = GetDlgItem(hDlg, IDOK);       // OK 按钮
        HWND hYes = GetDlgItem(hDlg, IDYES);     // Yes 按钮
        HWND hNo = GetDlgItem(hDlg, IDNO);       // No 按钮
        HWND hCancel = GetDlgItem(hDlg, IDCANCEL); // Cancel 按钮
        if (hOk) SetWindowTextW(hOk, L"OK");     // 改 OK
        if (hCancel) SetWindowTextW(hCancel, L"Cancel"); // 改 Cancel
        if (hYes) SetWindowTextW(hYes, L"Yes");  // 改 Yes
        if (hNo) SetWindowTextW(hNo, L"No");     // 改 No

        // 将 Yes / No 两个按钮水平居中（保持原有垂直位置和按钮间距）
        if (hYes && hNo)                         // 两按钮都在
        {
            RECT rcDlg;                          // 对话框客户区
            GetClientRect(hDlg, &rcDlg);         // 取区域

            RECT rcYes, rcNo;                    // 两个按钮矩形
            GetWindowRect(hYes, &rcYes);         // 取 Yes 屏幕矩形
            GetWindowRect(hNo, &rcNo);           // 取 No 屏幕矩形
            POINT ptYes = { rcYes.left, rcYes.top }; // Yes 左上角
            POINT ptNo = { rcNo.left, rcNo.top };   // No 左上角
            ScreenToClient(hDlg, &ptYes);        // 转客户区坐标
            ScreenToClient(hDlg, &ptNo);         // 转客户区坐标

            const int wYes = rcYes.right - rcYes.left; // Yes 宽
            const int hBtn = rcYes.bottom - rcYes.top; // 按钮高
            const int wNo = rcNo.right - rcNo.left;   // No 宽
            const int gap = ptNo.x - ptYes.x - wYes; // 保持原始按钮间距 // 间距
            const int totalW = wYes + gap + wNo;     // 两按钮总宽

            int xStart = (rcDlg.right - totalW) / 2; // 水平居中
            if (xStart < 0) xStart = 0;             // 防御
            const int y = ptYes.y; // 垂直位置不变 // Y 不变

            MoveWindow(hYes, xStart, y, wYes, hBtn, TRUE); // 移动 Yes
            MoveWindow(hNo, xStart + wYes + gap, y, wNo, hBtn, TRUE); // 移动 No
        }

        // 只改一次，立即卸载钩子
        if (g_hMsgBoxHook)                       // 钩子有效
        {
            UnhookWindowsHookEx(g_hMsgBoxHook);  // 卸载
            g_hMsgBoxHook = NULL;                // 置空
        }
    }
    return CallNextHookEx(g_hMsgBoxHook, nCode, wParam, lParam); // 继续钩子链
}

// 显示英文确认框：与 MessageBoxW 完全相同，仅按钮文字为英文
int ShowEnglishMessageBox(HWND hOwner, const wchar_t* text, const wchar_t* caption, UINT uType) // 英文消息框
{
    g_hMsgBoxHook = SetWindowsHookExW(WH_CBT, MsgBoxCbtProc, NULL, GetCurrentThreadId()); // 装 CBT 钩子
    int result = MessageBoxW(hOwner, text, caption, uType); // 显示消息框
    if (g_hMsgBoxHook)                           // 钩子仍在（用户操作异常路径）
    {
        UnhookWindowsHookEx(g_hMsgBoxHook);      // 卸载
        g_hMsgBoxHook = NULL;                    // 置空
    }
    return result;                               // 返回点击结果
}

// ==================== 左侧面板：设备型号 ====================
static LRESULT CALLBACK LeftPanelProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) // 左侧面板过程
{
    if (msg == WM_PAINT)                         // 绘制
    {
        PAINTSTRUCT ps;                          // 绘制结构
        HDC hdc = BeginPaint(hWnd, &ps);         // 开始
        RECT rc;                                 // 客户区
        GetClientRect(hWnd, &rc);                // 取区域

        SetBkMode(hdc, TRANSPARENT);             // 透明背景
        SetTextColor(hdc, g_uiTheme[UI_WIN_STATUS].Foreground); // 文字色（读配置）
        if (g_GlobalFont) SelectObject(hdc, g_GlobalFont); // 全局字体（DPI 自适应）

        const float scale = GetWindowScaleFactor(hWnd); // 现场读取，避免使用缓存的旧缩放值
        const wchar_t* text = L"Model: PHN16S-71"; // 设备型号文案（当前硬编码）
        rc.left += (LONG)(g_uiStatusParams.ModelLeftPad * scale); // 左侧留白（读配置）
        DrawTextW(hdc, text, -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE); // 左对齐垂直居中

        EndPaint(hWnd, &ps);                     // 结束
        return 0;                                // 完成
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam); // 其余交系统
}

// ==================== 中间面板：当前日期时间 ====================
static LRESULT CALLBACK CenterPanelProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) // 中间面板过程
{
    if (msg == WM_PAINT)                         // 绘制
    {
        PAINTSTRUCT ps;                          // 绘制结构
        HDC hdc = BeginPaint(hWnd, &ps);         // 开始
        RECT rc;                                 // 客户区
        GetClientRect(hWnd, &rc);                // 取区域

        SetBkMode(hdc, TRANSPARENT);             // 透明
        SetTextColor(hdc, g_uiTheme[UI_WIN_STATUS].Foreground); // 文字色（读配置）
        if (g_GlobalFont) SelectObject(hdc, g_GlobalFont); // 全局字体

        SYSTEMTIME st;                           // 系统时间
        GetLocalTime(&st);                       // 取本地时间
        wchar_t timeStr[64];                     // 文本缓冲
        wsprintfW(timeStr, L"%04u-%02u-%02u  %02u:%02u:%02u", // 格式化日期时间
            (unsigned)st.wYear, (unsigned)st.wMonth, (unsigned)st.wDay,
            (unsigned)st.wHour, (unsigned)st.wMinute, (unsigned)st.wSecond);

        DrawTextW(hdc, timeStr, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE); // 居中画
        EndPaint(hWnd, &ps);                     // 结束
        return 0;                                // 完成
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam); // 其余交系统
}

// ==================== 右侧按钮布局 ====================
// 每次调用都现场读取当前窗口 DPI，避免使用缓存的缩放因子
// （PMv2 跨显示器拖动后，缓存值可能仍是旧 DPI）
static void LayoutRightPanelButtons(HWND hWnd)   // 布局右侧按钮
{
    RECT rc;                                     // 客户区
    GetClientRect(hWnd, &rc);                    // 取区域
    const float scale = GetWindowScaleFactor(hWnd); // 现场读取缩放

    // 按钮尺寸随 DPI 缩放，并限制不超过面板高度（避免在 5% 高度状态栏中溢出）
    int btnSize = (int)(g_uiStatusParams.BtnSizeBase * scale); // 按钮边长（读配置）
    int gap = (int)(g_uiStatusParams.BtnGap * scale);          // 间距（读配置）
    int margin = (int)(g_uiStatusParams.BtnMargin * scale);    // 右边距（读配置）
    if (btnSize > rc.bottom - 4) btnSize = rc.bottom - 4; // 不超高
    if (btnSize < 12) btnSize = 12;              // 最小

    int y = (rc.bottom - btnSize) / 2;           // 垂直居中
    if (g_hBtnShutdown) MoveWindow(g_hBtnShutdown, rc.right - btnSize - margin, y, btnSize, btnSize, TRUE); // 关机最右
    if (g_hBtnReboot) MoveWindow(g_hBtnReboot, rc.right - btnSize * 2 - gap - margin, y, btnSize, btnSize, TRUE); // 重启在左
}

// ==================== 右侧面板：按钮区 ====================
static LRESULT CALLBACK RightPanelProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) // 右侧面板过程
{
    switch (msg)                                 // 消息分发
    {
    case WM_SIZE:                                // 尺寸变化
        LayoutRightPanelButtons(hWnd); // 现场读取当前 DPI 重排按钮 // 重排
        return 0;                                // 完成

    case WM_COMMAND:                             // 命令消息
        // 【关键修复】按钮点击消息从 RightPanel 继续向上转发到状态栏窗口
        return SendMessageW(GetParent(hWnd), WM_COMMAND, wParam, lParam); // 转发给父
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam); // 其余交系统
}

// ==================== 状态栏窗口过程 ====================
// 把状态栏客户区三等分：左侧型号 / 中间时间 / 右侧按钮
static void LayoutStatusBarPanels(HWND hWnd)     // 布局三个子面板
{
    RECT rc;                                     // 客户区
    GetClientRect(hWnd, &rc);                    // 取区域
    int panelWidth = rc.right / 3;               // 每份宽度

    MoveWindow(g_hPanelLeft, 0, 0, panelWidth, rc.bottom, TRUE);      // 左
    MoveWindow(g_hPanelCenter, panelWidth, 0, panelWidth, rc.bottom, TRUE); // 中
    MoveWindow(g_hPanelRight, panelWidth * 2, 0, rc.right - panelWidth * 2, rc.bottom, TRUE); // 右（吃余量）
}

// 按当前 DPI 重建按钮符号字体（Segoe UI Symbol，白色图标）
static void RecreateSymbolFont(HWND hWnd)        // 重建符号字体
{
    if (g_hSymbolFont)                           // 旧字体存在
    {
        DeleteObject(g_hSymbolFont);             // 删除
        g_hSymbolFont = NULL;                    // 置空
    }

    g_fScaleFactor = GetWindowScaleFactor(hWnd); // 现场读取当前 DPI 缩放
    int fontSize = -(int)(g_uiStatusParams.SymbolFontSize * g_fScaleFactor); // 符号字号（读配置）
    g_hSymbolFont = CreateFontW(fontSize, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, // 创建粗体符号字体
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Symbol");

    if (g_hSymbolFont)                           // 创建成功
    {
        SendMessageW(g_hBtnReboot, WM_SETFONT, (WPARAM)g_hSymbolFont, TRUE); // 应用到重启钮
        SendMessageW(g_hBtnShutdown, WM_SETFONT, (WPARAM)g_hSymbolFont, TRUE); // 应用到关机钮
    }
}

static LRESULT CALLBACK StatusBarWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) // 状态栏窗口过程
{
    switch (msg)                                 // 消息分发
    {
    case WM_CREATE:                              // 创建
    {
        HINSTANCE hInst = ((LPCREATESTRUCTW)lParam)->hInstance; // 实例句柄
        g_fScaleFactor = GetWindowScaleFactor(hWnd); // 取缩放

        // 注册内部子类（重复注册时跳过）
        WNDCLASSEXW wc = { sizeof(wc) };         // 类结构
        wc.hInstance = hInst;                    // 实例
        wc.hbrBackground = g_BrushPool[1]; // 面板背景（状态栏主题色，原生擦除）
        wc.hCursor = LoadCursorW(NULL, IDC_HAND); // 手型光标

        if (!GetClassInfoExW(hInst, L"LeftPanelClass", &wc)) // 左面板未注册
        {
            wc.lpfnWndProc = LeftPanelProc;      // 过程
            wc.lpszClassName = L"LeftPanelClass"; // 类名
            RegisterClassExW(&wc);               // 注册
        }
        if (!GetClassInfoExW(hInst, L"CenterPanelClass", &wc)) // 中面板未注册
        {
            wc.lpfnWndProc = CenterPanelProc;    // 过程
            wc.lpszClassName = L"CenterPanelClass"; // 类名
            RegisterClassExW(&wc);               // 注册
        }
        if (!GetClassInfoExW(hInst, L"RightPanelClass", &wc)) // 右面板未注册
        {
            wc.lpfnWndProc = RightPanelProc;     // 过程
            wc.lpszClassName = L"RightPanelClass"; // 类名
            RegisterClassExW(&wc);               // 注册
        }
        // 创建三个子面板
        g_hPanelLeft = CreateWindowExW(0, L"LeftPanelClass", NULL, WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hWnd, (HMENU)ID_PANEL_LEFT, hInst, NULL); // 左
        g_hPanelCenter = CreateWindowExW(0, L"CenterPanelClass", NULL, WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hWnd, (HMENU)ID_PANEL_CENTER, hInst, NULL); // 中
        g_hPanelRight = CreateWindowExW(0, L"RightPanelClass", NULL, WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hWnd, (HMENU)ID_PANEL_RIGHT, hInst, NULL); // 右

        // 创建两个原生按钮（符号文字 + Segoe UI Symbol 字体，父为右侧面板）
        g_hBtnReboot = CreateWindowExW(0, L"BUTTON", L"\u21BB", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, g_hPanelRight, (HMENU)ID_BTN_REBOOT, hInst, NULL); // 重启（↻）
        g_hBtnShutdown = CreateWindowExW(0, L"BUTTON", L"\u23FB", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, g_hPanelRight, (HMENU)ID_BTN_SHUTDOWN, hInst, NULL); // 关机（⏻）

        RecreateSymbolFont(hWnd);                // 建符号字体
        LayoutStatusBarPanels(hWnd);             // 初始布局

        // 每秒刷新时钟
        SetTimer(hWnd, STATUS_BAR_TIMER, 1000, NULL); // 1 秒定时器
        return 0;                                // 完成
    }

    case WM_SIZE:                                // 尺寸变化
        LayoutStatusBarPanels(hWnd);             // 重排面板
        return 0;                                // 完成

    case WM_DPICHANGED:                          // DPI 变化
    case WM_DPICHANGED_AFTERPARENT:              // 父链 DPI 变化（PMv2）
        // PMv2 下子窗口实际收到 AFTERPARENT（顶层处理完 WM_DPICHANGED 后按父→子下发），
        // 两种入口共用同一刷新逻辑，保证符号字体与按钮尺寸随 DPI 同步缩放
        RecreateSymbolFont(hWnd);   // 内部现场读取 DPI 并按新 DPI 重建 // 重建字体
        LayoutStatusBarPanels(hWnd);             // 重排面板
        if (g_hPanelRight) LayoutRightPanelButtons(g_hPanelRight); // 面板尺寸未变时也强制重排按钮
        InvalidateRect(hWnd, NULL, TRUE);        // 重绘
        return 0;                                // 完成

    case WM_TIMER:                               // 定时器
        if (wParam == STATUS_BAR_TIMER && g_hPanelCenter) // 时钟定时器且中面板存在
        {
            // TRUE=先擦除背景：清除旧时间文字后再重绘，防止秒数变化时字体重叠
            InvalidateRect(g_hPanelCenter, NULL, TRUE); // 刷新时钟
        }
        return 0;                                // 完成

    case WM_COMMAND:                             // 按钮命令
    {
        const WORD id = LOWORD(wParam);          // 控件 ID
        if (HIWORD(wParam) == BN_CLICKED && id == ID_BTN_REBOOT) // 重启按钮
        {
            // 英文确认窗口：点击"重启"后必须先确认才执行
            if (ShowEnglishMessageBox(g_hStatusBar, // 确认框
                    L"Are you sure you want to restart this computer?",
                    L"Restart", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES) // 确认
            {
                EnableShutdownPrivilege();       // 启用特权
                // WinPE 环境优先用 wpeutil 原生命令（WinPE 内没有 shutdown.exe）
                if (!RunWpeUtil(L"reboot"))      // WinPE 工具失败
                {
                    // 完整 Windows 下的兜底
                    ExitWindowsEx(EWX_REBOOT | EWX_FORCE, SHTDN_REASON_MAJOR_OTHER); // 系统重启
                }
            }
        }
        else if (HIWORD(wParam) == BN_CLICKED && id == ID_BTN_SHUTDOWN) // 关机按钮
        {
            // 英文确认窗口：点击"关机"后必须先确认才执行
            if (ShowEnglishMessageBox(g_hStatusBar, // 确认框
                    L"Are you sure you want to shut down this computer?",
                    L"Shut Down", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES) // 确认
            {
                EnableShutdownPrivilege();       // 启用特权
                // WinPE 环境优先用 wpeutil 原生命令（WinPE 内没有 shutdown.exe）
                if (!RunWpeUtil(L"shutdown"))    // WinPE 工具失败
                {
                    // 完整 Windows 下的兜底
                    ExitWindowsEx(EWX_POWEROFF | EWX_FORCE, SHTDN_REASON_MAJOR_OTHER); // 系统关机
                }
            }
        }
        return 0;                                // 完成
    }

    case WM_DESTROY:                             // 销毁
        KillTimer(hWnd, STATUS_BAR_TIMER);       // 停定时器
        if (g_hSymbolFont)                       // 字体存在
        {
            DeleteObject(g_hSymbolFont);         // 删除
            g_hSymbolFont = NULL;                // 置空
        }
        return 0;                                // 完成
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam); // 其余交系统
}

// ==================== 对外接口 ====================
BOOL StatusBarPanelRegisterClass(HINSTANCE hInstance) // 状态栏注册类赋值函数
{
    WNDCLASSEXW probe = { sizeof(probe) };         // 探测结构
    if (GetClassInfoExW(hInstance, L"StatusBarPanelClass", &probe)) return TRUE; // 已注册

    WNDCLASSEXW wc = { sizeof(wc) };               // 类结构
    wc.lpfnWndProc = StatusBarWndProc;             // 窗口过程
    wc.hInstance = hInstance;                      // 实例
    wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH); // 无刷
    wc.lpszClassName = L"StatusBarPanelClass";     // 类名
    return RegisterClassExW(&wc) != 0;             // 注册并返回结果
}

HWND RegisterStatusBarPanel(HWND hwndParent, const WindowLayoutConfig& layout, HINSTANCE hInstance) // 创建状态栏
{
    StatusBarPanelRegisterClass(hInstance);        // 类注册（重复调用自动跳过）

    g_hStatusBar = CreateWindowExW(0, L"StatusBarPanelClass", NULL, // 创建
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, // 子窗口+裁剪子级
        layout.StartX, layout.StartY, layout.Width, layout.Height, // 位置大小
        hwndParent, NULL, hInstance, NULL);      // 父等

    return g_hStatusBar;                         // 返回句柄
}

void StatusBarPanelSetEnabled(BOOL enabled)      // 锁定/恢复重启关机按钮
{
    if (g_hBtnReboot) EnableWindow(g_hBtnReboot, enabled ? TRUE : FALSE); // 重启钮
    if (g_hBtnShutdown) EnableWindow(g_hBtnShutdown, enabled ? TRUE : FALSE); // 关机钮
}
