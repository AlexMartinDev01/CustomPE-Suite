#define _CRT_SECURE_NO_WARNINGS // 禁用 CRT 对不安全函数（wcscpy/strcpy 等）的编译告警，便于移植旧代码
#include "SchemeParser.h"       // 引入本模块头文件：SchemeInfo/SchemePart 结构与对外接口声明
#include <stdio.h>              // 引入标准 I/O 头：提供 swprintf 宽字符格式化输出
#include <stdlib.h>             // 引入标准库头：提供 malloc/free 内存分配与 _wtoi64 字符串转整数
#include <string.h>             // 引入字符串处理头：提供 memcpy、wcslen、wcschr 等函数
#include <wchar.h>              // 引入宽字符头：提供宽字符字符串函数与 wcstoul 族函数

/* 大小写不敏感提取 <tag>...</tag> 的内容（自动去空白） */
static BOOL GetTag(const wchar_t* xml, const wchar_t* tag, wchar_t* out, int outLen)
{
    if (!out || outLen <= 0) return FALSE; // 输出缓冲区无效时直接失败，防止写坏内存
    out[0] = L'\0';                        // 先清空输出缓冲，保证失败/空内容时调用方拿到空串
    wchar_t open[80], close[80];           // 存放拼接出的开标签与闭标签字符串
    swprintf(open, 80, L"<%s>", tag);      // 生成开标签文本，如 "<ESP>"
    swprintf(close, 80, L"</%s>", tag);    // 生成闭标签文本，如 "</ESP>"
    const size_t openLen = wcslen(open);   // 记录开标签长度，供前缀比较使用
    const size_t closeLen = wcslen(close); // 记录闭标签长度，供前缀比较使用

    const wchar_t* p = xml;                // 游标指针，从 XML 文本头部开始扫描
    while (*p)                             // 只要游标还没走到字符串结尾就继续
    {
        const wchar_t* lt = wcschr(p, L'<'); // 找到下一个 '<' 字符位置
        if (!lt) break;                      // 后面再也没有标签了，结束查找
        if (_wcsnicmp(lt, open, openLen) == 0) // 若当前位置确实是目标开标签（忽略大小写）
        {
            const wchar_t* s = lt + openLen; // 内容起点：跳过开标签本身
            const wchar_t* e = NULL;         // 内容终点指针，先置空
            const wchar_t* q = s;            // 在内容区里查找闭标签的游标
            while (*q)                       // 遍历内容区
            {
                const wchar_t* cl = wcschr(q, L'<'); // 找下一个 '<'
                if (!cl) break;                      // 没找到闭标签，直接退出内层循环
                if (_wcsnicmp(cl, close, closeLen) == 0) { e = cl; break; } // 命中闭标签，记录终点
                q = cl + 1;                          // 跳过当前 '<' 继续向后找
            }
            if (!e) e = s + wcslen(s);               // 找不到闭标签时，把内容终点放宽到字符串末尾
            while (s < e && (*s == L' ' || *s == L'\t' || *s == L'\r' || *s == L'\n')) s++; // 去除内容左侧空白
            while (e > s && (e[-1] == L' ' || e[-1] == L'\t' || e[-1] == L'\r' || e[-1] == L'\n')) e--; // 去除内容右侧空白
            size_t n = (size_t)(e - s);              // 计算去除空白后的实际内容长度
            if (n >= (size_t)outLen) n = outLen - 1; // 若内容超出缓冲区，截断到最大可容纳长度（留 1 字节给结尾符）
            memcpy(out, s, n * sizeof(wchar_t));     // 把内容字节复制到输出缓冲区
            out[n] = L'\0';                          // 手动补上宽字符串结尾符
            return TRUE;                             // 成功提取，返回 TRUE
        }
        p = lt + 1;                          // 当前位置不是目标标签，跳过这个 '<' 继续扫描
    }
    return FALSE;                            // 全文本中找不到目标标签，返回 FALSE
}

