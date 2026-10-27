#!/usr/bin/env python3
"""Build a KebabRoot HFS+ volume image and its sparse extent stream.

The kernel can already write userdata's blocks, but it has no HFS+
implementation.  So the volume is built here, with Apple's own tools, and only
the blocks that carry data are shipped to the phone:

    make-root-volume.py --size 16g --image /tmp/KebabRoot.img \\
        --stream /tmp/KebabRoot.vext

What it does:

  1. creates a sparse raw image (`mkfile -n`) of the requested size, attaches it
     as a raw disk (`hdiutil ... diskimage-class=CRawDiskImage`), runs
     `newfs_hfs -v KebabRoot -b 4096` on the device and detaches it.  No
     partition map is written: the HFS+ volume starts at byte 0 of userdata,
     which is what the kernel's root mount expects;
  2. mounts the new volume and writes the directory skeleton that neither the
     installer nor launchd can create for itself: the `/dev` mountpoint (devfs
     supplies the nodes once it exists), `/private/var` and its subdirectories,
     `/private/tmp`, the `/etc`, `/tmp` and `/var` symlinks, and the root entry
     in `/etc/passwd` and `/etc/group`.  The installer's destination allowlist
     only covers six prefixes and it can only create regular files, so these
     paths cannot come from a KIST stream.  `--skip-skeleton` leaves the volume
     empty for an experiment;
  3. reads the new volume header and its allocation bitmap.  Only blocks the
     bitmap calls allocated are shipped: a free block may legally contain
     anything (the old volume's bytes live there after the switch), but an
     allocated one must contain this image's bytes.  Blocks that are allocated
     but all zero are therefore included too; they are rare once the metadata
     clump sizes are pinned, and leaving the old volume's catalog nodes there
     would be sloppy even though an unused B-tree node is never read;
  4. writes a VEXT v1 stream: a 32-byte header, then `extent_count` records of
     {u64 offset, u32 length, u32 reserved, length bytes of data}, then zero
     padding to a 64 KiB boundary.  Offsets and lengths are allocation-block
     aligned, the kernel only ever writes whole 4 KiB device blocks.

The stream is what `scripts/send-volume.py` hands to `VOL BEGIN`; the kernel
side (bsd/dev/kebab_vol.c) verifies every field and checks the volume against
the userdata partition before it writes a single block.

macOS only: uses hdiutil and newfs_hfs.  `--image` may name an existing image
(`--reuse`), which skips creation.
"""

import argparse
import hashlib
import os
import re
import struct
import subprocess
import sys
import tempfile

BLOCK = 4096
CHUNK = 65536
MAGIC = b"VEXT"
VERSION = 1
HDR = 32
EXT_HDR = 16
VH_OFF = 1024

# The smallest skeleton that lets launchd reach its first job.  /dev is only a
# mountpoint: the kernel mounts devfs over it in bsd_init and devfs creates
# console/tty/null/zero/klog itself, so no device node is ever written here.
SKELETON_DIRS = (
    ("/dev", 0o755),
    ("/private", 0o755),
    ("/private/etc", 0o755),
    ("/private/tmp", 0o1777),
    ("/private/var", 0o755),
    ("/private/var/db", 0o755),
    ("/private/var/db/com.apple.xpc.launchd", 0o755),
    ("/private/var/db/com.apple.xpc.launchd/config", 0o755),
    ("/private/var/log", 0o755),
    ("/private/var/log/com.apple.xpc.launchd", 0o755),
    ("/private/var/root", 0o700),
    ("/private/var/run", 0o755),
    ("/private/var/tmp", 0o1777),
)

# /etc, /tmp and /var are symlinks on iOS; launchd uses both spellings
# (/var/run and /private/var/...), so both have to resolve to the same tree.
SKELETON_LINKS = (
    ("etc", "private/etc"),
    ("tmp", "private/tmp"),
    ("var", "private/var"),
)

