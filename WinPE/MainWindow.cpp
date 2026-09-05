// 引入自定义的主窗口模块头文件（包含 RegisterMainWindow 函数声明和 WindowContext 结构体定义）
#include "MainWindow.h"

// 【修改】引入全局资源头文件，包含 RelayoutChildPanels 布局更新函数及全局画刷池 g_BrushPool
#include "GlobalResources.h"
#include "UIConfig/UIConfig.h" // 主窗口几何直接读取配置表计算结果

// 将窗口类名提取为文件级别的静态常量
// 优点：避免在注册窗口类和创建窗口时重复写字符串，防止拼写错误导致窗口创建失败
static const wchar_t MAIN_WINDOW_CLASS_NAME[] = L"MainWindowClass";

/**
 * @brief 内部消息处理函数（Window Procedure）
 * @note  它是窗口的"大脑"。操作系统会把所有的鼠标、键盘、重绘等消息发送到这个函数
 *        使用 static 关键字修饰，表示该函数仅在当前的 .cpp 文件中可见，实现良好的封装性
 */
static LRESULT CALLBACK MainWindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
        // 当窗口首次创建时，系统会发送此消息
    case WM_CREATE:
    {
        // lParam 包含了一个指向 CREATESTRUCTW 结构体的指针，里面保存了创建窗口时传入的参数
        CREATESTRUCTW* pCreateStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);

        // 将传入的上下文指针 (context) 绑定到当前窗口句柄上 (存储在 GWLP_USERDATA 槽位中)
        // 这样在后续处理其他消息时，就可以通过 GetWindowLongPtr 随时取回这个上下文数据
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pCreateStruct->lpCreateParams));
        return 0; // 返回 0 表示成功处理了创建消息
    }

        // 当窗口跨显示器移动或系统 DPI 变化时，系统会发送此消息（需要 Per-Monitor V2 感知）
    case WM_DPICHANGED:
    {
        // wParam 的高 16 位是新 DPI，低 16 位是旧 DPI
        gb_dpi = (int)HIWORD(wParam);

        // 按新 DPI 重建全局字体，所有面板文字随之缩放
        DestroyGlobalFont();
        CreateGlobalFont(gb_dpi);

        // lParam 指向系统建议的新窗口矩形，按建议调整窗口位置和大小
        const RECT* prcNew = reinterpret_cast<const RECT*>(lParam);
        SetWindowPos(hWnd, NULL, prcNew->left, prcNew->top,
            prcNew->right - prcNew->left, prcNew->bottom - prcNew->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
        // 窗口尺寸变化会触发 WM_SIZE，进而自动重新布局子面板并触发重绘
        return 0;
    }

        // 当窗口大小发生变化（包括首次显示、用户拖拽边缘、最大化/还原）时，系统会发送此消息
    case WM_SIZE:
    {
        // 【修改】触发子面板重排函数，根据当前主窗口的最新尺寸，重新计算并调整上中下三个子窗口的位置和大小
        RelayoutChildPanels(hWnd);
        return 0;
    }

        // 当用户点击窗口的 "X" 关闭按钮，或调用 DestroyWindow 时，系统会发送此消息
    case WM_DESTROY:
    {
        // 向当前线程的主消息队列发送 WM_QUIT 消息，通知主程序的消息循环 (GetMessage) 退出
        PostQuitMessage(0);
        return 0;
    }

    default:
        // 对于所有未在本 switch 中处理的消息（如拖动、最小化、鼠标点击等），
        // 必须交给 Windows 默认的窗口过程处理，否则窗口会失去基本行为
        return DefWindowProcW(hWnd, message, wParam, lParam);
    }
}

/**
 * @brief 内部函数：向操作系统注册窗口类
 * @note  在 Windows 中，创建窗口前必须先向系统"注册"一种窗口类型（相当于定义一个模板）
 */