/* 提取第 index 个 "<tag ...> ... </tag>" 块（从 0 开始；支持开标签带属性） */
static BOOL GetBlockN(const wchar_t* xml, const wchar_t* tag, int index,
                      wchar_t* out, int outLen)
{
    if (!xml || !tag || !out || outLen <= 0) return FALSE; // 任一参数无效直接失败
    wchar_t open[96], close[96];                  // 存放拼接出的开/闭标签前缀
    swprintf(open, 96, L"<%s", tag);              // 生成开标签前缀（不含 '>'，因此可匹配带属性的开标签）
    swprintf(close, 96, L"</%s>", tag);           // 生成闭标签完整文本
    const size_t openN = wcslen(open);            // 开标签前缀长度
    const size_t closeN = wcslen(close);          // 闭标签长度

    const wchar_t* p = xml;                       // 扫描游标
    int found = 0;                                // 已找到的第几个目标块（0-based）
    while (*p)                                    // 遍历整个 XML
    {
        const wchar_t* lt = wcschr(p, L'<');      // 找下一个 '<'
        if (!lt) break;                           // 找不到就结束
        if (_wcsnicmp(lt, open, openN) == 0)      // 当前标签前缀匹配目标开标签
        {
            const wchar_t* gt = wcschr(lt + openN, L'>'); // 在标签名之后找 '>'（属性结束）
            if (!gt) return FALSE;                        // 开标签没有闭合，格式非法
            const wchar_t* s = gt + 1;                    // 块内容起点：'>' 之后
            const wchar_t* q = s;                         // 在块内找闭标签的游标
            const wchar_t* e = NULL;                      // 块内容终点
            while (*q)                                    // 遍历块内容
            {
                const wchar_t* cl = wcschr(q, L'<');      // 找下一个 '<'
                if (!cl) break;                           // 没有则退出
                if (_wcsnicmp(cl, close, closeN) == 0) { e = cl; break; } // 命中闭标签
                q = cl + 1;                               // 继续向后
            }
            if (!e) return FALSE;                 // 找不到闭标签，格式非法
            if (found == index)                   // 若这正是我们想要的第 index 块
            {
                size_t n = (size_t)(e - s);       // 计算块内容长度
                if (n >= (size_t)outLen) n = outLen - 1; // 超长则截断
                memcpy(out, s, n * sizeof(wchar_t));      // 复制块内容
                out[n] = L'\0';                   // 补结尾符
                return TRUE;                      // 提取成功
            }
            found++;                              // 不是目标块，计数后跳过该块继续找
            p = e + closeN;                       // 游标跳到闭标签之后，避免块内嵌套干扰
            continue;
        }
        p = lt + 1;                               // 不是目标标签，向后移动一个字符
    }
    return FALSE;                                 // 全程未找到第 index 块
}

/* 从一段 XML 文本解析 ESP/MSR/OS/WinRE，返回分区数 */
static int ParseSchemeDocParts(const wchar_t* xml, SchemePart* parts, int maxParts)
{
    static const wchar_t* keys[] = { L"ESP", L"MSR", L"OS", L"WinRE" }; // 固定解析顺序
    int pc = 0;                                   // 已解析分区计数
    for (int k = 0; k < 4 && pc < maxParts; k++)  // 遍历键名且不超过调用方给的容量
    {
        wchar_t blk[8192] = { 0 };                // 块缓冲区
        if (!GetTag(xml, keys[k], blk, 8192)) continue; // 无此分区则跳过
        SchemePart* p = &parts[pc];               // 当前空位指针
        lstrcpynW(p->Key, keys[k], 16);           // 记录键名
        wchar_t tmp[256] = { 0 };                 // 通用临时缓冲
        if (GetTag(blk, L"Type", tmp, 256)) lstrcpynW(p->Type, tmp, 16); // 类型
        if (GetTag(blk, L"Label", tmp, 256)) lstrcpynW(p->Label, tmp, 32); // 标签
        if (GetTag(blk, L"FileSystem", tmp, 256)) lstrcpynW(p->Fs, tmp, 16); // 文件系统
        if (GetTag(blk, L"Size", tmp, 256) && tmp[0]) // <Size> 有内容
            p->SizeMB = _wtoi64(tmp);             // 转成 MB 数值
        else
            p->SizeMB = -1; // 空 = 占满剩余空间
        if (GetTag(blk, L"Id", tmp, 256)) lstrcpynW(p->Id, tmp, 80); // GUID
        if (GetTag(blk, L"Attributes", tmp, 256) && tmp[0]) // 属性有内容
            p->Attributes = _wcstoui64(tmp, NULL, 0); // 支持十六进制
        if (GetTag(blk, L"Letter", tmp, 256)) lstrcpynW(p->Letter, tmp, 8); // 盘符
        pc++;                                     // 计数 +1
    }
    return pc;                                    // 返回该文档片段解析出的分区数
}

// 从文件路径中提取"不含扩展名的文件名"作为默认方案名
static void SchemeNameFromFile(const wchar_t* path, wchar_t* name, int nameLen)
{
    const wchar_t* slash = wcsrchr(path, L'\\');  // 定位最后一个目录分隔符
    const wchar_t* base = slash ? slash + 1 : path; // 取出纯文件名部分
    lstrcpynW(name, base, nameLen);               // 复制到输出
    wchar_t* dot = wcsrchr(name, L'.');           // 找扩展名分隔点
    if (dot) *dot = L'\0';                        // 去掉扩展名
}