# launchd resolves its own uid/gid with getpwuid_r() before it builds a job's
# credentials.  Without these files every job fails with "Could not find uid
# associated with service", even when the plist does not name a user.
SKELETON_FILES = {
    "/private/etc/passwd":
        (0o644, b"root:*:0:0:System Administrator:/var/root:/bin/sh\n"),
    "/private/etc/master.passwd":
        (0o644, b"root:*:0:0::0:0:System Administrator:/var/root:/bin/sh\n"),
    "/private/etc/group":
        (0o644, b"wheel:*:0:root\n"),
}


def die(msg):
    print("make-root-volume: %s" % msg, file=sys.stderr)
    return 2


def run(cmd):
    print("+ " + " ".join(cmd), file=sys.stderr, flush=True)
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        raise RuntimeError("%s failed: %s" % (cmd[0], (r.stderr or r.stdout).strip()))
    return r.stdout


def parse_size(text):
    m = re.match(r"^(\d+(?:\.\d+)?)([bkmgt]?)$", text.strip().lower())
    if not m:
        raise ValueError("bad size %r" % text)
    n = float(m.group(1))
    suffix = m.group(2)
    mult = {"": 1, "b": 512, "k": 1 << 10, "m": 1 << 20,
            "g": 1 << 30, "t": 1 << 40}[suffix]
    value = int(n * mult)
    if value % BLOCK:
        value -= value % BLOCK
    return value


