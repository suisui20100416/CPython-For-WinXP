/* PC/xp_compat.h —— Windows XP SP3 32 位适配的集中声明补齐。
 *
 * 背景：构建钉在 SDK 7.1A 头 + UCRT 10.0.10240（唯一 XP 干净的 UCRT 世代，
 * 实测见 Python/xp-build/tests/crt_matrix/）。该世代的头不含 Vista+ API 的
 * 声明，于是所有 Vista+ 调用点都会在编译期暴露，而不是静默链接。
 *
 * 本文件只做三件事，不写任何运行逻辑：
 * 1. 提供源码在其目标平台会从新头像拿到的**类型与常量**
 *    （FILE_ID_128、LOAD_LIBRARY_SEARCH_*、ENABLE_VIRTUAL_TERMINAL_PROCESSING、
 *    PROC_THREAD_ATTRIBUTE_* 之类），让 3.15 的源代码原样编译；
 * 2. 凡是 XP 上不存在实现的 API（PathCch*、BCrypt 等），这里**只声明，
 *    实现由各使用点的既有动态探测/回退路径负责**（fileutils.c 已有
 *    GetProcAddress 探测分支）或由 xp 分支的兼容实现文件提供；
 * 3. 不含任何需要 XP 上不存在的导入库才能链接的符号。
 *
 * 所有声明都注明来源标准头（10.0.19041 SDK 的 shared/um），保持与新版
 * 构建在结构上一致。
 */
#ifndef Py_XP_COMPAT_H
#define Py_XP_COMPAT_H

#if defined(MS_WINDOWS) && defined(Py_BUILD_CORE) && defined(Py_XP)

/* XP 的头集词汇集必须先与 _WIN32_WINNT 一起进：xp_compat.h 是 pyconfig.h
 * 的尾巴位置的 include（早于后续任何 windows.h；但若某个 TU 先于 pyconfig
 * include 了 windows.h，则这里的值不生效——这在 pythoncore 的 include
 * 纪律下不会发生：全部 TU 先 #include "Python.h"）。 */
#if defined(Py_XP)
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0501
#endif
#ifndef WINVER
#define WINVER 0x0501
#endif
#ifndef NTDDI_VERSION
#define NTDDI_VERSION NTDDI_WINXP
#endif
#endif

/* SDK 7.1A 的 windows.h 在非 LEAN_AND_MEAN 路径无条件 #include <winsock.h>
 * （winsock1），而 pythoncore 的 socketmodule.h 会再拉 <winsock2.h>，
 * 一切 sockaddr/fd_set 全量重定义冲突。xp_compat.h 在 pyconfig.h 的位置最先
 * 被读到，在此定义 LEAN_AND_MEAN 以排除 winsock.h；真正需要 winsock 的
 * 翻译单元自己且只 include <winsock2.h>（上游本来就是这套顺序契约：正式
 * 构建的 vcxproj 也是 winsock2 已定义 _WINSOCK2API_ 后再挂 windows.h）。
 */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

/* NLS 词汇（LCMap*、LOCALE_NAME_*）：pythoncore 的 _winapi.c 直接引用，
 * 7.1A 头集里 winnls.h 不是 windows.h 的默认拉起项，在此显式带上
 * （LOCALE_NAME_INVARIANT 等全部来自这里）。 */
#include <winnls.h>

/* -------- FILE_ID_128 + FILE_ID_INFO + FILE_BASIC_INFO +
 * FILE_ATTRIBUTE_TAG_INFO + FILE_INFO_BY_HANDLE_CLASS --------
 * winbase.h 把这一族包在 #if (_WIN32_WINNT >= 0x0600)（7.1A 实测 :14729，
 * 新 SDK 同段）。XP 构建（0x0501）下不展开，需要在此补；新 SDK 构建下
 * （Py_XP 未定义或版本 ≥0x0600）由系统头提供，此处退化为空。 */
