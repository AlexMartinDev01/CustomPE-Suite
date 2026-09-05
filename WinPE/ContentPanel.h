#pragma once // 预处理指令：确保本头文件只被包含一次
#include <windows.h> // 引入 Windows API：HWND/BOOL/HINSTANCE 等
#include "WindowLayoutConfig.h" // 引入布局配置结构（注册接口参数类型）

struct DeployJob; // Utils/DeployEngine.h 中定义，避免头文件互相包含 // 前向声明

// 创建内容面板的接口
HWND RegisterContentPanel(HWND hwndParent, const WindowLayoutConfig& layout, HINSTANCE hInstance); // 注册类并创建面板

// 内容面板注册类赋值函数（同时注册面板类与卡片类，供统一调度）
BOOL ContentPanelRegisterClass(HINSTANCE hInstance);

// 当三步全部完成时，收集部署所需信息（目标盘/镜像/方案）
// 返回 TRUE 并填充 job；失败返回 FALSE 并把原因写入 err
BOOL ContentPanelBuildDeployJob(struct DeployJob* job, wchar_t* err, int errLen); // 组装部署任务

// 部署期间锁定/恢复三张卡片交互（恢复时按当前状态重新联动）
void ContentPanelSetEnabled(BOOL enabled); // 锁定/解锁接口
