"""Report the PE subsystem version of every executable in a tree.

Windows NT 5.1 refuses to load a PE32 image whose MajorSubsystemVersion is
greater than 5. A tree can be import-clean and still be unloadable for this
reason alone, so this check is a separate gate from the import audit.

Usage: python check_subsys.py <dir-or-file> [--max 5]
Exit code 1 if any image exceeds the limit.
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

PE_PATTERNS = ("*.exe", "*.dll", "*.pyd", "*.so")


def subsystem_version(path: Path):
    with open(path, "rb") as handle:
        data = handle.read()
    if len(data) < 0x40 or data[:2] != b"MZ":
        return None
    e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
    if data[e_lfanew:e_lfanew + 4] != b"PE\0\0":
        return None
    opt = e_lfanew + 24
    magic = struct.unpack_from("<H", data, opt)[0]
    if magic not in (0x10B, 0x20B):
        return None
    major, minor = struct.unpack_from("<HH", data, opt + 48)
    return major, minor, magic


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("target")
    parser.add_argument("--max", type=int, default=5)
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
    if target.is_dir():
        files: list[Path] = []
        for pattern in PE_PATTERNS:
            files.extend(sorted(target.rglob(pattern)))
        if args.exclude:
            files = [
                path for path in files
                if not any(
                    path.relative_to(target).as_posix().startswith(prefix)
                    for prefix in args.exclude
                )
            ]
    else:
        files = [target]

    offenders = []
    scanned = 0
    for path in files:
        info = subsystem_version(path)
        if info is None:
            continue
        scanned += 1
        major, minor, _ = info
        if major > args.max:
            offenders.append((path, major, minor))

    for path, major, minor in offenders:
        print("  %-70s subsys=%d.%d" % (str(path).replace("\\", "/"), major, minor))
    print("%d image(s) scanned, %d above subsystem %d" % (scanned, len(offenders), args.max))
    return 1 if offenders else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
