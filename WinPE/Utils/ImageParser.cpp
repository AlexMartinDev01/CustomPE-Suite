/* ==================== 镜像解析模块实现 ==================== */
#define _CRT_SECURE_NO_WARNINGS // 关闭 CRT 不安全函数告警（GetFileVersionInfo 相关移植代码）
#ifndef NOMINMAX                 // 若尚未定义 NOMINMAX
#define NOMINMAX                 // 定义它：阻止 windows.h 定义 min/max 宏，避免与 std 模板冲突
#endif

#include "ImageParser.h"   // 引入本模块头：ImgEntry/ImgEnumCallback 及接口声明
#include <stdio.h>         // 标准 I/O：swprintf 宽字符格式化
#include <stdlib.h>        // 标准库：malloc/free 动态内存
#include <string.h>        // 字符串：memcpy/wcslen/wcschr 等
#include <wchar.h>         // 宽字符函数
#include <stdarg.h>        // 可变参数：va_list/_vsnwprintf 用于错误格式化

#pragma comment(lib, "version.lib") // 链接 version.lib：GetFileVersionInfo / VerQueryValue

/* ESD 固态压缩标志：WIMCreateFile 需要此标志才能打开 ESD */
#define WIM_SOLID_FLAG 0x20000000u            // wimgapi 的固态 WIM/ESD 打开标志
/* virtdisk ISO 相关常量 */
#define ISO_STORAGE_TYPE_DEVICE_ISO ((ULONG)2) // OpenVirtualDisk 的 ISO 虚拟存储类型
#define ISO_OPEN_VHD_VER1 1                    // OpenVirtualDisk 参数版本号 V1
#define ISO_ACCESS_READ 0x000D0000u            // ISO 只读访问权限位组合
#define ISO_ATTACH_READONLY 0x00000001u        // AttachVirtualDisk 只读挂载标志
#define MAX_DRIVES 26                          // 盘符 A-Z 共 26 个

/* Microsoft ISO 供应商 GUID */
static const GUID kIsoVendorMs = {             // 微软 ISO 供应商标识（OpenVirtualDisk 需要）
    0xEC984AEC, 0xA0F9, 0x47E9,
    { 0x96, 0xFA, 0xEE, 0x13, 0x54, 0xD1, 0xAB, 0x08 }
};

/* ==================== 函数指针与结构体 ==================== */
// 动态加载 wimgapi.dll，所有 API 都通过函数指针调用（避免链接期依赖）
typedef HANDLE(WINAPI* PFN_WimCreate)(LPCWSTR, DWORD, DWORD, DWORD, DWORD, PDWORD); // WIMCreateFile
typedef DWORD(WINAPI* PFN_WimCount)(HANDLE);                                          // WIMGetImageCount
typedef BOOL(WINAPI* PFN_WimInfo)(HANDLE, PVOID*, PDWORD);                            // WIMGetImageInformation
typedef BOOL(WINAPI* PFN_WimClose)(HANDLE);                                           // WIMCloseHandle

// wimgapi 封装：模块句柄 + 4 个函数指针
typedef struct _WimApi
{
    HMODULE h;          // LoadLibrary 返回的模块句柄
    PFN_WimCreate Create; // WIMCreateFile 函数指针
    PFN_WimCount  Count;  // WIMGetImageCount 函数指针
    PFN_WimInfo   Info;   // WIMGetImageInformation 函数指针
    PFN_WimClose  Close;  // WIMCloseHandle 函数指针
} WimApi;

// ISO 虚拟存储类型（OpenVirtualDisk 的 StorageType 参数）
typedef struct _ISO_VST
{
    ULONG DeviceId;   // 存储类型：ISO
    GUID  VendorId;   // 供应商 GUID：微软
} ISO_VST;

// OpenVirtualDisk 参数（V1 版本）
typedef struct _ISO_OVDP
{
    ULONG Version;    // 结构版本号
    union { struct { ULONG RWDepth; } V1; }; // V1：读写深度
} ISO_OVDP;

// virtdisk.dll 三个函数指针类型
typedef DWORD(WINAPI* PFN_OpenVhd)(ISO_VST*, PCWSTR, DWORD, DWORD, ISO_OVDP*, PHANDLE);   // OpenVirtualDisk
typedef DWORD(WINAPI* PFN_AttachVhd)(HANDLE, PSECURITY_DESCRIPTOR, DWORD, ULONG, PVOID, LPOVERLAPPED); // AttachVirtualDisk
typedef DWORD(WINAPI* PFN_DetachVhd)(HANDLE, DWORD, ULONG);                                // DetachVirtualDisk

// ISO 挂载状态封装
typedef struct _IsoMnt
{
    HMODULE     hLib;     // virtdisk.dll 模块句柄
    HANDLE      hVhd;     // 打开的虚拟磁盘句柄
    BOOL        byUs;     // 该挂载是否由本进程创建（FALSE 表示复用了系统已有挂载）
    WCHAR       drv[MAX_DRIVES]; // 扫描到的候选盘符数组
    int         cnt;      // 候选盘符数量
    PFN_OpenVhd   open;   // OpenVirtualDisk 指针
    PFN_AttachVhd attach; // AttachVirtualDisk 指针
    PFN_DetachVhd detach; // DetachVirtualDisk 指针
} IsoMnt;

