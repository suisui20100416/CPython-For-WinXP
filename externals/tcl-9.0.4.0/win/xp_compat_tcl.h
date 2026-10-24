/*
 * xp_compat_tcl.h -- XP (NT 5.1) shims for Tcl 9.0.4 Windows sources.
 *
 * Tcl 9 requires Windows 7+ (tclWinPort.h sets WINVER/_WIN32_WINNT 0x0601)
 * and uses several Vista+ APIs directly.  For the CPython-on-XP port we
 * compile with _WIN32_WINNT=0x0501 and remap those calls here:
 *
 *  - SRWLock / CONDITION_VARIABLE (tclWinConsole.c): implemented on top of
 *    CRITICAL_SECTION + auto-reset events with a waiter count.  Shared and
 *    exclusive lock modes both map to exclusive CRITICAL_SECTION (the
 *    console driver never needs reader concurrency on XP).
 *  - CancelSynchronousIo (Vista+): no XP equivalent; safe no-op.  Callers
 *    tolerate failure (they just proceed to wait on the thread handle).
 *  - CreateSymbolicLinkW (Vista+): no XP equivalent; fails cleanly with
 *    ERROR_CALL_NOT_IMPLEMENTED, which callers convert to a Tcl error.
 *
 * This header is internal to the tcl build tree (externals), not part of
 * the CPython sources.
 */

#ifndef XP_COMPAT_TCL_H
#define XP_COMPAT_TCL_H

#ifndef _WIN32_WINNT
#error "xp_compat_tcl.h requires _WIN32_WINNT=0x0501 (XP target)"
#endif
#if _WIN32_WINNT >= 0x0600
#error "xp_compat_tcl.h is only for the XP (NT 5.1) build"
#endif

#include <windows.h>
/* ------------------------------------------------------------------ */
/* SRWLOCK emulation (Vista+): both modes map to a CRITICAL_SECTION.   */
/* The 7.1A SDK headers typedef SRWLOCK/CONDITION_VARIABLE to their    */
/* RTL_* types (Vista-era SDK, unguarded), so we must shadow the type  */
/* names with macros and route every API through a wrapper holding a   */
/* real CRITICAL_SECTION.                                              */
/* ------------------------------------------------------------------ */
#define SRWLOCK XPSRW
typedef struct { CRITICAL_SECTION cs; } XPSRW, *XPSRWLOCK;

#define InitializeSRWLock(lock)   InitializeCriticalSection(&(lock)->cs)
#define AcquireSRWLockShared(lock)      EnterCriticalSection(&(lock)->cs)
#define AcquireSRWLockExclusive(lock)   EnterCriticalSection(&(lock)->cs)
#define ReleaseSRWLockShared(lock)      LeaveCriticalSection(&(lock)->cs)
#define ReleaseSRWLockExclusive(lock)   LeaveCriticalSection(&(lock)->cs)


/* ------------------------------------------------------------------ */
/* CONDITION_VARIABLE emulation (Vista+): shadow the SDK type and     */
/* implement the classic XP pattern (auto-reset event + waiter count).*/
/* ------------------------------------------------------------------ */
#define CONDITION_VARIABLE XPCV
typedef struct {
    LONG waiters;
    LONG releaseCount;
    HANDLE event;      /* auto-reset event for the release broadcast */
    CRITICAL_SECTION cs;
} XPCV, *PXPCONDITION_VARIABLE;

#define INITIALIZE_CONDITION_VARIABLE(cond) \
    do { \
        memset((cond), 0, sizeof(*(cond))); \
        InitializeCriticalSection(&(cond)->cs); \
        (cond)->event = CreateEventW(NULL, FALSE, FALSE, NULL); \
    } while (0)
#define InitializeConditionVariable(cond) INITIALIZE_CONDITION_VARIABLE(cond)

static __inline void
xpWakeConditionVariable(XPCV *cv)
{
    EnterCriticalSection(&cv->cs);
    if (cv->waiters > cv->releaseCount) {
        cv->releaseCount++;
        SetEvent(cv->event);
    }
    LeaveCriticalSection(&cv->cs);
}

static __inline void
xpWakeAllConditionVariable(XPCV *cv)
{
    EnterCriticalSection(&cv->cs);
    cv->releaseCount = cv->waiters;
    SetEvent(cv->event);
    LeaveCriticalSection(&cv->cs);
}

static __inline BOOL
xpSleepConditionVariableSRW(XPCV *cv, XPSRW *srw,
                            DWORD ms, DWORD flags)
{
    DWORD rv;
    (void)flags;

    EnterCriticalSection(&cv->cs);
    cv->waiters++;
    cv->releaseCount--;
    LeaveCriticalSection(&cv->cs);

    /* Release the caller's lock while waiting. */
    LeaveCriticalSection(&srw->cs);

    rv = WaitForSingleObject(cv->event, ms);

    /* Re-acquire the caller's lock. */
    EnterCriticalSection(&srw->cs);

    EnterCriticalSection(&cv->cs);
    cv->waiters--;
    if (cv->releaseCount > cv->waiters) {
        cv->releaseCount = cv->waiters;
        SetEvent(cv->event);   /* pass the wakeup along if others wait */
    }
    LeaveCriticalSection(&cv->cs);

    return rv == WAIT_OBJECT_0;
}

