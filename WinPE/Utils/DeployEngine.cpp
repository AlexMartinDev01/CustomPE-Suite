#define _CRT_SECURE_NO_WARNINGS // 关闭 CRT 不安全函数告警
#include "DeployEngine.h"        // 引入本模块头：DeployJob/回调类型/接口声明
#include "ImageParser.h"         // 引入镜像解析模块：用于 ISO 挂载解析部署镜像源

#include <stdio.h>               // 标准 I/O：swprintf 宽字符格式化
#include <stdlib.h>              // 标准库：malloc/free
#include <string.h>              // 字符串：memcpy/strlen/strstr 等
#include <wchar.h>               // 宽字符函数
#include <winioctl.h>            // Windows I/O 控制：IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS

// 运行结果：0 成功；1 参数/方案无效；2 diskpart 失败；
// 3 找不到 ESP/System 盘符；4 DISM 失败；5 bcdboot 失败
#define DEPLOY_OK        0       // 部署成功
#define DEPLOY_BAD_INPUT 1       // 输入参数或分区方案无效
#define DEPLOY_PART_FAIL 2       // diskpart 分区阶段失败
#define DEPLOY_NO_LETTER 3       // 分区后找不到 System/ESP 盘符
#define DEPLOY_APPLY_FAIL 4      // DISM 释放镜像失败
#define DEPLOY_BOOT_FAIL 5       // bcdboot 写引导失败

// 查找卷标为 "CustomPE-LOG" 的固定磁盘（持久化部署状态/日志的虚拟磁盘）
static BOOL FindLogVolume(wchar_t* root, int rootLen)
{
    if (!root || rootLen <= 0) return FALSE;   // 参数检查
    root[0] = L'\0';                           // 输出先清空
    const DWORD mask = GetLogicalDrives();     // 取得已分配盘符位图
    for (int i = 0; i < 26; i++)               // 遍历 A-Z
    {
        if (!(mask & ((DWORD)1 << i))) continue; // 该盘符不存在则跳过
        wchar_t r[8];                          // 盘根路径缓冲
        wsprintfW(r, L"%c:\\", L'A' + i);      // 构造 "X:\"
        if (_wcsicmp(r, L"X:\\") == 0) continue; // 跳过 PE RAM 盘 X:
        if (GetDriveTypeW(r) != DRIVE_FIXED) continue; // 只找固定硬盘卷
        wchar_t vol[64] = L"";                 // 卷标缓冲
        DWORD sn = 0, maxLen = 0, flags = 0;   // 序列号/最大路径长度/文件系统标志（占位）
        if (GetVolumeInformationW(r, vol, 64, &sn, &maxLen, &flags, NULL, 0) && // 读取卷标
            _wcsicmp(vol, L"CustomPE-LOG") == 0) // 卷标匹配
        {
            lstrcpynW(root, r, rootLen);       // 输出盘根
            return TRUE;                       // 找到
        }
    }
    return FALSE;                              // 未找到
}

// 确定部署状态目录（写入 diskpart 脚本、部署中间状态）
static void StateDir(wchar_t* out, size_t sz)
{
    // 1) 优先写到 CustomPE-LOG 虚拟日志盘（可持久化，主机可直接挂载读取）
    wchar_t root[8] = L"";                     // 日志盘根
    if (FindLogVolume(root, 8))                // 存在日志盘
    {
        swprintf(out, sz, L"%sDeployState", root); // 状态目录 = <盘>:\DeployState
        return;                                // 返回
    }
    // 2) WinPE RAM 盘 X:
    if (GetDriveTypeW(L"X:\\") == DRIVE_RAMDISK) // 处于 PE 且 X: 是 RAM 盘
    {
        lstrcpynW(out, L"X:\\DeployState", (int)sz); // 使用 X:\DeployState
        return;                                // 返回
    }
    // 3) 最后才退回 exe 目录（仅适用于可写介质）
    wchar_t exe[MAX_PATH] = L"";               // exe 路径
    GetModuleFileNameW(NULL, exe, MAX_PATH);   // 取得当前模块完整路径
    wchar_t* slash = wcsrchr(exe, L'\\');      // 最后一个目录分隔符
    if (slash) slash[1] = L'\0';               // 截断到目录（保留尾部反斜杠）
    swprintf(out, sz, L"%lsDeployState", exe); // 状态目录 = exe 目录\DeployState
}

// 向脚本文件写入一段 ANSI 文本
static void WriteAnsi(HANDLE h, const char* s)
{
    DWORD w = 0;                               // 实际写入字节数
    WriteFile(h, s, (DWORD)strlen(s), &w, NULL); // 写入文件
}

// 把一行 diskpart 命令同时写入脚本文件并回调给日志
static void ScriptLine(HANDLE h, DeployLogFn log, void* user, const wchar_t* line)
{
    char a[1024] = "";                         // ANSI 缓冲
    WideCharToMultiByte(CP_ACP, 0, line, -1, a, 1024, NULL, NULL); // 宽字符转 ANSI
    WriteAnsi(h, a);                           // 写入脚本文件
    if (log) log(line, user);                  // 同步输出到日志回调
}