/* 解析单个 XML 文件里的 1..N 个方案 */
int SchemeParseFileEx(const wchar_t* path, SchemeInfo* out, int maxOut)
{
    if (!path || !out || maxOut <= 0) return 0;   // 参数合法性检查

    HANDLE hf = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, // 打开 XML 文件
        NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hf == INVALID_HANDLE_VALUE) return 0;     // 打开失败返回 0 个方案
    DWORD sz = GetFileSize(hf, NULL);             // 文件大小
    if (sz == 0 || sz > 4 * 1024 * 1024) { CloseHandle(hf); return 0; } // 空文件或 >4MB 直接拒绝
    char* buf = (char*)malloc(sz + 1);            // 分配字节缓冲
    if (!buf) { CloseHandle(hf); return 0; }      // 分配失败
    DWORD rd = 0;                                 // 实际读取字节数
    if (!ReadFile(hf, buf, sz, &rd, NULL) || rd == 0) { free(buf); CloseHandle(hf); return 0; } // 读取失败
    CloseHandle(hf);                              // 关闭文件
    buf[rd] = '\0';                               // 补 NUL

    const int wn = MultiByteToWideChar(CP_UTF8, 0, buf, (int)rd, NULL, 0); // UTF-8 -> 宽字符所需长度
    wchar_t* w = (wchar_t*)malloc((wn + 1) * sizeof(wchar_t));             // 分配宽字符缓冲
    if (!w) { free(buf); return 0; }              // 分配失败
    MultiByteToWideChar(CP_UTF8, 0, buf, (int)rd, w, wn); // 实际转换
    w[wn] = L'\0';                                // 补结尾符
    free(buf);                                    // 释放字节缓冲

    int parsed = 0;                               // 成功解析的方案数
    wchar_t probe[64] = { 0 };                    // 探测缓冲区
    const BOOL hasBlocks = GetBlockN(w, L"PartitionScheme", 0, probe, 64); // 探测是否存在 <PartitionScheme> 块
    if (hasBlocks)                                // 合并格式：一个文件内多个方案块
    {
        for (int idx = 0; idx < SCHEME_MAX_ITEMS && parsed < maxOut; idx++) // 逐个块解析，直到耗尽
        {
            wchar_t block[16384] = { 0 };         // 块内容缓冲区
            if (!GetBlockN(w, L"PartitionScheme", idx, block, 16384)) break; // 取不到第 idx 块则结束
            SchemeInfo* s = &out[parsed];         // 当前输出项
            ZeroMemory(s, sizeof(*s));            // 清零
            wchar_t nm[160] = { 0 };              // 方案名缓冲
            if (!GetTag(block, L"Name", nm, 160) || !nm[0]) // 块内没有 <Name>
            {
                /* 块内没有 <Name> 时用文件名 + 序号 */
                SchemeNameFromFile(path, nm, 160);         // 从文件名取默认名
                wchar_t suffix[64];                        // 序号后缀缓冲
                swprintf(suffix, 64, L" %d", idx + 1);     // 如 " 2"
                wcscat_s(nm, 160, suffix);                 // 拼接得到 "OEM_Partition_Schemes 2"
            }
            lstrcpynW(s->Name, nm, 160);          // 写方案名
            lstrcpynW(s->File, path, MAX_PATH);   // 写源文件路径
            s->PartCount = ParseSchemeDocParts(block, s->Parts, SCHEME_MAX_PARTS); // 解析该块内分区
            if (s->PartCount > 0) parsed++;       // 只有真正解析出分区才计数
        }
    }
    else
    {
        /* 传统单方案文件 */
        SchemeInfo* s = &out[0];                  // 只用输出数组第一项
        ZeroMemory(s, sizeof(*s));                // 清零
        SchemeNameFromFile(path, s->Name, 160);   // 方案名 = 文件名
        lstrcpynW(s->File, path, MAX_PATH);       // 记录路径
        s->PartCount = ParseSchemeDocParts(w, s->Parts, SCHEME_MAX_PARTS); // 解析整份文档
        if (s->PartCount > 0) parsed = 1;         // 成功则计数为 1
    }

    free(w);                                      // 释放宽字符 XML 缓冲
    return parsed;                                // 返回方案数量
}

/* 格式化为多行明细：标签 / 大小 / 文件系统 / 分区键名 */
void SchemeFormatDetail(const SchemeInfo* s, wchar_t* out, int outLen)
{
    if (!out || outLen <= 0) return;              // 输出缓冲无效直接返回
    out[0] = L'\0';                               // 先清空输出
    for (int i = 0; i < s->PartCount; i++)        // 遍历每个分区
    {
        const SchemePart* p = &s->Parts[i];       // 当前分区指针
        wchar_t sz[64];                           // 大小文本缓冲
        if (p->SizeMB <= 0)                       // -1/0：占满剩余空间的分区
            lstrcpynW(sz, L"REST", 64);           // 显示 REST
        else if (p->SizeMB >= 1024)               // 达到 1GB 以上
            swprintf(sz, 64, L"%.1f GB", (double)p->SizeMB / 1024.0); // 转 GB 显示
        else                                      // 小于 1GB
            swprintf(sz, 64, L"%lld MB", p->SizeMB); // 以 MB 显示

        const wchar_t* label = (p->Label[0] ? p->Label : p->Key); // 有标签用标签，否则退回键名
        wchar_t line[256];                        // 单行文本缓冲
        swprintf(line, 256, L"%-10s %9s  %-6s  (%s)", // 左对齐排版：标签/大小/文件系统/键名
            label, sz, (p->Fs[0] ? p->Fs : L"-"), p->Key);
        const int ln = lstrlenW(line);            // 当前行长度
        if (lstrlenW(out) + ln + 2 < outLen)      // 防止累计超出输出缓冲
        {
            wcscat(out, line);                    // 追加明细行
            wcscat(out, L"\r\n");                 // 追加换行
        }
    }
}
