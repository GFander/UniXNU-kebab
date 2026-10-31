#!/usr/bin/env python3
"""Pack an iOS userland tree into a KIST stream for the kebab userland installer.

The stream is a single self-describing file: one stream header, then one
fixed-size entry header plus its payload per file.  It is designed for a
transport that only ever moves bytes forward -- a USB bulk-OUT endpoint with a
512-byte packet size and a stop-and-wait console -- so a reader never seeks,
never needs the file length in advance, and can stop after any entry.

    stream header (32 bytes)
    entry header (192 bytes) + payload   x entry_count
    zero padding to a multiple of --chunk

All integers are little-endian.

Stream header
    0   4  magic          b"KIST"
    4   2  version        2
    6   2  flags          bit0: at least one file is deflated
                          bit1: skeleton stream (DIR and LINK entries allowed)
    8   4  entry_count
    12  4  header_size    sizeof(stream header), for forward compatibility
    16  8  total_usize    sum of file sizes and symlink target lengths
    24  8  total_wire     bytes after this header, padding included

Entry header
    0   4  magic          b"KENT"
    4   2  path_len       1..127
    6   2  mode           permission bits, 07777 mask (01777 keeps the sticky bit)
    8   2  flags          bit0: payload is a zlib stream (deflate)
                          bit1: directory entry, no payload
                          bit2: symlink entry, payload is the target string
    10  2  reserved       0
    12  8  wire_size      payload bytes that follow this header
    20  8  file_size      bytes the destination file must have
    28  32 sha256         SHA-256 of the *uncompressed* content
    60  128 path          destination path, UTF-8, no NUL required
    188 4  reserved       0

Why a fixed 192-byte header: the kernel parses this from 64 KiB chunks with a
small state machine, and a fixed header means no length-prefix recursion.  127
bytes is longer than any destination in the plan.

KIST v2 adds the two entry types the installer cannot express as regular
files.  A directory entry has no payload: wire_size and file_size are zero,
sha256 is all zero, and mode is the exact permission value (07777, so the
skeleton's 01777 is expressible; v1's 0777 mask dropped the sticky bit).  A
symlink entry stores its target in the payload: wire_size == file_size ==
len(target), sha256 is the digest of the target bytes, and deflate is not
used.  Either type is legal only in a skeleton stream (stream flag bit1).
v1 streams (regular files only) are still accepted by the kernel and by
`--verify`; this packer writes v2.

The explicit-list form of `--spec` accepts two extra line shapes:

    DIR<TAB>/private/tmp<TAB>01777
    LINK<TAB>/etc<TAB>private/etc

Everything else is `SRC<TAB>DEST[<TAB>MODE]` as before.  `--skeleton` marks
the stream as a skeleton stream; a stream that contains a DIR or LINK entry
without it is refused.

Compression is per entry and optional.  `--compress auto` (the default) tries
zlib and keeps the result only when it saves at least `--min-gain` (12.5%),
because a payload that barely compresses costs the receiver inflate time for
nothing.  `--compress none` produces a stream every build can install;
`--compress zlib` forces deflate for entries that shrink at all.

Resume works in two steps, both host-side, so a resumed stream does not carry
payload that is already on the phone:

    pack-userland.py ... --write-manifest plan.txt -o userland.kist
    send-userland.py --probe plan.txt --skip-list have.txt userland.kist
    pack-userland.py ... --skip-list have.txt -o resume.kist
    send-userland.py resume.kist

`--verify` re-reads a stream from disk and checks the magic, the version, every
entry's SHA-256 (inflating deflated payloads), the declared totals and the
padding, without needing the source tree.
"""

import argparse
import hashlib
import os
import struct
import sys
import tempfile
import zlib

STREAM_MAGIC = b"KIST"
ENTRY_MAGIC = b"KENT"
VERSION = 2
VERSION_MIN = 1
STREAM_HDR = struct.Struct("<4sHHI I QQ")   # 32 bytes
ENTRY_HDR = struct.Struct("<4sHHHH QQ 32s 128s I")  # 192 bytes
assert STREAM_HDR.size == 32, STREAM_HDR.size
assert ENTRY_HDR.size == 192, ENTRY_HDR.size