/* ==================== 错误信息 ==================== */
// 可变参数格式化错误消息到调用方缓冲区
static void SetErr(wchar_t* err, int errLen, const wchar_t* fmt, ...)
{
    if (!err || errLen <= 0) return;  // 无输出缓冲则直接返回
    va_list ap;                       // 可变参数列表
    va_start(ap, fmt);                // 开始遍历参数
    _vsnwprintf(err, errLen - 1, fmt, ap); // 格式化写入（留一位给结尾符）
    va_end(ap);                       // 结束遍历
    err[errLen - 1] = L'\0';          // 强制结尾符，保证字符串安全
}

/* ==================== wimgapi.dll 封装 ==================== */
// 依次尝试多个路径加载 wimgapi.dll 并解析 4 个导出函数
static BOOL WimLoad(WimApi* a)
{
    const wchar_t* paths[] = {           // 候选加载路径表
        L"C:\\Windows\\System32\\wimgapi.dll", // 64 位系统路径
        L"C:\\Windows\\SysWOW64\\wimgapi.dll", // 32 位兼容路径
        L"wimgapi.dll"                         // 依赖系统 PATH
    };
    a->h = NULL;                          // 句柄先置空
    for (int i = 0; i < 3; i++)           // 依次尝试
    {
        a->h = LoadLibraryW(paths[i]);    // 动态加载
        if (a->h) break;                  // 成功即停止
    }
    if (!a->h) return FALSE;              // 全部失败
    a->Create = (PFN_WimCreate)GetProcAddress(a->h, "WIMCreateFile");           // 取创建函数
    a->Count  = (PFN_WimCount)GetProcAddress(a->h, "WIMGetImageCount");          // 取计数函数
    a->Info   = (PFN_WimInfo)GetProcAddress(a->h, "WIMGetImageInformation");     // 取信息函数
    a->Close  = (PFN_WimClose)GetProcAddress(a->h, "WIMCloseHandle");            // 取关闭函数
    if (!a->Create || !a->Count || !a->Info || !a->Close) // 任一导出缺失
    {
        FreeLibrary(a->h);                // 卸载模块
        a->h = NULL;                      // 句柄置空
        return FALSE;                     // 返回失败
    }
    return TRUE;                          // 全部就绪
}

/* 先带 SOLID_FLAG 尝试打开（ESD），失败回退普通 WIM 模式 */
static HANDLE WimOpen(WimApi* a, const wchar_t* p)
{
    DWORD cr = 0;                         // 错误码输出
    HANDLE h = a->Create(p, GENERIC_READ, OPEN_EXISTING, WIM_SOLID_FLAG, 0, &cr); // 按 ESD 方式打开
    if (!h || h == INVALID_HANDLE_VALUE)  // 失败（普通 WIM 不支持 SOLID 标志）
        h = a->Create(p, GENERIC_READ, OPEN_EXISTING, 0, 0, &cr); // 回退普通 WIM 打开
    return h;                             // 返回句柄（可能无效）
}

// 释放 wimgapi 模块
static void WimFree(WimApi* a)
{
    if (a->h) { FreeLibrary(a->h); a->h = NULL; } // 卸载并置空
}

/* ==================== virtdisk.dll ISO 挂载 ==================== */
// 加载 virtdisk.dll 并解析三个函数
static BOOL IsoLoad(IsoMnt* m)
{
    if (m->hLib) return TRUE;             // 已加载则直接复用
    m->hLib = LoadLibraryW(L"virtdisk.dll"); // 动态加载
    if (!m->hLib) return FALSE;           // 加载失败
    m->open   = (PFN_OpenVhd)GetProcAddress(m->hLib, "OpenVirtualDisk");     // OpenVirtualDisk
    m->attach = (PFN_AttachVhd)GetProcAddress(m->hLib, "AttachVirtualDisk"); // AttachVirtualDisk
    m->detach = (PFN_DetachVhd)GetProcAddress(m->hLib, "DetachVirtualDisk"); // DetachVirtualDisk
    if (!m->open || !m->attach || !m->detach) // 任一函数缺失
    {
        FreeLibrary(m->hLib);             // 卸载
        m->hLib = NULL;                   // 置空
        return FALSE;                     // 失败
    }
    return TRUE;                          // 成功
}

// 检查某盘符下 sources 目录是否存在 .wim 或 .esd
static BOOL HasSrcImg(wchar_t d)
{
    const wchar_t* pat[] = { L"*.wim", L"*.esd" }; // 候选扩展名
    for (int i = 0; i < 2; i++)           // 遍历两种扩展名
    {
        wchar_t sp[32];                   // 通配路径缓冲
        swprintf(sp, 32, L"%c:\\sources\\%s", d, pat[i]); // 构造 "%c:\sources\*.wim"
        WIN32_FIND_DATAW fd;              // 查找结果
        ZeroMemory(&fd, sizeof(fd));      // 清零
        HANDLE h = FindFirstFileW(sp, &fd); // 首次查找
        if (h != INVALID_HANDLE_VALUE) { FindClose(h); return TRUE; } // 找到即关闭并返回真
    }
    return FALSE;                         // 都没有
}

