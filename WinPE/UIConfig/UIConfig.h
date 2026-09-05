#pragma once                     // 预处理指令：确保本头文件只被包含一次
#include <windows.h>              // 引入 Windows API：COLORREF/HWND/DWORD 等基础类型

/* ============================================================
 * UIConfig —— 界面配置模块（C 风格，存为 .cpp）
 * ------------------------------------------------------------
 * 职责：
 *   1. 统一存放每个窗口的几何、主题（颜色/字体/样式）；
 *   2. 每个窗口一个配置/布局/主题函数；
 *   3. UI_ConfigAll() 作为总调度，按顺序调用所有窗口函数；
 *   4. UI_RelayoutChildren() 在父窗口尺寸/DPI 变化时
 *      重算所有子窗口矩形并应用、通知重绘。
 * 当前模型：1 个主窗口 + 3 个顶层子窗口（状态栏/内容/底部）。
 * ============================================================ */

/* ---------------- 窗口枚举：作为全局数组下标 ---------------- */
typedef enum UI_WindowId
{
    UI_WIN_MAIN = 0,   // 主窗口
    UI_WIN_STATUS,     // 子窗口①：顶部状态栏
    UI_WIN_CONTENT,    // 子窗口②：中部内容面板
    UI_WIN_BOTTOM,     // 子窗口③：底部操作面板
    UI_WIN_COUNT       // 窗口总数（数组容量用）
} UI_WindowId;

/* ---------------- 几何结构体：大小 + 起始位置 ---------------- */
typedef struct UI_Rect
{
    int X;      // 起始 X（左上角）
    int Y;      // 起始 Y（左上角）
    int Width;  // 宽度
    int Height; // 高度
} UI_Rect;

/* ---------------- 字体结构体（之前缺失，现补齐） ---------------- */
typedef struct UI_FontSpec
{
    const wchar_t* Face; // 字体名（如 Microsoft YaHei UI）
    int            SizePt; // 字号（点）
    int            Weight; // 字重（FW_NORMAL/FW_BOLD）
    BOOL           Italic; // 是否斜体
} UI_FontSpec;

/* ---------------- 主题结构体：WIN32 外观/样式封装 ---------------- */
typedef struct UI_Theme
{
    COLORREF    Background; // 背景颜色
    COLORREF    Foreground; // 文字颜色
    COLORREF    Accent;     // 强调颜色（按钮/高亮）
    DWORD       Style;      // 窗口行为样式（WS_* 组合）
    DWORD       ExStyle;    // 扩展样式（WS_EX_* 组合）
    UI_FontSpec Font;       // 该窗口使用的字体
} UI_Theme;

/* ---------------- 布局结构体：每个窗口的几何 + 比例 ---------------- */
typedef struct UI_WindowLayout
{
    UI_Rect Rect;    // 计算后的最终矩形（创建/移动窗口直接使用）
    int      Percent; // 主窗口=边距百分比；子窗口=垂直占比（%）
} UI_WindowLayout;

/* ---------------- 状态栏内部控件布局参数 ---------------- */
typedef struct UI_StatusParams
{
    int BtnSizeBase;   // 按钮基准边长（96 DPI，随缩放因子放大）
    int BtnGap;        // 按钮间距基准
    int BtnMargin;     // 按钮距右边基准
    int SymbolFontSize;// 符号字体基准字号
    int ModelLeftPad;  // 型号文字左侧留白基准
    const wchar_t* SymbolFace; // 符号字体名（如 Segoe UI Symbol）
} UI_StatusParams;

/* ---------------- 内容面板内部控件布局参数 ---------------- */
typedef struct UI_ContentParams
{
    int CardMargin;    // 卡片内边距基准
    int CardPad;       // 控件间距基准
    int TitleH;        // 卡片标题栏高度基准
    int RowH;          // 按钮/输入行高基准
    int ComboH;        // 下拉框可见高度基准
    int DropH;         // 下拉列表展开高度基准
    int ListRatio;     // 方案列表占中间区比例（千分比 450=45%）
    int MinScanButtonW; // "Scan Disks" 按钮最小宽度基准
    int MinBrowseButtonW; // "Browse" 类按钮最小宽度基准
    int MinPathW;      // 路径输入框最小宽度基准
} UI_ContentParams;