F_STREAM_DEFLATE = 0x0001
F_STREAM_SKELETON = 0x0002
F_ENTRY_DEFLATE = 0x0001
F_ENTRY_DIR = 0x0002
F_ENTRY_SYMLINK = 0x0004
F_ENTRY_TYPE = F_ENTRY_DIR | F_ENTRY_SYMLINK
ZERO_SHA = b"\0" * 32

PATH_MAX = 128
MODE_MASK = 0o7777
DEFAULT_CHUNK = 64 * 1024
CHUNK_BLOCK = 1024 * 1024


def die(msg):
    print("pack-userland: %s" % msg, file=sys.stderr)
    sys.exit(1)


def human(n):
    if n >= 1 << 30:
        return "%.2f GiB" % (n / float(1 << 30))
    if n >= 1 << 20:
        return "%.1f MiB" % (n / float(1 << 20))
    if n >= 1 << 10:
        return "%.1f KiB" % (n / float(1 << 10))
    return "%d B" % n


def hash_and_size(path):
    h = hashlib.sha256()
    size = 0
    with open(path, "rb") as f:
        while True:
            b = f.read(CHUNK_BLOCK)
            if not b:
                break
            h.update(b)
            size += len(b)
    return h.digest(), size


def deflate_to(src, dst, level):
    """Compress src into dst, returning (wire_size, sha256, file_size)."""
    h = hashlib.sha256()
    co = zlib.compressobj(level)
    wire = 0
    size = 0
    with open(src, "rb") as fi, open(dst, "wb") as fo:
        while True:
            b = fi.read(CHUNK_BLOCK)
            if not b:
                break
            h.update(b)
            size += len(b)
            c = co.compress(b)
            if c:
                fo.write(c)
                wire += len(c)
        c = co.flush()
        fo.write(c)
        wire += len(c)
    return wire, h.digest(), size


class Entry(object):
    __slots__ = ("dest", "src", "mode", "wire", "size", "digest", "flags",
                 "payload", "kind", "target")

    def __init__(self, dest, src, mode, kind="file", target=None):
        self.dest = dest
        self.src = src
        self.mode = mode
        self.kind = kind
        self.target = target
        self.wire = 0
        self.size = 0
        self.digest = b""
        self.flags = 0
        self.payload = None      # file: path to the payload
                                 # link: the target bytes
                                 # dir:  None (no payload)

    @classmethod
    def directory(cls, dest, mode):
        e = cls(dest, None, mode, "dir")
        e.digest = ZERO_SHA
        e.flags = F_ENTRY_DIR
        return e

    @classmethod
    def symlink(cls, dest, target):
        raw = target.encode("utf-8")
        e = cls(dest, None, 0o777, "link", target)
        e.wire = e.size = len(raw)
        e.digest = hashlib.sha256(raw).digest()
        e.flags = F_ENTRY_SYMLINK
        e.payload = raw
        return e


def check_link_target(target):
    """A symlink target is bounded and printable so the console can log it."""
    if not target:
        die("symlink target must not be empty")
    raw = target.encode("utf-8")
    if len(raw) >= PATH_MAX:
        die("symlink target longer than %d bytes" % (PATH_MAX - 1))
    for b in raw:
        if b < 0x20 or b > 0x7e:
            die("symlink target contains a non-printable byte 0x%02x" % b)


def check_dest(dest, allow):
    """The same rules the kernel enforces, applied before anything is built."""
    if not dest.startswith("/"):
        die("%s: destination must be absolute" % dest)
    if len(dest.encode()) >= PATH_MAX:
        die("%s: destination longer than %d bytes" % (dest, PATH_MAX - 1))
    if ".." in dest.split("/"):
        die("%s: destination contains .." % dest)
    if "//" in dest or dest.endswith("/"):
        die("%s: destination has an empty component" % dest)
    if allow and not any(dest.startswith(p) for p in allow):
        die("%s: outside the allowlist %s"
            % (dest, ", ".join(allow)))