/* Map the SleepConditionVariableSRW family.  The console driver passes a
 * SRWLOCK* as the second argument and 0 as flags (exclusive mode only),
 * which is exactly the CRITICAL_SECTION contract here. */
#define WakeConditionVariable      xpWakeConditionVariable
#define WakeAllConditionVariable   xpWakeAllConditionVariable
#define SleepConditionVariableSRW  xpSleepConditionVariableSRW
#define SleepConditionVariableCS   xpSleepConditionVariableCS

static __inline BOOL
xpSleepConditionVariableCS(XPCV *cv, CRITICAL_SECTION *cs,
                           DWORD ms)
{
    XPSRW wrapper;
    wrapper.cs = *cs;
    return xpSleepConditionVariableSRW(cv, &wrapper, ms, 0);
}

/* ------------------------------------------------------------------ */
/* INIT_ONCE / InitOnceExecuteOnce (Vista+): one-time init emulation.  */
/* The 7.1A headers do not declare INIT_ONCE under _WIN32_WINNT=0x0501,*/
/* so define our own and route through a critical-section guard.      */
/* ------------------------------------------------------------------ */
#ifndef INIT_ONCE
#define INIT_ONCE XP_INIT_ONCE
typedef struct { CRITICAL_SECTION cs; volatile LONG inited; volatile LONG done; } XP_INIT_ONCE;
#define INIT_ONCE_STATIC_INIT { {0}, 0, 0 }
#ifndef PINIT_ONCE
#define PINIT_ONCE XP_INIT_ONCE *
#endif
#endif

#define InitOnceExecuteOnce(once, callback, param, context) \
    xp_InitOnceExecuteOnce((once), (callback), (param), (context))

static __inline BOOL
xp_InitOnceExecuteOnce(XP_INIT_ONCE *once, void *callback, void *param,
                       void **context)
{
    BOOL result = TRUE;
    void *ctx = NULL;
    LONG inited;

    /* XP has no lazy CRITICAL_SECTION initialization: a static
     * INIT_ONCE_STATIC_INIT ({0}) must be InitializeCriticalSection'd
     * before the first EnterCriticalSection, or the enter crashes.
     * Guard the init with InterlockedCompareExchange so it happens
     * exactly once across threads. */
    inited = InterlockedCompareExchange(&once->inited, 1, 0);
    if (!inited) {
        InitializeCriticalSection(&once->cs);
    }

    EnterCriticalSection(&once->cs);
    if (!once->done) {
        typedef BOOL (CALLBACK *once_fn)(XP_INIT_ONCE *, void *, void **);
        result = ((once_fn)callback)(once, param, &ctx);
        if (result) {
            once->done = 1;
        }
    }
    LeaveCriticalSection(&once->cs);

    if (result && context) {
        *context = ctx;
    }
    return result;
}

/* ------------------------------------------------------------------ */
/* RegGetValueA (Vista+ advapi32): XP has no export.  The only tcl use */
/* reads a REG_SZ value (code page) with a type filter; emulate with   */
/* RegQueryValueExA and reject non-REG_SZ results.                     */
/* ------------------------------------------------------------------ */
#define RegGetValueA(hkey, subkey, value, flags, type, data, cb) \
    xp_RegGetValueA((hkey), (subkey), (value), (flags), (type), (data), (cb))

static __inline LSTATUS
xp_RegGetValueA(HKEY hkey, const char *subkey, const char *value,
                DWORD flags, DWORD *type, void *data, DWORD *cb)
{
    DWORD t = 0;
    HKEY sub = NULL;
    LSTATUS st;

    (void)flags;
    if (subkey != NULL && subkey[0] != '\0') {
        st = RegOpenKeyExA(hkey, subkey, 0, KEY_QUERY_VALUE, &sub);
        if (st != ERROR_SUCCESS) {
            return st;
        }
        hkey = sub;
    }
    st = RegQueryValueExA(hkey, value, NULL, &t, (LPBYTE)data, cb);
    if (sub != NULL) {
        RegCloseKey(sub);
    }
    if (st == ERROR_SUCCESS && type != NULL) {
        *type = t;
    }
    return st;
}

/* ------------------------------------------------------------------ */
/* CancelSynchronousIo (Vista+): no XP equivalent.  Safe no-op; both   */
/* call sites tolerate failure (they wait on the thread handle after). */
/* ------------------------------------------------------------------ */
#define CancelSynchronousIo(hThread) \
    ((void)(hThread), SetLastError(ERROR_CALL_NOT_IMPLEMENTED), 0)

/* ------------------------------------------------------------------ */
/* CreateSymbolicLinkW (Vista+): XP has no symbolic links.  Fail with  */
/* ERROR_CALL_NOT_IMPLEMENTED; callers convert to a Tcl error.         */
/* ------------------------------------------------------------------ */
#define CreateSymbolicLinkW(...) xp_CreateSymbolicLinkW()
static __inline BOOLEAN
xp_CreateSymbolicLinkW(void)
{
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

#endif /* XP_COMPAT_TCL_H */
