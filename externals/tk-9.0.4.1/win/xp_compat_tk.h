/*
 * xp_compat_tk.h -- XP (NT 5.1) shims for Tk 9.0.4 Windows sources.
 *
 *  - ChangeWindowMessageFilter (Vista+): XP has no export; the only use
 *    (allow TaskbarButtonCreated through the UIPI filter) is meaningless
 *    on XP.  Provide a no-op returning FALSE.
 */
#ifndef XP_COMPAT_TK_H
#define XP_COMPAT_TK_H

#ifndef MSGFLT_ADD
#define MSGFLT_ADD 1
#endif

#define ChangeWindowMessageFilter(msg, action) \
    xp_ChangeWindowMessageFilter((msg), (action))

static __inline BOOL
xp_ChangeWindowMessageFilter(UINT message, DWORD action)
{
    (void)message;
    (void)action;
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

#endif /* XP_COMPAT_TK_H */

/* ------------------------------------------------------------------ */
/* SHCreateItemFromParsingName (Vista+ shell32): used only to set the */
/* initial folder on IFileDialog.  XP has no IFileDialog either; the   */
/* tk dialog driver falls back to the common dialog path.  Returning   */
/* E_NOTIMPL makes the caller skip SetFolder, which is the correct     */
/* behavior here.                                                      */
/* ------------------------------------------------------------------ */
#ifndef E_NOTIMPL
#define E_NOTIMPL 0x80004001L
#endif
static __inline HRESULT
SHCreateItemFromParsingNameXP(void)
{
    return E_NOTIMPL;
}
