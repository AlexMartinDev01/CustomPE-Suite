/* ============================================================
 * App.cpp —— 应用生命周期实现
 * 原 Main.cpp 的 wWinMain 主体整体收口到这里，
 * 顺序清晰、职责单一；窗口创建直接读取 UIConfig 预计算结果。
 * ============================================================ */
#include <windows.h>
#include "App.h"               // 本模块头
#include "MainWindow.h"        // 主窗口创建接口
#include "StatusBarPanel.h"    // 子窗口①创建接口
#include "ContentPanel.h"      // 子窗口②创建接口
#include "BottomPanel.h"       // 子窗口③创建接口 + 日志接口
#include "GlobalResources.h"   // gb_dpi / RelayoutChildPanels / 资源清理
#include "UIConfig/UIConfig.h" // 配置总调度与注册总函数
#include "System/System.h"     // DPI / 提权

// 把 UIConfig 计算好的矩形转成现有 Register* 接收的布局结构
static WindowLayoutConfig LayoutFromUiRect(const UI_Rect* r) // 适配器
{
    WindowLayoutConfig c = { 0 }; // 清零
    c.StartX = r->X;              // 起始 X
    c.StartY = r->Y;              // 起始 Y
    c.Width  = r->Width;          // 宽度
    c.Height = r->Height;         // 高度
    return c;                     // 返回
}

// 应用主流程入口
int WinPEMain(HINSTANCE hInstance, PWSTR pCmdLine, int nCmdShow)
{
    /* ---- 1. 提权自检（非管理员自动 runas 重启） ---- */
    if (!EnsureAdminAndRelaunch(pCmdLine))
        return 0; // 已请求提权或失败，当前实例退出

    /* ---- 2. DPI 感知与初始 DPI ---- */
    InitDpiAwareness();        // 必须早于任何窗口创建
    gb_dpi = GetSystemDpi();   // 系统 DPI 供 UIConfig 基准换算

    /* ---- 3. 配置：屏幕→逐窗口初始化→布局→主题，全部先算好 ---- */
    UI_ConfigAll();

    /* ---- 4. 注册：主窗口+三子窗口注册类赋值后统一注册 ---- */
    UI_RegisterAllClasses(hInstance);

    /* ---- 5. 创建主窗口（几何直接来自 UIConfig 结果） ---- */
    WindowContext context = { 0 };
    context.hInstance = hInstance;
    HWND mainhWnd = RegisterMainWindow(&context);
    if (!mainhWnd) return 1;

    /* ---- 6. 按主窗口真实 DPI 建字体、算初始布局 ---- */
    gb_dpi = GetWindowDpi(mainhWnd);
    CreateGlobalFont(gb_dpi);
    RelayoutChildPanels(mainhWnd); // 创建子窗口前先填好三块布局

    /* ---- 7. 按配置表依次创建三个子窗口 ---- */
    RegisterStatusBarPanel(mainhWnd,
        LayoutFromUiRect(&g_uiLayout[UI_WIN_STATUS].Rect), hInstance);
    RegisterBottomPanel(mainhWnd,
        LayoutFromUiRect(&g_uiLayout[UI_WIN_BOTTOM].Rect), hInstance);
    RegisterContentPanel(mainhWnd,
        LayoutFromUiRect(&g_uiLayout[UI_WIN_CONTENT].Rect), hInstance);

    /* ---- 8. 显示与消息循环 ---- */
    BottomPanelLog(L"WinPE started");
    ShowWindow(mainhWnd, SW_MAXIMIZE);            // 程序启动即最大化
    UpdateWindow(mainhWnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    /* ---- 9. 退出清理 ---- */
    DestroyGlobalFont();
    DestroyGlobalBrushes();
    return (int)msg.wParam;
}