#if !defined(_FILE_INFO_BY_HANDLE_CLASS_DECLARED) && \
    (!defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0600)
#define _FILE_INFO_BY_HANDLE_CLASS_DECLARED
typedef enum _FILE_INFO_BY_HANDLE_CLASS {
    FileBasicInfo,
    FileStandardInfo,
    FileNameInfo,
    FileRenameInfo,
    FileDispositionInfo,
    FileAllocationInfo,
    FileEndOfFileInfo,
    FileStreamInfo,
    FileCompressionInfo,
    FileAttributeTagInfo,
    FileIdBothDirectoryInfo,
    FileIdBothDirectoryRestartInfo,
    FileIoPriorityHintInfo,
    FileRemoteProtocolInfo,
    FileFullDirectoryInfo,
    FileFullDirectoryRestartInfo,
    FileStorageInfo,
    FileIdInfo,
    FileIdExtdDirectoryInfo,
    FileIdExtdDirectoryRestartInfo,
    FileDispositionInfoEx,
    FileRenameInfoEx,
    FileCaseSensitiveInfo,
    FileNormalizedNameInfo,
    MaximumFileInfoByHandleClass
} FILE_INFO_BY_HANDLE_CLASS, *PFILE_INFO_BY_HANDLE_CLASS;

#if !defined(FILE_INVALID_FILE_ID)
typedef struct _FILE_ID_128 {
    BYTE Identifier[16];
} FILE_ID_128, *PFILE_ID_128;
#endif

#if (!defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0600) && \
    !defined(_FILE_ID_INFO_DECLARED)
#define _FILE_ID_INFO_DECLARED
typedef struct _FILE_ID_INFO {
    ULONGLONG VolumeSerialNumber;
    FILE_ID_128 FileId;
} FILE_ID_INFO, *PFILE_ID_INFO;
#endif

typedef struct _FILE_BASIC_INFO {
    LARGE_INTEGER CreationTime;
    LARGE_INTEGER LastAccessTime;
    LARGE_INTEGER LastWriteTime;
    LARGE_INTEGER ChangeTime;
    DWORD FileAttributes;
} FILE_BASIC_INFO, *PFILE_BASIC_INFO;

typedef struct _FILE_ATTRIBUTE_TAG_INFO {
    DWORD FileAttributes;
    DWORD ReparseTag;
} FILE_ATTRIBUTE_TAG_INFO, *PFILE_ATTRIBUTE_TAG_INFO;
#endif

/* -------- FILE_BASIC_INFO / FILE_ID_INFO / FILE_ATTRIBUTE_TAG_INFO /
 * FILE_INFO_BY_HANDLE_CLASS (winbase.h 7.1A 已全部 typedef，见
 * winbase.h:14730-14900) --------
 * XP kernel32 没有 GetFileInformationByHandleEx 运行导出；类型与声明已在
 * SDK 头，因此本文件**不重复定义**；仅把符号重定向到 XP 实现。
 */

/* GetFileInformationByHandleEx（Win7+ 声明；XP kernel32 无导出）：
 * 实现在 PC/xp_compat.c（模拟 FileBasicInfo/FileAttributeTagInfo；
 * 其他 class 一律 ERROR_NOT_SUPPORTED）。符号重定向到 xp_ 前缀实现，
 * 链接时函数体随 pythoncore 提供，导入表不出现该符号。 */
#ifndef Py_USE_SYSTEM_FILEINFOAPI
BOOL
WINAPI
xp_GetFileInformationByHandleEx(
    HANDLE hFile,
    FILE_INFO_BY_HANDLE_CLASS FileInformationClass,
    LPVOID lpFileInformation,
    DWORD dwBufferSize);
#define GetFileInformationByHandleEx xp_GetFileInformationByHandleEx
#endif

/* -------- FILE_DEVICE_CONSOLE (winioctl.h, Win8+) -------- */
#ifndef FILE_DEVICE_CONSOLE
#define FILE_DEVICE_CONSOLE     0x00000068
#endif

