/* ============================================================
 * UIConfig 第二步：布局计算函数
 * ------------------------------------------------------------
 * 职责：根据屏幕/父窗口客户区大小，计算主窗口与三个子窗口
 *       的起始位置和尺寸，写回 g_uiLayout[]（并镜像到旧全局
 *       gb_*Layout，保证现有面板模块无需改动即可工作）。
 * UI_RelayoutChildren：父窗口尺寸/DPI 变化时重算并应用。
 * ============================================================ */
#include "../GlobalResources.h"  // gb_*Layout 旧镜像（迁移期兼容）
#include "UIConfig.h"            // 本模块头

/* ==================== 主窗口布局计算 ==================== */
void UI_CalcMainWindow(int screenW, int screenH) // 主窗口：DPI 缩放 + 屏幕居中
{
    UI_WindowLayout* L = &g_uiLayout[UI_WIN_MAIN]; // 主窗口布局条目

    const int w = UI_GetBaseWindowWidth();          // 基准宽度按 DPI 缩放
    const int h = UI_GetBaseWindowHeight();         // 基准高度按 DPI 缩放

    L->Rect.X      = (screenW - w) / 2;             // 水平居中
    L->Rect.Y      = (screenH - h) / 2;             // 垂直居中
    L->Rect.Width  = w;                             // 写入宽度
    L->Rect.Height = h;                             // 写入高度
}

/* 计算三个子窗口矩形（共用内部函数：父客户区 + 百分比） */
static void CalcChildRects(int parentW, int parentH) // 子窗口几何核心算法
{
    const int margin = parentH * g_uiLayout[UI_WIN_MAIN].Percent / 100; // 边距=父高×1%
    int availH = parentH - margin * 4;              // 扣除 4 个边距后的可用高度
    int availW = parentW - margin * 2;              // 扣除左右边距后的可用宽度
    if (availH < 0) availH = 0;                     // 窗口过小防御
    if (availW < 0) availW = 0;                     // 窗口过小防御

    const int statusH = availH * g_uiLayout[UI_WIN_STATUS].Percent / 100; // 状态栏高度
    const int bottomH = availH * g_uiLayout[UI_WIN_BOTTOM].Percent / 100; // 底部高度
    int contentH = availH - statusH - bottomH;      // 内容区吸收整数截断余量
    if (contentH < 0) contentH = 0;                 // 防御

    int y = margin;                                 // 纵向游标从顶部边距开始

    /* ---- 子窗口①：状态栏 ---- */
    UI_WindowLayout* S = &g_uiLayout[UI_WIN_STATUS]; // 状态栏条目
    S->Rect.X      = margin;                        // 左留边距
    S->Rect.Y      = y;                             // 顶部
    S->Rect.Width  = availW;                        // 横贯可用宽
    S->Rect.Height = statusH;                       // 计算高度
    y += statusH + margin;                          // 游标下移

    /* ---- 子窗口②：内容面板 ---- */
    UI_WindowLayout* C = &g_uiLayout[UI_WIN_CONTENT]; // 内容条目
    C->Rect.X      = margin;                        // 左留边距
    C->Rect.Y      = y;                             // 状态栏之下
    C->Rect.Width  = availW;                        // 横贯可用宽
    C->Rect.Height = contentH;                      // 吸收余量后的高度
    y += contentH + margin;                         // 游标下移

    /* ---- 子窗口③：底部面板 ---- */
    UI_WindowLayout* B = &g_uiLayout[UI_WIN_BOTTOM]; // 底部条目
    B->Rect.X      = margin;                        // 左留边距
    B->Rect.Y      = y;                             // 内容之下
    B->Rect.Width  = availW;                        // 横贯可用宽
    B->Rect.Height = bottomH;                       // 计算高度

}

/* ==================== 三个子窗口布局计算 ==================== */
void UI_CalcChildWindows(int parentW, int parentH) // 三个子窗口布局入口
{
    CalcChildRects(parentW, parentH);               // 调用核心算法
}

/* ==================== 一次性布局全部窗口 ==================== */
void UI_LayoutAll(void)                             // 主窗口+子窗口一次算好
{
    UI_CalcMainWindow(g_uiScreenWidth, g_uiScreenHeight); // 先算主窗口
    UI_CalcChildWindows(g_uiLayout[UI_WIN_MAIN].Rect.Width,  // 以主窗口宽
                        g_uiLayout[UI_WIN_MAIN].Rect.Height); // 和高为父空间
}

/* ==================== 父窗口尺寸变化：重算+应用+重绘 ==================== */
void UI_RelayoutChildren(HWND hMain)                // 重排三个子窗口
{
    if (!hMain) return;                             // 无主窗口直接返回
    RECT rc;                                        // 主窗口客户区
    if (!GetClientRect(hMain, &rc)) return;         // 取客户区失败返回
    if (rc.right <= 0 || rc.bottom <= 0) return;    // 最小化/零尺寸防御

    CalcChildRects(rc.right, rc.bottom);            // 按最新客户区重算

    /* 按类名找到三个子窗口并应用新矩形 */
    HWND hStatus  = FindWindowExW(hMain, NULL, L"StatusBarPanelClass", NULL); // 状态栏
    HWND hBottom  = FindWindowExW(hMain, NULL, L"BottomPanelClass", NULL);    // 底部
    HWND hContent = FindWindowExW(hMain, NULL, L"ContentPanelClass", NULL);   // 内容

    if (hStatus) MoveWindow(hStatus, g_uiLayout[UI_WIN_STATUS].Rect.X, // 应用状态栏位置（读配置表）
                            g_uiLayout[UI_WIN_STATUS].Rect.Y,
                            g_uiLayout[UI_WIN_STATUS].Rect.Width,
                            g_uiLayout[UI_WIN_STATUS].Rect.Height, TRUE);
    if (hBottom) MoveWindow(hBottom, g_uiLayout[UI_WIN_BOTTOM].Rect.X, // 应用底部位置（读配置表）
                            g_uiLayout[UI_WIN_BOTTOM].Rect.Y,
                            g_uiLayout[UI_WIN_BOTTOM].Rect.Width,
                            g_uiLayout[UI_WIN_BOTTOM].Rect.Height, TRUE);
    if (hContent) MoveWindow(hContent, g_uiLayout[UI_WIN_CONTENT].Rect.X, // 应用内容位置（读配置表）
                             g_uiLayout[UI_WIN_CONTENT].Rect.Y,
                             g_uiLayout[UI_WIN_CONTENT].Rect.Width,
                             g_uiLayout[UI_WIN_CONTENT].Rect.Height, TRUE);

    InvalidateRect(hMain, NULL, TRUE);              // 统一通知重绘（清背景防残影）
}

/* ==================== 旧入口包装（保持 GlobalResources 兼容） ==================== */
void RelayoutChildPanels(HWND hMainWnd)             // 响应式布局更新入口
{
    UI_RelayoutChildren(hMainWnd);                  // 全部收口到 UIConfig 布局层
}
