#pragma once

#include <windows.h>
#include "SchemeParser.h"

// ==================== Alt+F10 (WinRE) 恢复安装工具 ====================
// 说明：独立于 UI 的部署后置工具。仅在调用方满足以下条件时执行实际安装：
//   1. 当前分区方案 EnableAltF10Recovery 开关为 true；
//   2. 方案中确实包含 WinRE 恢复分区（Key=WinRE 或 GPT GUID de94bba4-...）。
// 作用：把正在安装的镜像自带的 winre.wim 复制到恢复分区，并写入
//       标准 BCD 的 "Windows Recovery Environment" 项与宏碁 OEM BCD
//       (ESP:\EFI\OEM\Boot\BCD) 的 "Acer Recovery Management" 项，
//       使目标宏碁机器开机按 Alt+F10 可进入与所装系统版本匹配的 WinRE。
// 行为约束：所有过程/结果只通过 log 回调输出（日志窗口+日志文件），
//           绝不弹窗；任何失败不改变主部署结果（由调用方决定是否忽略）。

// 在 bcdboot 写入成功之后调用。
// scheme     : 当前使用的分区方案（含开关与 WinRE 分区定义）
// winLetter  : 系统盘盘符，如 L"N:"（镜像已释放，winre.wim 应位于其
//              \Windows\System32\Recovery\ 下）
// espLetter  : ESP 盘符，如 L"R:"
// log/user   : 部署引擎的日志回调及其上下文
// 返回 0 = 完成或按条件跳过（均已写日志）；非 0 = 安装过程失败（仅日志）。
int AcerAltF10Install(const SchemeInfo* scheme,
                      const wchar_t* winLetter,
                      const wchar_t* espLetter,
                      void (*log)(const wchar_t* line, void* user),
                      void* user);
