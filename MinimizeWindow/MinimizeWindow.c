/* =====================================================
 * MinimizeWindow - whoever invokes it gets minimized,
 * but ONLY when the caller is a console window.
 *
 * Build (x64, static CRT, no runtime dependency):
 *   cl /nologo /O1 /MT /GL MinimizeWindow.c /Fe:MinimizeWindow.exe /link user32.lib
 *
 * Usage:
 *   MinimizeWindow.exe            -> minimize the caller's console window
 *   MinimizeWindow.exe -min       -> same as above (explicit action flag)
 *   MinimizeWindow.exe <keyword>  -> minimize every visible top-level window
 *                                    whose title contains <keyword>
 *   MinimizeWindow.exe -min <kw>  -> same as above
 *
 * Console-caller detection: GetConsoleProcessList() returns how many
 * processes share our console.  >= 2 means the caller is a console app
 * (cmd / AutoRun.cmd) sharing its window with us -> minimize it.
 * == 1 means the console was created for us alone (GUI caller spawned us)
 * -> do nothing.
 *
 * Return code: 0 = minimized, 1 = nothing to do.
 * ===================================================== */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wchar.h>

typedef struct {
    const WCHAR *keyword;
    int count;
} MatchCtx;

static BOOL IsMinFlag(const WCHAR *arg)
{
    return wcscmp(arg, L"-min") == 0
        || wcscmp(arg, L"--minimize") == 0
        || wcscmp(arg, L"/min") == 0;
}

static BOOL TitleContains(HWND hwnd, const WCHAR *needle)
{
    WCHAR title[512];
    int n = GetWindowTextW(hwnd, title, 512);
    if (n <= 0) return FALSE;

    size_t nlen = wcslen(needle);
    if (nlen == 0) return TRUE;
    for (int i = 0; i < n; i++) {
        if (_wcsnicmp(title + i, needle, nlen) == 0) return TRUE;
    }
    return FALSE;
}

static BOOL CALLBACK EnumProc(HWND hwnd, LPARAM lParam)
{
    MatchCtx *ctx = (MatchCtx *)lParam;
    if (!IsWindowVisible(hwnd)) return TRUE;
    if (TitleContains(hwnd, ctx->keyword)) {
        ShowWindowAsync(hwnd, SW_MINIMIZE);
        ctx->count++;
    }
    return TRUE;
}

/* Minimize the caller, but only when the caller is a console window. */
static int MinimizeCaller(void)
{
    DWORD consolePids[8];
    UINT n = GetConsoleProcessList(consolePids, 8);
    if (n < 2) return 1; /* console is ours alone -> caller is not a console app */

    HWND console = GetConsoleWindow();
    if (console == NULL) return 1;
    ShowWindowAsync(console, SW_MINIMIZE);
    return 0;
}

int wmain(int argc, WCHAR **argv)
{
    int start = 1;
    if (argc > 1 && IsMinFlag(argv[1])) {
        start = 2;
    }
    if (start >= argc) {
        return MinimizeCaller();
    }
    int total = 0;
    for (int i = start; i < argc; i++) {
        if (argv[i][0] == L'\0') continue; /* skip empty keywords */
        MatchCtx ctx = { argv[i], 0 };
        EnumWindows(EnumProc, (LPARAM)&ctx);
        total += ctx.count;
    }
    return total > 0 ? 0 : 1;
}