/* 扫描 A-Z，筛选 UDF/CDFS 或光驱且 sources 下有镜像的盘符 */
static BOOL IsoFindDrv(IsoMnt* m)
{
    m->cnt = 0;                           // 计数清零
    for (wchar_t c = L'A'; c <= L'Z'; c++) // 遍历全部盘符
    {
        wchar_t root[4] = L"A:\\";        // 盘根路径
        root[0] = c;                      // 替换盘符字母
        UINT t = GetDriveTypeW(root);     // 查询驱动器类型
        if (t == DRIVE_NO_ROOT_DIR || t == DRIVE_UNKNOWN) continue; // 不存在的盘跳过
        BOOL cd = (t == DRIVE_CDROM);     // 是否光驱
        BOOL udf = FALSE;                 // 是否 UDF/CDFS 文件系统
        wchar_t fs[64] = { 0 };           // 文件系统名缓冲
        if (GetVolumeInformationW(root, NULL, 0, NULL, NULL, NULL, fs, 64)) // 查询文件系统
        {
            if (wcscmp(fs, L"UDF") == 0 || wcscmp(fs, L"CDFS") == 0) udf = TRUE; // UDF/CDFS 标记
        }
        if (!udf && !cd) continue;        // 既不是 ISO 文件系统也不是光驱则跳过
        if (HasSrcImg(c) && m->cnt < MAX_DRIVES) m->drv[m->cnt++] = c; // 含镜像则记录盘符
    }
    return m->cnt > 0;                    // 是否找到至少一个
}

/* 挂载 ISO；失败时降级使用系统已有挂载 */
static BOOL IsoMount(IsoMnt* m, const wchar_t* iso, wchar_t* err, int errLen)
{
    if (!IsoLoad(m))                      // 先加载 virtdisk
    {
        SetErr(err, errLen, L"Failed to load virtdisk.dll"); // 报错
        return FALSE;                     // 失败
    }
    ISO_VST vst;                          // 存储类型
    ZeroMemory(&vst, sizeof(vst));        // 清零
    vst.DeviceId = ISO_STORAGE_TYPE_DEVICE_ISO; // 设为 ISO
    memcpy(&vst.VendorId, &kIsoVendorMs, sizeof(GUID)); // 复制微软供应商 GUID
    ISO_OVDP ovdp;                        // 打开参数
    ZeroMemory(&ovdp, sizeof(ovdp));      // 清零
    ovdp.Version = ISO_OPEN_VHD_VER1;     // 版本 V1
    ovdp.V1.RWDepth = 0;                  // 只读深度 0

    HANDLE hv = NULL;                     // 虚拟磁盘句柄
    DWORD er = m->open(&vst, iso, ISO_ACCESS_READ, 0, &ovdp, &hv); // 打开 ISO 虚拟磁盘
    if (er != ERROR_SUCCESS || !hv || hv == INVALID_HANDLE_VALUE)  // 打开失败
    {
        if (IsoFindDrv(m)) { m->byUs = FALSE; return TRUE; } // 已有系统挂载则降级复用
        SetErr(err, errLen, L"OpenVirtualDisk failed (0x%08X)", er); // 报错
        return FALSE;                     // 失败
    }
    er = m->attach(hv, NULL, ISO_ATTACH_READONLY, 0, NULL, NULL); // 只读挂载到盘符
    if (er != ERROR_SUCCESS)              // 挂载失败
    {
        CloseHandle(hv);                  // 关闭句柄
        if (IsoFindDrv(m)) { m->byUs = FALSE; return TRUE; } // 降级复用已有挂载
        SetErr(err, errLen, L"AttachVirtualDisk failed (0x%08X)", er); // 报错
        return FALSE;                     // 失败
    }
    for (int tries = 0; tries < 25; tries++) // 最多等待约 5 秒让盘符出现
    {
        if (IsoFindDrv(m)) { m->byUs = TRUE; m->hVhd = hv; return TRUE; } // 找到镜像盘符即成功
        Sleep(200);                       // 每 200ms 重试
    }
    m->detach(hv, 0, 0);                  // 超时：卸载
    CloseHandle(hv);                      // 关闭句柄
    SetErr(err, errLen, L"ISO mounted but sources drive not found"); // 报错
    return FALSE;                         // 失败
}

// 卸载由本进程创建的 ISO 挂载
static void IsoUmount(IsoMnt* m)
{
    if (!m->byUs || !m->hVhd || m->hVhd == INVALID_HANDLE_VALUE) return; // 非本进程挂载则不动
    if (m->detach) m->detach(m->hVhd, 0, 0); // 卸载虚拟磁盘
    CloseHandle(m->hVhd);                 // 关闭句柄
    m->hVhd = INVALID_HANDLE_VALUE;       // 句柄置无效
    m->byUs = FALSE;                      // 清除标记
}

// 销毁挂载状态并卸载 virtdisk.dll
static void IsoDestroy(IsoMnt* m)
{
    IsoUmount(m);                         // 先卸载 ISO
    if (m->hLib) { FreeLibrary(m->hLib); m->hLib = NULL; } // 卸载模块
}

/* ==================== XML / 版本解析 ==================== */
// 把 WIM XML 中的架构数字代码映射为可读文本
static const wchar_t* ArchStr(const wchar_t* a)
{
    if (wcscmp(a, L"0") == 0)  return L"x86";   // 0 -> x86
    if (wcscmp(a, L"5") == 0)  return L"ARM";   // 5 -> ARM
    if (wcscmp(a, L"6") == 0)  return L"IA64";  // 6 -> IA64
    if (wcscmp(a, L"9") == 0)  return L"x64";   // 9 -> x64
    if (wcscmp(a, L"12") == 0) return L"ARM64"; // 12 -> ARM64
    if (a[0] == L'\0')         return L"Unknown"; // 空 -> Unknown
    return a;                                   // 其余原样返回
}

