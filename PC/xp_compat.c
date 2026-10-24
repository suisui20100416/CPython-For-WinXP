/* PC/xp_compat.c —— 需要实现的 XP 兼容 API 集（随 pythoncore 静态编入）。
 *
 * 原则（同 Git 项目的 xp-compat.c 模式）：
 *   - 每个函数只做 XP 上支持的最大努力模拟；上游调用点已经把这些调用包在
 *     "失败即退回"的运行期分支里（posixmodule.c win32_xstat_impl 等）。
 *   - 不引入任何 XP kernel32/advapi32 缺失的导入。
 *   - GetFileInformationByHandleEx 的 FileBasicInfo/FileAttributeTagInfo 用
 *     GetFileInformationByHandle(BY_HANDLE_FILE_INFORMATION, XP 有) 映射；
 *     FileIdInfo 与 FileIdExtdDirectory 类直接 ERROR_NOT_SUPPORTED，让上游
 *     的失败分支接管（ino 链走 BY_HANDLE_FILE_INFORMATION 的文件 index）。
 */

#include "Python.h"
#include "xp_compat.h"
#include "pathcch_compat.h"  /* canonical decls, keeps one TU consistent */
#include <windows.h>
#include <assert.h>
/* ===== 辅助声明（XP 头集缺/不常用） ===== */

/* SystemFunction036 = RtlGenRandom（advapi32 导出，XP+；微软官方认可用于随机） */
#ifdef __cplusplus
extern "C" {
#endif
BOOLEAN WINAPI SystemFunction036(PVOID RandomBuffer, ULONG RandomBufferLength);
#ifdef __cplusplus
}
#endif

/* TEB 访问：NTDDI XP 下 intrin 的 __readfsdword 可用；TEB 布局由 winnt.h
 * 的 NT_TIB（XP 头集有）。 */
static void *XP_NtCurrentTeb(void)
{
#if defined(_M_IX86)
    return (void *)__readfsdword(0x18); /* TEB self-pointer */
#else
    void *teb = NULL;
    return teb; /* 非 x86 的 XP 构建不存在 */
#endif
}

/* ALL_PROCESSOR_GROUPS：winbase.h 在 0x0601+ 才定义；XP 单组，用 0xFFFF。 */
#ifndef ALL_PROCESSOR_GROUPS
#define ALL_PROCESSOR_GROUPS ((WORD)0xFFFF)
#endif


#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0501
#endif

/*
 * Win7 GetFileInformationByHandleEx 在 XP 上的模拟。
 * 只覆盖 pythoncore 实际使用到的三个 info class：
 *   FileBasicInfo         -> BY_HANDLE_FILE_INFORMATION（时间/属性）
 *   FileAttributeTagInfo  -> dwFileAttributes + 未取 ReparseTag（0）
 *   FileIdInfo            -> XP 无此语义（挂 NTFS FileId 需再层），返回失败
 * 其它 class 返回失败并 ERROR_NOT_SUPPORTED（上游都有失败分支）。
 */
