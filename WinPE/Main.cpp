/**
 * @file Main.cpp
 * @brief 应用程序入口（薄层）
 * @note  只做一件事：把控制权交给 App::WinPEMain。
 *        全局资源/DPI/提权/配置/生命周期均已按层拆分：
 *          System/    —— DPI、提权、GDI 资源
 *          UIConfig/  —— 布局/主题/注册调度
 *          App.cpp    —— 应用生命周期编排
 */

#include <windows.h> // Windows API：wWinMain 入口类型
#include "App.h"     // 应用主流程入口声明

// Win32 标准宽字符入口：由操作系统调用
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/,
                    PWSTR pCmdLine, int nCmdShow)
{
    // 全部实际逻辑收口在 App 层，本文件保持最薄
    return WinPEMain(hInstance, pCmdLine, nCmdShow);
}
