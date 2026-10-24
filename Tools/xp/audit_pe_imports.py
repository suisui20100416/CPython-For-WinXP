"""Audit a PE32 image for Windows XP SP3 incompatible imports.

Stage A gate tool: reads the import table and flags symbols/DLLs that NT 5.1
does not provide, so a build cannot silently "succeed" while shipping a binary
that fails to load before main().

Usage:
    python audit_pe_imports.py <file-or-dir> [--json report.json]

Exit code 0 = clean, 1 = incompatible imports found, 2 = usage/IO error.

The banned list is intentionally explicit rather than a fuzzy "Vista or newer"
heuristic: only symbols that have been confirmed absent on NT 5.1 belong here,
so the tool never blocks a legitimate XP build on a guess.
"""

from __future__ import annotations

import argparse
import json
import struct
import sys
from pathlib import Path

# Symbols known to be absent on Windows XP SP3 / NT 5.1. Sourced from the
# platform requirements of each API, verified against the SDK 7.1A import
# libraries where possible.
BANNED_SYMBOLS: dict[str, str] = {
    # --- kernel32 / synchronisation -----------------------------------------
    "GetTickCount64": "Vista",
    "InitOnceExecuteOnce": "Vista",
    "InitOnceInitialize": "Vista",
    "InitOnceBeginInitialize": "Vista",
    "InitOnceComplete": "Vista",
    "InitializeCriticalSectionEx": "Vista",
    "InitializeSRWLock": "Vista",
    "AcquireSRWLockExclusive": "Vista",
    "AcquireSRWLockShared": "Vista",
    "ReleaseSRWLockExclusive": "Vista",
    "ReleaseSRWLockShared": "Vista",
    "TryAcquireSRWLockExclusive": "Vista",
    "TryAcquireSRWLockShared": "Vista",
    "SleepConditionVariableCS": "Vista",
    "SleepConditionVariableSRW": "Vista",
    "WakeConditionVariable": "Vista",
    "WakeAllConditionVariable": "Vista",
    "WaitOnAddress": "Win8",
    "WakeByAddressSingle": "Win8",
    "WakeByAddressAll": "Win8",
    "CreateThreadpool": "Vista",
    "CreateThreadpoolIo": "Vista",
    "CreateThreadpoolTimer": "Vista",
    "CreateThreadpoolWait": "Vista",
    "CreateThreadpoolWork": "Vista",
    "CloseThreadpool": "Vista",
    "CloseThreadpoolIo": "Vista",
    "StartThreadpoolIo": "Vista",
    "CancelThreadpoolIo": "Vista",
    "WaitForThreadpoolIoCallbacks": "Vista",
    "SetThreadpoolThreadMaximum": "Vista",
    "SetThreadpoolThreadMinimum": "Vista",
    "SubmitThreadpoolWork": "Vista",
    "WaitForThreadpoolWorkCallbacks": "Vista",
    "SetThreadpoolCallbackPriority": "Win7",
    "CreateEventExW": "Vista",
    "CreateEventExA": "Vista",
    "CreateMutexExW": "Vista",
    "CreateSemaphoreExW": "Vista",
    "CreateWaitableTimerExW": "Vista",
    # --- files / paths ------------------------------------------------------
    "GetFinalPathNameByHandleW": "Vista",
    "GetFinalPathNameByHandleA": "Vista",
    "GetFileInformationByHandleEx": "Vista",
    "SetFileInformationByHandle": "Vista",
    "ReOpenFile": "Vista",
    "SetFileCompletionNotificationModes": "Vista",
    "SetFileIoOverlappedRange": "Vista",
    "CopyFile2": "Win8",
    "CopyFileExW": "XP (present; listed as control)",
    "GetLongPathNameTransactedW": "Vista",
    "CreateSymbolicLinkW": "Vista",
    "CreateSymbolicLinkA": "Vista",
    "GetFileVersionInfoExW": "Vista",
    "GetFileVersionInfoSizeExW": "Vista",
    "GetFileVersionInfoSizeExA": "Vista",
    "GetFileVersionInfoExA": "Vista",
    "PathCchCanonicalizeEx": "Win8",
    "PathCchCombineEx": "Win8",
    "PathCchAppendEx": "Win8",
    "PathCchSkipRoot": "Win8",
    "PathCchRemoveFileSpec": "Win8",
    "PathCchAddBackslash": "Win8",
    "PathCchAddBackslashEx": "Win8",
    "PathCchAddExtension": "Win8",
    "PathCchRenameExtension": "Win8",
    "PathCchRemoveExtension": "Win8",
    "PathCchRemoveBackslash": "Win8",
    "PathCchRemoveBackslashEx": "Win8",
    "PathCchFindExtension": "Win8",
    "PathCchIsRoot": "Win8",
    "PathCchCanonicalize": "Win8",
    "PathCchCombine": "Win8",
    "PathCchAppend": "Win8",
    "PathCchStripPrefix": "Win8",
    "PathCchStripToRoot": "Win8",
    "PathCchUnconvert": "Win8",
    "PathCchConvert": "Win8",
    "PathCchAlloc": "Win8",
    "PathCchFree": "Win8",
    "PathcchSkipRoot": "Win8",
    "PathAllocCanonicalize": "Win8",
    "PathAllocCombine": "Win8",
    # --- process / threads --------------------------------------------------
    "CancelIoEx": "Vista",
    "CancelSynchronousIo": "Vista",
    "GetThreadId": "Vista",
    # GetProcessId is NOT banned: Microsoft documents it as "Windows Vista,
    # Windows XP with SP1" (processthreadsapi.h requirements table). It was
    # listed here by mistake, and that mistake made every MSYS2 runtime down to
    # the XP-era ones look broken. GetProcessIdOfThread, by contrast, really is
    # Vista-only.
    "GetProcessIdOfThread": "Vista",
    "SetThreadDescription": "Win10 1607",
    "GetThreadDescription": "Win10 1607",
    "GetCurrentProcessorNumber": "Vista",
    "GetCurrentProcessorNumberEx": "Win7",
    "SetThreadErrorMode": "Win7",
    "GetThreadErrorMode": "Win7",
    "SetThreadStackGuarantee": "Vista",  # XP Pro x64 only, absent from 32-bit XP
    "SetThreadGroupAffinity": "Win7",
    "GetThreadGroupAffinity": "Win7",
    "SetProcessMitigationPolicy": "Win8",
    "GetProcessMitigationPolicy": "Win8",
    "QueryFullProcessImageNameW": "Vista",
    "QueryFullProcessImageNameA": "Vista",
    "CreateProcessWithTokenW": "XP (present; listed as control)",
    "CreateProcessAsUserW": "XP (present; listed as control)",
    "InitializeProcThreadAttributeList": "Vista",
    "UpdateProcThreadAttribute": "Vista",
    "DeleteProcThreadAttributeList": "Vista",
    "IsWow64Process2": "Win10 1803",
    "GetSystemTimePreciseAsFileTime": "Win8",
    "GetSystemTimeAdjustmentPrecise": "Win10",
    "GetSystemCpuSetInformation": "Win10",
    "GetActiveProcessorCount": "Win7",
    "GetActiveProcessorGroupCount": "Win7",
    # --- I/O completion / networking ---------------------------------------
    "GetQueuedCompletionStatusEx": "Vista",
    "CreateIoCompletionPort": "XP (present; listed as control)",
    "InetPtonW": "Vista",
    "InetPtonA": "Vista",
    "InetNtopW": "Vista",
    "InetNtopA": "Vista",
    "WSAPoll": "Win8",
    "GetAddrInfoExW": "Vista",
    "GetAddrInfoExOverlappedResult": "Vista",
    "SetAddrInfoExW": "Vista",
    "IfCreateCompartmentW": "Vista",
    "if_nametoindex": "XP (present in XP SP2+ iphlpapi)",
    "GetAdaptersAddresses": "XP SP1",
    # --- registry / system info --------------------------------------------
    "RegDeleteTreeW": "Vista",
    "RegDeleteTreeA": "Vista",
    "RegDeleteKeyExW": "Vista",
    "RegDeleteKeyExA": "Vista",
    "RegCopyTreeW": "Vista",
    "RegCopyTreeA": "Vista",
    "RegGetValueW": "Vista",
    "RegGetValueA": "Vista",
    "RegSetKeyValueW": "Vista",
    "RegSetKeyValueA": "Vista",
    "RegDeleteKeyValueW": "Vista",
    "RegDeleteKeyValueA": "Vista",
    "RegLoadAppKeyW": "Vista",
    "RegLoadAppKeyA": "Vista",
    "RegLoadMUIStringW": "Vista",
    "RegLoadMUIStringA": "Vista",
    "RegCreateKeyTransactedW": "Vista",
    "RegCreateKeyTransactedA": "Vista",
    "RegOpenKeyTransactedW": "Vista",
    "RegOpenKeyTransactedA": "Vista",
    "RegDeleteKeyTransactedW": "Vista",
    "RegDeleteKeyTransactedA": "Vista",
    "RegDisableReflectionKey": "Vista",
    "RegEnableReflectionKey": "Vista",
    "RegQueryReflectionKey": "Vista",
    "RegQueryValueExW": "XP (present; listed as control)",
    "SHGetKnownFolderPath": "Vista",
    "SHGetKnownFolderIDList": "Vista",
    "SHCreateItemFromParsingName": "Vista",
    "SetWindowLongPtrW": "XP (present via macro in 64-bit SDKs)",
    "GetSystemMetricsForDpi": "Win10 1607",
    "AdjustWindowRectExForDpi": "Win10 1607",
    "GetDpiForWindow": "Win10 1607",
    "GetDpiForSystem": "Win10 1607",
    "SetProcessDPIAware": "Vista",
    "GetPointerInfo": "Vista",
    "CreateDXGIFactory2": "Win8.1",
    "SetProcessDpiAwarenessContext": "Win10 1703",
    # The Ex logical-processor query is Win7+; XP only has the non-Ex form.
    # thread-utils.c uses the Ex form in online_cpus().
    "GetLogicalProcessorInformationEx": "Win7",
    "GetLogicalProcessorInformationEx2": "Win7",
    # --- memory ------------------------------------------------------------
    "VirtualAlloc2": "Win10 1803",
    "VirtualAlloc2FromApp": "Win10 1803",
    "MapViewOfFile3": "Win10 1803",
    "VirtualAllocExNuma": "Win7",
    "SetProcessWorkingSetSizeEx": "Vista",
    "QueryVirtualMemoryInformation": "Win10 1803",
    "GetProcessGroupAffinity": "Win7",
    "DiscardVirtualMemory": "Win8.1",
    "OfferVirtualMemory": "Win8.1",
    "ReclaimVirtualMemory": "Win8.1",
    # --- crypto / TLS baselines (CryptoAPI Next Gen is Vista+) -------------
    "BCryptGenRandom": "Vista",
    "BCryptOpenAlgorithmProvider": "Vista",
    "BCryptCloseAlgorithmProvider": "Vista",
    "BCryptCreateHash": "Vista",
    "BCryptHashData": "Vista",
    "BCryptFinishHash": "Vista",
    "BCryptDestroyHash": "Vista",
    "NCryptOpenStorageProvider": "Vista",
    # --- console ------------------------------------------------------------
    "GetConsoleScreenBufferInfoEx": "Vista",
    "SetConsoleScreenBufferInfoEx": "Vista",
    "GetConsoleFontSize": "Vista",
    "GetCurrentConsoleFontEx": "Vista",
    "SetCurrentConsoleFontEx": "Vista",
    "ReadConsoleInputExW": "Win10",
    "WriteConsoleInputExW": "Win10",
    "GetConsoleMode": "XP (present; listed as control)",
    # --- library loading / TLS callbacks ------------------------------------
    "SetDefaultDllDirectories": "Win8",
    "AddDllDirectory": "Win8",
    "RemoveDllDirectory": "Win8",
    "LoadLibraryEx2": "Win10",
    "FlsAlloc": "Vista",
    "FlsFree": "Vista",
    "FlsGetValue": "Vista",
    "FlsSetValue": "Vista",
    "TrySubmitThreadpoolCallback": "Vista",
    "SetThreadpoolTimer": "Vista",
    "SetWaitableTimerEx": "Win7",
    "CreateWaitableTimerExW": "Vista",
    # --- COM / shell / UI ---------------------------------------------------
    "CoInitializeEx": "XP (present; listed as control)",
    "RoInitialize": "Win8",
    "RoUninitialize": "Win8",
    "WindowsCreateString": "Win8",
    "WindowsDeleteString": "Win8",
}