def walk_tree(root, allow):
    entries = []
    root = os.path.abspath(root)
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames.sort()
        for name in sorted(filenames):
            full = os.path.join(dirpath, name)
            if not os.path.isfile(full) or os.path.islink(full):
                continue
            rel = os.path.relpath(full, root).replace(os.sep, "/")
            dest = "/" + rel
            check_dest(dest, allow)
            entries.append(Entry(dest, full, os.stat(full).st_mode & MODE_MASK))
    return entries


def parse_map(spec, allow):
    if ":" not in spec:
        die("--map wants SRC:DEST, got %r" % spec)
    src, dest = spec.split(":", 1)
    if not os.path.isfile(src):
        die("--map source %s is not a file" % src)
    check_dest(dest, allow)
    return Entry(dest, src, os.stat(src).st_mode & MODE_MASK)


def parse_spec(path, allow):
    """DIR/LINK or SRC<TAB>DEST[<TAB>MODE] lines: the explicit list form."""
    entries = []
    with open(path, "r") as f:
        for lineno, line in enumerate(f, 1):
            line = line.rstrip("\n")
            if not line or line.lstrip().startswith("#"):
                continue
            parts = line.split("\t")
            if len(parts) < 2:
                die("%s:%d: want DIR/LINK or SRC<TAB>DEST[<TAB>MODE]"
                    % (path, lineno))
            tag = parts[0].strip()
            if tag == "DIR":
                if len(parts) < 3 or not parts[2].strip():
                    die("%s:%d: DIR wants PATH<TAB>MODE" % (path, lineno))
                dest = parts[1].strip()
                check_dest(dest, allow)
                entries.append(Entry.directory(dest, int(parts[2].strip(), 8)))
                continue
            if tag == "LINK":
                if len(parts) < 3:
                    die("%s:%d: LINK wants PATH<TAB>TARGET" % (path, lineno))
                dest = parts[1].strip()
                target = parts[2].strip()
                check_dest(dest, allow)
                check_link_target(target)
                entries.append(Entry.symlink(dest, target))
                continue
            src, dest = parts[0].strip(), parts[1].strip()
            if not os.path.isfile(src):
                die("%s:%d: %s is not a file" % (path, lineno, src))
            check_dest(dest, allow)
            if len(parts) > 2 and parts[2].strip():
                mode = int(parts[2].strip(), 8)
            else:
                mode = os.stat(src).st_mode & MODE_MASK
            entries.append(Entry(dest, src, mode))
    return entries


def read_skip_list(path):
    """Lines of `dest` or `dest<TAB>size<TAB>sha256`; returns {dest: (size,hex)}."""
    skip = {}
    with open(path, "r") as f:
        for line in f:
            line = line.rstrip("\n")
            if not line or line.startswith("#"):
                continue
            parts = line.split("\t")
            dest = parts[0].strip()
            if not dest.startswith("/"):
                continue
            if len(parts) >= 3:
                try:
                    skip[dest] = (int(parts[1]), parts[2].strip().lower())
                except ValueError:
                    skip[dest] = None
            else:
                skip[dest] = None
    return skip


