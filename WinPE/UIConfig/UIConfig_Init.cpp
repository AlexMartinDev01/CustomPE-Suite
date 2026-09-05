/* ============================================================
 * UIConfig 第一步：全局变量定义 + 屏幕信息 + 各窗口初始化
 * ------------------------------------------------------------
 * 每个窗口一个独立初始化函数；UI_InitScreen 在最前。
 * 这里只“定义默认值”，真正的坐标计算在 Layout 文件里完成。
 * ============================================================ */
#include "../GlobalResources.h"  // 引用 gb_dpi 等现有全局（迁移期内保持镜像）
#include "UIConfig.h"            // 本模块头：结构体/数组/函数声明

/* ==================== 全局配置变量定义 ==================== */
int             g_uiScreenWidth  = 0;     // 主屏幕宽度，初始化后由 UI_InitScreen 填充
int             g_uiScreenHeight = 0;     // 主屏幕高度，初始化后由 UI_InitScreen 填充
int             g_uiDpi          = 96;    // 当前 DPI，默认 96（标准）
UI_WindowLayout g_uiLayout[UI_WIN_COUNT]; // 每窗口布局表（几何+比例）
UI_Theme        g_uiTheme[UI_WIN_COUNT];  // 每窗口主题表（颜色+样式+字体）
UI_StatusParams  g_uiStatusParams;   // 状态栏内部控件布局参数（96 DPI 基准）
UI_ContentParams g_uiContentParams;  // 内容面板内部控件布局参数（96 DPI 基准）
UI_BottomParams  g_uiBottomParams;   // 底部面板内部控件布局参数（96 DPI 基准）
/* 细节调色板默认值（与改造前界面一致，集中一处便于修改） */
UI_Palette g_uiPalette = {
    RGB(0, 120, 215),    // Card1 蓝（Win8 强调蓝）
    RGB(247, 99, 12),    // Card2 橙（Win8 橙）
    RGB(177, 70, 194),   // Card3 紫（Win8 紫）
    RGB(255, 255, 255),  // LogBg 白色日志背景
    RGB(28, 28, 28),     // LogFg 深色文字
    RGB(117, 117, 117),  // StatusNeutral 中灰（白字可读）
    RGB(196, 43, 28),    // Danger 红（Win8 红）
    RGB(208, 208, 208),  // ProgressTrack 浅灰轨道
    RGB(0, 120, 215),    // ProgressFill 蓝色填充
    RGB(168, 198, 226),  // DemoDisabledOn 浅蓝
    RGB(200, 200, 200),  // DemoDisabledOff 浅灰
    RGB(0, 120, 215),    // DemoOn 蓝
    RGB(158, 158, 158),  // DemoOff 灰
    RGB(190, 190, 190),  // DeployDisabled 浅灰
    RGB(163, 30, 21),    // DeployPressed 深红
    RGB(232, 17, 35)     // DeployNormal 亮红
};

/* ---------------- 内部常量：设计基准值（96 DPI） ---------------- */
static const int UI_BASE_WINDOW_WIDTH  = 1280; // 主窗口设计宽度
static const int UI_BASE_WINDOW_HEIGHT = 720;  // 主窗口设计高度

/* ---------------- 默认字体：全局通用 ---------------- */
static const wchar_t UI_FONT_FACE[] = L"Microsoft YaHei UI"; // 微软雅黑 UI
static const int     UI_FONT_SIZE   = 9;                    // 9pt
static const int     UI_FONT_WEIGHT = FW_NORMAL;            // 常规字重

/* ---------------- 工具：填充一个窗口的默认主题 ---------------- */
static void SetDefaultFont(UI_FontSpec* f) // 写入通用字体规格
{
    f->Face   = UI_FONT_FACE;   // 字体名
    f->SizePt = UI_FONT_SIZE;   // 字号
    f->Weight = UI_FONT_WEIGHT; // 字重
    f->Italic = FALSE;          // 非斜体
}

/* ==================== 屏幕/DPI 信息 ==================== */
void UI_InitScreen(void) // 读取并保存屏幕宽高与当前 DPI
{
    g_uiScreenWidth  = GetSystemMetrics(SM_CXSCREEN); // 主屏物理宽度
    g_uiScreenHeight = GetSystemMetrics(SM_CYSCREEN); // 主屏物理高度
    g_uiDpi          = gb_dpi ? gb_dpi : 96;          // 取现有全局 DPI，无则 96
}

/* ==================== 主窗口初始化 ==================== */
void UI_InitWindowMain(void) // 主窗口：边距百分比 + 深色主题 + 常规窗口样式
{
    UI_WindowLayout* L = &g_uiLayout[UI_WIN_MAIN]; // 布局条目
    UI_Theme*        T = &g_uiTheme[UI_WIN_MAIN];  // 主题条目

    L->Percent = 1;                        // 边距占客户区高度的 1%（布局引擎使用）
    L->Rect.X = L->Rect.Y = 0;             // 起始坐标由 UI_CalcMainWindow 计算
    L->Rect.Width = L->Rect.Height = 0;    // 尺寸由 UI_CalcMainWindow 计算

    T->Background = RGB(240, 240, 240);    // 主窗口背景：Win8 浅灰
    T->Foreground = RGB(28, 28, 28);       // 前景文字：深色
    T->Accent     = RGB(0, 120, 215);      // 强调色：Win8 蓝
    T->Style      = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN; // 有边框常规主窗口
    T->ExStyle    = 0;                     // 无扩展样式
    SetDefaultFont(&T->Font);              // 全局字体
}

