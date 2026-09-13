#define _CRT_SECURE_NO_WARNINGS

#include "AcerAltF10Recovery.h"

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

// 与宏碁 NAPP 一致的恢复项固定 GUID（同一台机器两种 BCD store 可共用）
#define ALT_F10_GUID L"{572bcd56-ffa7-11d9-aae0-0007e994107d}"
#define RAMDISK_ID   L"{ramdiskoptions}"

typedef void (*LogFn)(const wchar_t* line, void* user);

// 带格式的日志行：全部走调用方回调（最终落到 UI 日志窗口与日志文件）
static void LogLine(LogFn log, void* user, const wchar_t* fmt, ...)
{
    if (!log) return;
    wchar_t buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vswprintf(buf, 1024, fmt, ap);
    va_end(ap);
    log(buf, user);
}

// 隐藏窗口执行外部工具，返回进程退出码（0xFFFFFFFE=启动失败，0xFFFFFFFD=超时）
static DWORD RunTool(const wchar_t* exe, const wchar_t* args, DWORD timeoutMs)
{
    wchar_t cmd[2400];
    swprintf(cmd, 2400, L"\"%ls\" %ls", exe, args);

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    memset(&pi, 0, sizeof(pi));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW,
                        NULL, NULL, &si, &pi))
        return 0xFFFFFFFE;

    WaitForSingleObject(pi.hProcess, timeoutMs ? timeoutMs : INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return code;
}

// 执行一条 bcdedit 命令并记录命令/退出码（不视为致命）
static DWORD RunBcdEdit(LogFn log, void* user, const wchar_t* args)
{
    LogLine(log, user, L"[altf10] > bcdedit %ls", args);
    DWORD rc = RunTool(L"bcdedit.exe", args, 20000);
    if (rc != 0)
        LogLine(log, user, L"[altf10]   bcdedit rc=0x%08lX (不影响后续则容忍)", rc);
    return rc;
}

// 逐层创建目录（已存在则忽略）
static void EnsureDir(const wchar_t* path)
{
    if (!path || !path[0]) return;
    CreateDirectoryW(path, NULL); // 已存在时返回 FALSE 且 ERROR_ALREADY_EXISTS，可忽略
}

// 把 "W" / "W:" 统一成 "W:"
static void NormalizeLetter(const wchar_t* in, wchar_t* out, int outLen)
{
    if (!out || outLen <= 0) return;
    out[0] = L'\0';
    if (!in || !in[0]) return;
    lstrcpynW(out, in, outLen);
    if (out[1] != L':')
    {
        int n = lstrlenW(out);
        if (n + 1 < outLen) { out[n] = L':'; out[n + 1] = L'\0'; }
    }
}

// 方案中是否含 WinRE 分区；命中则拷贝到 part
static BOOL FindWinREPart(const SchemeInfo* scheme, SchemePart* part)
{
    if (!scheme) return FALSE;
    for (int i = 0; i < scheme->PartCount && i < SCHEME_MAX_PARTS; i++)
    {
        const SchemePart* p = &scheme->Parts[i];
        const BOOL byKey = (_wcsicmp(p->Key, L"WinRE") == 0);
        const BOOL byId = (wcsstr(p->Id, L"de94bba4") != NULL);
        if (byKey || byId)
        {
            if (part) *part = *p;
            return TRUE;
        }
    }
    return FALSE;
}

// 复制单个文件；缺失时记日志并返回 FALSE（“没找到”也只写日志）
static BOOL CopyFileChecked(LogFn log, void* user,
                            const wchar_t* src, const wchar_t* dst,
                            const wchar_t* what)
{
    if (GetFileAttributesW(src) == INVALID_FILE_ATTRIBUTES)
    {
        LogLine(log, user, L"[altf10] NOT FOUND: %ls (%ls)", what, src);
        return FALSE;
    }
    if (!CopyFileW(src, dst, FALSE))
    {
        LogLine(log, user, L"[altf10] ERROR: copy %ls failed (0x%08lX)", what, GetLastError());
        return FALSE;
    }
    LogLine(log, user, L"[altf10] copied %ls: %ls -> %ls", what, src, dst);
    return TRUE;
}

