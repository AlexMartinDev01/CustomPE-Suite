#pragma once // 预处理指令：确保本头文件只被包含一次
#include <windows.h> // 引入 Windows API：HWND/HINSTANCE 等
#include "WindowLayoutConfig.h" // 引入布局配置结构（注册接口参数类型）

HWND RegisterBottomPanel(HWND hwndParent, const WindowLayoutConfig& layout, HINSTANCE hInstance); // 注册并创建底部面板

// 底部面板注册类赋值函数（供 UI_RegisterAllClasses 统一调度）
BOOL BottomPanelRegisterClass(HINSTANCE hInstance);

// 更新底部状态步数（0-3），并同步状态区背景色为对应卡片颜色：
//   1 -> 卡片①（浅蓝）  2 -> 卡片②（橙黄）  3 -> 卡片③（紫色）
void BottomPanelSetStep(int step); // 步骤状态更新接口

// 统一执行日志：带时间戳写入日志窗口，并实时追加到 exe 同目录的日志文件
void BottomPanelLog(const wchar_t* line); // 三路日志输出接口
