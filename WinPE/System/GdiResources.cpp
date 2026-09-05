/* ============================================================
 * System/GdiResources.cpp —— 全局 GDI 资源（画刷池/全局字体）
 * 实现原 Main.cpp 的资源管理部分，供所有面板共享。
 * ============================================================ */
#include "../GlobalResources.h" // extern 声明与常量（MAX_BRUSHES 等）
#include "../UIConfig/UIConfig.h" // 字体规格来自主题配置

HBRUSH g_BrushPool[MAX_BRUSHES] = { 0 }; // 全局画刷池：按下标取用各面板配色
int    g_BrushCount = 0;                 // 画刷池中已创建画刷数量
HFONT  g_GlobalFont = NULL;              // 全局字体（所有面板共用）

// 获取全局画刷：按颜色创建并缓存，防越界与泄漏
HBRUSH GetGlobalBrush(COLORREF color)
{
    if (g_BrushCount < MAX_BRUSHES)       // 容量检查
    {
        HBRUSH hBrush = CreateSolidBrush(color); // 创建纯色画刷
        if (hBrush)
        {
            g_BrushPool[g_BrushCount++] = hBrush; // 入池
            return hBrush;
        }
    }
    return NULL;
}

// 统一销毁所有全局画刷（程序退出时调用）
void DestroyGlobalBrushes(void)
{
    for (int i = 0; i < g_BrushCount; i++) // 遍历池
    {
        if (g_BrushPool[i])
        {
            DeleteObject(g_BrushPool[i]); // 释放
            g_BrushPool[i] = NULL;        // 防悬空
        }
    }
    g_BrushCount = 0;                     // 计数复位
}

// 创建全局字体：规格来自 UIConfig 主窗口主题，按 DPI 缩放
HFONT CreateGlobalFont(int dpi)
{
    if (g_GlobalFont) return g_GlobalFont; // 已创建直接复用
    const UI_FontSpec* F = &g_uiTheme[UI_WIN_MAIN].Font; // 主题字体规格
    const wchar_t* face = (F->Face && F->Face[0]) ? F->Face : L"Microsoft YaHei UI"; // 缺省
    const int sizePt = F->SizePt > 0 ? F->SizePt : 9;  // 缺省 9pt
    const int weight = F->Weight > 0 ? F->Weight : FW_NORMAL; // 缺省常规
    int fontHeight = -MulDiv(sizePt, dpi, 72); // 按 DPI 换算高度（负值=字符高度）
    g_GlobalFont = CreateFontW(fontHeight, 0, 0, 0, weight, F->Italic, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, face);
    if (!g_GlobalFont)
        g_GlobalFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT); // 兜底默认 GUI 字体
    return g_GlobalFont;
}

// 销毁全局字体（退出或 DPI 重建前）
void DestroyGlobalFont(void)
{
    if (g_GlobalFont && g_GlobalFont != GetStockObject(DEFAULT_GUI_FONT))
        DeleteObject(g_GlobalFont);
    g_GlobalFont = NULL;
}
