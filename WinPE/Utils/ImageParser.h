#pragma once // 预处理指令：确保本头文件在同一个编译单元中只被包含一次
#include <windows.h> // 引入 Windows API 核心头文件：提供 BOOL、HANDLE、wchar_t 相关支持

// ==================== 镜像解析模块（移植自 ConsoleApplication1） ====================
// 说明：动态加载 wimgapi.dll 读取 WIM/ESD 的映像信息；
//       支持 .iso（通过 virtdisk.dll 临时挂载后定位 sources\install.wim|esd）。

// 单个映像（版本）信息
typedef struct ImgEntry
{
    int     Index;            // 映像索引（从 1 开始；对应 DISM /Apply-Image 的 /Index:N）
    wchar_t DisplayName[512]; // 版本显示名（XML DISPLAYNAME > VERSIONINFO 的优先级）
    wchar_t FileVersion[64];  // 文件版本号（可能为空；来自 XML VERSION 或 VERSIONINFO）
    wchar_t Arch[16];         // 架构：x86 / x64 / ARM / ARM64 等（XML ARCH 数字代码转可读名）
} ImgEntry;

// 每解析到一个映像回调一次；返回 FALSE 可提前结束
typedef BOOL (*ImgEnumCallback)(const ImgEntry* entry, void* ctx);

// 解析 .wim/.esd/.iso 镜像文件，对每个映像调用一次 cb
// 返回值：解析到的映像数量（>=0）；失败返回 -1 并把原因写入 err
int ImgParseFile(const wchar_t* path, ImgEnumCallback cb, void* ctx,
                 wchar_t* err, int errLen);

// 为部署解析镜像源：.wim/.esd 直接返回原路径；
// .iso 临时挂载并定位 sources\install.wim|esd（返回的路径保持有效，
// 直到调用 ImgReleaseMountedIso）。
BOOL ImgResolveImageFile(const wchar_t* path, wchar_t* out, int outLen,
                         wchar_t* err, int errLen);

// 释放 ImgResolveImageFile 为 .iso 创建的挂载
void ImgReleaseMountedIso(void);