/* -------- FILE_FS_PERSISTENT_VOLUME_INFORMATION + FSCTL (Win8+ storage) -------- */
#ifndef FILE_FS_PERSISTENT_VOLUME_INFORMATION_DECLARED
#define FILE_FS_PERSISTENT_VOLUME_INFORMATION_DECLARED
typedef struct _FILE_FS_PERSISTENT_VOLUME_INFORMATION {
    ULONG Version;
    ULONG FlagMask;
    ULONG Flags;
    ULONG VolumeFlags;
    ULONG Reserved[2];
} FILE_FS_PERSISTENT_VOLUME_INFORMATION, *PFILE_FS_PERSISTENT_VOLUME_INFORMATION;
#endif
#ifndef FSCTL_QUERY_PERSISTENT_VOLUME_STATE
#define FSCTL_QUERY_PERSISTENT_VOLUME_STATE CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 169, METHOD_BUFFERED, FILE_ANY_ACCESS)
#endif

/* -------- LOAD_LIBRARY_SEARCH_* (libloaderapi.h, Win8+) --------
 * XP 没有这些 flag 的实现（AddDllDirectory 也只在带 KB2533623 的内核更新里）。
 * 这里只补常量供编译。没有隐藏的加载器兼容层：XP 会忽略这些位并按默认
 * 搜索顺序查找依赖，而默认顺序不包含扩展模块所在的 DLLs\ 目录。
 * dynload_win.c 因此在 Py_XP 下改用 XP 支持的
 * LOAD_WITH_ALTERED_SEARCH_PATH（以 .pyd 自身目录为搜索起点）；其余
 * 使用点（python3.dll 探测、fileutils.c 的 SYSTEM32）在默认搜索下仍能
 * 解析到目标 DLL。
 */
#ifndef LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR
#define LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR    0x00000100
#endif
#ifndef LOAD_LIBRARY_SEARCH_DEFAULT_DIRS
#define LOAD_LIBRARY_SEARCH_DEFAULT_DIRS    0x00001000
#endif
#ifndef LOAD_LIBRARY_SEARCH_REQUIRE_RELOCATED
#define LOAD_LIBRARY_SEARCH_REQUIRE_RELOCATED 0x00010000
#endif
#ifndef LOAD_LIBRARY_SEARCH_SYSTEM32
#define LOAD_LIBRARY_SEARCH_SYSTEM32        0x00000800
#endif
#ifndef LOAD_LIBRARY_SEARCH_APPLICATION_DIR
#define LOAD_LIBRARY_SEARCH_APPLICATION_DIR 0x00000200
#endif
#ifndef LOAD_LIBRARY_SEARCH_USER_DIRS
#define LOAD_LIBRARY_SEARCH_USER_DIRS       0x00000400
#endif

/* -------- AddDllDirectory / SetDefaultDllDirectories (Win8+, KB2533623) --------
 * XP SP3 无 AddDllDirectory。运行期探测分支负责降级；这里补真实签名与
 * flag，供探测代码引用类型。
 */
typedef void *DLL_DIRECTORY_COOKIE;

/* -------- ENABLE_VIRTUAL_TERMINAL_PROCESSING (Win10 console) --------
 * initconfig.c 控制台模式判断使用；XP/早版系统没有 VT 处理，探测分支
 * (SetConsoleMode 失败) 自然走回退。
 */
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING  0x0004
#endif
#ifndef DISABLE_NEWLINE_AUTO_RETURN
#define DISABLE_NEWLINE_AUTO_RETURN         0x0008
#endif
#ifndef ENABLE_VIRTUAL_TERMINAL_INPUT
#define ENABLE_VIRTUAL_TERMINAL_INPUT       0x0200
#endif

