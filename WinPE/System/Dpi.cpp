/* ============================================================
 * System/Dpi.cpp —— DPI 感知与查询（实现原 Main.cpp 基础设施）
 * ============================================================ */
#include "../GlobalResources.h" // extern int gb_dpi（进程级 DPI 状态）
#include "System.h"             // 本层头文件

int gb_dpi = 96; // 当前显示器 DPI 全局状态（标准 96，高 DPI 为 120/144 等）

// 启用 Per-Monitor V2 DPI 感知（Win10 1607+），老系统回退到系统级 DPI 感知
void InitDpiAwareness(void)
{
    typedef BOOL(WINAPI* PFN_SetProcessDpiAwarenessContext)(DPI_AWARENESS_CONTEXT); // 动态函数指针
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll"); // 取 user32 模块，避免重复加载
    if (hUser32)
    {
        PFN_SetProcessDpiAwarenessContext pfn = // 解析导出函数
            (PFN_SetProcessDpiAwarenessContext)GetProcAddress(hUser32, "SetProcessDpiAwarenessContext");
        if (pfn && pfn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
            return; // Per-Monitor V2 生效
    }
    SetProcessDPIAware(); // 回退：系统级 DPI 感知（Vista+）
}

// 获取指定窗口所在显示器的 DPI（优先 GetDpiForWindow，失败回退 GDI）
int GetWindowDpi(HWND hWnd)
{
    typedef UINT(WINAPI* PFN_GetDpiForWindow)(HWND); // 动态函数指针
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (hUser32)
    {
        PFN_GetDpiForWindow pfn = (PFN_GetDpiForWindow)GetProcAddress(hUser32, "GetDpiForWindow");
        if (pfn)
        {
            UINT dpi = pfn(hWnd);
            if (dpi != 0) return (int)dpi;
        }
    }
    HDC hdc = GetDC(hWnd);                    // 窗口 DC
    int dpi = GetDeviceCaps(hdc, LOGPIXELSX); // 水平 DPI
    ReleaseDC(hWnd, hdc);                     // 释放
    return dpi;
}

// 获取系统当前 DPI（创建窗口前使用）
int GetSystemDpi(void)
{
    typedef UINT(WINAPI* PFN_GetDpiForSystem)(void); // 动态函数指针
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (hUser32)
    {
        PFN_GetDpiForSystem pfn = (PFN_GetDpiForSystem)GetProcAddress(hUser32, "GetDpiForSystem");
        if (pfn)
        {
            UINT dpi = pfn();
            if (dpi != 0) return (int)dpi;
        }
    }
    HDC hdc = GetDC(NULL);                    // 屏幕 DC
    int dpi = GetDeviceCaps(hdc, LOGPIXELSX); // 水平 DPI
    ReleaseDC(NULL, hdc);                     // 释放
    return dpi;
}

// 获取窗口 DPI 缩放因子（所有面板共用，替代各文件重复实现）
float GetWindowScaleFactor(HWND hWnd)
{
    int dpi = hWnd ? GetWindowDpi(hWnd) : GetSystemDpi(); // 优先窗口 DPI
    if (dpi <= 0) dpi = 96;                   // 防御：无效按 96
    return (float)dpi / 96.0f;                // 缩放因子
}