/* ==================== 子窗口①：顶部状态栏 ==================== */
void UI_InitWindowStatus(void) // 状态栏：5% 高 + 深蓝灰主题
{
    UI_WindowLayout* L = &g_uiLayout[UI_WIN_STATUS]; // 布局条目
    UI_Theme*        T = &g_uiTheme[UI_WIN_STATUS];  // 主题条目

    L->Percent = 5;                         // 垂直占比 5%
    L->Rect.X = L->Rect.Y = 0;              // 由布局函数填充
    L->Rect.Width = L->Rect.Height = 0;     // 由布局函数填充

    T->Background = RGB(0, 120, 215);       // 状态栏背景：Win8 蓝
    T->Foreground = RGB(255, 255, 255);     // 文字白色
    T->Accent     = RGB(255, 255, 255);     // 强调色：白色（符号按钮区）
    T->Style      = WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN; // 子窗口
    T->ExStyle    = 0;                      // 无扩展
    SetDefaultFont(&T->Font);               // 全局字体

    g_uiStatusParams.BtnSizeBase    = 30;   // 按钮基准边长 30px
    g_uiStatusParams.BtnGap         = 8;    // 按钮间距 8px
    g_uiStatusParams.BtnMargin      = 10;   // 右边距 10px
    g_uiStatusParams.SymbolFontSize = 18;   // 符号字号基准 18px
    g_uiStatusParams.ModelLeftPad   = 20;   // 型号文字左留白 20px
    g_uiStatusParams.SymbolFace     = L"Segoe UI Symbol"; // 符号字体名
}

/* ==================== 子窗口②：中部内容面板 ==================== */
void UI_InitWindowContent(void) // 内容面板：65% 高 + 深灰蓝主题
{
    UI_WindowLayout* L = &g_uiLayout[UI_WIN_CONTENT]; // 布局条目
    UI_Theme*        T = &g_uiTheme[UI_WIN_CONTENT];  // 主题条目

    L->Percent = 65;                        // 垂直占比 65%
    L->Rect.X = L->Rect.Y = 0;              // 由布局函数填充
    L->Rect.Width = L->Rect.Height = 0;     // 由布局函数填充

    T->Background = RGB(255, 255, 255);     // 内容背景：白色
    T->Foreground = RGB(255, 255, 255);     // 卡片标题文字：白色（画在彩色卡片上）
    T->Accent     = RGB(0, 120, 215);       // 强调色：Win8 蓝
    T->Style      = WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN; // 子窗口
    T->ExStyle    = 0;                      // 无扩展
    SetDefaultFont(&T->Font);               // 全局字体

    g_uiContentParams.CardMargin = 10;      // 卡片内边距基准
    g_uiContentParams.CardPad    = 6;       // 控件间距基准
    g_uiContentParams.TitleH     = 30;      // 标题栏高度基准
    g_uiContentParams.RowH       = 26;      // 输入行高度基准
    g_uiContentParams.ComboH     = 22;      // 下拉可见高度基准
    g_uiContentParams.DropH      = 180;     // 下拉展开高度基准
    g_uiContentParams.ListRatio  = 450;     // 方案列表占中间区 45%（千分比）
    g_uiContentParams.MinScanButtonW   = 72;  // Scan 按钮最小宽度基准
    g_uiContentParams.MinBrowseButtonW = 56;  // Browse 按钮最小宽度基准
    g_uiContentParams.MinPathW         = 40;  // 路径框最小宽度基准
}

/* ==================== 子窗口③：底部操作面板 ==================== */
void UI_InitWindowBottom(void) // 底部面板：30% 高 + 深蓝灰主题
{
    UI_WindowLayout* L = &g_uiLayout[UI_WIN_BOTTOM]; // 布局条目
    UI_Theme*        T = &g_uiTheme[UI_WIN_BOTTOM];  // 主题条目

    L->Percent = 30;                        // 垂直占比 30%
    L->Rect.X = L->Rect.Y = 0;              // 由布局函数填充
    L->Rect.Width = L->Rect.Height = 0;     // 由布局函数填充

    T->Background = RGB(235, 235, 235);     // 底部背景：Win8 浅灰
    T->Foreground = RGB(255, 255, 255);     // 按钮文字：白色（画在彩色按钮上）
    T->Accent     = RGB(0, 120, 215);       // 强调色：Win8 蓝（部署/进度）
    T->Style      = WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN; // 子窗口
    T->ExStyle    = 0;                      // 无扩展
    SetDefaultFont(&T->Font);               // 全局字体

    g_uiBottomParams.StatusPct   = 15;      // 状态文本区宽占比
    g_uiBottomParams.ProgressPct = 50;      // 进度条区宽占比
    g_uiBottomParams.DemoPct     = 15;      // 演示开关区宽占比
    g_uiBottomParams.MinBtnW     = 120;     // 部署按钮最小宽度
}

/* 主窗口基准尺寸按 DPI 缩放（内部使用，供布局文件调用） */
int UI_GetBaseWindowWidth(void)             // 暴露设计宽度（内部辅助，供布局计算）
{
    return UI_BASE_WINDOW_WIDTH * g_uiDpi / 96; // 按 DPI 放大
}

int UI_GetBaseWindowHeight(void)            // 暴露设计高度（内部辅助，供布局计算）
{
    return UI_BASE_WINDOW_HEIGHT * g_uiDpi / 96; // 按 DPI 放大
}