/* -------- PROC_THREAD_ATTRIBUTE_* (Win7+) --------
 * winbase.h:3877 无条件 typedef 了 P/LPPROC_THREAD_ATTRIBUTE_LIST
 * （不透明指针），本文件不再重复。LPPROC 指向的结构布局 XP 不关心：
 * stub 恒返回 FALSE。
 * STARTUPINFOEXW（ Vista+ 扩展）在 7.1A 处于 0x0600 guard（winbase:6586
 * 实测为 0x0600 块），需要本地补：布局 = STARTUPINFO 在前 + 属性列表
 * 指针在后（与 Vista 布局一致）。 */
#ifndef _PROC_THREAD_ATTRIBUTE_XP_DECLARED
#define _PROC_THREAD_ATTRIBUTE_XP_DECLARED
typedef struct _STARTUPINFOEXW {
    STARTUPINFOW StartupInfo;
    LPPROC_THREAD_ATTRIBUTE_LIST lpAttributeList;
} STARTUPINFOEXW, *LPSTARTUPINFOEXW;
#endif

#ifndef PROC_THREAD_ATTRIBUTE_HANDLE_LIST
#define PROC_THREAD_ATTRIBUTE_HANDLE_LIST   0x00020002
#endif
#ifndef PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
#define PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE 0x00020016
#endif

/* XP 无 ProcThreadAttributeList 实现（XP kernel32 缺导出）。stub 返回
 * 失败，subprocess 的 lpAttributeList 属性在上层表现为不可用。 */
#ifndef Py_USE_SYSTEM_PROCATTR
BOOL  WINAPI xp_InitializeProcThreadAttributeList(
    LPPROC_THREAD_ATTRIBUTE_LIST, DWORD, DWORD, SIZE_T *);
VOID  WINAPI xp_DeleteProcThreadAttributeList(LPPROC_THREAD_ATTRIBUTE_LIST);
BOOL  WINAPI xp_UpdateProcThreadAttribute(
    LPPROC_THREAD_ATTRIBUTE_LIST, DWORD, DWORD_PTR, PVOID, SIZE_T,
    PVOID, PVOID);
#define InitializeProcThreadAttributeList  xp_InitializeProcThreadAttributeList
#define DeleteProcThreadAttributeList      xp_DeleteProcThreadAttributeList
#define UpdateProcThreadAttribute          xp_UpdateProcThreadAttribute
#endif

/* ===== 7.1A 声明存在、XP kernel32 无导出的一组：统一 xp_ stub =====
 * （声明已经由 winbase.h/其他头提供，无需重复；实现见 PC/xp_compat.c。） */
#ifndef Py_USE_SYSTEM_FILEINFOAPI
BOOL WINAPI xp_SetFileInformationByHandle(HANDLE,
    FILE_INFO_BY_HANDLE_CLASS, LPVOID, DWORD);
#define SetFileInformationByHandle xp_SetFileInformationByHandle

DWORD WINAPI xp_GetFinalPathNameByHandleW(HANDLE, LPWSTR, DWORD, DWORD);
#define GetFinalPathNameByHandleW xp_GetFinalPathNameByHandleW

BOOLEAN WINAPI xp_CreateSymbolicLinkW(LPCWSTR, LPCWSTR, DWORD);
#define CreateSymbolicLinkW xp_CreateSymbolicLinkW

DWORD WINAPI xp_GetActiveProcessorCount(WORD);
#define GetActiveProcessorCount xp_GetActiveProcessorCount

DLL_DIRECTORY_COOKIE WINAPI xp_AddDllDirectory(PCWSTR);
#define AddDllDirectory xp_AddDllDirectory

BOOLEAN WINAPI xp_RemoveDllDirectory(DLL_DIRECTORY_COOKIE);
#define RemoveDllDirectory xp_RemoveDllDirectory

HANDLE WINAPI xp_CreateWaitableTimerExW(LPSECURITY_ATTRIBUTES, LPCWSTR,
    DWORD, DWORD);