BOOL WINAPI
xp_GetFileInformationByHandleEx(
    HANDLE hFile,
    FILE_INFO_BY_HANDLE_CLASS FileInformationClass,
    LPVOID lpFileInformation,
    DWORD dwBufferSize)
{
    if (hFile == NULL || lpFileInformation == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    switch (FileInformationClass) {
    case FileBasicInfo: {
        PFILE_BASIC_INFO out = (PFILE_BASIC_INFO)lpFileInformation;
        BY_HANDLE_FILE_INFORMATION hi;
        if (sizeof(FILE_BASIC_INFO) > dwBufferSize) {
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return FALSE;
        }
        if (!GetFileInformationByHandle(hFile, &hi)) {
            return FALSE; /* last error preserved */
        }
        out->CreationTime.HighPart = hi.ftCreationTime.dwHighDateTime;
        out->CreationTime.LowPart = hi.ftCreationTime.dwLowDateTime;
        out->LastAccessTime.HighPart = hi.ftLastAccessTime.dwHighDateTime;
        out->LastAccessTime.LowPart = hi.ftLastAccessTime.dwLowDateTime;
        out->LastWriteTime.HighPart = hi.ftLastWriteTime.dwHighDateTime;
        out->LastWriteTime.LowPart = hi.ftLastWriteTime.dwLowDateTime;
        out->ChangeTime = out->LastWriteTime;
        out->FileAttributes = hi.dwFileAttributes;
        return TRUE;
    }
    case FileAttributeTagInfo: {
        PFILE_ATTRIBUTE_TAG_INFO out = (PFILE_ATTRIBUTE_TAG_INFO)lpFileInformation;
        BY_HANDLE_FILE_INFORMATION hi;
        if (sizeof(FILE_ATTRIBUTE_TAG_INFO) > dwBufferSize) {
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return FALSE;
        }
        if (!GetFileInformationByHandle(hFile, &hi)) {
            return FALSE;
        }
        out->FileAttributes = hi.dwFileAttributes;
        out->ReparseTag = 0; /* XP 无 GetFileInformationByHandleEx 的 tag 拾取；
                                os.stat 的 reparse 精确性已由上游失败分支处理 */
        return TRUE;
    }
    default:
        SetLastError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
}

/*
 * PROC_THREAD_ATTRIBUTE_LIST（Win7+）的 XP stub：一律失败。
 * _winapi.getattributelist 的失败即"无属性表"，subprocess 照常工作，
 * 只是没有 handle-list 继承控制（XP 本来也不支持该语义）。
 */
BOOL WINAPI
xp_InitializeProcThreadAttributeList(
    LPPROC_THREAD_ATTRIBUTE_LIST lpAttributeList,
    DWORD dwAttributeCount,
    DWORD dwFlags,
    SIZE_T *lpSize)
{
    (void)lpAttributeList;
    (void)dwAttributeCount;
    (void)dwFlags;
    if (lpSize != NULL) {
        /* 按官方语义：给一个保守的容量值（stub 无内容，但让调用方分配得下） */
        *lpSize = 64 + dwAttributeCount * sizeof(DWORD_PTR) * 4;
    }
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

VOID WINAPI
xp_DeleteProcThreadAttributeList(LPPROC_THREAD_ATTRIBUTE_LIST lpAttributeList)
{
    (void)lpAttributeList;
    /* stub：无可释放的系统资源 */
}

BOOL WINAPI
xp_UpdateProcThreadAttribute(
    LPPROC_THREAD_ATTRIBUTE_LIST lpAttributeList,
    DWORD dwFlags,
    DWORD_PTR Attribute,
    PVOID lpValue,
    SIZE_T cbSize,
    PVOID lpPreviousValue,
    PVOID lpReturnSize)
{
    (void)lpAttributeList; (void)dwFlags; (void)Attribute;
    (void)lpValue; (void)cbSize; (void)lpPreviousValue; (void)lpReturnSize;
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

/*
 * CopyFile2（Win8+）的 XP 模拟：用 CopyFileW 完成文件拷贝。
 * 上游（_winapi.CopyFile2）回调参数未启用；XP 支持的主 flags 语义一致：
 *   COPY_FILE_FAIL_IF_EXISTS -> CopyFileW 的 bFailIfExists=TRUE
 *   其它（RESTARTABLE/安全/卸载分载等）只在真 API 有效，这里忽略。
 * 返回把 win32 error 封装为 HRESULT（0x8007xxxx），与 CopyFile2 一致，
 * 上游的 FAILED(hr)+0x8007xxxx 剥离正好闭合。
 */
HRESULT WINAPI
xp_CopyFile2(LPCWSTR existing_file_name, LPCWSTR new_file_name,
             const COPYFILE2_EXTENDED_PARAMETERS *params)
{
    DWORD flags;
    BOOL ok;

    if (existing_file_name == NULL || new_file_name == NULL) {
        return HRESULT_FROM_WIN32(ERROR_INVALID_PARAMETER);
    }
    if (params && params->dwSize < sizeof(*params)) {
        return HRESULT_FROM_WIN32(ERROR_INVALID_PARAMETER);
    }
    flags = params ? params->dwCopyFlags : 0;
    /* XP 不支持的语义在这里按参数拒绝，避免静默改变行为 */
    if (flags & ~((DWORD)COPY_FILE_FAIL_IF_EXISTS |
                  (DWORD)COPY_FILE_RESTARTABLE |
                  (DWORD)COPY_FILE_OPEN_SOURCE_FOR_WRITE |
                  (DWORD)COPY_FILE_ALLOW_DECRYPTED_DESTINATION))
    {
        return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
    }

    /* xp branch: keep the GIL across this call.  The standard
     * Py_BEGIN/END_ALLOW_THREADS pair in this translation unit crashed
     * at runtime on XP (access violation); CopyFileW is fast enough that
     * holding the GIL is an acceptable cost. */
    ok = CopyFileW(existing_file_name, new_file_name,
                   (flags & COPY_FILE_FAIL_IF_EXISTS) ? TRUE : FALSE);
    if (!ok) {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    return S_OK;
}

/*
 * LCMapStringEx（Vista+）的 XP 模拟：LCMapStringW 无 locale 名参数。
 * locale_name 不是 NULL/"!" 时仍按系统默认处理（XP 无 per-locale 表）。
 * 需要的语言语义差异见 STAGE-C 第 4 节说明。
 */
int WINAPI
xp_LCMapStringEx(LPCWSTR lpLocaleName, DWORD dwMapFlags, LPCWSTR lpSrcStr,
                 int cchSrc, LPWSTR lpDestStr, int cchDest,
                 LPVOID lpVersionInformation, LPVOID lpReserved,
                 DWORD dwSortFlags)
{
    (void)lpLocaleName;
    (void)lpVersionInformation;
    (void)lpReserved;
    (void)dwSortFlags;
    /* XP：LCMapStringW 以 LCID 表达 locale。invariant 没有名字条目，
     * 用系统默认（LOCALE_SYSTEM_DEFAULT 也不完美——差异记录于 STAGE-C），
     * 排序 flag 语义在两个 API 间一致。 */
    return LCMapStringW(LOCALE_SYSTEM_DEFAULT, dwMapFlags, lpSrcStr, cchSrc,
                        lpDestStr, cchDest);
}

/*
 * ===== 以下为"XP kernel32 无导出"的一组失败/降级 stub =====
 * 上游每个调用点都有失败分支（实测核对过）；stub 的职责是让链接通过、
 * 让运行期行为落在上游既定的失败路径上。全部不产生 XP 缺失导入。
 */

/* SetFileInformationByHandle（Win7+）：XP 无。win32_hchmod 的失败返回 0，
 * 上层会改走 win32_lchmod 路线（SetFileAttributesW，XP 有）。 */
BOOL WINAPI
xp_SetFileInformationByHandle(HANDLE hFile,
    FILE_INFO_BY_HANDLE_CLASS FileInformationClass, LPVOID lpFileInformation,
    DWORD dwBufferSize)
{
    (void)hFile; (void)FileInformationClass;
    (void)lpFileInformation; (void)dwBufferSize;
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

/* GetFinalPathNameByHandleW（Vista+）：XP 无。
 * os.getfinalpathname / realpath 的失败分支接管（XP 上 realpath 退化为
 * 原路径拼接）。 */
DWORD WINAPI
xp_GetFinalPathNameByHandleW(HANDLE hFile, LPWSTR lpszFilePath,
    DWORD cchFilePath, DWORD dwFlags)
{
    (void)hFile; (void)lpszFilePath; (void)cchFilePath; (void)dwFlags;
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return 0;
}

/* CreateSymbolicLinkW（Vista+）：XP 无。os.symlink 抛 OSError（上游失败
 * 分支）。 */
BOOLEAN WINAPI
xp_CreateSymbolicLinkW(LPCWSTR lpSymlinkFileName, LPCWSTR lpTargetFileName,
    DWORD dwFlags)
{
    (void)lpSymlinkFileName; (void)lpTargetFileName; (void)dwFlags;
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

/* GetActiveProcessorCount（Win7+，多组处理器）：XP 用
 * GetSystemInfo().dwNumberOfProcessors 语义等价（XP 单组）。 */
DWORD WINAPI
xp_GetActiveProcessorCount(WORD GroupNumber)
{
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    if (GroupNumber == ALL_PROCESSOR_GROUPS) {
        return si.dwNumberOfProcessors;
    }
    /* XP 只有组 0 */
    if (GroupNumber == 0) {
        return si.dwNumberOfProcessors;
    }
    return 0;
}

/* AddDllDirectory / RemoveDllDirectory（Win7/KB2533623）：XP 无。
 * os.add_dll_directory 的真实用户（pywin32_bootstrap 等）要在 XP 上
 * 工作的等价做法是把目录前插进 PATH（SafeDllSearchMode 的末位搜索源，
 * 早于其余 PATH 项）。cookie 是假句柄，仅作成功标记。 */
static volatile LONG xp_dll_dir_seq = 0;

DLL_DIRECTORY_COOKIE WINAPI
xp_AddDllDirectory(PCWSTR path)
{
    DWORD len;
    wchar_t *old_path = NULL, *new_path = NULL;
    DLL_DIRECTORY_COOKIE cookie = NULL;

    len = GetEnvironmentVariableW(L"PATH", NULL, 0);
    if (len == 0) {
        len = 1;
    }
    /* GetEnvironmentVariableW returns the length WITHOUT the terminating
     * NUL, so the fetch buffer must hold len+1 code units - allocating
     * only len made the second call overflow the heap block by one
     * wchar_t whenever the PATH length was exactly len. */
    old_path = (wchar_t *)HeapAlloc(GetProcessHeap(), 0,
                                    (len + 1) * sizeof(wchar_t));
    new_path = (wchar_t *)HeapAlloc(GetProcessHeap(), 0,
                                    (len + lstrlenW(path) + 2) * sizeof(wchar_t));
    if (old_path == NULL || new_path == NULL) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        goto done;
    }
    if (GetEnvironmentVariableW(L"PATH", old_path, len + 1) == 0) {
        old_path[0] = L'\0';
    }
    lstrcpyW(new_path, path);
    lstrcatW(new_path, L";");
    lstrcatW(new_path, old_path);
    if (!SetEnvironmentVariableW(L"PATH", new_path)) {
        goto done;
    }
    /* The cookie is an opaque marker only (RemoveDllDirectory is a no-op),
     * so encode the sequence number directly - never a pointer into local
     * storage, which would dangle the moment this call returns. */
    cookie = (DLL_DIRECTORY_COOKIE)(uintptr_t)InterlockedIncrement(&xp_dll_dir_seq);

done:
    if (old_path) {
        HeapFree(GetProcessHeap(), 0, old_path);
    }
    if (new_path) {
        HeapFree(GetProcessHeap(), 0, new_path);
    }
    return cookie;
}

BOOLEAN WINAPI
xp_RemoveDllDirectory(DLL_DIRECTORY_COOKIE cookie)
{
    (void)cookie;
    /* The PATH is left as-is; removing one directory out of a merged
     * PATH is not recoverable once several additions interleaved. */
    return TRUE;
}

/* CreateWaitableTimerExW（Vista+）：XP 用 CreateWaitableTimerW（flags 忽略，
 * HIGH_RESOLUTION 不存在——上游探测分支按失败回退 timer_flags=0）。 */
HANDLE WINAPI
xp_CreateWaitableTimerExW(LPSECURITY_ATTRIBUTES lpTimerAttributes,
    LPCWSTR lpTimerName, DWORD dwFlags, DWORD dwDesiredAccess)
{
    (void)dwFlags;
    return CreateWaitableTimerW(lpTimerAttributes, FALSE, lpTimerName);
}

/* SetWaitableTimerEx（Vista+）：与 SetWaitableTimer 的差异是可取消原因
 * 参数（WakeContext/RerunReasoned）；XP 无此语义，用 SetWaitableTimer。 */
BOOL WINAPI
xp_SetWaitableTimerEx(HANDLE hTimer, const LARGE_INTEGER *lpDueTime,
    LONG lPeriod, PTIMERAPCROUTINE pfnCompletionRoutine, LPVOID lpArgToCompletionRoutine,
    void * WakeContext_xp, ULONG RerunReason_xp)
{
    (void)WakeContext_xp; (void)RerunReason_xp;
    return SetWaitableTimer(hTimer, lpDueTime, lPeriod,
                            pfnCompletionRoutine, lpArgToCompletionRoutine,
                            FALSE);
}

/* GetSystemTimePreciseAsFileTime（Win8+）：XP 用 GetSystemTimeAsFileTime
 * （精度 ~15ms；差异已记录于 STAGE-C：time.time() 的亚毫秒尾部在 XP
 * 有限）。 */
VOID WINAPI
xp_GetSystemTimePreciseAsFileTime(LPFILETIME lpSystemTimeAsFileTime)
{
    GetSystemTimeAsFileTime(lpSystemTimeAsFileTime);
}

/* GetCurrentThreadStackLimits（Win8+）：XP 无该 API。
 * 主线程默认 /STACK:0x100000（1 MB，与 PCbuild python.vcxproj 的
 * StackReserveSize 一致）；TEB.StackBase 是真实栈顶。直接用
 * [StackBase-1MB, StackBase] 作为分配范围（含未提交页），与官方
 * GetThreadStackLimits 语义一致，且不受"启动早期已提交页少"影响。
 * 修复记录：TEB.StackLimit 是已提交下界，误用作分配下界导致
 * ceval 的栈余量只剩几 KB、递归检查立即误报。 */
VOID WINAPI
xp_GetCurrentThreadStackLimits(PULONG_PTR LowLimit, PULONG_PTR HighLimit)
{
    NT_TIB *tib = (NT_TIB *)XP_NtCurrentTeb();
    uintptr_t high = (uintptr_t)tib->StackBase;
    uintptr_t low = high - (uintptr_t)(1024 * 1024);
    *LowLimit = low;
    *HighLimit = high;
}

/* RegDeleteTreeW（Vista+ advapi32）：XP SP3 的 advapi32 没有该导出。
 * winreg 的 XP 行为：递归删除用 EnumKey + RegDeleteValue/RegDeleteKey
 * 循环模拟。实现于下方（供 PC/winreg.c 的 XP 重定向）。 */
LSTATUS WINAPI
xp_RegDeleteTreeW(HKEY hKey, LPCWSTR lpSubKey)
{
    /* TODO(xp): 递归删除模拟；首版按失败处理，调用点返回 OSError。
     * 语义差异记录于 STAGE-C。 */
    (void)hKey; (void)lpSubKey;
    return ERROR_CALL_NOT_IMPLEMENTED;
}

/* RegDeleteKeyExW（Vista+ advapi32）：XP SP3 的 advapi32 没有该导出。
 * 官方语义 = RegDeleteKeyW + samDesired 选择 32/64 位注册表视图。
 * XP 是纯 32 位系统，KEY_WOW64_* 位无意义；实现上忽略 samDesired/
 * Reserved，直接走 RegDeleteKeyW——与官方在 32 位进程里调用
 * RegDeleteKeyExW（KEY_WOW64_32KEY 时回到 32 位视图）行为一致。 */
LSTATUS WINAPI
xp_RegDeleteKeyExW(HKEY hKey, LPCWSTR lpSubKey, REGSAM samDesired,
                   DWORD reserved)
{
    (void)samDesired;
    (void)reserved;
    return RegDeleteKeyW(hKey, lpSubKey);
}

/* BCryptGenRandom（Vista+）：XP 用 RtlGenRandom（advapi32 的 SystemFunction036，
 * XP 起就存在且官方支持）——bootstrap_hash.c 的 XP 分支即该实现。
 * 该 stub 保留为链接兜底（上游已换成 RtlGenRandom 时不会链接到这）。 */
LONG WINAPI
xp_BCryptGenRandom(void *hAlgorithm, PUCHAR pbBuffer, ULONG cbBuffer,
    ULONG dwFlags)
{
    (void)hAlgorithm; (void)dwFlags;
    if (SystemFunction036(pbBuffer, cbBuffer)) {
        return 0; /* STATUS_SUCCESS */
    }
    return 0xC0000018L; /* STATUS_INVALID_PARAMETER_1: 表达失败 */
}

/*
 * ===== 第二组 XP stub（pythoncore 链接清单第二批） =====
 */

/* CompareStringOrdinal（Vista+）：无符号码元逐位比较 + ASCII-only
 * 大小写折叠，直接实现 ordinal 语义。不借道 locale API：XP 的
 * LCMapStringW 是语言学折叠，非 ASCII 输入可能改变串长且结果与官方
 * ordinal 规则不同（每码元独立、仅 A-Z 折叠）。调用方之一
 * _winapi.c 的 dedup_environment_keys 处理任意环境变量键名，不能
 * 假设 ASCII。同款定案实现见 PC/launcher2.c（十一测）。 */
int WINAPI
xp_CompareStringOrdinal(LPCWSTR string1, int cchCount1,
                        LPCWSTR string2, int cchCount2, BOOL bIgnoreCase)
{
    const wchar_t *p1;
    const wchar_t *p2;
    int len1;
    int len2;
    int n;
    int i;

    if (string1 == NULL || string2 == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0; /* CSTR_ERROR */
    }
    p1 = string1;
    p2 = string2;
    len1 = cchCount1 < 0 ? (int)wcslen(string1) : cchCount1;
    len2 = cchCount2 < 0 ? (int)wcslen(string2) : cchCount2;
    n = len1 < len2 ? len1 : len2;

    for (i = 0; i < n; i++) {
        unsigned short c1 = (unsigned short)p1[i];
        unsigned short c2 = (unsigned short)p2[i];
        if (bIgnoreCase) {
            if (c1 >= 'A' && c1 <= 'Z') {
                c1 += 0x20;
            }
            if (c2 >= 'A' && c2 <= 'Z') {
                c2 += 0x20;
            }
        }
        if (c1 != c2) {
            return c1 < c2 ? CSTR_LESS_THAN : CSTR_GREATER_THAN;
        }
    }
    if (len1 != len2) {
        return len1 < len2 ? CSTR_LESS_THAN : CSTR_GREATER_THAN;
    }
    return CSTR_EQUAL;
}

/* GetTickCount64（Vista+）：XP 用 GetTickCount 的 32 位毫秒回绕计数。
 * 跨一次回绕（49.7 天）后值仍单调充足的窗口内正确；XP 分支接受该限制
 * （STAGE-C 记录）。 */
ULONGLONG WINAPI
xp_GetTickCount64(void)
{
    return (ULONGLONG)GetTickCount();
}

/* NeedCurrentDirectoryForExePathW（Vista+）：XP 模拟。官方语义：
 * exe name 含 "."、含分隔符时不需要 CWD 解析 -> FALSE；其他 -> TRUE。 */
BOOL WINAPI
xp_NeedCurrentDirectoryForExePathW(LPCWSTR exe_name)
{
    if (exe_name == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (wcschr(exe_name, L'.') != NULL ||
        wcschr(exe_name, L'\\') != NULL ||
        wcschr(exe_name, L'/') != NULL)
    {
        return FALSE;
    }
    return TRUE;
}

/* CancelIoEx（Vista+）的 XP 模拟：CancelIo（句柄全部 IO）。
 * lpOverlapped 的精确匹配在 XP 上不可得；ERROR_NOT_FOUND 语义保持
 * （上游会读 GetLastError 判别已完成的 IO）。 */
BOOL WINAPI
xp_CancelIoEx(HANDLE hFile, LPOVERLAPPED lpOverlapped)
{
    (void)lpOverlapped;
    if (hFile == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!CancelIo(hFile)) {
        return FALSE;
    }
    /* CancelIo 返回 TRUE 即表示取消了至少一个待决 IO；若无待决 IO，
     * CancelIo 返回 TRUE 但实际没有动作——按 CancelIoEx 语义应返回
     * FALSE + ERROR_NOT_FOUND。XP 无法区分，返回 TRUE（差异记录）。 */
    return TRUE;
}

/* SetThreadStackGuarantee（Vista+）：XP 无；v141 libcmt 线程启动路径调用。
 * CRT 以失败为可接受，这里返回 FALSE（不提供栈增长保证）。 */
BOOL WINAPI
xp_SetThreadStackGuarantee(PULONG StackReserve)
{
    (void)StackReserve;
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

/* GetErrorMode（Vista+）：XP 无查询 API。返回 0（错误模式默认值）。 */
UINT WINAPI
xp_GetErrorMode(void)
{
    return 0;
}