/* 提取指定索引的 <IMAGE INDEX="n"> 节点片段 */
static wchar_t* ImgBlock(const wchar_t* xml, DWORD idx)
{
    if (xml[0] == 0xFEFF || xml[0] == 0xFFFE) xml++; // 跳过 UTF-16 BOM
    wchar_t tag[64];                          // 精确开标签缓冲
    swprintf(tag, 64, L"<IMAGE INDEX=\"%lu\">", (unsigned long)idx); // 构造精确标签
    const wchar_t* pos = NULL;                // 找到的标签位置
    const wchar_t* sf = xml;                  // 扫描游标
    while (*sf)                               // 遍历
    {
        const wchar_t* lt = wcschr(sf, L'<'); // 找 '<'
        if (!lt) break;                       // 结束
        if (_wcsnicmp(lt, tag, wcslen(tag)) == 0) { pos = lt; break; } // 精确命中
        sf = lt + 1;                          // 继续
    }
    if (!pos)                                 // 精确匹配失败（可能格式略有差异）
    {
        sf = xml; DWORD fi = 0;               // 退化为按顺序数 <IMAGE> 标签
        while (*sf)                           // 遍历
        {
            const wchar_t* lt = wcschr(sf, L'<'); // 找 '<'
            if (!lt) break;                   // 结束
            const wchar_t* tn = lt + 1;       // 标签名起点
            while (*tn && *tn != L':' && *tn != L' ' && *tn != L'>' && *tn != L'/') tn++; // 找到标签名结尾
            if (*tn == L':') tn++;            // 跳过命名空间前缀冒号（如 wim:IMAGE）
            if (_wcsnicmp(tn, L"IMAGE", 5) == 0 && // 标签名是 IMAGE
                (tn[5] == L' ' || tn[5] == L'>' || tn[5] == L'/')) // 且确实是完整单词
            {
                fi++;                         // 计数 +1
                if (fi == idx) { pos = lt; break; } // 第 idx 个就是目标
            }
            sf = lt + 1;                      // 继续
        }
    }
    if (!pos) return NULL;                    // 找不到返回空
    const wchar_t* b = wcschr(pos, L'>');     // 内容起点：开标签的 '>' 之后
    if (!b) return NULL;                      // 无 '>' 视为非法
    b++;                                      // 跳过 '>'
    const wchar_t* e = NULL;                  // 内容终点
    const wchar_t* cs = b;                    // 查找游标
    while (*cs)                               // 遍历内容
    {
        const wchar_t* lt = wcschr(cs, L'<'); // 找 '<'
        if (!lt) { e = cs + wcslen(cs); break; } // 到末尾
        if (_wcsnicmp(lt, L"</IMAGE>", 8) == 0) { e = lt; break; } // 命中闭标签
        cs = lt + 1;                          // 继续
    }
    size_t l = e ? (size_t)(e - b) : wcslen(b); // 计算内容长度
    wchar_t* r = (wchar_t*)malloc((l + 1) * sizeof(wchar_t)); // 分配结果缓冲
    if (r) { memcpy(r, b, l * sizeof(wchar_t)); r[l] = L'\0'; } // 复制并补结尾符
    return r;                                 // 返回（调用方负责释放）
}

/* 提取 DISPLAYNAME（多种大小写变体） */
static const wchar_t* XmlDisplayName(const wchar_t* xml)
{
    static wchar_t buf[4096];                 // 静态缓冲：返回指针生命周期长
    if (!xml) return L"";                     // 空输入
    if (xml[0] == 0xFEFF || xml[0] == 0xFFFE) xml++; // 跳过 BOM
    const wchar_t* tags[] = { L"<DISPLAYNAME>", L"<displayname>", L"<DisplayName>", NULL }; // 候选大小写
    for (int ti = 0; tags[ti]; ti++)          // 依次尝试
    {
        const wchar_t* sf = xml;              // 游标
        size_t tl = wcslen(tags[ti]);         // 标签长度
        while (*sf)                           // 遍历
        {
            const wchar_t* lt = wcschr(sf, L'<'); // 找 '<'
            if (!lt) break;                   // 结束
            if (_wcsnicmp(lt, tags[ti], tl) == 0) // 命中开标签
            {
                const wchar_t* content = lt + tl; // 内容起点
                wchar_t ct[64];               // 闭标签缓冲
                swprintf(ct, 64, L"</%.*s>", (int)(tl - 2), tags[ti] + 1); // 由开标签名生成闭标签
                const wchar_t* end = NULL;    // 内容终点
                const wchar_t* ss = content;  // 查找游标
                while (*ss)                   // 遍历内容
                {
                    const wchar_t* clt = wcschr(ss, L'<'); // 找 '<'
                    if (!clt) { end = ss + wcslen(ss); break; } // 到末尾
                    if (_wcsnicmp(clt, ct, wcslen(ct)) == 0) { end = clt; break; } // 命中闭标签
                    ss = clt + 1;             // 继续
                }
                size_t cl = end ? (size_t)(end - content) : wcslen(content); // 内容长度
                if (cl >= 4096) cl = 4095;    // 截断保护
                memcpy(buf, content, cl * sizeof(wchar_t)); // 复制
                buf[cl] = L'\0';              // 补结尾符
                wchar_t* tr = buf;            // 修剪游标
                while (*tr == L' ' || *tr == L'\t' || *tr == L'\r' || *tr == L'\n') tr++; // 去左侧空白
                size_t t2 = wcslen(tr);       // 修剪后长度
                while (t2 > 0 && (tr[t2 - 1] == L' ' || tr[t2 - 1] == L'\t' || tr[t2 - 1] == L'\r' || tr[t2 - 1] == L'\n')) t2--; // 去右侧空白
                if (tr != buf) memmove(buf, tr, t2 * sizeof(wchar_t)); // 内容前移
                buf[t2] = L'\0';              // 补结尾符
                if (buf[0] != L'\0') return buf; // 非空即返回
            }
            sf = lt + 1;                      // 继续
        }
    }
    return L"";                               // 全部未命中
}