def build(args):
    allow = args.allow or []
    if args.root and args.map:
        die("use either --root or --map, not both")
    if not args.root and not args.map and not args.spec:
        die("need --root DIR, --spec FILE or at least one --map SRC:DEST")

    entries = []
    if args.root:
        entries += walk_tree(args.root, allow)
    if args.map:
        entries += [parse_map(m, allow) for m in args.map]
    if args.spec:
        entries += parse_spec(args.spec, allow)
    if args.mode:
        overrides = {}
        for m in args.mode:
            if "=" not in m:
                die("--mode wants DEST=MODE, got %r" % m)
            d, v = m.split("=", 1)
            overrides[d] = int(v, 8)
        for e in entries:
            if e.dest in overrides:
                e.mode = overrides[e.dest]
    for e in entries:
        if e.mode & ~MODE_MASK:
            die("%s: mode 0%o has bits outside 07777" % (e.dest, e.mode))
    nonfile = [e for e in entries if e.kind != "file"]
    if nonfile and not args.skeleton:
        die("%d DIR/LINK entr%s in a stream without --skeleton"
            % (len(nonfile), "y" if len(nonfile) == 1 else "ies"))

    skip = read_skip_list(args.skip_list) if args.skip_list else {}
    kept = []
    skipped = []
    for e in entries:
        if e.kind == "file" and e.dest in skip:
            skipped.append(e)
        else:
            kept.append(e)
    entries = sorted(kept, key=lambda e: e.dest)

    # Hash (and compress) regular files before the header can be written: the
    # header carries the entry count and the totals.  Directory and symlink
    # entries are already complete (no payload and a target payload).
    tmpdir = (os.path.dirname(os.path.abspath(args.output))
              if args.output else tempfile.gettempdir())
    level = args.level
    for i, e in enumerate(entries):
        if e.kind == "dir":
            print("  %-58s %10s -> %10s dir mode %04o"
                  % (e.dest, "-", "-", e.mode))
            continue
        if e.kind == "link":
            print("  %-58s %10s -> %10s link -> %s"
                  % (e.dest, human(e.size), human(e.wire), e.target))
            continue
        if args.compress == "none":
            e.digest, e.size = hash_and_size(e.src)
            e.wire = e.size
            e.payload = e.src
        else:
            fd, tmp = tempfile.mkstemp(prefix=".kist-", dir=tmpdir)
            os.close(fd)
            try:
                wire, digest, size = deflate_to(e.src, tmp, level)
            except Exception:
                os.unlink(tmp)
                raise
            e.digest, e.size = digest, size
            if args.compress == "zlib":
                keep = wire < size
            else:
                keep = wire <= size - (size * args.min_gain) // 100
            if keep:
                e.wire = wire
                e.flags = F_ENTRY_DEFLATE
                e.payload = tmp
            else:
                os.unlink(tmp)
                e.wire = e.size
                e.payload = e.src
        print("  %-58s %10s -> %10s %s" % (
            e.dest, human(e.size), human(e.wire),
            "deflate" if e.flags else "store"))

    stream_flags = 0
    if any(e.flags & F_ENTRY_DEFLATE for e in entries):
        stream_flags |= F_STREAM_DEFLATE
    if args.skeleton:
        stream_flags |= F_STREAM_SKELETON
    total_usize = sum(e.size for e in entries)
    body = sum(ENTRY_HDR.size + e.wire for e in entries)
    pad = (-(STREAM_HDR.size + body)) % args.chunk
    total_wire = body + pad

    if args.list_only:
        print("plan: %d entries, %s on the wire, %s installed, %d B padding"
              % (len(entries), human(STREAM_HDR.size + total_wire),
                 human(total_usize), pad))
        for e in entries:
            if e.kind == "file":
                print("%s\t%d\t%s" % (e.dest, e.size, e.digest.hex()))
            elif e.kind == "link":
                print("%s\t%d\t%s\tlink\t%s"
                      % (e.dest, e.size, e.digest.hex(), e.target))
            else:
                print("%s\t0\t%s\tdir" % (e.dest, e.digest.hex()))
        print("skipped (already on the target): %d" % len(skipped))
        for e in skipped:
            print("SKIP\t%s" % e.dest)
        return 0

    out = open(args.output, "wb")
    try:
        out.write(b"\0" * STREAM_HDR.size)   # rewritten once the totals are known
        for e in entries:
            path = e.dest.encode("utf-8")
            out.write(ENTRY_HDR.pack(ENTRY_MAGIC, len(path), e.mode, e.flags, 0,
                                     e.wire, e.size, e.digest, path, 0))
            if e.kind == "dir":
                continue
            if e.kind == "link":
                out.write(e.payload)
                continue
            if e.payload is None:
                die("internal: entry %s has no payload" % e.dest)
            with open(e.payload, "rb") as f:
                left = e.wire
                while left:
                    b = f.read(min(left, CHUNK_BLOCK))
                    if not b:
                        die("%s: payload shorter than declared" % e.dest)
                    out.write(b)
                    left -= len(b)
        if pad:
            out.write(b"\0" * pad)
        out.seek(0)
        out.write(STREAM_HDR.pack(STREAM_MAGIC, VERSION, stream_flags,
                                  len(entries), STREAM_HDR.size, total_usize,
                                  total_wire))
        out.flush()
        os.fsync(out.fileno())
    finally:
        out.close()

    # Remove the temporary compressed payloads only after the stream is written.
    for e in entries:
        if e.kind == "file" and e.payload and e.payload != e.src:
            os.unlink(e.payload)

    total = STREAM_HDR.size + total_wire
    kinds = {}
    for e in entries:
        kinds[e.kind] = kinds.get(e.kind, 0) + 1
    print("stream: %s, %d entries (%s), %s on the wire (%.1f%% of %s installed)"
          % (args.output, len(entries),
             ", ".join("%d %s" % (kinds[k], k) for k in sorted(kinds)),
             human(total),
             100.0 * total / total_usize if total_usize else 0.0,
             human(total_usize)))
    if pad:
        print("padding: %d bytes to a %d-byte chunk boundary" % (pad, args.chunk))
    if skipped:
        print("skipped: %d entries already correct on the target" % len(skipped))
    if args.write_manifest:
        files = [e for e in entries if e.kind == "file"]
        with open(args.write_manifest, "w") as f:
            for e in files:
                f.write("%s\t%d\t%s\n" % (e.dest, e.size, e.digest.hex()))
            for e in skipped:
                f.write("%s\t%d\t%s\n" % (e.dest, e.size, e.digest.hex()))
        print("manifest: %s (%d files; %d dir/link entries not listed)"
              % (args.write_manifest, len(files),
                 len(entries) - len(files)))
    return 0


