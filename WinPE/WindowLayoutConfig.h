#pragma once // 预处理指令：确保本头文件只被包含一次
#include <windows.h> // 引入 Windows API（本文件实际仅使用基础类型，保留以保持一致）
// 定义一个结构体，用于存储窗口布局配置
struct WindowLayoutConfig {
	int Width = 0;  // 窗口宽度（像素）
	int Height = 0; // 窗口高度（像素）
	int StartX = 0;	// 窗口起始 X 坐标（左上角）
	int StartY = 0;	// 窗口起始 Y 坐标（左上角）
};