BOOL MainWindowRegisterClass(HINSTANCE hInstance)
{
    // 声明一个扩展窗口类结构体，并初始化为全 0（防止未初始化的垃圾值导致系统崩溃）
    WNDCLASSEXW wc = { 0 };

    WNDCLASSEXW probe = { sizeof(probe) };       // 探测结构
    if (GetClassInfoExW(hInstance, MAIN_WINDOW_CLASS_NAME, &probe)) return TRUE; // 已注册则直接成功

    wc.cbSize = sizeof(WNDCLASSEXW);      // 设置结构体的大小，这是 Windows API 的惯例，用于版本兼容
    wc.style = CS_HREDRAW | CS_VREDRAW;   // 设置窗口类样式：当窗口水平或垂直大小改变时，自动触发重绘
    wc.lpfnWndProc = MainWindowProc;      // 绑定消息处理函数（函数指针），所有该类的窗口都会用这个函数处理消息
    wc.hInstance = hInstance;             // 绑定当前程序的实例句柄
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW); // 加载系统默认的箭头鼠标光标
    // 【修改】使用全局背景画刷池中的 [0]（需在 Main.cpp 中提前通过 GetGlobalBrush 初始化）
    wc.hbrBackground = g_BrushPool[0]; // 主窗口固定使用画刷数组下标 0（黑色）

    wc.lpszClassName = MAIN_WINDOW_CLASS_NAME; // 指定刚才定义的窗口类名称，后续创建窗口时通过这个名字来匹配
    wc.hIcon = LoadIconW(NULL, IDI_APPLICATION);   // 加载系统默认的大图标（显示在 Alt+Tab 切换界面）
    wc.hIconSm = LoadIconW(NULL, IDI_APPLICATION); // 加载系统默认的小图标（显示在窗口左上角和任务栏）

    // 调用 Windows API 注册该窗口类，成功返回非零值，失败返回 0
    return RegisterClassExW(&wc) != 0;
}

/**
 * @brief 对外暴露的接口：注册窗口类并创建主窗口
 * @param context 指向窗口上下文结构体的指针（通常包含 hInstance 等全局信息）
 * @return 成功返回主窗口的句柄 (HWND)，失败返回 NULL
 */
HWND RegisterMainWindow(WindowContext* context)
{
    // 防御性编程：检查传入的指针是否为空，或者实例句柄是否有效，防止程序因空指针崩溃
    if (!context || !context->hInstance) return NULL;

    // 1. 注册窗口类，如果注册失败则直接返回 NULL，终止后续的创建流程
    if (!MainWindowRegisterClass(context->hInstance))
    {
        return NULL;
    }

    // 2. 调用 Windows API 创建窗口实例
    HWND hWnd = CreateWindowExW(
        0,                              // dwExStyle：扩展窗口样式，0 表示无额外扩展样式
        MAIN_WINDOW_CLASS_NAME,         // lpClassName：要创建的窗口类名称（必须与注册时一致）
        L"WinPE",                       // lpWindowName：窗口标题栏上显示的文字
        g_uiTheme[UI_WIN_MAIN].Style,       // dwStyle：无边框样式（来自 UIConfig 主题）
        g_uiLayout[UI_WIN_MAIN].Rect.X, // 窗口的左上角 X（UIConfig 已算好）
        g_uiLayout[UI_WIN_MAIN].Rect.Y, // 窗口的左上角 Y
        g_uiLayout[UI_WIN_MAIN].Rect.Width, // 窗口宽度
        g_uiLayout[UI_WIN_MAIN].Rect.Height, // 窗口高度
        NULL,                           // hWndParent：父窗口句柄，NULL 表示这是一个顶级窗口
        NULL,                           // hMenu：菜单句柄，NULL 表示使用窗口类默认的菜单
        context->hInstance,             // hInstance：当前程序的实例句柄
        context                         // lpParam：传递给 WM_CREATE 消息的附加数据（这里把 context 传进去，方便在消息循环中使用）
    );

    // 3. 如果窗口创建成功，强制发送 WM_PAINT 消息，确保窗口创建后立即绘制出内容
    if (hWnd != NULL) {
        UpdateWindow(hWnd);
    }

    // 返回创建好的窗口句柄，供主程序的消息循环使用
    return hWnd;
}