# Symbols that modern msvcrt.dll exports but the XP msvcrt.dll does not.
# The secure-CRT (_s) family arrived with VS2005 and is not part of the CRT that
# XP ships. Comparing msvcrt imports against a modern toolchain's libmsvcrt.a
# would not catch these, because that library itself carries them.
BANNED_MSVCRT: dict[str, str] = {
    "swprintf_s": "VS2005+ secure CRT, absent from XP msvcrt.dll",
    "sprintf_s": "VS2005+ secure CRT, absent from XP msvcrt.dll",
    "vsprintf_s": "VS2005+ secure CRT, absent from XP msvcrt.dll",
    "vswprintf_s": "VS2005+ secure CRT, absent from XP msvcrt.dll",
    "swprintf_s_l": "VS2005+ secure CRT, absent from XP msvcrt.dll",
    "_snprintf_s": "VS2005+ secure CRT, absent from XP msvcrt.dll",
    "_snwprintf_s": "VS2005+ secure CRT, absent from XP msvcrt.dll",
    "_vsnprintf_s": "VS2005+ secure CRT, absent from XP msvcrt.dll",
    "_vsnwprintf_s": "VS2005+ secure CRT, absent from XP msvcrt.dll",
    "strcpy_s": "VS2005+ secure CRT, absent from XP msvcrt.dll",
    "strcat_s": "VS2005+ secure CRT, absent from XP msvcrt.dll",
    "strncpy_s": "VS2005+ secure CRT, absent from XP msvcrt.dll",
    "strncat_s": "VS2005+ secure CRT, absent from XP msvcrt.dll",
    "wcscpy_s": "VS2005+ secure CRT, absent from XP msvcrt.dll",
    "wcscat_s": "VS2005+ secure CRT, absent from XP msvcrt.dll",
    "wcsncpy_s": "VS2005+ secure CRT, absent from XP msvcrt.dll",
    "wcsncat_s": "VS2005+ secure CRT, absent from XP msvcrt.dll",
    "memcpy_s": "VS2005+ secure CRT, absent from XP msvcrt.dll",
    "memmove_s": "VS2005+ secure CRT, absent from XP msvcrt.dll",
    "gets_s": "VS2005+ secure CRT, absent from XP msvcrt.dll",
    "scanf_s": "VS2005+ secure CRT, absent from XP msvcrt.dll",
    "sscanf_s": "VS2005+ secure CRT, absent from XP msvcrt.dll",
    "swscanf_s": "VS2005+ secure CRT, absent from XP msvcrt.dll",
    "fopen_s": "VS2005+ secure CRT, absent from XP msvcrt.dll",
    "freopen_s": "VS2005+ secure CRT, absent from XP msvcrt.dll",
    "tmpfile_s": "VS2005+ secure CRT, absent from XP msvcrt.dll",
    "tmpnam_s": "VS2005+ secure CRT, absent from XP msvcrt.dll",
    "strerror_s": "VS2005+ secure CRT, absent from XP msvcrt.dll",
    "strerror_r": "absent from XP msvcrt.dll",
    "_get_errno": "VS2005+ secure CRT, absent from XP msvcrt.dll",
    "_set_errno": "VS2005+ secure CRT, absent from XP msvcrt.dll",
    "_wcserror_s": "VS2005+ secure CRT, absent from XP msvcrt.dll",
    "vsnprintf_s": "VS2005+ secure CRT, absent from XP msvcrt.dll",
    "strnlen_s": "VS2005+ secure CRT, absent from XP msvcrt.dll",
    "wcsnlen_s": "VS2005+ secure CRT, absent from XP msvcrt.dll",
    "wcrtomb_s": "VS2005+ secure CRT, absent from XP msvcrt.dll",
    "mbrtowc_s": "VS2005+ secure CRT, absent from XP msvcrt.dll",
    "abort_s": "VS2005+ secure CRT, absent from XP msvcrt.dll",
}

