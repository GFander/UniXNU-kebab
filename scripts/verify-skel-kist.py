#!/usr/bin/env python3
"""Verify the semantic contents of a KIST v2 root-volume skeleton stream.

`pack-userland.py --verify` proves the wire format: headers, sizes, digests
and padding.  This test proves the *meaning*: every destination is one of the
skeleton paths, every directory mode is the exact table value (01777 keeps its
sticky bit), every symlink points at the expected target, and every file
contains the expected root entry.  It is the host-side counterpart of the
kernel installer's exact allowlist and attribute check.

    scripts/verify-skel-kist.py ~/ios-userland/kist/skel.kist

Exit status is 0 only when the stream matches the table in
scripts/make-minimal-userland.py.
"""

import argparse
import hashlib
import importlib.util
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
BUILDER = os.path.join(HERE, "make-minimal-userland.py")

STREAM_MAGIC = b"KIST"
ENTRY_MAGIC = b"KENT"
STREAM_HDR = struct.Struct("<4sHHIIQQ")
ENTRY_HDR = struct.Struct("<4sHHHHQQ32s128sI")
F_STREAM_DEFLATE = 0x0001
F_STREAM_SKELETON = 0x0002
F_ENTRY_DEFLATE = 0x0001
F_ENTRY_DIR = 0x0002
F_ENTRY_SYMLINK = 0x0004
F_ENTRY_TYPE = F_ENTRY_DIR | F_ENTRY_SYMLINK
MODE_MASK = 0o7777
PATH_MAX = 128


def die(msg):
    print("verify-skel-kist: %s" % msg, file=sys.stderr)
    return 1


def load_builder():
    spec = importlib.util.spec_from_file_location("make_minimal_userland",
                                                  BUILDER)
    if spec is None or spec.loader is None:
        raise RuntimeError("cannot load %s" % BUILDER)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def expected_table(mm):
    table = {}
    for dest, mode in mm.SKELETON_DIRS:
        table[dest] = ("dir", mode, None)
    for dest, target in mm.SKELETON_LINKS:
        table[dest] = ("link", 0o777, target)
    for dest, (mode, data) in mm.SKELETON_FILES.items():
        table[dest] = ("file", mode, data)
    return table


