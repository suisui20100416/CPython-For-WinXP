/* PC/pathcch_compat.c —— XP SP3 上 PathCch* 的最小实现。
 *
 * 只支撑 Python 3.15 源码真正调用到的三个函数（全树 grep 实证）：
 *   PathCchSkipRoot   （fileutils.c _Py_isabs/_Py_splitroot、posixmodule.c:5714）
 *   PathCchCombineEx  （fileutils.c join_relfile、PC/venvlauncher.c）
 *   PathCchCanonicalizeEx（venvlauncher潜在的调用；本包未调用即可不链接）
 *
 * 全部用纯算法实现，只依赖 XP kernel32，绝不产生 Vista+ 导入。
 * 返回值语义按 pathcch.h：S_OK/E_INVALIDARG/E_NOT_SUFFICIENT_BUFFER。
 */
#define PY_SSIZE_T_CLEAN
#include <windows.h>
#include "pathcch_compat.h"
#include <wchar.h>
#include <assert.h>

/* ---------- 内部工具 ---------- */

static int is_sep(wchar_t c)
{
    return c == L'\\' || c == L'/';
}

static size_t path_len(const wchar_t *s)
{
    return wcslen(s);
}

/* Win8 PathCchSkipRoot 语义：root 结束位置的指针。
 *   "\\server\share" -> 指向 share 之后第一个字符
 *   "\\?\" "\\.\" "\??\" 前缀处理与其后的驱动/UNC
 *   "C:\" 或 "C:"    -> 指向其后的第一个字符（"C:" 也算 root）
 *   "\path"          -> 指向第一段后的第一个字符
 *   相对路径（无 root）-> E_INVALIDARG，rootEnd 不动
 */
HRESULT
xp_PathCchSkipRoot(const wchar_t *path, const wchar_t **rootEnd)
{
    if (path == NULL) {
        return E_INVALIDARG;
    }
    const wchar_t *p = path;

    /* 设备/命名空间前缀： \\?\ \\.\ \??\ \\?\UNC\ */
    if (is_sep(p[0])) {
        const wchar_t *q = p + 1;
        if (is_sep(q[0])) {
            /* \\ 前缀：UNC 或设备 */
            q++;
            if ((q[0] == L'?' || q[0] == L'.') && is_sep(q[1])) {
                /* \\?\ 或 \\. */
                q += 2;
                if (is_sep(q[0])) {
                    q++;
                    /* \\?\UNC\server\share */
                    if ((q[0] == L'U' || q[0] == L'u') &&
                        (q[1] == L'N' || q[1] == L'n') &&
                        (q[2] == L'C' || q[2] == L'c') &&
                        is_sep(q[3])) {
                        q += 4; /* 已经跳过 "UNC\"; 其后是一段 server */
                        /* 跳过 server 段与其分隔符 */
                        while (!is_sep(*q) && *q) {
                            q++;
                        }
                        if (is_sep(*q)) {
                            q++;
                        }
                        *rootEnd = q;
                        return S_OK;
                    }
                    /* \\?\C:\ 形式 */
                    if ((q[0] && q[1] == L':') && is_sep(q[2])) {
                        *rootEnd = q + 3;
                        return S_OK;
                    }
                    /* 设备路径其余形式 \\?\Device\...：视为 root=前缀+一段 */
                    while (!is_sep(*q) && *q) {
                        q++;
                    }
                    if (is_sep(*q)) {
                        q++;
                    }
                    *rootEnd = q;
                    return S_OK;
                }
                *rootEnd = q;
                return S_OK;
            }
            /* 标准 UNC：\\server\share\ */
            while (!is_sep(*q) && *q) {           /* server */
                q++;
            }
            if (is_sep(*q)) {
                q++;
            }
            while (!is_sep(*q) && *q) {           /* share */
                q++;
            }
            if (is_sep(*q)) {
                q++;
            }
            *rootEnd = q;
            return S_OK;
        }
        /* 单独 \path：root 是第一个分隔符后 */
        q++;
        *rootEnd = q;
        return S_OK;
    }

    /* 盘符：C: 或 C:\ */
    if ((p[0]) && p[1] == L':') {
        if (is_sep(p[2])) {
            /* C:\ 后 */
            *rootEnd = p + 3;
        } else {
            /* C: 相对（"C:foo"），Win8 语义 root 结束在冒号后 */
            *rootEnd = p + 2;
        }
        return S_OK;
    }

    /* 相对路径无 root */
    return E_INVALIDARG;
}

/* Combine：dirname + 分隔符(按需) + relfile。
 * bufsize 开销含结尾 0。超长返回 E_NOT_SUFFICIENT_BUFFER，buffer 置空串。
 * 与 Win8 的不同：本实现不做 \\?\ 规范化、不处理超过 INT_MAX 的长度。
 */