# DLLs that do not exist on XP at all. api-ms-win-* are loadable only if the
# redistributable UCRT/VCRuntime is installed beside the binary or system-wide;
# they are reported separately rather than banned, because shipping them is a
# legitimate (if heavy) strategy.
MODERN_DLLS: set[str] = {
    "d2d1.dll",
    "d3d11.dll",
    "dxgi.dll",
    "shcore.dll",
    "dcomp.dll",
    "wlanapi.dll",
}

# XP-absent DLLs this port must NEVER depend on (the "native" definition:
# no UCRT/vcruntime redist side-load, no One-Core-API propsys).  Importing
# any of these is a blocking failure, not a note.  16-test finding:
# _wmi.pyd imported propsys.dll and passed every audit round because the
# old MODERN_DLLS treatment only printed a note.  wintrust.dll was removed
# from the "modern" set - WinVerifyTrust ships with XP SP3 (min client
# "Windows XP" per Microsoft Learn).
BANNED_DLLS: set[str] = {
    "vcruntime140.dll",
    "vcruntime140_1.dll",
    "msvcp140.dll",
    "msvcp140_1.dll",
    "msvcp140_2.dll",
    "concrt140.dll",
    "vccorlib140.dll",
    "ucrtbase.dll",
    "api-ms-win-crt-runtime-l1-1-0.dll",
    "bcrypt.dll",
    "ncrypt.dll",
    "propsys.dll",
}