#define CreateWaitableTimerExW xp_CreateWaitableTimerExW

BOOL WINAPI xp_SetWaitableTimerEx(HANDLE, const LARGE_INTEGER *, LONG,
    PTIMERAPCROUTINE, LPVOID, void *, ULONG);
#define SetWaitableTimerEx xp_SetWaitableTimerEx

VOID WINAPI xp_GetSystemTimePreciseAsFileTime(LPFILETIME);
#define GetSystemTimePreciseAsFileTime xp_GetSystemTimePreciseAsFileTime

VOID WINAPI xp_GetCurrentThreadStackLimits(PULONG_PTR, PULONG_PTR);
#define GetCurrentThreadStackLimits xp_GetCurrentThreadStackLimits

LSTATUS WINAPI xp_RegDeleteTreeW(HKEY, LPCWSTR);
#define RegDeleteTreeW xp_RegDeleteTreeW

/* RegDeleteKeyExW（Vista+ advapi32）：XP SP3 的 advapi32 没有该导出。
 * 语义 = RegDeleteKeyW + WOW64 视图选择；XP 是 32 位系统，无 64 位视图，
 * samDesired 的 KEY_WOW64_* 位无意义，直接转发 RegDeleteKeyW 等价。 */
LSTATUS WINAPI xp_RegDeleteKeyExW(HKEY, LPCWSTR, REGSAM, DWORD);
#define RegDeleteKeyExW xp_RegDeleteKeyExW

LONG WINAPI xp_BCryptGenRandom(void *, PUCHAR, ULONG, ULONG);
#define BCryptGenRandom xp_BCryptGenRandom

/* CompareStringOrdinal（Vista+）：大小写无关的"序数"比较（与 locale 无关）。
 * XP 用 LCMapStringW 折叠大小写后 wcsncmp（记录语义近似；对非拉丁字母
 * 与 ordinal 差异见 STAGE-C）。CSTR_* 常量取自 winnls/winnls.h。 */
int WINAPI xp_CompareStringOrdinal(LPCWSTR, int, LPCWSTR, int, BOOL);
#define CompareStringOrdinal xp_CompareStringOrdinal

/* GetTickCount64（Vista+）：XP 上以 GetTickCount 为主。64 位回绕合并不做
 * （系统重启即重置的窗口）—— BatchedWait 的 deadline 语义在毫秒级窗口
 * 下正确；记录于 STAGE-C。 */
ULONGLONG WINAPI xp_GetTickCount64(void);
#define GetTickCount64 xp_GetTickCount64

/* NeedCurrentDirectoryForExePathW（Vista+）：按官方文档的 XP 等价规则
 * （公开说明：名字含点或前导分隔符则不需要 CWD）。 */
BOOL WINAPI xp_NeedCurrentDirectoryForExePathW(LPCWSTR);
#define NeedCurrentDirectoryForExePathW xp_NeedCurrentDirectoryForExePathW
#endif

/* _winapi.c 的 LCMapStringEx flag 声明缺口（7.1A winnls.h 时代部分 flag
 * 缺失）。LCMAP_SORTHANDLE 在 XP 上未支持，仅常量补齐；调用点将其与
 * LCMAP_HASH/LCMAP_SORTKEY 一并列为"unsupported flags"错误分支。 */
#ifndef LCMAP_SORTHANDLE
#define LCMAP_SORTHANDLE                    0x20000000
#endif
#ifndef LCMAP_HASH
#define LCMAP_HASH                          0x00080000
#endif
#ifndef LCMAP_BYTEREV
#define LCMAP_BYTEREV                       0x00000800
#endif
#ifndef LCMAP_SORTKEY
#define LCMAP_SORTKEY                       0x00000400
#endif

