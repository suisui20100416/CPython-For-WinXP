"""Read MSI tables via msi.dll COM-free API using ctypes."""
import ctypes
from ctypes import wintypes, c_void_p, c_uint, c_int, POINTER, create_unicode_buffer, byref

msi = ctypes.windll.msi
msi.MsiOpenDatabaseW.restype = c_int
msi.MsiOpenDatabaseW.argtypes = [wintypes.LPCWSTR, wintypes.LPCWSTR, POINTER(c_void_p)]
msi.MsiDatabaseOpenViewW.restype = c_int
msi.MsiDatabaseOpenViewW.argtypes = [c_void_p, wintypes.LPCWSTR, POINTER(c_void_p)]
msi.MsiViewExecute.restype = c_int
msi.MsiViewExecute.argtypes = [c_void_p, c_void_p]
msi.MsiViewFetch.restype = c_int
msi.MsiViewFetch.argtypes = [c_void_p, POINTER(c_void_p)]
msi.MsiRecordGetStringW.restype = c_int
msi.MsiRecordGetStringW.argtypes = [c_void_p, c_uint, wintypes.LPWSTR, POINTER(c_uint)]
msi.MsiRecordGetInteger.restype = c_int
msi.MsiRecordGetInteger.argtypes = [c_void_p, c_uint]
msi.MsiCloseHandle.restype = c_int
msi.MsiCloseHandle.argtypes = [c_void_p]

MSICONDERROR = -1

def open_db(path):
    h = c_void_p()
    r = msi.MsiOpenDatabaseW(path, None, byref(h))
    if r != 0:
        raise RuntimeError(f"MsiOpenDatabaseW {r}")
    return h

def query(hdb, sql):
    view = c_void_p()
    r = msi.MsiDatabaseOpenViewW(hdb, sql, byref(view))
    if r != 0:
        raise RuntimeError(f"MsiDatabaseOpenViewW {r} ({sql})")
    r = msi.MsiViewExecute(view, None)
    if r != 0:
        raise RuntimeError(f"MsiViewExecute {r}")
    rows = []
    while True:
        rec = c_void_p()
        r = msi.MsiViewFetch(view, byref(rec))
        if r == 259:  # ERROR_NO_MORE_ITEMS
            break
        if r != 0:
            raise RuntimeError(f"MsiViewFetch {r}")
        fields = msi.MsiRecordGetFieldCount(rec)
        row = []
        for i in range(1, fields + 1):
            buf = create_unicode_buffer(4096)
            n = c_uint(4095)
            msi.MsiRecordGetStringW(rec, i, buf, byref(n))
            row.append(buf.value)
        rows.append(row)
        msi.MsiCloseHandle(rec)
    msi.MsiCloseHandle(view)
    return rows

import sys
path = sys.argv[1]
tables = sys.argv[2:]
h = open_db(path)
for t in tables:
    try:
        rows = query(h, f"SELECT * FROM `{t}`")
        print(f"-- {t} ({len(rows)} rows)")
        for r in rows:
            print("   " + " | ".join(r))
    except Exception as e:
        print(f"-- {t}: ERROR {e}")
msi.MsiCloseHandle(h)