# api-ms-win-* is handled by prefix, everything else exactly.
_API_SET_PREFIX = "api-ms-win-"
_DLL_LOAD_SYMBOL = "<dll-load>"


def read_imports(path: Path) -> tuple[list[tuple[str, str]], str]:
    """Return ([(dll, symbol)], error). Uses llvm-readobj when available."""
    import subprocess

    llvm_readobj = Path(r"C:\Program Files\LLVM\bin\llvm-readobj.exe")
    if not llvm_readobj.is_file():
        return [], "llvm-readobj.exe not found; install LLVM or set the path"

    try:
        proc = subprocess.run(
            [str(llvm_readobj), "--coff-imports", str(path)],
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=120,
        )
    except OSError as error:
        return [], f"failed to run llvm-readobj: {error}"

    if proc.returncode != 0:
        return [], f"llvm-readobj failed: {proc.stderr.strip()[:200]}"

    imports: list[tuple[str, str]] = []
    current: str | None = None
    for line in proc.stdout.splitlines():
        line = line.strip()
        if line.startswith("Name:") and ".dll" in line.lower():
            current = line.split(":", 1)[1].strip().lower()
        elif line.startswith("Symbol:") and current:
            symbol = line.split(":", 1)[1].strip()
            # Strip the ordinal suffix llvm appends, e.g. "Foo (123)".
            if symbol.endswith(")") and " (" in symbol:
                symbol = symbol.rsplit(" (", 1)[0]
            imports.append((current, symbol))
    return imports, ""


