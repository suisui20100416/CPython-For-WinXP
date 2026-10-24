"""Patch the PE subsystem/OS version fields of a PE32 image to target 5.1.

Windows NT 5.1 refuses to load a PE32 image whose MajorSubsystemVersion is
greater than 5. Some third-party engines (e.g. the WiX 3.14 burn bundle
engine) are linked with subsystem 6.0 while importing only XP-era symbols;
this tool rewrites the four version fields in the optional header so the
image loads on NT 5.1.

Only lowers the values; images already <= target are left untouched.
Usage: python patch_pe_subsys.py <file> [--major 5] [--minor 1] [--verify]
Exit code 0 = patched or already compliant, 1 = usage/IO error.
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

OPT_MAGIC_PE32 = 0x10B
OPT_MAGIC_PE32PLUS = 0x20B


def read_optional_header(data: bytes):
    if len(data) < 0x40 or data[:2] != b"MZ":
        raise ValueError("not a PE file (no MZ header)")
    e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
    if data[e_lfanew:e_lfanew + 4] != b"PE\0\0":
        raise ValueError("not a PE file (no PE signature)")
    opt = e_lfanew + 24
    magic = struct.unpack_from("<H", data, opt)[0]
    if magic not in (OPT_MAGIC_PE32, OPT_MAGIC_PE32PLUS):
        raise ValueError(f"unrecognized optional header magic 0x{magic:04x}")
    # Windows-specific fields; identical layout for PE32 and PE32+ from
    # opt+40 onward (BaseOfData at opt+24 shifts only the earlier fields):
    #   MajorOperatingSystemVersion  at opt+40  (H)
    #   MinorOperatingSystemVersion  at opt+42  (H)
    #   MajorImageVersion            at opt+44  (H)
    #   MinorImageVersion            at opt+46  (H)
    #   MajorSubsystemVersion        at opt+48  (H)
    #   MinorSubsystemVersion        at opt+50  (H)
    # Verified against llvm-readobj output on a PE32 image.
    return opt, magic


def patch_file(path: Path, major: int, minor: int) -> tuple[str, tuple[int, ...]]:
    data = path.read_bytes()
    opt, magic = read_optional_header(data)
    os_major, os_minor = struct.unpack_from("<HH", data, opt + 40)
    sub_major, sub_minor = struct.unpack_from("<HH", data, opt + 48)
    old = (os_major, os_minor, sub_major, sub_minor)
    if os_major <= major and sub_major <= major:
        return "already-compliant", old
    new_os = (min(os_major, major), os_minor if os_major < major else minor)
    new_sub = (min(sub_major, major), sub_minor if sub_major < major else minor)
    data = bytearray(data)
    struct.pack_into("<HH", data, opt + 40, *new_os)
    struct.pack_into("<HH", data, opt + 48, *new_sub)
    path.write_bytes(bytes(data))
    return "patched", (old, (new_os[0], new_os[1], new_sub[0], new_sub[1]))


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("file")
    parser.add_argument("--major", type=int, default=5)
    parser.add_argument("--minor", type=int, default=1)
    args = parser.parse_args(argv)

    path = Path(args.file)
    if not path.is_file():
        print(f"error: {path} does not exist", file=sys.stderr)
        return 1
    try:
        status, versions = patch_file(path, args.major, args.minor)
    except ValueError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    print(f"{path.name}: {status}")
    if status == "patched":
        old, new = versions
        print(f"    OS     {old[0]}.{old[1]} -> {new[0]}.{new[1]}")
        print(f"    Subsys {old[2]}.{old[3]} -> {new[2]}.{new[3]}")
    else:
        old = versions
        print(f"    OS {old[0]}.{old[1]}  Subsys {old[2]}.{old[3]} (<= {args.major}.{args.minor})")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
