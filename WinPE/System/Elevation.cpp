/* ============================================================
 * System/Elevation.cpp —— 管理员提权自检与 runas 重启
 * ============================================================ */
#include "System.h"   // 本层头文件
#include <shellapi.h> // ShellExecuteW
#pragma comment(lib, "shell32.lib")

// 判断当前进程是否已经以管理员（高完整性令牌）运行
BOOL IsProcessElevated(void)
{
    HANDLE hToken = NULL;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken))
        return FALSE;

    TOKEN_ELEVATION elev;
    DWORD size = 0;
    const BOOL ok = GetTokenInformation(hToken, TokenElevation, &elev, sizeof(elev), &size);
    CloseHandle(hToken);
    return ok && elev.TokenIsElevated;
}

// 未提权则用 runas 单次重启后退出；返回 TRUE 表示当前实例可继续执行
BOOL EnsureAdminAndRelaunch(PWSTR pCmdLine)
{
    if (IsProcessElevated())
        return TRUE; // 已是管理员，无需重启

    wchar_t exePath[MAX_PATH] = L"";
    if (GetModuleFileNameW(NULL, exePath, MAX_PATH) == 0)
        return FALSE;

    HINSTANCE hr = ShellExecuteW(NULL, L"runas", exePath, pCmdLine, NULL, SW_SHOWNORMAL);
    if ((INT_PTR)hr <= 32)
    {
        MessageBoxW(NULL,
            L"This program requires administrator privileges.\n"
            L"Failed to request elevation. Please run it as administrator.",
            L"WinPE", MB_OK | MB_ICONERROR);
        return FALSE;
    }
    return FALSE; // 提权实例已启动，本实例应退出
}