def machine_type(path: Path) -> str:
    with path.open("rb") as handle:
        if handle.read(2) != b"MZ":
            return "not-a-pe"
        handle.seek(0x3C)
        pe_offset = struct.unpack("<I", handle.read(4))[0]
        handle.seek(pe_offset)
        if handle.read(4) != b"PE\0\0":
            return "not-a-pe"
        return {0x14C: "i386", 0x8664: "amd64", 0x1C0: "arm"}.get(
            struct.unpack("<H", handle.read(2))[0], "unknown"
        )


def audit_file(path: Path) -> dict:
    imports, error = read_imports(path)
    result = {
        "file": str(path),
        "arch": machine_type(path),
        "banned": [],
        "modern_dlls": [],
        "api_set_dlls": [],
        "error": error,
    }
    if error:
        return result

    banned_seen: set[tuple[str, str]] = set()
    for dll, symbol in imports:
        # Two independent checks. BANNED_SYMBOLS covers kernel32/ntdll-era APIs;
        # BANNED_MSVCRT covers the secure-CRT family that XP's msvcrt.dll lacks.
        # The second exists because the toolchain's own libmsvcrt.a exports these
        # symbols, so a naive "is it in the import library" check passes while
        # the real XP loader rejects the binary.
        if symbol in BANNED_MSVCRT and dll == "msvcrt.dll":
            banned_seen.add((dll, symbol))
        if symbol in BANNED_SYMBOLS:
            requirement = BANNED_SYMBOLS[symbol]
            if requirement.startswith("XP"):
                continue  # documented control entry, present on XP
            banned_seen.add((dll, symbol))
        if dll in BANNED_DLLS:
            banned_seen.add((dll, _DLL_LOAD_SYMBOL))
        if dll.startswith(_API_SET_PREFIX):
            if dll not in result["api_set_dlls"]:
                result["api_set_dlls"].append(dll)
        elif dll in MODERN_DLLS:
            if dll not in result["modern_dlls"]:
                result["modern_dlls"].append(dll)

    result["banned"] = [
        {"dll": dll, "symbol": symbol,
         "requires": BANNED_SYMBOLS.get(
             symbol, BANNED_MSVCRT.get(
                 symbol, "XP SP3: the DLL itself does not exist"
                 if symbol == _DLL_LOAD_SYMBOL else "unknown"))}
        for dll, symbol in sorted(banned_seen)
    ]
    return result