/* -------- LOCALE_NAME_*（winnls.h 的 WINVER>=0x0600 段，XP 下不展开）-------- */
#ifndef LOCALE_NAME_INVARIANT
#define LOCALE_NAME_INVARIANT                 L""
#endif
#ifndef LOCALE_NAME_SYSTEM_DEFAULT
#define LOCALE_NAME_SYSTEM_DEFAULT            L""
#endif
#ifndef LOCALE_NAME_USER_DEFAULT
#define LOCALE_NAME_USER_DEFAULT              (NULL)
#endif
#ifndef LOCALE_NAME_MAX_LENGTH
#define LOCALE_NAME_MAX_LENGTH                85
#endif

/* -------- LCMAP_*（winnls.h 的 WINVER>=Win7 段 + 8.x 新增）--------
 * LCMAP_TITLECASE 在 7.1A 属 WINVER>=0x0601 段；LCMAP_SORTHANDLE/HASH 是
 * Win8 词汇（7.1A 头没有）。均只被 _winapi 当作"unsupported flags"或
 * 模块常量用，值与正式 SDK 一致。 */
#ifndef LCMAP_TITLECASE
#define LCMAP_TITLECASE                       0x00000300
#endif

/* LCMapStringEx（Vista+ NLS）：XP 只有 LCMapStringW。XP 模拟以
 * LCMapStringW 实现；XP 的 LCMapStringW 没有"locale name"参数，统一以
 * 系统默认 locale 处理——上游 pythoncore 的唯一调用（_winapi 的
 * sortenvironmentkey 与 _winapi.LCMapStringEx）传 LOCALE_NAME_INVARIANT
 * 或用户 locale 名，语义差异记录于 STAGE-C：排序结果可能随系统区域
 * 设置不同（XP 无 invariant sort handle）。实现于 PC/xp_compat.c。
 * （LOCALE_NAME_* 常量来自 7.1A winnls.h。LCMapStringEx 本体在 7.1A
 * 头集中没有声明路径，故在此补。） */
int WINAPI xp_LCMapStringEx(
    LPCWSTR lpLocaleName, DWORD dwMapFlags, LPCWSTR lpSrcStr, int cchSrc,
    LPWSTR lpDestStr, int cchDest, LPVOID lpVersionInformation,
    LPVOID lpReserved, DWORD dwSortFlags);
#define LCMapStringEx xp_LCMapStringEx

/* CopyFile2（Win8+）：XP kernel32 无导出。xp_CopyFile2 用 CopyFileW 模拟
 * 主体语义（flags 中 XP 支持的 COPY_FILE_* 减集；XP 没有的语义按失败返回）。
 * 实现在 PC/xp_compat.c。 */
#ifndef COPY_FILE_FAIL_IF_EXISTS
#define COPY_FILE_FAIL_IF_EXISTS            0x00000001
#endif
#ifndef COPY_FILE_RESTARTABLE
#define COPY_FILE_RESTARTABLE               0x00000002
#endif
#ifndef COPY_FILE_OPEN_SOURCE_FOR_WRITE
#define COPY_FILE_OPEN_SOURCE_FOR_WRITE     0x00000008
#endif
#ifndef COPY_FILE_ALLOW_DECRYPTED_DESTINATION
#define COPY_FILE_ALLOW_DECRYPTED_DESTINATION 0x00000010
#endif
#ifndef COPY_FILE_COPY_SYMLINK
#define COPY_FILE_COPY_SYMLINK              0x00000800
#endif
#ifndef COPY_FILE_NO_BUFFERING
#define COPY_FILE_NO_BUFFERING              0x00001000
#endif
#ifndef COPY_FILE_NO_OFFLOAD
#define COPY_FILE_NO_OFFLOAD                0x00040000
#endif
#ifndef COPY_FILE_REQUEST_SECURITY_PRIVILEGES
#define COPY_FILE_REQUEST_SECURITY_PRIVILEGES 0x00002000
#endif
#ifndef COPY_FILE_RESUME_FROM_PAUSE
#define COPY_FILE_RESUME_FROM_PAUSE         0x00004000
#endif