/* 从 VERSIONINFO 读取产品名/版本号/版本描述（DISM 同款权威数据） */
static BOOL ReadWimVersion(const wchar_t* path,
    wchar_t* productName, size_t pnLen, wchar_t* fileVer, size_t fvLen, wchar_t* edition, size_t edLen)
{
    if (productName && pnLen > 0) productName[0] = L'\0'; // 输出先清空
    if (fileVer && fvLen > 0) fileVer[0] = L'\0';          // 输出先清空
    if (edition && edLen > 0) edition[0] = L'\0';          // 输出先清空
    DWORD dummy = 0;                          // 句柄占位
    DWORD vSz = GetFileVersionInfoSizeW(path, &dummy); // 查询版本资源大小
    if (vSz == 0) return FALSE;               // 无版本资源
    BYTE* vBuf = (BYTE*)malloc(vSz);          // 分配版本信息缓冲
    if (!vBuf) return FALSE;                  // 分配失败
    if (!GetFileVersionInfoW(path, 0, vSz, vBuf)) { free(vBuf); return FALSE; } // 读取失败

    VS_FIXEDFILEINFO* ffi = NULL;             // 固定文件信息
    UINT ffiLen = 0;                          // 结构长度
    if (VerQueryValueW(vBuf, L"\\", (LPVOID*)&ffi, &ffiLen) && // 查询根信息
        ffi && ffiLen >= sizeof(VS_FIXEDFILEINFO)) // 结构有效
    {
        if (fileVer && fvLen > 0)             // 需要版本号
            swprintf(fileVer, fvLen, L"%u.%u.%u.%u", // 按 4 段格式输出
                HIWORD(ffi->dwFileVersionMS), LOWORD(ffi->dwFileVersionMS),
                HIWORD(ffi->dwFileVersionLS), LOWORD(ffi->dwFileVersionLS));
    }
    struct LANGANDCODEPAGE { WORD wLang; WORD wCodePage; } *lcp = NULL; // 语言/代码页结构
    UINT lcpLen = 0;                          // 长度
    VerQueryValueW(vBuf, L"\\VarFileInfo\\Translation", (LPVOID*)&lcp, &lcpLen); // 查询翻译表
    if (lcp && lcpLen >= sizeof(struct LANGANDCODEPAGE)) // 翻译表有效
    {
        const wchar_t* names[] = { L"ProductName", L"FileDescription", L"InternalName", NULL }; // 候选键
        for (int si = 0; names[si]; si++)     // 依次尝试
        {
            wchar_t q[256];                   // 查询路径
            swprintf(q, 256, L"\\StringFileInfo\\%04x%04x\\%s", // 按语言/代码页拼路径
                lcp->wLang, lcp->wCodePage, names[si]);
            wchar_t* val = NULL;              // 值指针
            UINT valLen = 0;                  // 值长度
            if (VerQueryValueW(vBuf, q, (LPVOID*)&val, &valLen) && val && valLen > 0) // 查询成功
            {
                if (si == 0 && productName && pnLen > 0 && productName[0] == L'\0') // ProductName 优先
                { wcsncpy(productName, val, pnLen - 1); productName[pnLen - 1] = L'\0'; } // 复制产品名
                if (si == 1 && edition && edLen > 0 && edition[0] == L'\0') // FileDescription
                { wcsncpy(edition, val, edLen - 1); edition[edLen - 1] = L'\0'; } // 复制描述
                if (si == 2 && edition && edLen > 0 && edition[0] == L'\0') // InternalName
                { wcsncpy(edition, val, edLen - 1); edition[edLen - 1] = L'\0'; } // 复制内部名
            }
        }
    }
    free(vBuf);                               // 释放版本缓冲
    return (productName && productName[0]) || (fileVer && fileVer[0]) || (edition && edition[0]); // 任一字段有效即成功
}

