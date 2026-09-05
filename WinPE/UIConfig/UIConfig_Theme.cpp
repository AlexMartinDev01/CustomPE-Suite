/* ============================================================
 * UIConfig 第三步：主题修改函数
 * ------------------------------------------------------------
 * 每个窗口一个主题函数。当前阶段职责：
 *   1. 依据 g_uiTheme[] 中的颜色创建全局画刷；
 *   2. 保证画刷池下标与现有面板引用保持一致；
 *   3. 字体规格已由 Init 阶段写入主题，后续 WM_SETFONT 下发。
 * UI_ApplyThemes() 依序调用全部窗口主题函数。
 * ============================================================ */
#include "../GlobalResources.h"  // GetGlobalBrush / g_BrushPool
#include "UIConfig.h"            // 本模块头

/* ==================== 主窗口主题 ==================== */
void UI_ThemeMainWindow(void) // 主窗口：创建背景画刷 → 画刷池[0]
{
    UI_Theme* T = &g_uiTheme[UI_WIN_MAIN];      // 主窗口主题
    /* 画刷池 [0] 被主窗口类背景引用，颜色来自主题 */
    GetGlobalBrush(T->Background);              // 按主题背景创建画刷
}

/* ==================== 子窗口①：状态栏主题 ==================== */
void UI_ThemeStatusWindow(void) // 状态栏：创建背景画刷 → 画刷池[1]
{
    UI_Theme* T = &g_uiTheme[UI_WIN_STATUS];    // 状态栏主题
    GetGlobalBrush(T->Background);              // 创建背景画刷（池[1]）
}

/* ==================== 子窗口③：底部面板主题（先于内容创建，保持池下标） ==================== */
void UI_ThemeBottomWindow(void) // 底部面板：创建背景画刷 → 画刷池[2]
{
    UI_Theme* T = &g_uiTheme[UI_WIN_BOTTOM];    // 底部主题
    GetGlobalBrush(T->Background);              // 创建背景画刷（池[2]）
}

/* ==================== 子窗口②：内容面板主题 ==================== */
void UI_ThemeContentWindow(void) // 内容面板：创建背景画刷 → 画刷池[3]
{
    UI_Theme* T = &g_uiTheme[UI_WIN_CONTENT];   // 内容主题
    GetGlobalBrush(T->Background);              // 创建背景画刷（池[3]）
}

/* ==================== 总主题应用 ==================== */
void UI_ApplyThemes(void) // 依序应用全部主题（顺序=画刷池既有下标约定）
{
    /* 注意顺序：主窗口[0]→状态栏[1]→底部[2]→内容[3]，
     * 与现有面板引用 g_BrushPool 的下标保持一致 */
    UI_ThemeMainWindow();   // 画刷池 [0] = 主窗口背景
    UI_ThemeStatusWindow(); // 画刷池 [1] = 状态栏背景
    UI_ThemeBottomWindow(); // 画刷池 [2] = 底部面板背景
    UI_ThemeContentWindow();// 画刷池 [3] = 内容面板背景
}

/* ==================== 细节调色板应用 ==================== */
void UI_ApplyPalette(void) // 按既有下标顺序创建画刷池 [4]..[9]
{
    GetGlobalBrush(g_uiPalette.Card1);         // [4] 卡片① 蓝
    GetGlobalBrush(g_uiPalette.Card2);         // [5] 卡片② 琥珀
    GetGlobalBrush(g_uiPalette.Card3);         // [6] 卡片③ 紫
    GetGlobalBrush(g_uiPalette.LogBg);         // [7] 日志区终端黑
    GetGlobalBrush(g_uiPalette.StatusNeutral); // [8] 状态区中性灰
    GetGlobalBrush(g_uiPalette.Danger);        // [9] 危险红
}
