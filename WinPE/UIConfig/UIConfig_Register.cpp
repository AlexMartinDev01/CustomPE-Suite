/* ============================================================
 * UIConfig 窗口注册总函数
 * ------------------------------------------------------------
 * 职责：统一调度每个窗口的“注册类赋值函数”，最后统一注册。
 * 每个窗口文件负责实现自己的注册类赋值函数：
 *   - MainWindowRegisterClass        （主窗口）
 *   - StatusBarPanelRegisterClass    （子窗口①）
 *   - ContentPanelRegisterClass      （子窗口②，含卡片类）
 *   - BottomPanelRegisterClass       （子窗口③）
 * 各函数内部均做 GetClassInfoEx 幂等保护，重复调用不会报错。
 * ============================================================ */
#include <windows.h>              // Windows API：HINSTANCE 等
#include "../MainWindow.h"        // 主窗口注册类赋值函数声明
#include "../StatusBarPanel.h"    // 子窗口①注册类赋值函数声明
#include "../ContentPanel.h"      // 子窗口②注册类赋值函数声明
#include "../BottomPanel.h"       // 子窗口③注册类赋值函数声明
#include "UIConfig.h"             // 本模块头（声明 UI_RegisterAllClasses）

/* ==================== 总注册 ==================== */
void UI_RegisterAllClasses(HINSTANCE hInstance) // 窗口注册总函数
{
    /* 主窗口注册类赋值 + 注册 */
    MainWindowRegisterClass(hInstance);

    /* 三个子窗口各自注册类赋值 + 注册 */
    StatusBarPanelRegisterClass(hInstance);  // 子① 状态栏
    ContentPanelRegisterClass(hInstance);    // 子② 内容面板（含卡片）
    BottomPanelRegisterClass(hInstance);     // 子③ 底部面板
}