/* 通用 XML 字段提取：大小写不敏感，取第一个匹配标签的内容并去除空白 */
static BOOL GetField(const wchar_t* xml, const wchar_t* field, wchar_t* out, int outLen)
{
    if (!out || outLen <= 0) return FALSE;    // 参数检查
    out[0] = L'\0';                           // 清空输出
    wchar_t open[80], close[80];              // 开/闭标签缓冲
    swprintf(open, 80, L"<%s>", field);       // 生成开标签
    swprintf(close, 80, L"</%s>", field);     // 生成闭标签
    const size_t openLen = wcslen(open);      // 开标签长度
    const size_t closeLen = wcslen(close);    // 闭标签长度

    const wchar_t* p = xml;                   // 扫描游标
    while (*p)                                // 遍历
    {
        const wchar_t* lt = wcschr(p, L'<');  // 找 '<'
        if (!lt) break;                       // 结束
        if (_wcsnicmp(lt, open, openLen) == 0) // 命中开标签
        {
            const wchar_t* s = lt + openLen;  // 内容起点
            const wchar_t* e = NULL;          // 终点
            const wchar_t* q = s;             // 查找游标
            while (*q)                        // 遍历内容
            {
                const wchar_t* cl = wcschr(q, L'<'); // 找 '<'
                if (!cl) break;               // 结束
                if (_wcsnicmp(cl, close, closeLen) == 0) { e = cl; break; } // 命中闭标签
                q = cl + 1;                   // 继续
            }
            if (!e) e = s + wcslen(s);        // 无闭标签则到末尾
            /* 去除首尾空白 */
            while (s < e && (*s == L' ' || *s == L'\t' || *s == L'\r' || *s == L'\n')) s++; // 去左空白
            while (e > s && (e[-1] == L' ' || e[-1] == L'\t' || e[-1] == L'\r' || e[-1] == L'\n')) e--; // 去右空白
            size_t n = (size_t)(e - s);       // 净长度
            if (n >= (size_t)outLen) n = outLen - 1; // 截断保护
            memcpy(out, s, n * sizeof(wchar_t)); // 复制
            out[n] = L'\0';                   // 补结尾符
            return TRUE;                      // 成功
        }
        p = lt + 1;                           // 继续
    }
    return FALSE;                             // 未找到
}

/* 从 <WINDOWS><VERSION> 拼装 "10.0.BUILD.SPBUILD" */
static void ComposeXmlVersion(const wchar_t* blk, wchar_t* out, int outLen)
{
    out[0] = L'\0';                           // 清空输出
    wchar_t maj[16] = L"", min[16] = L"", bld[16] = L"", sp[16] = L""; // 四段版本缓冲
    if (!GetField(blk, L"MAJOR", maj, 16)) return; // 必须有主版本号
    GetField(blk, L"MINOR", min, 16);         // 次版本
    GetField(blk, L"BUILD", bld, 16);         // 构建号
    GetField(blk, L"SPBUILD", sp, 16);        // Service Pack 构建号
    if (!min[0]) lstrcpynW(min, L"0", 16);    // 缺失补 0
    if (!bld[0]) { lstrcpynW(out, maj, outLen); return; } // 无构建号只返回主版本
    if (!sp[0]) lstrcpynW(sp, L"0", 16);      // 缺失补 0
    swprintf(out, outLen, L"%s.%s.%s.%s", maj, min, bld, sp); // 拼装完整版本
}

/* 在盘符 sources 目录查找 install.wim / install.esd（优先安装镜像，跳过 boot.wim） */
static BOOL FindInstallWim(wchar_t drv, wchar_t* out, int outLen)
{
    const wchar_t* names[] = { L"install.wim", L"install.esd" }; // 候选文件名
    for (int k = 0; k < 2; k++)               // 依次查找
    {
        wchar_t pat[80];                      // 路径缓冲
        swprintf(pat, 80, L"%c:\\sources\\%s", drv, names[k]); // 构造完整路径
        WIN32_FIND_DATAW fd;                  // 查找结果
        ZeroMemory(&fd, sizeof(fd));          // 清零
        HANDLE h = FindFirstFileW(pat, &fd);  // 查找
        if (h != INVALID_HANDLE_VALUE)        // 存在
        {
            FindClose(h);                     // 关闭句柄
            swprintf(out, outLen, L"%c:\\sources\\%s", drv, names[k]); // 输出路径
            return TRUE;                      // 成功
        }
    }
    return FALSE;                             // 都没有
}