typedef struct _COPYFILE2_EXTENDED_PARAMETERS_XP {
    DWORD   dwSize;
    DWORD   dwCopyFlags;
    LPVOID  pfCopyProgressCallback;
    LPVOID  pvCallbackContext;
} COPYFILE2_EXTENDED_PARAMETERS;

/* _winapi 执行期把这些常量暴露到 Python（模块常数对），XP 下也必须
 * 编译憇；值取自正式 winbase.h/ilm（Win8 时代的定义）。 */
enum XP_COPYFILE2_MESSAGE_TYPE {
    COPYFILE2_CALLBACK_CHUNK_STARTED      = 1,
    COPYFILE2_CALLBACK_CHUNK_FINISHED     = 2,
    COPYFILE2_CALLBACK_STREAM_STARTED     = 3,
    COPYFILE2_CALLBACK_STREAM_FINISHED    = 4,
    COPYFILE2_CALLBACK_POLL_CONTINUE      = 5,
    COPYFILE2_CALLBACK_ERROR              = 6
};
#ifndef COPYFILE2_PROGRESS_CONTINUE
#define COPYFILE2_PROGRESS_CONTINUE           0
#endif
#ifndef COPYFILE2_PROGRESS_CANCEL
#define COPYFILE2_PROGRESS_CANCEL             1
#endif
#ifndef COPYFILE2_PROGRESS_STOP
#define COPYFILE2_PROGRESS_STOP               2
#endif
#ifndef COPYFILE2_PROGRESS_QUIET
#define COPYFILE2_PROGRESS_QUIET              3
#endif
#ifndef COPYFILE2_PROGRESS_PAUSE
#define COPYFILE2_PROGRESS_PAUSE              4
#endif
#ifndef COPY_FILE_DIRECTORY
#define COPY_FILE_DIRECTORY                   0x00000080
#endif
#ifndef COPY_FILE_REQUEST_COMPRESSED_TRAFFIC
#define COPY_FILE_REQUEST_COMPRESSED_TRAFFIC  0x00008000
#endif

HRESULT WINAPI xp_CopyFile2(
    LPCWSTR existing_file_name,
    LPCWSTR new_file_name,
    const COPYFILE2_EXTENDED_PARAMETERS *params);
#define CopyFile2 xp_CopyFile2

/* COPYFILE2 回调枚举/消息常量仅在头里被类型引用；_winapi.c 只在注释/未
 * 启用分支引用回调形态，本头不提供（编译面不触及）。 */

/* -------- pathcch.h 替身（核心三点：fileutils.c / getpath.c + pathconfig.c、
 * posixmodule.c） --------------------------------------------------------------
 * XP SP3 没有 PathCch*（Win8 API set）。这些使用点全部经由运行期动态探测或
 * 纯算法处理；XP 构建下我们把 pathcch.h 的词汇直接补在这里：
 *   - PATHCCH_ALLOW_LONG_PATHS 等常量
 *   - PPathCchSkipRoot/PathCchCombineEx 的签名声明
 * 实现由 PC/pathcch_compat.c 提供（随 pythoncore 编译链接，全部走 XP kernel32）。
 */
#ifndef PATHCCH_ALLOW_LONG_PATHS
#define PATHCCH_ALLOW_LONG_PATHS 0x00000001
#endif
#ifndef PATHCCH_NONE
#define PATHCCH_NONE 0x00000000
#endif
#ifndef PATHCCH_FORCE_ENABLE_LONG_PATH_PROCESS
#define PATHCCH_FORCE_ENABLE_LONG_PATH_PROCESS 0x00000002
#endif
#ifndef PATHCCH_FORCE_DISABLE_LONG_PATH_PROCESS
#define PATHCCH_FORCE_DISABLE_LONG_PATH_PROCESS 0x00000004
#endif

#ifndef Py_XP_PATHCCH_DECLARE
#define Py_XP_PATHCCH_DECLARE