def verify(args):
    path = args.verify
    size = os.path.getsize(path)
    bad = 0
    skel = False
    kinds = {"file": 0, "dir": 0, "link": 0}
    with open(path, "rb") as f:
        raw = f.read(STREAM_HDR.size)
        if len(raw) != STREAM_HDR.size:
            die("%s: shorter than a stream header" % path)
        magic, version, flags, count, hdr_size, total_usize, total_wire = \
            STREAM_HDR.unpack(raw)
        if magic != STREAM_MAGIC:
            die("%s: bad stream magic %r" % (path, magic))
        if version not in (VERSION_MIN, VERSION):
            die("%s: version %d, expected %d..%d"
                % (path, version, VERSION_MIN, VERSION))
        if version == 1:
            if flags & ~F_STREAM_DEFLATE:
                die("%s: v1 stream has unknown flags 0x%04x" % (path, flags))
            skel = False
        else:
            if flags & ~(F_STREAM_DEFLATE | F_STREAM_SKELETON):
                die("%s: stream has unknown flags 0x%04x" % (path, flags))
            skel = bool(flags & F_STREAM_SKELETON)
        if hdr_size != STREAM_HDR.size:
            die("%s: header_size %d, expected %d"
                % (path, hdr_size, STREAM_HDR.size))
        if size != STREAM_HDR.size + total_wire:
            die("%s: file is %d bytes, header declares %d"
                % (path, size, STREAM_HDR.size + total_wire))
        print("%s: v%d%s, %d entries, %s installed, %s on the wire"
              % (path, version, " skeleton" if skel else "", count,
                 human(total_usize), human(size)))

        seen_usize = 0
        seen_wire = 0
        for i in range(count):
            h = f.read(ENTRY_HDR.size)
            if len(h) != ENTRY_HDR.size:
                die("%s: entry %d header truncated" % (path, i))
            (emagic, plen, mode, eflags, _r, wire, fsize, digest,
             rawpath, _r2) = ENTRY_HDR.unpack(h)
            if emagic != ENTRY_MAGIC:
                die("%s: entry %d bad magic %r" % (path, i, emagic))
            if not 1 <= plen <= PATH_MAX - 1:
                die("%s: entry %d path_len %d" % (path, i, plen))
            dest = rawpath[:plen].decode("utf-8", "replace")
            etype = eflags & F_ENTRY_TYPE
            if eflags & ~(F_ENTRY_DEFLATE | F_ENTRY_TYPE):
                print("FAIL %s: unknown entry flags 0x%04x" % (dest, eflags))
                bad += 1
            if etype == F_ENTRY_TYPE:
                print("FAIL %s: entry is both a directory and a symlink" % dest)
                bad += 1
            if version == 1 and etype:
                print("FAIL %s: v1 stream cannot carry a DIR/LINK entry" % dest)
                bad += 1
            if etype and not skel:
                print("FAIL %s: DIR/LINK entry outside a skeleton stream" % dest)
                bad += 1
            if mode & ~MODE_MASK:
                print("FAIL %s: mode 0%o has bits outside 07777" % (dest, mode))
                bad += 1
            if (not dest.startswith("/") or ".." in dest.split("/")
                    or "//" in dest or dest.endswith("/")
                    or any(ord(c) < 0x20 or ord(c) > 0x7e for c in dest)):
                print("FAIL %s: refusing path" % dest)
                bad += 1

            is_dir = etype == F_ENTRY_DIR
            is_link = etype == F_ENTRY_SYMLINK
            h256 = hashlib.sha256()
            got = 0
            target = bytearray()
            dec = (zlib.decompressobj()
                   if (eflags & F_ENTRY_DEFLATE and not is_dir and not is_link)
                   else None)
            while got < wire:
                b = f.read(min(wire - got, CHUNK_BLOCK))
                if not b:
                    die("%s: %s payload truncated at %d of %d"
                        % (path, dest, got, wire))
                got += len(b)
                if is_dir:
                    continue
                if is_link:
                    target += b
                    h256.update(b)
                    continue
                if dec is not None:
                    try:
                        h256.update(dec.decompress(b))
                    except zlib.error as ex:
                        die("%s: %s inflate failed: %s" % (path, dest, ex))
                else:
                    h256.update(b)

            if is_dir:
                if eflags & F_ENTRY_DEFLATE:
                    print("FAIL %s: directory entry is deflated" % dest)
                    bad += 1
                if wire or fsize or digest != ZERO_SHA:
                    print("FAIL %s: directory entry has a payload or hash"
                          % dest)
                    bad += 1
                kinds["dir"] += 1
                print("OK   %-58s %10s dir  mode %04o" % (dest, "-", mode))
                seen_wire += ENTRY_HDR.size + wire
                continue

            if is_link:
                if eflags & F_ENTRY_DEFLATE:
                    print("FAIL %s: symlink entry is deflated" % dest)
                    bad += 1
                if wire != fsize:
                    print("FAIL %s: symlink payload %d != target size %d"
                          % (dest, wire, fsize))
                    bad += 1
                if not 0 < wire < PATH_MAX:
                    print("FAIL %s: symlink target length %d" % (dest, wire))
                    bad += 1
                if any(b < 0x20 or b > 0x7e for b in target):
                    print("FAIL %s: symlink target is not printable" % dest)
                    bad += 1
                if h256.digest() != digest:
                    print("FAIL %s: symlink sha256 %s != header %s"
                          % (dest, h256.hexdigest(), digest.hex()))
                    bad += 1
                kinds["link"] += 1
                print("OK   %-58s %10s -> %-32s mode %04o"
                      % (dest, human(fsize),
                         target.decode("utf-8", "replace"), mode))
                seen_usize += fsize
                seen_wire += ENTRY_HDR.size + wire
                continue

            if dec is not None:
                h256.update(dec.flush())
                if not dec.eof:
                    print("FAIL %s: deflate stream did not end" % dest)
                    bad += 1
            else:
                if wire != fsize:
                    print("FAIL %s: stored payload %d != file_size %d"
                          % (dest, wire, fsize))
                    bad += 1
            if h256.digest() != digest:
                print("FAIL %s: sha256 %s != header %s"
                      % (dest, h256.hexdigest(), digest.hex()))
                bad += 1
            kinds["file"] += 1
            print("OK   %-58s %10s mode %04o %s" % (
                dest, human(fsize), mode,
                "deflate" if eflags & F_ENTRY_DEFLATE else "store"))
            seen_usize += fsize
            seen_wire += ENTRY_HDR.size + wire

        tail = size - STREAM_HDR.size
        if tail != total_wire:
            print("FAIL stream: consumed %d of %d wire bytes" % (tail, total_wire))
            bad += 1
        padding = total_wire - seen_wire
        if padding < 0:
            print("FAIL stream: entries overflow total_wire")
            bad += 1
        elif padding:
            pad = f.read(padding)
            if len(pad) != padding or any(pad):
                print("FAIL stream: %d padding bytes are not zero" % padding)
                bad += 1
            else:
                print("OK   padding: %d zero bytes" % padding)
        if seen_usize != total_usize:
            print("FAIL stream: total_usize %d != header %d"
                  % (seen_usize, total_usize))
            bad += 1
        if f.read(1):
            print("FAIL stream: trailing bytes after the declared stream")
            bad += 1
    if bad:
        print("verify: %d problem(s)" % bad)
        return 1
    print("verify: %d entries (%d files, %d dirs, %d symlinks), "
          "all digests match" % (count, kinds["file"], kinds["dir"],
                                 kinds["link"]))
    return 0