/* ==================== 解析单个 WIM/ESD 文件 ==================== */
static int ImgParseOne(WimApi* api, const wchar_t* path,
    ImgEnumCallback cb, void* ctx, wchar_t* err, int errLen)
{
    HANDLE hw = WimOpen(api, path);           // 打开 WIM/ESD
    if (!hw || hw == INVALID_HANDLE_VALUE)    // 打开失败
    {
        SetErr(err, errLen, L"Cannot open %s", path); // 报错
        return -1;                            // 失败
    }
    DWORD cnt = api->Count(hw);               // 查询映像数量
    if (cnt == 0)                             // 无映像
    {
        api->Close(hw);                       // 关闭
        SetErr(err, errLen, L"No image found in %s", path); // 报错
        return -1;                            // 失败
    }

    wchar_t viProd[512] = L"", viVer[128] = L"", viEd[512] = L""; // VERSIONINFO 结果缓冲
    const BOOL hasVI = ReadWimVersion(path, viProd, 512, viVer, 128, viEd, 512); // 读取版本资源

    int found = 0;                            // 成功枚举计数
    for (DWORD i = 1; i <= cnt; i++)          // 映像索引从 1 开始
    {
        PVOID pInfo = NULL;                   // XML 信息指针
        DWORD cbInfo = 0;                     // 信息字节数
        if (!api->Info(hw, &pInfo, &cbInfo) || !pInfo || cbInfo == 0) continue; // 读取失败则跳过
        size_t xl = cbInfo / sizeof(wchar_t); // 字节数转宽字符数
        wchar_t* xml = (wchar_t*)malloc((xl + 1) * sizeof(wchar_t)); // 分配副本缓冲
        if (!xml) continue;                   // 分配失败跳过
        memcpy(xml, pInfo, cbInfo);           // 复制 XML
        while (xl > 0 && xml[xl - 1] == L'\0') xl--; // 去掉尾部多余 NUL
        xml[xl] = L'\0';                      // 补结尾符

        wchar_t* blk = ImgBlock(xml, i);      // 提取第 i 个 IMAGE 块
        free(xml);                            // 释放整体副本
        if (!blk) continue;                   // 提取失败跳过

        /* 显示名优先级：XML DISPLAYNAME > XML NAME > VERSIONINFO > Unknown */
        wchar_t disp[512] = L"";              // 显示名缓冲
        const wchar_t* dn = XmlDisplayName(blk); // 尝试 DISPLAYNAME
        if (dn[0] != L'\0')                   // 命中
            lstrcpynW(disp, dn, 512);         // 使用 DISPLAYNAME
        else if (GetField(blk, L"NAME", disp, 512)) // 尝试 NAME
        {
            /* 已取到 NAME */
        }
        else if (hasVI && viEd[0] != L'\0')   // VERSIONINFO 描述
            lstrcpynW(disp, viEd, 512);       // 使用描述
        else if (hasVI && viProd[0] != L'\0') // VERSIONINFO 产品名
            lstrcpynW(disp, viProd, 512);     // 使用产品名
        else
            lstrcpynW(disp, L"Unknown", 512); // 全部失败兜底

        /* 架构：XML <ARCH> 数字代码转可读名称 */
        wchar_t archRaw[16] = L"";            // 架构原始码缓冲
        GetField(blk, L"ARCH", archRaw, 16);  // 提取 ARCH

        /* 版本：优先 XML <VERSION>，其次 VERSIONINFO */
        wchar_t verXml[64] = L"";             // XML 版本缓冲
        ComposeXmlVersion(blk, verXml, 64);   // 从 VERSION 拼版本
        free(blk);                            // 释放块副本

        ImgEntry e;                           // 输出条目
        ZeroMemory(&e, sizeof(e));            // 清零
        e.Index = (int)i;                     // 映像索引
        lstrcpynW(e.DisplayName, disp, 512);  // 显示名
        if (verXml[0]) lstrcpynW(e.FileVersion, verXml, 64); // 优先 XML 版本
        else if (hasVI) lstrcpynW(e.FileVersion, viVer, 64); // 其次 VERSIONINFO
        lstrcpynW(e.Arch, ArchStr(archRaw), 16); // 架构文本

        found++;                              // 计数 +1
        if (!cb(&e, ctx)) break; // 调用方要求提前结束 // 回调返回 FALSE 则停止
    }
    api->Close(hw);                           // 关闭 WIM 句柄
    if (found == 0)                           // 一个都没解析出来
        SetErr(err, errLen, L"Failed to read image information from %s", path); // 报错
    return found;                             // 返回数量
}

/* ==================== 对外接口 ==================== */
int ImgParseFile(const wchar_t* path, ImgEnumCallback cb, void* ctx,
                 wchar_t* err, int errLen)
{
    if (err && errLen > 0) err[0] = L'\0';    // 错误缓冲先清空
    if (!path || !path[0] || !cb)             // 参数检查
    {
        SetErr(err, errLen, L"Invalid arguments"); // 报错
        return -1;                            // 失败
    }

    WimApi api;                               // wimgapi 封装
    ZeroMemory(&api, sizeof(api));            // 清零
    if (!WimLoad(&api))                       // 加载失败
    {
        SetErr(err, errLen, L"Failed to load wimgapi.dll"); // 报错
        return -1;                            // 失败
    }

    const wchar_t* ext = wcsrchr(path, L'.'); // 扩展名起始
    const bool isIso = ext && _wcsicmp(ext, L".iso") == 0; // 判断是否 ISO

    if (!isIso)                               // 普通 WIM/ESD
    {
        const int n = ImgParseOne(&api, path, cb, ctx, err, errLen); // 直接解析
        WimFree(&api);                        // 释放模块
        return n;                             // 返回结果
    }

    /* .iso：临时挂载并定位 sources\install.wim|esd */
    IsoMnt mnt;                               // 挂载状态
    ZeroMemory(&mnt, sizeof(mnt));            // 清零
    mnt.hVhd = INVALID_HANDLE_VALUE;          // 句柄初始化为无效
    mnt.byUs = FALSE;                         // 默认不是自己挂载

    if (!IsoMount(&mnt, path, err, errLen))   // 挂载失败
    {
        WimFree(&api);                        // 释放模块
        return -1;                            // 失败
    }

    int n = -1;                               // 结果默认失败
    wchar_t full[MAX_PATH] = L"";             // 安装镜像完整路径
    BOOL found = FALSE;                       // 是否已定位
    /* 优先读取 install.wim / install.esd（系统版本所在镜像） */
    for (int i = 0; i < mnt.cnt && !found; i++) // 遍历候选盘符
        found = FindInstallWim(mnt.drv[i], full, MAX_PATH); // 找 install.wim/esd
    if (!found)                               // 没找到标准名
    {
        /* 兜底：任意 sources 下的 wim/esd（跳过 boot.wim） */
        const wchar_t* pats[] = { L"*.wim", L"*.esd" }; // 通配模式表
        for (int i = 0; i < mnt.cnt && !found; i++) // 遍历盘符
        {
            for (int k = 0; k < 2 && !found; k++) // 遍历扩展名
            {
                wchar_t pat[80];              // 通配路径
                swprintf(pat, 80, L"%c:\\sources\\%s", mnt.drv[i], pats[k]); // 构造通配
                WIN32_FIND_DATAW fd;          // 查找结果
                ZeroMemory(&fd, sizeof(fd));  // 清零
                HANDLE hf = FindFirstFileW(pat, &fd); // 查找
                if (hf != INVALID_HANDLE_VALUE) // 找到文件
                {
                    FindClose(hf);            // 关闭句柄
                    if (_wcsicmp(fd.cFileName, L"boot.wim") != 0) // 排除 boot.wim
                    {
                        swprintf(full, MAX_PATH, L"%c:\\sources\\%s", mnt.drv[i], fd.cFileName); // 输出路径
                        found = TRUE;         // 标记成功
                    }
                }
            }
        }
    }
    if (found)                                // 已定位
        n = ImgParseOne(&api, full, cb, ctx, err, errLen); // 解析安装镜像
    else                                      // 未定位
        SetErr(err, errLen, L"No install.wim/esd found on mounted ISO"); // 报错

    IsoDestroy(&mnt);                         // 卸载 ISO
    WimFree(&api);                            // 释放 wimgapi
    return n;                                 // 返回解析结果
}