def create_image(image, size, name):
    if os.path.exists(image):
        os.unlink(image)
    run(["mkfile", "-n", str(size), image])
    out = run(["hdiutil", "attach", "-nomount", "-noverify",
               "-imagekey", "diskimage-class=CRawDiskImage", image])
    m = re.search(r"/dev/(disk\d+)", out)
    if not m:
        raise RuntimeError("hdiutil did not name a device: %r" % out)
    dev = m.group(1)
    try:
        run(["newfs_hfs", "-v", name, "-b", str(BLOCK),
             "-c", "c=256,e=32,a=8", "-I", "c=256,e=32,a=8",
             "/dev/r" + dev])
    finally:
        run(["hdiutil", "detach", "/dev/" + dev, "-quiet"])
    print("created %s: %d bytes (%d blocks of %d)"
          % (image, size, size // BLOCK, BLOCK), file=sys.stderr)


def populate_skeleton(image):
    """Mount the new image and write the paths a KIST stream cannot reach."""
    if os.geteuid() != 0:
        raise RuntimeError("the volume skeleton needs root (hdiutil -owners on "
                           "and chown to root:wheel); re-run under sudo or "
                           "pass --skip-skeleton")

    mountpoint = tempfile.mkdtemp(prefix="kebab-root-")
    attached = False
    try:
        run(["hdiutil", "attach", "-noverify", "-noautoopen", "-owners", "on",
             "-mountpoint", mountpoint, image])
        attached = True
        for path, _mode in SKELETON_DIRS:
            os.makedirs(os.path.join(mountpoint, path.lstrip("/")),
                        exist_ok=True)
        for path, (mode, data) in SKELETON_FILES.items():
            full = os.path.join(mountpoint, path.lstrip("/"))
            with open(full, "wb") as f:
                f.write(data)
        for name, target in SKELETON_LINKS:
            full = os.path.join(mountpoint, name)
            if os.path.lexists(full):
                os.unlink(full)
            os.symlink(target, full)

        # Deepest first, so chmod/chown of a parent cannot affect a child that
        # still needs its own mode.  Symlinks get lchown, never chown.
        for path, mode in sorted(SKELETON_DIRS,
                                 key=lambda item: -item[0].count("/")):
            full = os.path.join(mountpoint, path.lstrip("/"))
            os.chmod(full, mode)
            os.chown(full, 0, 0)
        for path, (mode, _data) in SKELETON_FILES.items():
            full = os.path.join(mountpoint, path.lstrip("/"))
            os.chmod(full, mode)
            os.chown(full, 0, 0)
        for name, _target in SKELETON_LINKS:
            os.lchown(os.path.join(mountpoint, name), 0, 0)
    finally:
        if attached:
            run(["hdiutil", "detach", mountpoint, "-quiet"])
        try:
            os.rmdir(mountpoint)
        except OSError:
            pass
    print("skeleton: %d directories, %d files, %d symlinks"
          % (len(SKELETON_DIRS), len(SKELETON_FILES), len(SKELETON_LINKS)),
          file=sys.stderr)


def read_block(f, index, block=BLOCK):
    f.seek(index * block)
    data = f.read(block)
    if len(data) != block:
        raise RuntimeError("short read at block %d" % index)
    return data


def parse_volume(image):
    size = os.path.getsize(image)
    with open(image, "rb") as f:
        vh = read_block(f, VH_OFF // BLOCK)[VH_OFF % BLOCK:VH_OFF % BLOCK + 512]
        sig, ver = struct.unpack_from(">HH", vh, 0)
        if sig != 0x482B or ver != 4:
            raise RuntimeError("not an HFS+ volume header: sig 0x%04x ver %d"
                               % (sig, ver))
        bsize, total, free = struct.unpack_from(">III", vh, 40)
        if bsize != BLOCK:
            raise RuntimeError("allocation block size %d, the kernel writes %d"
                               % (bsize, BLOCK))
        if total * bsize != size:
            raise RuntimeError("volume says %d bytes, image is %d"
                               % (total * bsize, size))
        attrs = struct.unpack_from(">I", vh, 4)[0]
        journal = struct.unpack_from(">I", vh, 12)[0]
        print("volume header: sig 0x%04x version %d attributes 0x%08x "
              "journal 0x%x" % (sig, ver, attrs, journal), file=sys.stderr)
        if journal:
            raise RuntimeError("volume is journaled; the kernel mounts a "
                               "non-journaled root")
        # allocation file fork: logical size, clump, total blocks, extents
        afork = 112
        logical = struct.unpack_from(">Q", vh, afork)[0]
        ablocks = struct.unpack_from(">I", vh, afork + 12)[0]
        bitmap = bytearray()
        for i in range(8):
            start, count = struct.unpack_from(">II", vh, afork + 16 + i * 8)
            if not count:
                continue
            for b in range(count):
                bitmap += read_block(f, start + b)
        need = (total + 7) // 8
        if logical < need or len(bitmap) < need:
            raise RuntimeError("allocation bitmap too small: %d < %d"
                               % (len(bitmap), need))
        bitmap = bitmap[:need]
        if ablocks != (need + BLOCK - 1) // BLOCK:
            print("note: allocation file %d blocks for %d bytes"
                  % (ablocks, need), file=sys.stderr)
    return {"size": size, "bsize": bsize, "total": total, "free": free,
            "vh": vh, "bitmap": bitmap}


def allocated_blocks(vol):
    bitmap = vol["bitmap"]
    out = []
    for i in range(vol["total"]):
        if bitmap[i >> 3] & (1 << (7 - (i & 7))):
            out.append(i)
    return out


def build_extents(image, vol, blocks):
    """Merge consecutive allocated blocks; data comes straight from the image."""
    extents = []
    with open(image, "rb") as f:
        cur_start = None
        cur_end = None
        cur_len = 0
        for b in blocks:
            if cur_start is not None and b == cur_end + 1:
                cur_end = b
                continue
            if cur_start is not None:
                extents.append((cur_start * BLOCK, (cur_end - cur_start + 1) * BLOCK))
            cur_start = cur_end = b
        if cur_start is not None:
            extents.append((cur_start * BLOCK, (cur_end - cur_start + 1) * BLOCK))

        out = []
        for off, length in extents:
            f.seek(off)
            data = f.read(length)
            if len(data) != length:
                raise RuntimeError("short extent read at 0x%x" % off)
            out.append((off, data))
    return out


def write_stream(path, vol, extents):
    pad = 0
    body = HDR + sum(EXT_HDR + len(d) for _, d in extents)
    if body % CHUNK:
        pad = CHUNK - body % CHUNK
    wire = body + pad
    sha = hashlib.sha256()
    with open(path, "wb") as f:
        hdr = struct.pack("<4sHHQIIQ", MAGIC, VERSION, HDR, vol["size"],
                          vol["bsize"], len(extents),
                          sum(len(d) for _, d in extents))
        f.write(hdr)
        sha.update(hdr)
        for off, data in extents:
            eh = struct.pack("<QII", off, len(data), 0)
            f.write(eh)
            f.write(data)
            sha.update(eh)
            sha.update(data)
        if pad:
            f.write(b"\0" * pad)
            sha.update(b"\0" * pad)
    return wire, pad, sha.hexdigest()


def verify_stream(path, vol, extents, wire):
    if os.path.getsize(path) != wire:
        raise RuntimeError("stream size %d != declared %d"
                           % (os.path.getsize(path), wire))
    with open(path, "rb") as f:
        hdr = f.read(HDR)
        magic, version, hsize, size, bsize, count, data_bytes = \
            struct.unpack("<4sHHQIIQ", hdr)
        if magic != MAGIC or version != VERSION or hsize != HDR:
            raise RuntimeError("bad stream header")
        if size != vol["size"] or bsize != vol["bsize"] or count != len(extents):
            raise RuntimeError("stream header disagrees with the volume")
        if data_bytes != sum(len(d) for _, d in extents):
            raise RuntimeError("stream data length mismatch")
        for off, data in extents:
            eh = f.read(EXT_HDR)
            eoff, elen, rsvd = struct.unpack("<QII", eh)
            if (eoff, elen, rsvd) != (off, len(data), 0):
                raise RuntimeError("extent header mismatch at 0x%x" % off)
            got = f.read(elen)
            if got != data:
                raise RuntimeError("extent data mismatch at 0x%x" % off)
        rest = f.read()
        if rest.strip(b"\0") or len(rest) != len(rest):
            raise RuntimeError("nonzero padding")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--size", default="16g", help="volume size (default 16g)")
    ap.add_argument("--image", default="/tmp/KebabRoot.img")
    ap.add_argument("--stream", default="/tmp/KebabRoot.vext")
    ap.add_argument("--name", default="KebabRoot")
    ap.add_argument("--reuse", action="store_true",
                    help="use the existing image instead of rebuilding it")
    ap.add_argument("--skip-skeleton", action="store_true",
                    help="leave /dev, /var, /tmp and /etc off the volume "
                         "(launchd will not be able to spawn a job)")
    args = ap.parse_args()

    try:
        size = parse_size(args.size)
    except ValueError as exc:
        return die(str(exc))
    if size < 8 << 30:
        return die("refusing a root volume below 8 GiB")
    if size > 64 << 30:
        return die("refusing a root volume above 64 GiB")
    if args.reuse:
        if not os.path.exists(args.image):
            return die("--reuse but %s does not exist" % args.image)
        if os.path.getsize(args.image) != size:
            return die("%s is %d bytes, not %d"
                       % (args.image, os.path.getsize(args.image), size))
    else:
        create_image(args.image, size, args.name)

    if not args.skip_skeleton:
        try:
            populate_skeleton(args.image)
        except RuntimeError as exc:
            return die(str(exc))

    vol = parse_volume(args.image)
    blocks = allocated_blocks(vol)
    extents = build_extents(args.image, vol, blocks)
    zero_blocks = 0
    nonzero_bytes = 0
    for off, data in extents:
        for i in range(0, len(data), BLOCK):
            block = data[i:i + BLOCK]
            if any(block):
                nonzero_bytes += len(block)
            else:
                zero_blocks += 1
    wire, pad, digest = write_stream(args.stream, vol, extents)
    verify_stream(args.stream, vol, extents, wire)

    print("volume      %s" % args.image)
    print("volume size %d bytes (%d MiB), block size %d, total blocks %d"
          % (vol["size"], vol["size"] >> 20, vol["bsize"], vol["total"]))
    print("free blocks %d (%d MiB), allocated blocks %d"
          % (vol["free"], (vol["free"] * vol["bsize"]) >> 20, len(blocks)))
    print("extents     %d, data %d bytes (%d KiB), non-zero %d bytes, "
          "allocated-but-zero blocks %d"
          % (len(extents), sum(len(d) for _, d in extents),
             sum(len(d) for _, d in extents) >> 10, nonzero_bytes,
             zero_blocks))
    print("stream      %s, %d bytes (%d chunks), padding %d, sha256 %s"
          % (args.stream, wire, wire // CHUNK, pad, digest))
    return 0


if __name__ == "__main__":
    sys.exit(main())