def gather_targets(target: Path, excludes: list[str] | None = None) -> list[Path]:
    if target.is_dir():
        files: list[Path] = []
        for pattern in ("*.exe", "*.dll", "*.pyd", "*.so"):
            files.extend(sorted(target.rglob(pattern)))
        if excludes:
            kept: list[Path] = []
            for path in files:
                rel = path.relative_to(target).as_posix()
                if any(rel.startswith(prefix) for prefix in excludes):
                    continue
                kept.append(path)
            files = kept
        return files
    return [target]


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("target", help="PE file or directory to audit")
    parser.add_argument("--json", help="write a JSON report to this path")
    parser.add_argument(
        "--exclude",
        action="append",
        default=[],
        metavar="PREFIX",
        help="skip paths under this directory prefix (relative to target); "
             "repeatable",
    )
    args = parser.parse_args(argv)

    target = Path(args.target)
    if not target.exists():
        print(f"error: {target} does not exist", file=sys.stderr)
        return 2

    targets = gather_targets(target, args.exclude)
    if not targets:
        print(f"error: no PE files found under {target}", file=sys.stderr)
        return 2

    results = [audit_file(path) for path in targets]
    failures = 0

    for result in results:
        name = Path(result["file"]).name
        if result["error"]:
            print(f"[ERROR] {name}: {result['error']}")
            failures += 1
            continue
        status = "FAIL" if result["banned"] else "PASS"
        if result["banned"]:
            failures += 1
        print(f"[{status}] {name} (arch={result['arch']})")
        for entry in result["banned"]:
            print(
                f"        XP-missing import: {entry['dll']}!{entry['symbol']}"
                f"  (requires {entry['requires']})"
            )
        if result["api_set_dlls"]:
            print(
                "        note: imports API-set DLLs "
                f"({len(result['api_set_dlls'])}): "
                + ", ".join(sorted(result["api_set_dlls"])[:4])
                + (" ..." if len(result["api_set_dlls"]) > 4 else "")
            )
            print(
                "        note: these require the UCRT/vcruntime redist be present"
                " on the XP machine; /MT static linking avoids this"
            )
        if result["modern_dlls"]:
            print(
                "        note: imports non-XP-system DLLs: "
                + ", ".join(sorted(result["modern_dlls"]))
            )

    if args.json:
        Path(args.json).write_text(
            json.dumps(results, indent=2), encoding="utf-8"
        )
        print(f"report: {args.json}")

    print(
        f"\n{len(results)} file(s) audited, {failures} with blocking issues,"
        f" {sum(len(r['banned']) for r in results)} banned import(s) total."
    )
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