/* ==================== 部署用镜像源解析 ==================== */
static IsoMnt g_deployMnt;                    // 部署期全局挂载状态
static BOOL g_deployMntActive = FALSE;        // 挂载是否有效

void ImgReleaseMountedIso(void)               // 释放部署期 ISO 挂载
{
    if (g_deployMntActive)                    // 仅在有效时释放
    {
        IsoDestroy(&g_deployMnt);             // 卸载并释放模块
        ZeroMemory(&g_deployMnt, sizeof(g_deployMnt)); // 清零结构
        g_deployMntActive = FALSE;            // 标记无效
    }
}

BOOL ImgResolveImageFile(const wchar_t* path, wchar_t* out, int outLen,
                         wchar_t* err, int errLen)
{
    if (err && errLen > 0) err[0] = L'\0';    // 错误缓冲清空
    if (out && outLen > 0) out[0] = L'\0';    // 输出清空
    if (!path || !path[0] || !out || outLen <= 0) // 参数检查
    {
        if (err) SetErr(err, errLen, L"Invalid image source"); // 报错
        return FALSE;                         // 失败
    }

    const wchar_t* ext = wcsrchr(path, L'.'); // 扩展名
    const BOOL isIso = ext && _wcsicmp(ext, L".iso") == 0; // 是否 ISO
    if (!isIso)                               // WIM/ESD 直接可用
    {
        lstrcpynW(out, path, outLen);         // 原样返回路径
        return TRUE;                          // 成功
    }

    ImgReleaseMountedIso();                   // 清理上次残留挂载
    ZeroMemory(&g_deployMnt, sizeof(g_deployMnt)); // 清零结构
    g_deployMnt.hVhd = INVALID_HANDLE_VALUE;  // 句柄置无效
    g_deployMnt.byUs = FALSE;                 // 默认非自挂

    if (!IsoMount(&g_deployMnt, path, err, errLen)) return FALSE; // 挂载失败
    g_deployMntActive = TRUE;                 // 标记挂载有效

    wchar_t full[MAX_PATH] = L"";             // 安装镜像路径
    BOOL found = FALSE;                       // 是否找到
    for (int i = 0; i < g_deployMnt.cnt && !found; i++) // 找 install.wim/esd
        found = FindInstallWim(g_deployMnt.drv[i], full, MAX_PATH);
    if (!found)                               // 标准名找不到
    {
        const wchar_t* pats[] = { L"*.wim", L"*.esd" }; // 兜底通配
        for (int i = 0; i < g_deployMnt.cnt && !found; i++) // 遍历盘符
        {
            for (int k = 0; k < 2 && !found; k++) // 遍历扩展名
            {
                wchar_t pat[80];              // 通配路径
                swprintf(pat, 80, L"%c:\\sources\\%s", g_deployMnt.drv[i], pats[k]); // 构造通配
                WIN32_FIND_DATAW fd;          // 查找结果
                ZeroMemory(&fd, sizeof(fd));  // 清零
                HANDLE hf = FindFirstFileW(pat, &fd); // 查找
                if (hf != INVALID_HANDLE_VALUE) // 找到
                {
                    FindClose(hf);            // 关闭句柄
                    if (_wcsicmp(fd.cFileName, L"boot.wim") != 0) // 排除 boot.wim
                    {
                        swprintf(full, MAX_PATH, L"%c:\\sources\\%s", // 拼路径
                                 g_deployMnt.drv[i], fd.cFileName);
                        found = TRUE;         // 标记成功
                    }
                }
            }
        }
    }

    if (!found)                               // 仍找不到
    {
        ImgReleaseMountedIso();               // 释放挂载
        SetErr(err, errLen, L"No install.wim/esd found on mounted ISO"); // 报错
        return FALSE;                         // 失败
    }

    lstrcpynW(out, full, outLen);             // 输出安装镜像路径
    return TRUE;                              // 成功
}
