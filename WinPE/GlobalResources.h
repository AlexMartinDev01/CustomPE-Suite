#pragma once // 预处理指令：确保本头文件只被包含一次
#include <windows.h> // 引入 Windows API：HBRUSH/HFONT/COLORREF 等
#include "WindowLayoutConfig.h" // 引入布局配置结构（保持类型一致；几何现由 UIConfig 管理）

// 说明：旧 gb_screenWidth/gb_*Layout/gb_*Percent 镜像已删除，
//       统一改由 UIConfig 的 g_uiScreen*/g_uiLayout[] 提供。

// ==================== 全局画刷管理器 ====================
#define MAX_BRUSHES 255 // 画刷池容量上限
extern HBRUSH g_BrushPool[MAX_BRUSHES]; // 全局画刷池：按下标取用各面板配色
extern int g_BrushCount; // 画刷池中已创建画刷数量
HBRUSH GetGlobalBrush(COLORREF color); // 创建或复用画刷（按颜色）
void DestroyGlobalBrushes(); // 统一销毁全部画刷

// ==================== DPI 与全局字体 ====================
extern int gb_dpi;                 // 当前显示器 DPI（标准 96，高 DPI 为 120/144 等）
extern HFONT g_GlobalFont;         // 全局字体（所有面板共用，DPI 变化时重建）
HFONT CreateGlobalFont(int dpi);   // 创建全局字体（按 DPI 缩放）
void DestroyGlobalFont();          // 销毁全局字体

// ==================== 响应式布局更新 ====================
// 当主窗口大小改变时，重新计算并移动子面板
void RelayoutChildPanels(HWND hMainWnd); // 布局引擎入口