HRESULT
xp_PathCchCombineEx(wchar_t *buffer, size_t bufsize,
                    const wchar_t *dirname, const wchar_t *relfile,
                    unsigned long flags)
{
    size_t dlen, rlen;
    int need_sep = 0;

    (void)flags; /* XP 无长路径形态；忽略 */

    if (buffer == NULL || bufsize == 0) {
        return E_INVALIDARG;
    }

    dlen = dirname ? path_len(dirname) : 0;
    rlen = relfile ? path_len(relfile) : 0;

    if (relfile == NULL || relfile[0] == 0) {
        /* Win8：relfile 空时结果 = dirname */
        if (dlen + 1 > bufsize) {
            return E_NOT_SUFFICIENT_BUFFER;
        }
        if (dirname == NULL) {
            buffer[0] = 0;
            return S_OK;
        }
        wmemcpy(buffer, dirname, dlen);
        buffer[dlen] = 0;
        return S_OK;
    }

    if (dirname == NULL || dlen == 0) {
        if (rlen + 1 > bufsize) {
            return E_NOT_SUFFICIENT_BUFFER;
        }
        wmemcpy(buffer, relfile, rlen);
        buffer[rlen] = 0;
        return S_OK;
    }

    need_sep = !is_sep(dirname[dlen - 1]) && relfile[0] != 0;

    if (dlen + (size_t)need_sep + rlen + 1 > bufsize) {
        buffer[0] = 0;
        return E_NOT_SUFFICIENT_BUFFER;
    }

    wmemcpy(buffer, dirname, dlen);
    if (need_sep) {
        buffer[dlen] = L'\\';
    }
    wmemcpy(buffer + dlen + need_sep, relfile, rlen);
    buffer[dlen + need_sep + rlen] = 0;
    return S_OK;
}

/* Canonicalize：当前无调用方（全树 grep 实证；头文件声明保留备查）。
 * 注意：本实现借道 GetFullPathNameW，它规范分隔符与 "."，但**不折叠
 * ".."** —— 与 Win8 PathCchCanonicalizeEx 的核心语义不同。若未来有人
 * 调用，必须先补 ".." 折叠，不能按注释想当然。 */
HRESULT
xp_PathCchCanonicalizeEx(wchar_t *buffer, size_t bufsize, const wchar_t *path)
{
    if (buffer == NULL || bufsize == 0 || path == NULL) {
        return E_INVALIDARG;
    }

    /* 直接用 GetFullPathNameW（XP 有）：分隔符/相对段规范化 */
    wchar_t *filepart = NULL;
    /* 先探测长度 */
    size_t n = GetFullPathNameW(path, (DWORD)bufsize, buffer, &filepart);
    if (n == 0) {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    if (n > bufsize) {
        return E_NOT_SUFFICIENT_BUFFER;
    }
    return S_OK;
}

/* RemoveFileSpec：去掉末尾"文件或尾目录"段，保留根。
 * 语义按 Win8 pathcch.h：成功去掉返回 S_OK；路径已是根（无成分可去）
 * 返回 S_FALSE 且不改动；buffer/cch 非法返回 E_INVALIDARG。 */
HRESULT
xp_PathCchRemoveFileSpec(wchar_t *path, size_t cchBuffer)
{
    size_t len;

    if (path == NULL || cchBuffer == 0) {
        return E_INVALIDARG;
    }
    len = path_len(path);
    if (len == 0) {
        return E_INVALIDARG;
    }

    /* 去掉末尾分隔符（但保留 "C:\" 这类根尾部的分隔符） */
    while (len > 0 && is_sep(path[len - 1])) {
        if (len >= 2 && path[len - 2] == L':') {
            /* "C:\" 根：nothing to remove */
            return S_FALSE;
        }
        len--;
    }
    if (len == 0) {
        /* 全是分隔符（"\\" 或 "\"）：root 本身 */
        return S_FALSE;
    }

    /* 现在 len 指向最后一个非分隔字符之后；向左找分隔符 */
    {
        size_t i = len;
        while (i > 0 && !is_sep(path[i - 1])) {
            i--;
        }
        if (i == 0) {
            /* 无分隔符：单段（如 "python.exe" 或 "C:" 盘符）。
             * "C:" 是 root；"python.exe" 去掉后是空串，按 shlwapi
             * PathRemoveFileSpec 语义返回 S_FALSE（无父目录）。 */
            if (len >= 2 && path[1] == L':') {
                /* "C:foo" -> "C:" 可去；"C:" 本身 root 不可去 */
                if (len == 2) {
                    return S_FALSE;
                }
                path[2] = L'\0';
                return S_OK;
            }
            /* 单段非盘符：父目录为空，保持不动返回 S_FALSE */
            return S_FALSE;
        }
        /* 去掉尾段；与 Win8 一致返回不带结尾分隔符的父目录，
         * "C:\" 这类根形态（分隔符前是冒号）保留分隔符。 */
        if (path[i - 1] == L':' && i == 1) {
            /* "C:\foo" -> "C:\"（i 指向 '\'，path[0]='C' path[1]=':'） */
            path[i] = L'\0';
            return S_OK;
        }
        path[i] = L'\0';
        if (i > 1 && is_sep(path[i - 1]) && path[i - 2] != L':') {
            path[i - 1] = L'\0';
        }
        return S_OK;
    }
}
