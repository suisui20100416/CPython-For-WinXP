/* PC/pathcch_compat.h —— XP SP3 无 PathCch* API 时的替身实现声明。
 *
 * 上游使用点（fileutils.c / posixmodule.c / getpath.c / pathconfig.c /
 * venvlauncher.c）在新平台 include <pathcch.h>（Win8 API set）。
 * 本oproject提供同名函数的 XP 实现：
 *   - PathCchSkipRoot：手工扫描盘符/UNC/\? \ 前缀（与 fileutils.c 的
 *     _Py_skiproot 一致，直接转发到 _Py_skiproot 更安全，但它在本头之后
 *     声明；这里用自包含实现）；
 *   - PathCchCombineEx：Combine 完成量语义（长路径 flag 忽略）。
 *
 * 实现细节只依赖 XP kernel32，绝不生 Vista+ 导入。
 * 诺基亚量仅覆盖 Python 使用到的两个函数（全树 grep 确认）。
 */
#ifndef Py_PATHCCH_COMPAT_H
#define Py_PATHCCH_COMPAT_H

#if defined(Py_XP) && !defined(DO_NOT_USE_PATHCCH)

/* -------- pathcch.h 的必要常量与签名（Win8 起） -------- */
#ifndef PATHCCH_ALLOW_LONG_PATHS
#define PATHCCH_ALLOW_LONG_PATHS 0x00000001
#endif

#ifndef PATHCCH_MAX_CCH
#define PATHCCH_MAX_CCH 0x8000
#endif

#ifndef __cplusplus
#include <unknwn.h>
#endif

#define PATHCCH_HRESULT HRESULT

/* -------- XP 实现（供 XP 构建链接；发行时随 pythoncore 静态编入） ------- */
#ifdef __cplusplus
extern "C" {
#endif

/* 返回 pszPath 中 root 的结尾位置（含前缀与盘符），失败返回 E_NOT_SUFFICIENT_BUFFER 类似 */
HRESULT xp_PathCchSkipRoot(const wchar_t *path, const wchar_t **rootEnd);

/* Combine dirname + relfile；PATHCCH_ALLOW_LONG_PATHS 仅长于 MAX_PATH 时
 * 有语义，XP 无 \\?\ 规范化 helper，我们按短路径语义处理（与
 * pathconfig/_Py_skiproot 行为一致）。 */
HRESULT xp_PathCchCombineEx(wchar_t *buffer, size_t bufsize,
                            const wchar_t *dirname, const wchar_t *relfile,
                            unsigned long flags);

HRESULT xp_PathCchCanonicalizeEx(wchar_t *buffer, size_t bufsize,
                                 const wchar_t *path);

/* RemoveFileSpec：去掉最后一段（文件或尾目录）。S_OK 去掉了；S_FALSE
 * 已是根（无可去）。与 PathCchRemoveFileSpec 语义一致。 */
HRESULT xp_PathCchRemoveFileSpec(wchar_t *path, size_t cchBuffer);

#ifdef __cplusplus
}
#endif

/* -------- 宏重定向（XP 构建把调用点改名为 xp_* 实现） --------
 * 逐个 #ifndef 保护：core TU 里 xp_compat.h 先定义了同名宏，这里不重复；
 * launcher 之类的独立程序没有 xp_compat.h，从这里拿宏。 */
#ifndef Py_USE_SYSTEM_PATHCCH
#ifndef PathCchSkipRoot
#define PathCchSkipRoot(path, rootEnd) \
    xp_PathCchSkipRoot((path), (rootEnd))
#endif
#ifndef PathCchCombineEx
#define PathCchCombineEx(buffer, bufsize, dirname, relfile, flags) \
    xp_PathCchCombineEx((buffer), (bufsize), (dirname), (relfile), (flags))
#endif
#ifndef PathCchCanonicalizeEx
#define PathCchCanonicalizeEx(buffer, bufsize, path) \
    xp_PathCchCanonicalizeEx((buffer), (bufsize), (path))
#endif
#ifndef PathCchRemoveFileSpec
#define PathCchRemoveFileSpec(path, cchBuffer) \
    xp_PathCchRemoveFileSpec((path), (cchBuffer))
#endif
#endif /* !Py_USE_SYSTEM_PATHCCH */

#endif /* MS_WINDOWS && Py_XP && !DO_NOT_USE_PATHCCH */

#endif /* Py_PATHCCH_COMPAT_H */