// 从输出块中提取最后一个 NN%
static int ExtractPercent(const char* chunk, size_t len)
{
    const char* pct = NULL;                    // 最后一个 '%' 位置
    for (size_t i = 0; i < len; i++)           // 遍历输出
        if (chunk[i] == '%') pct = &chunk[i];  // 记录每个 '%'，最终保留最后一个
    if (!pct) return -1;                       // 无百分号
    size_t j = (size_t)(pct - chunk);          // '%' 相对起点偏移
    if (j == 0) return -1;                     // 前面没有数字
    char num[32];                              // 数字文本缓冲
    size_t n = 0;                              // 数字字符数
    while (j > 0 && n < 30 &&                  // 向前取数字与小数点
           ((chunk[j - 1] >= '0' && chunk[j - 1] <= '9') || chunk[j - 1] == '.'))
        num[n++] = chunk[--j];                 // 逆序收集
    if (n == 0) return -1;                     // 无数字
    for (size_t k = 0; k < n / 2; k++)         // 反转字符
    {
        char t = num[k]; num[k] = num[n - 1 - k]; num[n - 1 - k] = t; // 交换
    }
    num[n] = 0;                                // 补结尾符
    double v = atof(num);                      // 文本转浮点
    if (v < 0 || v > 100) return -1;           // 非法范围
    return (int)(v + 0.5);                     // 四舍五入为整数百分比
}

// 执行外部命令（匿名管道捕获输出；可选 NN% 进度回调）
static DWORD RunTool(const wchar_t* exe, const wchar_t* args, DWORD timeoutMs,
                     DeployProgressFn onPct, void* user,
                     char* out, size_t outsz)
{
    wchar_t cmd[2400];                         // 完整命令行缓冲
    swprintf(cmd, 2400, L"\"%ls\" %ls", exe, args); // 给 exe 加引号后拼接参数

    if (!out || outsz == 0)                    // 不需要捕获输出
    {
        // 不需要捕获输出：直接隐藏运行并等待退出
        STARTUPINFOW si0;                      // 启动信息
        PROCESS_INFORMATION pi0;               // 进程信息
        memset(&si0, 0, sizeof(si0));          // 清零
        memset(&pi0, 0, sizeof(pi0));          // 清零
        si0.cb = sizeof(si0);                  // 结构大小
        si0.dwFlags = STARTF_USESHOWWINDOW;    // 使用窗口显示设置
        si0.wShowWindow = SW_HIDE;             // 隐藏窗口
        if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, // 创建进程（无窗口）
                            NULL, NULL, &si0, &pi0))
            return 0xFFFFFFFE;                 // 启动失败专用返回码
        WaitForSingleObject(pi0.hProcess, timeoutMs ? timeoutMs : INFINITE); // 等待结束（0=无限）
        DWORD code = 0;                        // 退出码
        GetExitCodeProcess(pi0.hProcess, &code); // 读取退出码
        CloseHandle(pi0.hThread);              // 关闭线程句柄
        CloseHandle(pi0.hProcess);             // 关闭进程句柄
        return code;                           // 返回退出码
    }

    out[0] = 0;                                // 输出缓冲先清空

    SECURITY_ATTRIBUTES sa;                    // 管道安全属性
    sa.nLength = sizeof(sa); sa.bInheritHandle = TRUE; sa.lpSecurityDescriptor = NULL; // 允许子进程继承写端
    HANDLE hRead = NULL, hWrite = NULL;        // 管道读写端
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return 0xFFFFFFFF; // 创建管道失败
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0); // 读端不继承

    STARTUPINFOW si;                           // 启动信息
    PROCESS_INFORMATION pi;                    // 进程信息
    memset(&si, 0, sizeof(si));                // 清零
    memset(&pi, 0, sizeof(pi));                // 清零
    si.cb = sizeof(si);                        // 结构大小
    si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES; // 使用隐藏窗口 + 标准句柄
    si.wShowWindow = SW_HIDE;                  // 隐藏窗口
    si.hStdOutput = hWrite;                    // 子进程标准输出 = 管道写端
    si.hStdError = hWrite;                     // 子进程标准错误 = 管道写端
    if (!CreateProcessW(NULL, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, // 创建进程（句柄可继承）
                        NULL, NULL, &si, &pi))
    {
        CloseHandle(hRead);                    // 关闭读端
        CloseHandle(hWrite);                   // 关闭写端
        return 0xFFFFFFFE; // launch failed    // 启动失败
    }
    CloseHandle(hWrite);                       // 父进程关闭写端（否则读端永不结束）
    CloseHandle(pi.hThread);                   // 关闭线程句柄

    size_t total = 0;                          // 已缓存输出字节数
    char buf[4096];                            // 读取缓冲
    DWORD elapsed = 0;                         // 已等待毫秒
    DWORD result = 0;                          // 最终结果
    for (;;)                                   // 主循环：轮询进程 + 读管道
    {
        DWORD wait = WaitForSingleObject(pi.hProcess, 200); // 每 200ms 检查进程
        if (wait == WAIT_TIMEOUT)              // 进程仍在运行
        {
            elapsed += 200;                    // 累计等待时间
            if (timeoutMs > 0 && elapsed > timeoutMs) // 超过超时
            {
                TerminateProcess(pi.hProcess, 1); // 强杀进程
                result = 0xFFFFFFFD; // timeout // 记录超时返回码
                break;                         // 退出循环
            }
        }
        DWORD avail = 0;                       // 管道可用字节数
        if (PeekNamedPipe(hRead, NULL, 0, NULL, &avail, NULL) && avail > 0) // 有数据可读
        {
            DWORD rd = 0;                      // 本次读取字节数
            DWORD want = (avail > sizeof(buf) - 1) ? (DWORD)(sizeof(buf) - 1) : avail; // 单次最多读 4095
            if (ReadFile(hRead, buf, want, &rd, NULL) && rd > 0) // 读取
            {
                if (onPct)                     // 需要进度
                {
                    int p = ExtractPercent(buf, rd); // 从输出提取百分比
                    if (p >= 0) onPct(p, user); // 回调进度
                }
                if (total < outsz - 1)         // 输出缓冲未满
                {
                    size_t cp = (total + rd <= outsz - 1) ? rd : (outsz - 1 - total); // 可复制长度
                    memcpy(out + total, buf, cp); // 追加到输出
                    total += cp;               // 更新长度
                }
            }
        }
        else if (wait == WAIT_OBJECT_0)        // 进程已退出且管道空
        {
            break;                             // 结束循环
        }
    }
    if (result == 0)                           // 没有被超时终止
    {
        DWORD code = 0;                        // 退出码
        GetExitCodeProcess(pi.hProcess, &code); // 读取
        result = code;                         // 作为结果
    }
    DWORD rd = 0;                              // 残余读取计数
    while (total < outsz - 1 &&                // 缓冲还有空间
           ReadFile(hRead, buf, sizeof(buf) - 1, &rd, NULL) && rd > 0) // 排空管道残留
    {
        size_t cp = (total + rd <= outsz - 1) ? rd : (outsz - 1 - total); // 可复制长度
        memcpy(out + total, buf, cp);          // 追加
        total += cp;                           // 更新
    }
    CloseHandle(hRead);                        // 关闭读端
    CloseHandle(pi.hProcess);                  // 关闭进程句柄
    out[total] = 0;                            // 补结尾符
    return result;                             // 返回结果
}

