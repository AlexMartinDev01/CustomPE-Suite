#pragma once // 预处理指令：确保本头文件只被包含一次
#include <windows.h> // 引入 Windows API：HINSTANCE/HWND 等

// 定义窗口上下文结构体（Context）
// 用于在窗口模块和主程序之间传递数据，避免使用全局变量
struct WindowContext {
    HINSTANCE hInstance; // 应用程序实例句柄（由操作系统分配，代表当前运行的程序）
};

// 对外提供的一键初始化接口（注册窗口类 + 创建窗口）
// 参数：context 指针（包含程序实例句柄等上下文信息）
// 返回值：成功返回窗口句柄（HWND），失败返回 NULL
HWND RegisterMainWindow(WindowContext * context); // 注册并创建主窗口

// 主窗口注册类赋值函数（供 UI_RegisterAllClasses 统一调度）
BOOL MainWindowRegisterClass(HINSTANCE hInstance);
