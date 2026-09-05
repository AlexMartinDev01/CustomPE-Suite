#pragma once                     // 预处理指令：确保本头文件只被包含一次
#include <windows.h>              // 引入 Windows API：HWND/HINSTANCE/PWSTR 等

/* ============================================================
 * System —— 平台基础设施层
 * 职责：DPI 感知、提权自检、GDI 全局资源等与 UI 无关的系统能力。
 * 每个主题一个 .cpp，不依赖任何窗口模块。
 * ============================================================ */

/* ---- DPI（System/Dpi.cpp） ---- */
void InitDpiAwareness(void);  // 启用 Per-Monitor V2（旧系统回退）
int  GetWindowDpi(HWND hWnd); // 取窗口所在显示器 DPI
int  GetSystemDpi(void);      // 取系统 DPI（建窗前用）
float GetWindowScaleFactor(HWND hWnd); // 取窗口 DPI 缩放因子（dpi/96）

/* ---- 提权（System/Elevation.cpp） ---- */
BOOL IsProcessElevated(void);                  // 当前进程是否管理员
BOOL EnsureAdminAndRelaunch(PWSTR pCmdLine);   // 未提权则 runas 重启；返回 TRUE 表示可继续