/* ---------------- 底部面板内部控件布局参数 ---------------- */
typedef struct UI_BottomParams
{
    int StatusPct;     // 状态文本区宽占比（%）
    int ProgressPct;   // 进度条区宽占比（%）
    int DemoPct;       // 演示开关区宽占比（%）
    int MinBtnW;       // 部署按钮最小宽度
} UI_BottomParams;

/* ---------------- 细节调色板：内部控件使用的颜色 ---------------- */
typedef struct UI_Palette
{
    COLORREF Card1;            // 卡片① 高对比蓝
    COLORREF Card2;            // 卡片② 高对比琥珀
    COLORREF Card3;            // 卡片③ 高对比紫
    COLORREF LogBg;            // 日志区终端黑背景
    COLORREF LogFg;            // 日志区文字色
    COLORREF StatusNeutral;    // 状态区未完成步骤的中性灰
    COLORREF Danger;           // 重启/关机高对比红
    COLORREF ProgressTrack;    // 进度条轨道色
    COLORREF ProgressFill;     // 进度条填充绿
    COLORREF DemoDisabledOn;   // 演示开启但被禁用时的暗绿
    COLORREF DemoDisabledOff;  // 演示关闭且被禁用时的暗灰
    COLORREF DemoOn;           // 演示开启时的绿
    COLORREF DemoOff;          // 演示关闭时的灰
    COLORREF DeployDisabled;   // 部署按钮禁用灰
    COLORREF DeployPressed;    // 部署按钮按下深红
    COLORREF DeployNormal;     // 部署按钮正常红
} UI_Palette;

/* ==================== 全局配置数据（C 风格 extern） ==================== */
extern int            g_uiScreenWidth;   // 主屏幕宽度（像素）
extern int            g_uiScreenHeight;  // 主屏幕高度（像素）
extern int            g_uiDpi;           // 当前 DPI（96/120/144...）
extern UI_WindowLayout g_uiLayout[UI_WIN_COUNT]; // 每窗口布局（下标=UI_WindowId）
extern UI_Theme       g_uiTheme[UI_WIN_COUNT];   // 每窗口主题（下标=UI_WindowId）
extern UI_StatusParams  g_uiStatusParams;   // 状态栏内部控件布局参数
extern UI_ContentParams g_uiContentParams;  // 内容面板内部控件布局参数
extern UI_BottomParams  g_uiBottomParams;   // 底部面板内部控件布局参数
extern UI_Palette       g_uiPalette;        // 细节调色板

/* ==================== 第一步：初始化函数 ==================== */
void UI_InitScreen(void);        // 获取并保存屏幕大小 / DPI
void UI_InitWindowMain(void);    // 主窗口：默认位置样式 + 主题初值
void UI_InitWindowStatus(void);  // 子窗口①：默认布局比例 + 主题初值
void UI_InitWindowContent(void); // 子窗口②：默认布局比例 + 主题初值
void UI_InitWindowBottom(void);  // 子窗口③：默认布局比例 + 主题初值

/* 内部辅助：主窗口设计基准尺寸（按 DPI 缩放），供布局函数使用 */
int UI_GetBaseWindowWidth(void);
int UI_GetBaseWindowHeight(void);

/* ==================== 第二步：布局计算函数 ==================== */
void UI_CalcMainWindow(int screenW, int screenH);   // 主窗口布局（按屏幕居中/DPI 缩放）
void UI_CalcChildWindows(int parentW, int parentH); // 三个子窗口布局（按父客户区+百分比）
void UI_LayoutAll(void);                            // 主窗口+子窗口一次算好
void UI_RelayoutChildren(HWND hMain);               // 父尺寸变化：重算并 MoveWindow+重绘

/* ==================== 第三步：主题修改函数 ==================== */
void UI_ThemeMainWindow(void);    // 主窗口主题应用
void UI_ThemeStatusWindow(void);  // 子窗口①主题应用
void UI_ThemeContentWindow(void); // 子窗口②主题应用
void UI_ThemeBottomWindow(void);  // 子窗口③主题应用
void UI_ApplyThemes(void);        // 依序应用全部窗口主题（含画刷创建）
void UI_ApplyPalette(void);       // 应用细节调色板（创建画刷池 4..9）

/* ==================== 总调度 ==================== */
void UI_ConfigAll(void);          // 依序：屏幕→各窗口初始化→布局→主题

/* ==================== 窗口注册总函数 ==================== */
void UI_RegisterAllClasses(HINSTANCE hInstance); // 统一注册所有窗口类
