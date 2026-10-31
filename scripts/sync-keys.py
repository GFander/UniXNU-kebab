#!/usr/bin/env python3
"""Copy the host-tested key module into the xnu tree, or check the copy.

package/KebabKeys.c and .h are the module KebabKeysHostTests.c drives.  The
kernel build needs the same code in its own tree, so bsd/dev/kebab_keys_core.{c,h}
are generated copies of those two files, not a second implementation.  The only
transformations are mechanical:

  * a banner that says where the file came from and that it must not be edited;
  * KEBAB_KEYS_KERNEL defined before the header, which switches the module from
    <stdint.h>/<string.h> to <sys/types.h> plus <libkern/libkern.h>;
  * the header include renamed to the kernel file's own name.

    python scripts/sync-keys.py --out  ~/kebab-build/dxb/xnu/bsd/dev
    python scripts/sync-keys.py --check ~/kebab-build/dxb/xnu/bsd/dev
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SRC_C = os.path.join(ROOT, "package", "KebabKeys.c")
SRC_H = os.path.join(ROOT, "package", "KebabKeys.h")

BANNER_C = """/*
 * bsd/dev/kebab_keys_core.c -- the PM8150 key decode and debounce, compiled
 * into the kernel.
 *
 * This file is a generated copy of package/KebabKeys.c from the kebab-dev
 * repository.  That copy is the one KebabKeysHostTests.c drives; this one is
 * what /dev/kebab_keys calls, and the two are byte-identical apart from this
 * banner and the two lines below.  Do not edit here: change
 * package/KebabKeys.c and re-run
 *
 *     python scripts/sync-keys.py --out <xnu>/bsd/dev
 *
 * so that the kernel cannot drift away from what the host tests prove.
 */
#define KEBAB_KEYS_KERNEL 1
#include "kebab_keys_core.h"
"""

BANNER_H = """/*
 * bsd/dev/kebab_keys_core.h -- header of the PM8150 key decode.
 *
 * Generated copy of package/KebabKeys.h from the kebab-dev repository; see
 * bsd/dev/kebab_keys_core.c.  With KEBAB_KEYS_KERNEL set it selects the kernel
 * headers instead of the host ones.  Do not edit: re-run
 * scripts/sync-keys.py.
 */
"""


def read(path):
    with open(path, "r", encoding="utf-8", newline="") as handle:
        return handle.read()


def generate_c():
    src = read(SRC_C)
    marker = '#include "KebabKeys.h"\n'
    if marker not in src:
        raise SystemExit("package/KebabKeys.c no longer includes its header")
    lines = src.split("\n", 1)
    spdx, rest = lines[0], lines[1]
    rest = rest.replace(marker, "", 1)
    return spdx + "\n" + BANNER_C + rest


def generate_h():
    src = read(SRC_H)
    lines = src.split("\n", 1)
    return lines[0] + "\n" + BANNER_H + lines[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--out", metavar="DIR", help="directory to write into")
    group.add_argument("--check", metavar="DIR", help="directory to compare")
    args = parser.parse_args()
    wanted = {
        "kebab_keys_core.c": generate_c(),
        "kebab_keys_core.h": generate_h(),
    }
    if args.out:
        os.makedirs(args.out, exist_ok=True)
        for name, text in wanted.items():
            # newline="\n": the kernel tree is LF even when this runs on Windows.
            with open(os.path.join(args.out, name), "w", encoding="utf-8",
                    newline="\n") as out:
                out.write(text)
        print("wrote " + ", ".join(sorted(wanted)) + " to " + args.out)
        return 0
    stale = []
    for name, text in wanted.items():
        path = os.path.join(args.check, name)
        if not os.path.exists(path) or read(path) != text:
            stale.append(name)
    if stale:
        print("out of date: " + ", ".join(stale) + " (re-run --out)")
        return 1
    print("in sync: the kernel copy matches package/KebabKeys.{c,h}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