// 写完恢复文件后移除恢复分区的临时盘符（失败仅告警）
static void RemoveTempLetter(LogFn log, void* user, const wchar_t* letter)
{
    if (!letter || !letter[0]) return;
    wchar_t tmp[MAX_PATH] = L"";
    GetTempPathW(MAX_PATH, tmp);
    wchar_t script[MAX_PATH];
    swprintf(script, MAX_PATH, L"%lsacer_altf10_remove.txt", tmp);

    HANDLE h = CreateFileW(script, GENERIC_WRITE, 0, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
    {
        LogLine(log, user, L"[altf10] WARN: cannot create temp diskpart script");
        return;
    }
    char line[256];
    const char L = (letter[0] >= L'A' && letter[0] <= L'Z') ? (char)letter[0] : 'W';
    int n = sprintf(line, "select volume %c\r\nremove letter=%c\r\nexit\r\n", L, L);
    DWORD w = 0;
    WriteFile(h, line, (DWORD)n, &w, NULL);
    CloseHandle(h);

    wchar_t args[1024];
    swprintf(args, 1024, L"/s \"%ls\"", script);
    DWORD rc = RunTool(L"diskpart.exe", args, 30000);
    if (rc != 0)
        LogLine(log, user, L"[altf10] WARN: remove temp letter %ls failed rc=0x%08lX", letter, rc);
    else
        LogLine(log, user, L"[altf10] temp letter %ls removed", letter);
    DeleteFileW(script);
}

// 标准 BCD：写入 "Windows Recovery Environment"（ramdisk -> winre.wim）
static int InstallStandardBcd(LogFn log, void* user,
                              const wchar_t* espLetter,
                              const wchar_t* recLetter)
{
    wchar_t store[MAX_PATH];
    swprintf(store, MAX_PATH, L"%ls\\EFI\\Microsoft\\Boot\\BCD", espLetter);
    wchar_t args[2400];

    // 先清掉可能残留的同名固定 GUID 项，保证可重复部署
    swprintf(args, 2400, L"/store \"%ls\" /delete %s /cleanup", store, ALT_F10_GUID);
    RunBcdEdit(log, user, args); // 不存在时 rc!=0，属预期，容忍

    swprintf(args, 2400, L"/store \"%ls\" /create %s /d \"Ramdisk options\"", store, RAMDISK_ID);
    RunBcdEdit(log, user, args); // 已存在时 rc!=0，属预期，容忍

    swprintf(args, 2400, L"/store \"%ls\" /set %s ramdisksdidevice partition=%ls",
             store, RAMDISK_ID, recLetter);
    RunBcdEdit(log, user, args);
    swprintf(args, 2400, L"/store \"%ls\" /set %s ramdisksdipath \\Recovery\\WindowsRE\\boot.sdi",
             store, RAMDISK_ID);
    RunBcdEdit(log, user, args);

    swprintf(args, 2400, L"/store \"%ls\" /create %s /d \"Windows Recovery Environment\" /application osloader",
             store, ALT_F10_GUID);
    if (RunBcdEdit(log, user, args) != 0)
    {
        LogLine(log, user, L"[altf10] ERROR: cannot create standard WinRE BCD entry");
        return -1;
    }

    swprintf(args, 2400, L"/store \"%ls\" /set %s device ramdisk=[%ls]\\Recovery\\WindowsRE\\winre.wim,%s",
             store, ALT_F10_GUID, recLetter, RAMDISK_ID);
    RunBcdEdit(log, user, args);
    swprintf(args, 2400, L"/store \"%ls\" /set %s osdevice ramdisk=[%ls]\\Recovery\\WindowsRE\\winre.wim,%s",
             store, ALT_F10_GUID, recLetter, RAMDISK_ID);
    RunBcdEdit(log, user, args);
    swprintf(args, 2400, L"/store \"%ls\" /set %s path \\windows\\system32\\winload.efi",
             store, ALT_F10_GUID);
    RunBcdEdit(log, user, args);
    swprintf(args, 2400, L"/store \"%ls\" /set %s systemroot \\windows", store, ALT_F10_GUID);
    RunBcdEdit(log, user, args);
    swprintf(args, 2400, L"/store \"%ls\" /set %s winpe yes", store, ALT_F10_GUID);
    RunBcdEdit(log, user, args);
    swprintf(args, 2400, L"/store \"%ls\" /set %s nx optin", store, ALT_F10_GUID);
    RunBcdEdit(log, user, args);
    swprintf(args, 2400, L"/store \"%ls\" /set %s detecthal yes", store, ALT_F10_GUID);
    RunBcdEdit(log, user, args);

    LogLine(log, user, L"[altf10] standard BCD WinRE entry installed (%s)", ALT_F10_GUID);
    return 0;
}

// 宏碁 OEM BCD：ESP:\EFI\OEM\Boot\bootmgfw.efi + BCD（Alt+F10 的固件路径）
static int InstallOemBcd(LogFn log, void* user,
                         const wchar_t* espLetter,
                         const wchar_t* recLetter)
{
    // 目录与引导文件
    wchar_t oemBoot[MAX_PATH];
    swprintf(oemBoot, MAX_PATH, L"%ls\\EFI\\OEM\\Boot", espLetter);
    wchar_t tmp[MAX_PATH];
    swprintf(tmp, MAX_PATH, L"%ls\\EFI", espLetter);
    EnsureDir(tmp);
    swprintf(tmp, MAX_PATH, L"%ls\\EFI\\OEM", espLetter);
    EnsureDir(tmp);
    EnsureDir(oemBoot);

    wchar_t src[MAX_PATH], dst[MAX_PATH];
    swprintf(src, MAX_PATH, L"%ls\\EFI\\Microsoft\\Boot\\bootmgfw.efi", espLetter);
    swprintf(dst, MAX_PATH, L"%ls\\bootmgfw.efi", oemBoot);
    if (!CopyFileChecked(log, user, src, dst, L"OEM bootmgfw.efi"))
        return -1;

    // 重建 OEM BCD（可重复部署）
    wchar_t bcdPath[MAX_PATH];
    swprintf(bcdPath, MAX_PATH, L"%ls\\EFI\\OEM\\Boot\\BCD", espLetter);
    DeleteFileW(bcdPath);
    wchar_t args[2400];
    swprintf(args, 2400, L"/createstore \"%ls\"", bcdPath);
    if (RunBcdEdit(log, user, args) != 0)
    {
        LogLine(log, user, L"[altf10] ERROR: cannot create OEM BCD store");
        return -1;
    }

    swprintf(args, 2400, L"/store \"%ls\" /create {bootmgr} /d \"Acer Recovery Management\"", bcdPath);
    RunBcdEdit(log, user, args);
    swprintf(args, 2400, L"/store \"%ls\" /set {bootmgr} device partition=%ls", bcdPath, espLetter);
    RunBcdEdit(log, user, args);
    swprintf(args, 2400, L"/store \"%ls\" /set {bootmgr} path \\EFI\\OEM\\Boot\\bootmgfw.efi", bcdPath);
    RunBcdEdit(log, user, args);
    swprintf(args, 2400, L"/store \"%ls\" /set {bootmgr} timeout 0", bcdPath);
    RunBcdEdit(log, user, args);

    swprintf(args, 2400, L"/store \"%ls\" /create %s /d \"Acer Recovery Management\"", bcdPath, RAMDISK_ID);
    RunBcdEdit(log, user, args);
    swprintf(args, 2400, L"/store \"%ls\" /set %s ramdisksdidevice partition=%ls",
             bcdPath, RAMDISK_ID, recLetter);
    RunBcdEdit(log, user, args);
    swprintf(args, 2400, L"/store \"%ls\" /set %s ramdisksdipath \\Recovery\\WindowsRE\\boot.sdi",
             bcdPath, RAMDISK_ID);
    RunBcdEdit(log, user, args);

    swprintf(args, 2400, L"/store \"%ls\" /create %s /d \"Acer Recovery Management\" /application osloader",
             bcdPath, ALT_F10_GUID);
    if (RunBcdEdit(log, user, args) != 0)
    {
        LogLine(log, user, L"[altf10] ERROR: cannot create OEM recovery osloader entry");
        return -1;
    }
    swprintf(args, 2400, L"/store \"%ls\" /set %s device ramdisk=[%ls]\\Recovery\\WindowsRE\\winre.wim,%s",
             bcdPath, ALT_F10_GUID, recLetter, RAMDISK_ID);
    RunBcdEdit(log, user, args);
    swprintf(args, 2400, L"/store \"%ls\" /set %s osdevice ramdisk=[%ls]\\Recovery\\WindowsRE\\winre.wim,%s",
             bcdPath, ALT_F10_GUID, recLetter, RAMDISK_ID);
    RunBcdEdit(log, user, args);
    swprintf(args, 2400, L"/store \"%ls\" /set %s path \\windows\\system32\\winload.efi",
             bcdPath, ALT_F10_GUID);
    RunBcdEdit(log, user, args);
    swprintf(args, 2400, L"/store \"%ls\" /set %s systemroot \\windows", bcdPath, ALT_F10_GUID);
    RunBcdEdit(log, user, args);
    swprintf(args, 2400, L"/store \"%ls\" /set %s winpe yes", bcdPath, ALT_F10_GUID);
    RunBcdEdit(log, user, args);
    swprintf(args, 2400, L"/store \"%ls\" /set %s nx optin", bcdPath, ALT_F10_GUID);
    RunBcdEdit(log, user, args);
    swprintf(args, 2400, L"/store \"%ls\" /set %s detecthal yes", bcdPath, ALT_F10_GUID);
    RunBcdEdit(log, user, args);
    swprintf(args, 2400, L"/store \"%ls\" /displayorder %s -addfirst", bcdPath, ALT_F10_GUID);
    RunBcdEdit(log, user, args);
    swprintf(args, 2400, L"/store \"%ls\" /default %s", bcdPath, ALT_F10_GUID);
    RunBcdEdit(log, user, args);

    LogLine(log, user, L"[altf10] OEM BCD installed: %ls", bcdPath);
    return 0;
}

// ==================== 对外入口 ====================
int AcerAltF10Install(const SchemeInfo* scheme,
                      const wchar_t* winLetter,
                      const wchar_t* espLetter,
                      void (*log)(const wchar_t* line, void* user),
                      void* user)
{
    if (!scheme || !winLetter || !espLetter)
    {
        if (log) log(L"[altf10] invalid arguments; skip", user);
        return 0;
    }

    // 条件1：分区方案开关必须为 true
    if (!scheme->EnableAltF10Recovery)
    {
        LogLine(log, user, L"[altf10] scheme '%ls': EnableAltF10Recovery=off, skip", scheme->Name);
        return 0;
    }

    // 条件2：方案必须包含 WinRE 恢复分区
    SchemePart winre;
    memset(&winre, 0, sizeof(winre));
    if (!FindWinREPart(scheme, &winre))
    {
        LogLine(log, user, L"[altf10] scheme '%ls': switch on but no WinRE partition, skip",
                scheme->Name);
        return 0;
    }
    LogLine(log, user, L"[altf10] scheme '%ls': EnableAltF10Recovery=on, WinRE partition found",
            scheme->Name);

    wchar_t recLetter[8];
    NormalizeLetter(winre.Letter[0] ? winre.Letter : L"W", recLetter, 8);

    // 复制镜像自带 WinRE 到恢复分区（版本与所装系统一致）
    wchar_t winreDir[MAX_PATH], winreSrc[MAX_PATH], winreDst[MAX_PATH], recRoot[MAX_PATH];
    swprintf(winreDir, MAX_PATH, L"%ls\\Recovery\\WindowsRE", recLetter);
    swprintf(recRoot, MAX_PATH, L"%ls\\Recovery", recLetter);
    EnsureDir(recRoot);
    EnsureDir(winreDir);

    swprintf(winreSrc, MAX_PATH, L"%ls\\Windows\\System32\\Recovery\\winre.wim", winLetter);
    swprintf(winreDst, MAX_PATH, L"%ls\\winre.wim", winreDir);
    if (!CopyFileChecked(log, user, winreSrc, winreDst, L"winre.wim"))
    {
        LogLine(log, user, L"[altf10] image has no Winre.wim; Alt+F10 setup skipped (see log)");
        return 0; // 没找到：仅日志，不中断部署
    }

    // boot.sdi：优先取所装系统的 EFI 版本，其次 System32 版本
    wchar_t sdiSrc[MAX_PATH], sdiDst[MAX_PATH];
    BOOL sdiOk = FALSE;
    swprintf(sdiSrc, MAX_PATH, L"%ls\\Windows\\Boot\\DVD\\EFI\\boot.sdi", winLetter);
    swprintf(sdiDst, MAX_PATH, L"%ls\\boot.sdi", winreDir);
    if (CopyFileChecked(log, user, sdiSrc, sdiDst, L"boot.sdi"))
        sdiOk = TRUE;
    if (!sdiOk)
    {
        swprintf(sdiSrc, MAX_PATH, L"%ls\\Windows\\System32\\boot.sdi", winLetter);
        sdiOk = CopyFileChecked(log, user, sdiSrc, sdiDst, L"boot.sdi");
    }
    if (!sdiOk)
    {
        LogLine(log, user, L"[altf10] ERROR: boot.sdi not found in deployed image");
        return -1;
    }

    // 标准 BCD（Windows 恢复入口）
    if (InstallStandardBcd(log, user, espLetter, recLetter) != 0)
    {
        LogLine(log, user, L"[altf10] standard BCD WinRE entry failed (logged above)");
        // 不返回：继续 OEM BCD，宏碁 Alt+F10 主要依赖 OEM 路径
    }

    // 宏碁 OEM BCD（Alt+F10 实际触发路径）
    if (InstallOemBcd(log, user, espLetter, recLetter) != 0)
    {
        LogLine(log, user, L"[altf10] ERROR: OEM BCD setup failed (logged above)");
        RemoveTempLetter(log, user, recLetter);
        return -1;
    }

    RemoveTempLetter(log, user, recLetter);
    LogLine(log, user, L"[altf10] Alt+F10(WinRE) recovery setup completed");
    return 0;
}