def parse(path):
    size = os.path.getsize(path)
    with open(path, "rb") as f:
        raw = f.read(STREAM_HDR.size)
        if len(raw) != STREAM_HDR.size:
            raise RuntimeError("%s: short stream header" % path)
        magic, version, flags, count, hsize, usize, wire = \
            STREAM_HDR.unpack(raw)
        if magic != STREAM_MAGIC:
            raise RuntimeError("%s: bad magic %r" % (path, magic))
        if version != 2:
            raise RuntimeError("%s: version %d, want 2" % (path, version))
        if flags != F_STREAM_SKELETON:
            raise RuntimeError("%s: stream flags 0x%04x, want skeleton-only "
                               "0x%04x" % (path, flags, F_STREAM_SKELETON))
        if hsize != STREAM_HDR.size:
            raise RuntimeError("%s: header size %d" % (path, hsize))
        if size != STREAM_HDR.size + wire:
            raise RuntimeError("%s: file size %d, header wire %d"
                               % (path, size, wire))
        entries = {}
        seen_usize = 0
        seen_wire = 0
        for i in range(count):
            h = f.read(ENTRY_HDR.size)
            if len(h) != ENTRY_HDR.size:
                raise RuntimeError("%s: entry %d header truncated" % (path, i))
            (emagic, plen, mode, eflags, _r, w, fsize, digest,
             rawpath, _r2) = ENTRY_HDR.unpack(h)
            if emagic != ENTRY_MAGIC:
                raise RuntimeError("%s: entry %d bad magic" % (path, i))
            if not 1 <= plen < PATH_MAX:
                raise RuntimeError("%s: entry %d path length %d"
                                   % (path, i, plen))
            dest = rawpath[:plen].decode("utf-8")
            etype = eflags & F_ENTRY_TYPE
            if eflags & ~(F_ENTRY_TYPE | F_ENTRY_DEFLATE):
                raise RuntimeError("%s: %s unknown flags 0x%04x"
                                   % (path, dest, eflags))
            if etype == F_ENTRY_TYPE:
                raise RuntimeError("%s: %s is both dir and symlink"
                                   % (path, dest))
            if eflags & F_ENTRY_DEFLATE:
                raise RuntimeError("%s: %s is deflated; skeleton entries are "
                                   "stored" % (path, dest))
            if mode & ~MODE_MASK:
                raise RuntimeError("%s: %s mode 0%o" % (path, dest, mode))
            data = f.read(w)
            if len(data) != w:
                raise RuntimeError("%s: %s payload truncated" % (path, dest))
            seen_wire += ENTRY_HDR.size + w
            seen_usize += fsize
            if etype == F_ENTRY_DIR:
                if w or fsize or digest != b"\0" * 32:
                    raise RuntimeError("%s: %s dir has payload/hash"
                                       % (path, dest))
                entries[dest] = ("dir", mode, None)
            elif etype == F_ENTRY_SYMLINK:
                if w != fsize or not data:
                    raise RuntimeError("%s: %s symlink payload mismatch"
                                       % (path, dest))
                if hashlib.sha256(data).digest() != digest:
                    raise RuntimeError("%s: %s symlink hash mismatch"
                                       % (path, dest))
                target = data.decode("utf-8")
                if any(ord(c) < 0x20 or ord(c) > 0x7e for c in target):
                    raise RuntimeError("%s: %s symlink target not printable"
                                       % (path, dest))
                entries[dest] = ("link", mode, target)
            else:
                if w != fsize:
                    raise RuntimeError("%s: %s file payload mismatch"
                                       % (path, dest))
                if hashlib.sha256(data).digest() != digest:
                    raise RuntimeError("%s: %s file hash mismatch"
                                       % (path, dest))
                entries[dest] = ("file", mode, data)
        pad = f.read()
        if len(pad) != wire - seen_wire or any(pad):
            raise RuntimeError("%s: padding is not %d zero bytes"
                               % (path, wire - seen_wire))
        if seen_usize != usize:
            raise RuntimeError("%s: installed bytes %d != header %d"
                               % (path, seen_usize, usize))
    return entries


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("stream", nargs="?", default=os.path.expanduser(
        "~/ios-userland/kist/skel.kist"))
    args = ap.parse_args()

    mm = load_builder()
    want = expected_table(mm)
    try:
        got = parse(args.stream)
    except (RuntimeError, OSError) as exc:
        return die(str(exc))

    bad = 0
    for dest in sorted(set(want) | set(got)):
        if dest not in got:
            print("FAIL missing %s" % dest)
            bad += 1
            continue
        if dest not in want:
            print("FAIL unexpected %s" % dest)
            bad += 1
            continue
        wkind, wmode, wdata = want[dest]
        gkind, gmode, gdata = got[dest]
        if gkind != wkind:
            print("FAIL %s: type %s, want %s" % (dest, gkind, wkind))
            bad += 1
            continue
        if gmode != wmode:
            print("FAIL %s: mode 0%o, want 0%o" % (dest, gmode, wmode))
            bad += 1
            continue
        if wkind == "link":
            if gdata != wdata:
                print("FAIL %s: -> %s, want %s" % (dest, gdata, wdata))
                bad += 1
                continue
            print("OK   %-28s link  -> %s" % (dest, gdata))
        elif wkind == "dir":
            print("OK   %-28s dir   mode 0%o" % (dest, gmode))
        else:
            if gdata != wdata:
                print("FAIL %s: content differs" % dest)
                bad += 1
                continue
            text = wdata.decode("ascii", "replace").rstrip("\n")
            print("OK   %-28s file  mode 0%o  %s" % (dest, gmode, text))

    # The old packer masked modes with 0777, which is exactly why the sticky
    # bit could not be expressed.  Spell the two computations out so this
    # test fails if someone "simplifies" the mask back.
    if (0o1777 & 0o777) != 0o777:
        print("FAIL mode arithmetic: 01777 & 0777 should lose the sticky bit")
        bad += 1
    if (0o1777 & MODE_MASK) != 0o1777:
        print("FAIL mode arithmetic: 01777 & 07777 must keep the sticky bit")
        bad += 1
    print("mode mask: 01777 & 07777 = 0%o (old 0777 mask = 0%o)"
          % (0o1777 & MODE_MASK, 0o1777 & 0o777))

    if bad:
        print("verify-skel-kist: %d problem(s)" % bad)
        return 1
    print("verify-skel-kist: %d entries match the skeleton table" % len(want))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except RuntimeError as exc:
        sys.exit(die(str(exc)))