// 生成 diskpart 脚本：剩余空间分区（System）固定挪到末尾
static int BuildPartitionScript(const DeployJob* job, const wchar_t* dir,
                                DeployLogFn log, void* user,
                                wchar_t* err, int errLen)
{
    if (err && errLen > 0) err[0] = 0;         // 错误缓冲清空
    int restIdx = -1;                          // "占满剩余空间"分区在数组中的下标
    ULONGLONG fixedMB = 0;                     // 固定大小分区总和（MB）
    for (int i = 0; i < job->Scheme.PartCount; i++) // 遍历方案分区
    {
        const SchemePart* p = &job->Scheme.Parts[i]; // 当前分区
        if (p->SizeMB < 0) restIdx = i;        // 找到剩余空间分区并记录下标
        else fixedMB += (ULONGLONG)p->SizeMB;  // 累加固定分区大小
    }
    if (restIdx < 0)                           // 没有剩余空间分区
    {
        if (err) lstrcpynW(err, L"Partition scheme has no remaining-space (rest) partition.", errLen); // 报错
        return -1;                             // 失败
    }
    if (job->DiskBytes > 0 && job->DiskBytes <= fixedMB * 1024ULL * 1024ULL) // 固定分区总和超过盘容量
    {
        if (err) lstrcpynW(err, L"Fixed partitions exceed the target disk capacity.", errLen); // 报错
        return -1;                             // 失败
    }

    if (!CreateDirectoryW(dir, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) // 创建目录失败且非已存在
    {
        if (err) lstrcpynW(err, L"Failed to create DeployState directory.", errLen); // 报错
        return -1;                             // 失败
    }
    wchar_t path[MAX_PATH];                    // 脚本完整路径
    swprintf(path, MAX_PATH, L"%ls\\partition.txt", dir); // 脚本固定名 partition.txt
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, // 以独占写方式创建（覆盖旧文件）
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)             // 创建失败
    {
        if (err) lstrcpynW(err, L"Failed to write diskpart script.", errLen); // 报错
        return -1;                             // 失败
    }

    wchar_t line[640];                         // 命令行缓冲
    swprintf(line, 640, L"select disk %lu\r\n", job->DiskIndex); // 选择目标盘
    ScriptLine(h, log, user, line);            // 写入并记录
    ScriptLine(h, log, user, L"clean\r\n");    // 清空整盘（破坏性）
    ScriptLine(h, log, user, L"convert gpt\r\n"); // 转换为 GPT

    // 固定分区在前，剩余空间分区（System）放最后
    int order[SCHEME_MAX_PARTS], oc = 0;       // 分区执行顺序表
    for (int i = 0; i < job->Scheme.PartCount && oc < SCHEME_MAX_PARTS; i++) // 先放非剩余分区
        if (i != restIdx) order[oc++] = i;     // 记录顺序
    order[oc++] = restIdx;                     // 剩余空间分区排最后

    for (int k = 0; k < oc; k++)               // 按顺序生成每条命令
    {
        const SchemePart* p = &job->Scheme.Parts[order[k]]; // 当前分区
        const BOOL isEfi = (_wcsicmp(p->Key, L"ESP") == 0 || _wcsicmp(p->Type, L"efi") == 0); // 是否 EFI 分区
        const BOOL isMsr = (_wcsicmp(p->Key, L"MSR") == 0 || _wcsicmp(p->Type, L"msr") == 0); // 是否 MSR
        const BOOL isRest = (p->SizeMB < 0);   // 是否剩余空间分区
        const char* fsA = (_wcsicmp(p->Fs, L"FAT32") == 0) ? "fat32" : "ntfs"; // 文件系统小写文本

        if (isEfi)                             // EFI 系统分区分支
        {
            swprintf(line, 640, L"create partition efi size=%lld\r\n", p->SizeMB); // 创建 EFI 分区
            ScriptLine(h, log, user, line);    // 写入
            swprintf(line, 640, L"format quick fs=%hs label=\"%ls\"\r\n", fsA, // 快速格式化
                     p->Label[0] ? p->Label : L"ESP"); // 标签缺省用 ESP
            ScriptLine(h, log, user, line);    // 写入
            if (p->Id[0])                      // 有自定义 GUID
            {
                swprintf(line, 640, L"set id=%ls\r\n", p->Id); // 设置 GPT 类型 GUID
                ScriptLine(h, log, user, line);// 写入
            }
            ScriptLine(h, log, user, L"gpt attributes=0x0000000000000000\r\n"); // 清除 GPT 属性
            ScriptLine(h, log, user, L"assign\r\n"); // 分配盘符
        }
        else if (isMsr)                        // Microsoft 保留分区分支
        {
            swprintf(line, 640, L"create partition msr size=%lld\r\n", p->SizeMB); // 创建 MSR
            ScriptLine(h, log, user, line);    // 写入（MSR 不格式化不分配盘符）
        }
        else if (isRest)                       // 剩余空间分区（OS/System）分支
        {
            ScriptLine(h, log, user, L"create partition primary align=1024\r\n"); // 创建主分区（占满剩余）
            swprintf(line, 640, L"format quick fs=ntfs label=\"%ls\"\r\n", // 格式化 NTFS
                     p->Label[0] ? p->Label : L"System"); // 标签缺省 System
            ScriptLine(h, log, user, line);    // 写入
            if (p->Id[0])                      // 自定义 GUID
            {
                swprintf(line, 640, L"set id=%ls\r\n", p->Id); // 设置类型
                ScriptLine(h, log, user, line);// 写入
            }
            if (p->Attributes)                 // 需要属性
            {
                swprintf(line, 640, L"gpt attributes=0x%016I64x\r\n", p->Attributes); // 写属性（如 WinRE 隐藏位）
                ScriptLine(h, log, user, line);// 写入
            }
            ScriptLine(h, log, user, L"assign\r\n"); // 分配盘符
        }
        else                                   // 普通固定大小主分区（如 WinRE）
        {
            swprintf(line, 640, L"create partition primary size=%lld align=1024\r\n", // 按大小创建
                     p->SizeMB);
            ScriptLine(h, log, user, line);    // 写入
            swprintf(line, 640, L"format quick fs=%hs label=\"%ls\"\r\n", fsA, // 格式化
                     p->Label[0] ? p->Label : L"Data"); // 标签缺省 Data
            ScriptLine(h, log, user, line);    // 写入
            if (p->Id[0])                      // 自定义 GUID
            {
                swprintf(line, 640, L"set id=%ls\r\n", p->Id); // 设置类型
                ScriptLine(h, log, user, line);// 写入
            }
            if (p->Attributes)                 // 需要属性
            {
                swprintf(line, 640, L"gpt attributes=0x%016I64x\r\n", p->Attributes); // 写属性
                ScriptLine(h, log, user, line);// 写入
            }
        }
    }
    ScriptLine(h, log, user, L"exit\r\n");     // 结束 diskpart 会话
    CloseHandle(h);                            // 关闭脚本文件
    return 0;                                  // 成功
}

