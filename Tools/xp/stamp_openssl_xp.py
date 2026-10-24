"""Stamp every existing Makefile rule target into the near future.

git checkouts write all files at extraction time in arbitrary order, so an
openssl in-tree build in a fresh clone decides its generated files (headers,
.pc/.cnf/der_*.h, configdata.pm, ...) are out of date and regenerates them
via perl recipes - and the perl shipped with Git for Windows cannot run any
of them (no Pod::Usage, and `perl Configure` itself needs
ExtUtils::MakeMaker).

Instead of whack-a-mole on the dependency graph, stamp every rule target
that exists on disk 120 seconds into the future.  Make then sees every
generated file as newer than its sources and goes straight to compiling.
Running this on an already-configured tree is harmless (it just re-stamps).
The openssl sources must stay unmodified: a touched .in/.pl source would
re-wake the perl path on the next build.

Usage: stamp_openssl_xp.py <openssl-src-dir>
"""

import os
import re
import sys
import time
from pathlib import Path

# A rule line starts with a target list, then ':'.  Variable assignments
# ("FOO = ...", "FOO := ...") and pattern rules ("%.o: %.c") must not match.
RULE = re.compile(r"^([^\s#%:=][^:=]*?):(?!=)\s")

SPECIAL_PREFIXES = (".", "unix", "windows")


def main(root: str) -> int:
    top = Path(root)
    makefile = top / "Makefile"
    text = makefile.read_text(encoding="utf-8", errors="replace")

    future = time.time() + 120
    targets = []
    for line in text.splitlines():
        m = RULE.match(line)
        if not m:
            continue
        for name in m.group(1).split():
            if name.startswith(SPECIAL_PREFIXES):
                continue  # .PHONY etc. and platform pseudo targets
            p = top / name
            if p.is_file():
                targets.append(p)

    for p in targets:
        os.utime(p, (future, future))

    print(f"[stamp] {len(targets)} generated files stamped +120s in {top}")
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(sys.argv[1]))
