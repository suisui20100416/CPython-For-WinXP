"""Cross-check every kernel32 import of a PE32 file against the Windows 7.1A
SDK Kernel32.Lib, which is the import library shipped for XP-era targets.

This is stronger evidence than a hand-written banned-symbol list for the
"does this symbol exist on NT 5.1" question, because the import library is
Microsoft's own record of the XP export set. It complements
audit_pe_imports.py, which catches known-Vista+ APIs by name and reports
API-set/UCRT dependencies.

Usage:
    python verify_against_sdk71a.py <file-or-dir> [--json report.json]

Exit code 0 = every kernel32 import is present in the XP-era import library,
1 = at least one import is unknown to it, 2 = usage/IO error.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

DEFAULT_LIB = Path(
    r"C:\Program Files (x86)\Microsoft SDKs\Windows\v7.1A\Lib\Kernel32.Lib"
)
LLVM_READOBJ = Path(r"C:\Program Files\LLVM\bin\llvm-readobj.exe")

# Import libraries record symbol names with these decoration patterns; the
# probe uses plain __cdecl C names, so a substring test on the raw bytes is
# sufficient and avoids needing a full COFF archive parser.
def read_imports(path: Path) -> tuple[list[tuple[str, str]], str]:
    if not LLVM_READOBJ.is_file():
        return [], "llvm-readobj.exe not found"
    try:
        proc = subprocess.run(
            [str(LLVM_READOBJ), "--coff-imports", str(path)],
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
    dll: str | None = None
    for line in proc.stdout.splitlines():
        line = line.strip()
        if line.startswith("Name:") and ".dll" in line.lower():
            dll = line.split(":", 1)[1].strip()
        elif line.startswith("Symbol:") and dll:
            name = line.split(":", 1)[1].strip()
            match = re.match(r"^(.*?) \((\d+)\)$", name)
            if match:
                name = match.group(1)
            imports.append((dll, name))
    return imports, ""


def audit_file(path: Path, lib_bytes: bytes) -> dict:
    imports, error = read_imports(path)
    result = {
        "file": str(path),
        "total_imports": len(imports),
        "non_kernel32": [],
        "unknown_to_sdk71a": [],
        "error": error,
    }
    if error:
        return result
    for dll, symbol in imports:
        if dll.lower() != "kernel32.dll":
            result["non_kernel32"].append({"dll": dll, "symbol": symbol})
            continue
        if symbol.encode("ascii", "replace") not in lib_bytes:
            result["unknown_to_sdk71a"].append({"dll": dll, "symbol": symbol})
    return result


def gather_targets(target: Path) -> list[Path]:
    if target.is_dir():
        files: list[Path] = []
        for pattern in ("*.exe", "*.dll", "*.pyd"):
            files.extend(sorted(target.rglob(pattern)))
        return files
    return [target]


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("target")
    parser.add_argument("--json")
    parser.add_argument("--lib", default=str(DEFAULT_LIB))
    args = parser.parse_args(argv)

    lib_path = Path(args.lib)
    if not lib_path.is_file():
        print(f"error: XP-era import library not found: {lib_path}", file=sys.stderr)
        return 2
    lib_bytes = lib_path.read_bytes()

    target = Path(args.target)
    if not target.exists():
        print(f"error: {target} does not exist", file=sys.stderr)
        return 2

    targets = gather_targets(target)
    if not targets:
        print(f"error: no PE files under {target}", file=sys.stderr)
        return 2

    results = [audit_file(path, lib_bytes) for path in targets]
    failures = 0
    for result in results:
        name = Path(result["file"]).name
        if result["error"]:
            print(f"[ERROR] {name}: {result['error']}")
            failures += 1
            continue
        unknown = result["unknown_to_sdk71a"]
        status = "FAIL" if unknown else "PASS"
        if unknown:
            failures += 1
        print(
            f"[{status}] {name}: {result['total_imports']} imports, "
            f"{len(unknown)} unknown to SDK 7.1A Kernel32.Lib"
        )
        for entry in unknown:
            print(f"        {entry['dll']}!{entry['symbol']}")
        non_kernel = {e["dll"] for e in result["non_kernel32"]}
        if non_kernel:
            print(
                "        other DLLs imported: " + ", ".join(sorted(non_kernel))
                + " (audit those separately; only kernel32 is verified here)"
            )

    if args.json:
        Path(args.json).write_text(json.dumps(results, indent=2), encoding="utf-8")
        print(f"report: {args.json}")

    print(f"\n{len(results)} file(s) checked, {failures} with unknown imports.")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