// 校验盘符 \\.\X: 落在目标盘上，并回读卷标（DeployPE CoreDeploy::VolumeOnDisk 同款）
static BOOL VolLetterOnDisk(const wchar_t* letter2, DWORD diskIndex,
                            wchar_t* labelOut, size_t lsz)
{
    wchar_t dev[16], root[8];                  // 设备路径与盘根
    swprintf(dev, 16, L"\\\\.\\%ls", letter2); // 构造 "\\.\X:" 设备名
    HANDLE h = CreateFileW(dev, 0,             // 打开卷设备（0 权限即可发 IOCTL）
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return FALSE; // 打开失败
    VOLUME_DISK_EXTENTS ext;                   // 卷-盘扩展信息
    DWORD br = 0;                              // 返回字节数
    BOOL got = DeviceIoControl(h, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, // 查询卷所在磁盘
                               NULL, 0, &ext, sizeof(ext), &br, NULL);
    CloseHandle(h);                            // 关闭句柄
    if (!got || ext.NumberOfDiskExtents == 0) return FALSE; // 查询失败或不在物理盘
    if (ext.Extents[0].DiskNumber != diskIndex) return FALSE; // 所在盘不是目标盘
    if (labelOut && lsz)                       // 需要卷标
    {
        labelOut[0] = 0;                       // 先清空
        swprintf(root, 8, L"%ls\\", letter2);  // 构造 "X:\"
        GetVolumeInformationW(root, labelOut, (DWORD)lsz, NULL, NULL, NULL, NULL, 0); // 读卷标
    }
    return TRUE;                               // 确认在该盘上
}

// 在目标盘上找 ESP / System 盘符。
// 第一优先：diskpart list volume 读回真实盘符（隐藏卷也能列出，DeployPE CorePart 同款）；
// 兜底：FindFirstVolume 枚举 + 卷标/文件系统匹配。
static int FindLetters(const DeployJob* job, wchar_t* win, wchar_t* esp,
                       DeployLogFn log, void* user)
{
    win[0] = 0;                                // 系统盘符先清空
    esp[0] = 0;                                // ESP 盘符先清空

    /* ---- Phase 1: diskpart list volume（隐藏 ESP 卷也能看到） ---- */
    wchar_t dir[MAX_PATH] = L"";               // 状态目录
    StateDir(dir, MAX_PATH);                   // 取得目录
    CreateDirectoryW(dir, NULL);               // 确保存在
    wchar_t lvScript[MAX_PATH];                // 临时脚本路径
    swprintf(lvScript, MAX_PATH, L"%ls\\listvol.txt", dir); // listvol.txt
    HANDLE lh = CreateFileW(lvScript, GENERIC_WRITE, 0, NULL, // 创建脚本
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (lh != INVALID_HANDLE_VALUE)            // 脚本创建成功
    {
        const char* lv = "list volume\r\nexit\r\n"; // diskpart 命令
        DWORD lw = 0;                          // 写入字节数
        WriteFile(lh, lv, (DWORD)strlen(lv), &lw, NULL); // 写入
        CloseHandle(lh);                       // 关闭

        wchar_t args[1024];                    // diskpart 参数
        swprintf(args, 1024, L"/s \"%ls\"", lvScript); // /s 指定脚本
        char lvOut[16384] = "";                // diskpart 输出缓冲
        if (RunTool(L"diskpart.exe", args, 20000, NULL, user, // 运行 diskpart
                    lvOut, sizeof(lvOut)) == 0) // 成功
        {
            char* save = NULL;                 // strtok 上下文
            char* line = strtok_s(lvOut, "\r\n", &save); // 按行切分
            while (line)                       // 逐行解析
            {
                if (strstr(line, "Volume"))    // 只处理卷行
                {
                    char toks[8][64];          // 最多 8 个字段
                    int ntok = 0;              // 实际字段数
                    char tmp[512];             // 行副本
                    strncpy_s(tmp, sizeof(tmp), line, _TRUNCATE); // 复制
                    char* save2 = NULL;        // strtok 上下文
                    char* tk = strtok_s(tmp, " ", &save2); // 按空格切
                    while (tk && ntok < 8)     // 遍历字段
                    {
                        if (tk[0])             // 非空字段
                        {
                            strncpy_s(toks[ntok], 64, tk, _TRUNCATE); // 保存
                            ntok++;            // 计数
                        }
                        tk = strtok_s(NULL, " ", &save2); // 下一个
                    }
                    /* 行格式: Volume N  Ltr  Label ...（Ltr 为单字符） */
                    if (ntok >= 4 && toks[2][0] && !toks[2][1] && // 第 3 列是单字符盘符
                        (strcmp(toks[3], "ESP") == 0 ||          // 卷标是 ESP
                         strcmp(toks[3], "System") == 0))        // 或 System
                    {
                        wchar_t cand[3];       // 候选盘符
                        swprintf(cand, 3, L"%c:", toks[2][0]); // "X:"
                        wchar_t lab[64] = L""; // 卷标
                        if (VolLetterOnDisk(cand, job->DiskIndex, lab, 64)) // 确认在目标盘
                        {
                            if (_wcsicmp(lab, L"ESP") == 0 && !esp[0]) // ESP
                                lstrcpynW(esp, cand, 3);          // 记录
                            else if (_wcsicmp(lab, L"System") == 0 && !win[0]) // System
                                lstrcpynW(win, cand, 3);          // 记录
                        }
                    }
                }
                line = strtok_s(NULL, "\r\n", &save); // 下一行
            }
        }
    }
    if (win[0] && esp[0])                      // 两个都找到了
    {
        if (log)                               // 记录日志
        {
            wchar_t line[160];                 // 日志缓冲
            swprintf(line, 160, L"[deploy] re-anchor(list volume): System=%ls ESP=%ls", // 记录盘符
                     win, esp);
            log(line, user);                   // 回调
        }
        return 0;                              // 成功
    }

    /* ---- Phase 2: 卷枚举兜底（盘号匹配；标签优先，文件系统兜底） ---- */
    wchar_t vol[64];                           // 卷 GUID 名
    HANDLE hFind = FindFirstVolumeW(vol, 64);  // 开始枚举卷
    if (hFind == INVALID_HANDLE_VALUE) return -1; // 枚举失败

    do                                         // 遍历所有卷
    {
        wchar_t paths[1024];                   // 卷路径列表
        DWORD plen = 0;                        // 缓冲长度
        if (!GetVolumePathNamesForVolumeNameW(vol, paths, 1024, &plen) || plen == 0) // 取挂载路径
            continue;                          // 无路径则跳过
        wchar_t root[8] = L"";                 // 盘根
        lstrcpynW(root, paths, 4); // "X:\"    // 取第一个路径
        if (root[0] < L'A' || root[0] > L'Z') continue; // 非盘符挂载跳过

        HANDLE hv = CreateFileW(vol, 0,        // 打开卷
            FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (hv == INVALID_HANDLE_VALUE) continue; // 失败跳过
        VOLUME_DISK_EXTENTS ext;               // 卷-盘映射
        DWORD br = 0;                          // 返回长度
        BOOL got = DeviceIoControl(hv, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, // 查询所在盘
                                   NULL, 0, &ext, sizeof(ext), &br, NULL);
        CloseHandle(hv);                       // 关闭
        if (!got || ext.NumberOfDiskExtents == 0) continue; // 查询失败
        if (ext.Extents[0].DiskNumber != job->DiskIndex) continue; // 不在目标盘

        wchar_t label[64] = L"";               // 卷标
        wchar_t fsName[32] = L"";              // 文件系统
        DWORD fsFlags = 0;                     // 标志
        GetVolumeInformationW(root, label, 64, NULL, NULL, &fsFlags, fsName, 32); // 读卷信息
        wchar_t letter[3] = L"X:";             // 盘符
        letter[0] = root[0];                   // 填入字母
        if (_wcsicmp(label, L"ESP") == 0 && !esp[0]) // 标签 ESP
            lstrcpynW(esp, letter, 3);         // 记录
        else if (_wcsicmp(label, L"System") == 0 && !win[0]) // 标签 System
            lstrcpynW(win, letter, 3);         // 记录
        else if (!win[0] && !esp[0])           // 都还没有：按文件系统兜底
        {
            /* 标签缺失时的兜底：FAT32 -> ESP，NTFS -> System（DeployPE 同款） */
            if (_wcsicmp(fsName, L"FAT32") == 0 && !esp[0]) // FAT32
                lstrcpynW(esp, letter, 3);     // 当 ESP
            else if (_wcsicmp(fsName, L"NTFS") == 0 && !win[0]) // NTFS
                lstrcpynW(win, letter, 3);     // 当 System
        }
    } while (FindNextVolumeW(hFind, vol, 64)); // 下一个卷
    FindVolumeClose(hFind);                    // 关闭枚举句柄

    if (log)                                   // 记录日志
    {
        wchar_t line[160];                     // 日志缓冲
        swprintf(line, 160, L"[deploy] re-anchor(enum): System=%ls ESP=%ls", // 记录结果
                 win[0] ? win : L"-", esp[0] ? esp : L"-");
        log(line, user);                       // 回调
    }
    return (win[0] && esp[0]) ? 0 : -1;        // 两个都找到才成功
}

// DISM 进度回调上下文与映射：DISM 0-100% -> 总体进度 20-90%
typedef struct DeployApplyMap                 // 进度映射上下文
{
    DeployProgressFn fn;                       // 上层进度回调
    void* user;                                // 上层用户上下文
} DeployApplyMap;

static void DeployApplyPctShim(int pct, void* ctx) // DISM 进度中转
{
    DeployApplyMap* m = (DeployApplyMap*)ctx;  // 取上下文
    if (!m || !m->fn) return;                  // 无效直接返回
    int v = 20 + pct * 70 / 100;               // 把 DISM 0-100 映射到 20-90
    if (v > 90) v = 90;                        // 上限 90
    m->fn(v, m->user);                         // 转发给上层
}

static int ApplyImage(const DeployJob* job, const wchar_t* winLetter, // 执行 DISM Apply
                      DeployLogFn log, DeployProgressFn progress, void* user)
{
    DeployApplyMap map = { progress, user };   // 初始化映射上下文
    wchar_t src[MAX_PATH] = L"";               // 实际镜像源路径
    wchar_t err[256] = L"";                    // 错误缓冲
    if (!ImgResolveImageFile(job->ImagePath, src, MAX_PATH, err, 256)) // 解析镜像源（ISO 会挂载）
    {
        if (log) log(err, user);               // 记录错误
        return -1;                             // 失败
    }
    const BOOL mountedIso = (_wcsicmp(src, job->ImagePath) != 0); // 是否发生了 ISO 挂载
    if (mountedIso && log)                     // 挂载时记录
    {
        wchar_t line[512];                     // 日志缓冲
        swprintf(line, 512, L"[deploy] ISO mounted, using %ls", src); // 提示实际路径
        log(line, user);                       // 回调
    }

    wchar_t scratch[MAX_PATH] = L"";           // DISM 临时目录
    GetTempPathW(MAX_PATH, scratch);           // 获取系统临时目录
    size_t sl = wcslen(scratch);               // 长度
    while (sl > 0 && (scratch[sl - 1] == L'\\' || scratch[sl - 1] == L'/')) // 去尾部斜杠
        scratch[--sl] = 0;

    wchar_t args[1800];                        // DISM 参数
    swprintf(args, 1800,                       // 构造 Apply-Image 命令
             L"/Apply-Image /ImageFile:\"%ls\" /Index:%d /ApplyDir:%ls\\ /ScratchDir:\"%ls\"",
             src, job->ImageIndex, winLetter, scratch); // 镜像/索引/目标盘/临时目录
    if (log)                                   // 记录命令
    {
        wchar_t line[1900];                    // 日志缓冲
        swprintf(line, 1900, L"[deploy] > dism %ls", args); // 完整命令
        log(line, user);                       // 回调
    }

    char out[4096] = "";                       // 输出缓冲
    DWORD rc = RunTool(L"dism.exe", args, 0, DeployApplyPctShim, &map, out, sizeof(out)); // 运行 DISM
    if (rc != 0)                               // 失败
    {
        if (log)                               // 记录
        {
            wchar_t line[512];                 // 日志缓冲
            swprintf(line, 512, L"[deploy] DISM failed (0x%08lX)", rc); // 错误码
            log(line, user);                   // 回调
        }
        if (mountedIso) ImgReleaseMountedIso(); // 释放挂载
        return -1;                             // 失败
    }
    if (mountedIso) ImgReleaseMountedIso();    // 成功后释放挂载
    return 0;                                  // 成功
}

static int WriteBoot(const DeployJob* job, const wchar_t* win, const wchar_t* esp, // 写引导
                     DeployLogFn log, DeployProgressFn progress, void* user)
{
    wchar_t dummy[8] = { 0 };                  // 固件变量探测缓冲
    DWORD r = GetFirmwareEnvironmentVariableW( // 探测 UEFI 固件变量
        L"", L"{00000000-0000-0000-0000-000000000000}", dummy, sizeof(dummy));
    BOOL uefi = TRUE;                          // 默认假设 UEFI
    if (r == 0)                                // 查询失败
    {
        DWORD e = GetLastError();              // 错误码
        if (e == ERROR_NOACCESS || e == ERROR_INVALID_FUNCTION) // 固件不支持 = Legacy
            uefi = FALSE;                      // 判定为 BIOS 模式
    }
    if (log) log(uefi ? L"[boot] firmware mode: UEFI" : L"[boot] firmware mode: Legacy BIOS", user); // 记录固件模式

    wchar_t args[800];                         // bcdboot 参数
    if (uefi)                                  // UEFI 分支
    {
        swprintf(args, 800, L"%ls\\Windows /s %ls /f UEFI", win, esp); // bcdboot System:\Windows /s ESP: /f UEFI
    }
    else                                       // Legacy BIOS 分支
    {
        wchar_t bs[800];                       // bootsect 参数
        swprintf(bs, 800, L"/nt60 %ls /mbr", win); // 对系统分区写主引导
        RunTool(L"bootsect.exe", bs, 30000, NULL, user, NULL, 0); // 尽力而为
        swprintf(args, 800, L"%ls\\Windows /f BIOS", win); // bcdboot /f BIOS
    }
    if (log)                                   // 记录命令
    {
        wchar_t line[900];                     // 日志缓冲
        swprintf(line, 900, L"[boot] > bcdboot %ls", args); // 完整命令
        log(line, user);                       // 回调
    }
    char out[4096] = "";                       // 输出缓冲
    DWORD rc = RunTool(L"bcdboot.exe", args, 120000, NULL, user, out, sizeof(out)); // 运行 bcdboot
    if (rc != 0)                               // 失败
    {
        if (log)                               // 记录
        {
            wchar_t line[512];                 // 日志缓冲
            swprintf(line, 512, L"[boot] bcdboot failed (0x%08lX)", rc); // 错误码
            log(line, user);                   // 回调
        }
        return -1;                             // 失败
    }
    return 0;                                  // 成功
}

int DeployRun(const DeployJob* job, DeployLogFn log, DeployProgressFn progress, // 部署总入口
              void* user)
{
    if (!job || !job->ImagePath[0] || job->ImageIndex <= 0 || // 输入合法性
        job->Scheme.PartCount <= 0)            // 方案非空
    {
        if (log) log(L"[deploy] Invalid job: disk/image/scheme must all be selected.", user); // 报错
        return DEPLOY_BAD_INPUT;               // 参数错误
    }

    wchar_t line[640];                         // 日志行缓冲
    swprintf(line, 640, L"[deploy] target Disk%lu | %ls | fingerprint=%ls", // 记录目标盘
             job->DiskIndex, job->DiskModel, job->Fingerprint);
    if (log) log(line, user);                  // 输出
    swprintf(line, 640, L"[deploy] image=%ls index=%d", job->ImagePath, job->ImageIndex); // 记录镜像
    if (log) log(line, user);                  // 输出

    wchar_t dir[MAX_PATH] = L"";               // 状态目录
    StateDir(dir, MAX_PATH);                   // 取得目录
    if (progress) progress(5, user);           // 总进度 5%

    wchar_t err[256] = L"";                    // 错误缓冲
    if (BuildPartitionScript(job, dir, log, user, err, 256) != 0) // 生成 diskpart 脚本
    {
        if (log && err[0]) log(err, user);     // 记录错误
        return DEPLOY_BAD_INPUT;               // 方案错误
    }
    if (log) log(L"[deploy] diskpart script generated; starting partition...", user); // 提示开始分区

    wchar_t script[MAX_PATH];                  // 脚本路径
    swprintf(script, MAX_PATH, L"%ls\\partition.txt", dir); // 拼路径

    // ==================== 演示模式：只生成脚本并模拟 ====================
    if (job->DemoMode)                         // 演示模式
    {
        if (log)                               // 输出模拟信息
        {
            log(L"[deploy] DEMO: diskpart script generated (not executed):", user); // 提示未执行
            swprintf(line, 640, L"[deploy] DEMO: %ls", script); // 脚本位置
            log(line, user);                   // 输出
            log(L"[deploy] DEMO: would run  diskpart /s partition.txt", user); // 模拟 diskpart
            log(L"[deploy] DEMO: would run  dism /Apply-Image /Index:... (apply to System)", user); // 模拟 DISM
            log(L"[deploy] DEMO: would run  bcdboot System:\\Windows /s ESP: /f UEFI", user); // 模拟 bcdboot
        }
        for (int p = 10; p <= 100; p += 5)     // 模拟进度 10->100
        {
            if (progress) progress(p, user);   // 上报进度
            Sleep(120); // 模拟执行耗时，便于观察进度 // 每步停 120ms
        }
        if (log) log(L"[deploy] DEMO completed successfully (no disk changes).", user); // 完成提示
        return DEPLOY_OK;                      // 模拟成功
    }

    wchar_t args[1200];                        // diskpart 参数
    swprintf(args, 1200, L"/s \"%ls\"", script); // /s 脚本模式
    if (log)                                   // 记录命令
    {
        swprintf(line, 640, L"[deploy] > diskpart %ls", args); // 命令
        log(line, user);                       // 输出
    }
    char out[16384] = "";                      // diskpart 输出缓冲
    DWORD rc = RunTool(L"diskpart.exe", args, 20 * 60 * 1000, // 运行（最长 20 分钟）
                       NULL, user, out, sizeof(out));
    if (rc != 0)                               // 失败
    {
        if (log)                               // 记录
        {
            swprintf(line, 640, L"[deploy] diskpart failed (0x%08lX)", rc); // 错误码
            log(line, user);                   // 输出
        }
        return DEPLOY_PART_FAIL;               // 分区失败
    }
    if (log && out[0])                         // 有输出则记录
    {
        /* diskpart 即使返回 0 也可能内部失败，把输出写进日志便于定位 */
        const int wn = MultiByteToWideChar(CP_ACP, 0, out, -1, NULL, 0); // ANSI 转宽字符所需长度
        if (wn > 1)                            // 有内容
        {
            wchar_t* w = (wchar_t*)malloc((wn + 1) * sizeof(wchar_t)); // 分配
            if (w)                             // 成功
            {
                MultiByteToWideChar(CP_ACP, 0, out, -1, w, wn); // 转换
                w[wn] = 0;                     // 结尾符
                for (int i = 0; w[i]; i++)     // 把换行换成空格
                    if (w[i] == L'\r' || w[i] == L'\n') w[i] = L' ';
                wchar_t dline[2200];           // 单行日志缓冲
                lstrcpynW(dline, w, 2200);     // 截断复制
                swprintf(line, 640, L"[deploy] diskpart output: %ls", dline); // 日志
                log(line, user);               // 输出
                free(w);                       // 释放
            }
        }
    }
    if (progress) progress(15, user);          // 总进度 15%

    wchar_t win[3] = L"", esp[3] = L"";        // System/ESP 盘符
    if (FindLetters(job, win, esp, log, user) != 0) // 回读盘符失败
    {
        if (log) log(L"[deploy] Fatal: cannot locate System/ESP volume on target disk.", user); // 报错
        return DEPLOY_NO_LETTER;               // 找不到卷
    }
    if (log)                                   // 记录
    {
        swprintf(line, 640, L"[deploy] volumes: System=%ls ESP=%ls", win, esp); // 盘符
        log(line, user);                       // 输出
    }

    if (progress) progress(20, user);          // 总进度 20%
    if (ApplyImage(job, win, log, progress, user) != 0) // DISM 释放
        return DEPLOY_APPLY_FAIL;              // 失败
    if (progress) progress(90, user);          // 总进度 90%

    if (WriteBoot(job, win, esp, log, progress, user) != 0) // bcdboot
        return DEPLOY_BOOT_FAIL;               // 失败
    if (progress) progress(100, user);         // 总进度 100%
    if (log) log(L"[deploy] deployment completed successfully.", user); // 完成
    return DEPLOY_OK;                          // 成功
}

BOOL DeployIsCurrentBootDisk(DWORD diskIndex)  // 目标盘是否为当前启动盘
{
    wchar_t exe[MAX_PATH] = L"";               // exe 路径
    if (!GetModuleFileNameW(NULL, exe, MAX_PATH)) return FALSE; // 失败
    if (exe[1] != L':') return FALSE;          // 非盘符路径无法判断
    wchar_t dev[8] = L"\\\\.\\X:";             // 卷设备名模板
    dev[4] = exe[0];                           // 替换为 exe 所在盘符
    HANDLE h = CreateFileW(dev, 0,             // 打开卷
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return FALSE; // 打开失败
    VOLUME_DISK_EXTENTS ext;                   // 卷-盘映射
    DWORD br = 0;                              // 返回长度
    BOOL got = DeviceIoControl(h, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, // 查询所在盘
                               NULL, 0, &ext, sizeof(ext), &br, NULL);
    CloseHandle(h);                            // 关闭
    return got && ext.NumberOfDiskExtents > 0 && // 查询成功且有映射
           ext.Extents[0].DiskNumber == diskIndex; // 且所在盘 == 目标盘
}
