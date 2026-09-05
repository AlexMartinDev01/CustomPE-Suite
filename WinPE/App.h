#pragma once                 // 预处理指令：确保本头文件只被包含一次
#include <windows.h>          // Windows API：HINSTANCE/PWSTR/int

/* ============================================================
 * App —— 应用生命周期编排层
 * 职责：按固定顺序完成 提权 → DPI → 配置 → 注册 → 创建窗口
 *       → 消息循环 → 资源清理。Main.cpp 只保留薄入口。
 * ============================================================ */

// 应用主流程入口（由 wWinMain 调用）
int WinPEMain(HINSTANCE hInstance, PWSTR pCmdLine, int nCmdShow);
