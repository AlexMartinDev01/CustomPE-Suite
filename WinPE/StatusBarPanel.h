#pragma once // 预处理指令：确保本头文件只被包含一次
#include <windows.h> // 引入 Windows API：HWND/HINSTANCE/UINT 等
#include "WindowLayoutConfig.h" // 引入布局配置结构（注册接口参数类型）

HWND RegisterStatusBarPanel(HWND hwndParent, const WindowLayoutConfig& layout, HINSTANCE hInstance); // 注册并创建顶部状态栏

// 状态栏注册类赋值函数（供 UI_RegisterAllClasses 统一调度）
BOOL StatusBarPanelRegisterClass(HINSTANCE hInstance);

// 部署期间锁定/恢复重启、关机按钮
void StatusBarPanelSetEnabled(BOOL enabled); // 按钮锁定接口

// 英文确认框（与重启/关机同款）：按钮文字为 Yes / No，样式与原生 MessageBox 一致
int ShowEnglishMessageBox(HWND hOwner, const wchar_t* text, // 英文消息框（首次声明）
                          const wchar_t* caption, UINT uType);