HRESULT
xp_PathCchSkipRoot(const wchar_t *path, const wchar_t **rootEnd);

HRESULT
xp_PathCchCombineEx(wchar_t *buffer, size_t bufsize,
                    const wchar_t *dirname, const wchar_t *relfile,
                    unsigned long flags);

HRESULT
xp_PathCchCanonicalizeEx(wchar_t *buffer, size_t bufsize, const wchar_t *path);

/* 使用点写的是 PathCchSkipRoot/PathCchCombineEx 本名；XP 构建重定向到实现。
 * 若目标平台真提供 pathcch.h（非 XP 构建，比如官方 Win10 构建），则不进本段，
 * 不会破坏官方构建。 */
#ifndef Py_USE_SYSTEM_PATHCCH
#define PathCchSkipRoot(path, rootEnd) \
    xp_PathCchSkipRoot((path), (rootEnd))
#define PathCchCombineEx(buffer, bufsize, dirname, relfile, flags) \
    xp_PathCchCombineEx((buffer), (bufsize), (dirname), (relfile), (flags))
#define PathCchCanonicalizeEx(buffer, bufsize, path) \
    xp_PathCchCanonicalizeEx((buffer), (bufsize), (path))
#endif

/* -------- SYMBOLIC_LINK_FLAG_* (NT 6.0+/winbase) --------
 * posixmodule.c 的 symlink() 引用 CreateSymbolicLinkW 的 flag 常量；
 * XP 无该 API（上游调用失败时按错误抛 OSError），常量仅支撑编译。 */
#ifndef SYMBOLIC_LINK_FLAG_DIRECTORY
#define SYMBOLIC_LINK_FLAG_DIRECTORY                  0x1
#endif
#ifndef SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE
#define SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE  0x2
#endif

/* -------- AF_BTH (ws2def.h/ws2bth.h) --------
 * socketmodule.h 以 #ifdef AF_BTH 决定是否拉 <ws2bth.h> 并定义
 * SOCKADDR_BTH_REDEF。在钉 XP SDK 头集下，AF_BTH（蓝牙 RFCOMM/L2CAP，
 * 值 32，XP 实际支持）没有被 winsock2 的 include 链在所有 TU 中 timely
 * 定义，该块被跳过导致 bt_rc 成员类型未知。在 xp_compat.h（pyconfig.h 的
 * 引入点，先于 socketmodule.h）补定义，socketmodule.h 即可正常进入
 * ws2bth.h 分支。 */
#ifndef AF_BTH
#define AF_BTH 32
#endif

#endif /* Py_XP_PATHCCH_DECLARE */

/* GetErrorMode（Vista+）：XP 无该查询 API；错误模式只能由 SetErrorMode
 * 设置（XP 有）。返回 0（默认模式），调用方（msvcrt.GetErrorMode）把它
 * 当作查询结果。 */
UINT WINAPI xp_GetErrorMode(void);
#define GetErrorMode xp_GetErrorMode

/* CancelIoEx（Vista+）：XP 只有 CancelIo（取消句柄全部 IO）。
 * 语义差异：CancelIo 取消所有重叠操作而非仅指定的那个；对 Python
 * 的 _overlapped 取消场景可接受（并发取消精确性差异记录于 STAGE-C）。 */
BOOL WINAPI xp_CancelIoEx(HANDLE, LPOVERLAPPED);
#define CancelIoEx xp_CancelIoEx

/* SetThreadStackGuarantee（Vista+）：v141 静态 CRT 的线程初始化调用它；
 * XP 无此 API。CRT 容忍失败（失败即不保证栈增长），stub 返回 FALSE 让
 * CRT 继续。 */
BOOL WINAPI xp_SetThreadStackGuarantee(PULONG);
#define SetThreadStackGuarantee xp_SetThreadStackGuarantee


#endif /* MS_WINDOWS && Py_BUILD_CORE */

#endif /* Py_XP_COMPAT_H */