def main():
    ap = argparse.ArgumentParser(
        description="pack a userland tree into a KIST stream",
        formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__)
    ap.add_argument("-o", "--output", help="stream file to write")
    ap.add_argument("--root", help="directory tree to pack, rooted at /")
    ap.add_argument("--map", action="append", metavar="SRC:DEST",
                    help="one explicit file and its destination (repeatable)")
    ap.add_argument("--spec", metavar="FILE",
                    help="DIR/LINK lines or SRC<TAB>DEST[<TAB>MODE] lines")
    ap.add_argument("--skeleton", action="store_true",
                    help="mark the stream as a skeleton stream (allows DIR/LINK)")
    ap.add_argument("--mode", action="append", metavar="DEST=MODE",
                    help="override a destination's mode, octal (repeatable)")
    ap.add_argument("--allow", action="append", metavar="PREFIX",
                    help="allowed destination prefix (repeatable; default any)")
    ap.add_argument("--compress", choices=("auto", "none", "zlib"),
                    default="auto", help="per-entry compression (default auto)")
    ap.add_argument("--level", type=int, default=6, help="zlib level (default 6)")
    ap.add_argument("--min-gain", type=int, default=12,
                    help="auto mode: keep deflate only above this %% saving")
    ap.add_argument("--chunk", type=int, default=DEFAULT_CHUNK,
                    help="pad the stream to this chunk size (default %d)"
                         % DEFAULT_CHUNK)
    ap.add_argument("--skip-list", metavar="FILE",
                    help="paths (or path/size/sha256 lines) already on target")
    ap.add_argument("--write-manifest", metavar="FILE",
                    help="write path/size/sha256 lines for --probe")
    ap.add_argument("--list", dest="list_only", action="store_true",
                    help="print the plan and write nothing")
    ap.add_argument("--verify", metavar="FILE",
                    help="re-read and check an existing stream")
    args = ap.parse_args()

    if args.verify:
        return verify(args)
    if not args.output and not args.list_only:
        ap.error("need -o/--output, or --list, or --verify")
    if args.chunk < 512 or args.chunk % 512:
        ap.error("--chunk must be a multiple of 512")
    return build(args)


if __name__ == "__main__":
    sys.exit(main())
