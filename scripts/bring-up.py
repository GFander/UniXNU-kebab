#!/usr/bin/env python3
"""One-command P1 bring-up for the kebab board.

This is the host side of the sequence: flash the root-volume image, imprint the
16 GiB HFS+ volume, flash the installer image, send skel.kist, base.kist and
cache.kist, then boot without kebab_noinit and watch launchd.

The script is deliberately linear and stops at the first observation it did not
expect.  Every checkpoint states what it wanted to see; on failure the report
names the checkpoint, the expectation, what was actually seen, and the last 30
console lines.  The complete console is written to the run log from the first
byte after a reboot.

Examples
--------
    python bring-up.py --from vol
    python bring-up.py --from install --to cache
    python bring-up.py --dry-run --work-dir work/p1
    python bring-up.py --dry-run --work-dir work/p1-fixtures

The board is currently stranded, so the real sequence has not been run.  The
offline checks are exercised by scripts/test-bring-up.py (fake board) and the
--dry-run mode (artifact, stream and boot-argument verification).

No kernel code is changed by this script.  It uses only commands that exist in
bsd/dev/kebab_usb.c, kebab_install.c and kebab_vol.c; see the command table in
docs/BRING-UP-STATE.md section 69.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import threading
import time
import zlib
from collections import deque
from dataclasses import dataclass
from typing import Any, Callable, Iterable, Sequence

try:
    import serial
    from serial.tools import list_ports
except ImportError:  # dry-run and tests must work without pyserial
    serial = None
    list_ports = None


# ---------------------------------------------------------------------------
# Constants shared with the kernel.  Keep these next to the parser references
# in docs/BRING-UP-STATE.md section 69 so a kernel change is easy to audit.
# ---------------------------------------------------------------------------

CHUNK = 64 * 1024
VENDOR_ID = 0x1209
PRODUCT_ID = 0x0001

VOLUME_BYTES = 16 * 1024 * 1024 * 1024
VOLUME_BLOCKS = 4194304
VOLUME_BLOCK_SIZE = 4096
VOLUME_STREAM_BYTES = 1769472
VOLUME_STREAM_DATA = 1744896
VOLUME_STREAM_EXTENTS = 3

IMAGE_BYTES = 96 * 1024 * 1024
IMAGE_PAYLOAD_OFFSET = 0x800000

KIST_STREAM_HDR = 32
KIST_ENTRY_HDR = 192
KIST_MAGIC = b"KIST"
KENT_MAGIC = b"KENT"
KIST_VERSION = 2
KIST_VERSION_MIN = 1
KIST_F_DEFLATE = 0x0001
KIST_F_DIR = 0x0002
KIST_F_SYMLINK = 0x0004
KIST_F_TYPE = KIST_F_DIR | KIST_F_SYMLINK
KIST_SF_SKELETON = 0x0002
KIST_MODE_MASK = 0o7777
KIST_PATH_MAX = 128

VEXT_MAGIC = b"VEXT"
VEXT_VERSION = 1
VEXT_HDR = 32
VEXT_EXT_HDR = 16

BASE_ENTRIES = 6
CACHE_ENTRIES = 44
SAMPLE_PATHS = 8

# skel.kist's fixed shape.  The numbers come from make-minimal-userland.py's
# SKELETON_DIRS/LINKS/FILES tables and are asserted by the dry run; a change
# there has to be reflected here and in the kernel's exact path allowlist.
SKEL_ENTRIES = 16
SKEL_FILES = 3
SKEL_DIRS = 10
SKEL_LINKS = 3
SKEL_INSTALLED = 153
SKEL_WIRE = 65536

FASTBOOT_DEFAULT = r"D:\TOOL\platform-tools\fastboot.exe"
PORT_DEFAULT = "COM7"

# Boot-argument sets.  The base set is what board.toml's [payload] section
# already carries for every kebab boot; the phase delta is what this procedure
# adds or removes.  ufsrw=1 and kebabusb=1 are not optional: without them the
# volume writer refuses the stream and the console does not exist.
BASE_ARGS = {
    "kebabusb=1",
    "ufsrw=1",
    "rootrw=1",
    "kebab_pon_write=1",
    "kebab_cs=15",
}
VOL_ARGS = BASE_ARGS | {"kebab_vol=1", "kebab_noinit=1", "kebab_install=1"}
INSTALL_ARGS = BASE_ARGS | {"kebab_noinit=1", "kebab_install=1"}
FINAL_ARGS = BASE_ARGS | {"kebab_dsc=1", "kebab_dsc_map=1"}

# Commands, exactly as the kernel's single-transfer matchers expect them.
# The trailing newline is part of every match.
CMD_IMEM = b"IMEM?\n"
CMD_FASTBOOT = b"FASTBOOT\n"
CMD_REBOOT = b"REBOOT\n"
CMD_INST_STATUS = b"INST?\n"
CMD_VOL_STATUS = b"VOL?\n"

BOOT_BANNER_RE = re.compile(
    r"^(KEBAB USB: running \(DSTS|KEBAB USB: event thread started|"
    r"Darwin Kernel Version|KEBAB: kebab_noinit=1:)"
)
PANIC_RE = re.compile(
    r"(^panic\(cpu|Debugger called:|^KEBAB PANIC|^panic: |"
    r"KEBAB USB: panic console cannot be serviced|"
    r"KEBAB REBOOT: PEHaltRestart returned)"
)
FAIL_RE = re.compile(
    r"^(INST ABORT|INST TIMEOUT|VOL ABORT|VOL FAIL|VOL TIMEOUT|"
    r"FAIL |panic\(cpu|Debugger called:|KEBAB PANIC|"
    r"KEBAB USB: panic console cannot be serviced)"
)
INST_ACK_RE = re.compile(r"^INST ACK (\d+)$")
VOL_ACK_RE = re.compile(r"^VOL ACK (\d+)$")
INST_BEGIN_RE = re.compile(r"^INST BEGIN (\d+) (dry|write) chunk (\d+)$")
VOL_BEGIN_RE = re.compile(r"^VOL BEGIN (\d+) volume (\d+) (dry|write) chunk (\d+)$")
INST_DONE_RE = re.compile(
    r"^INST DONE entries (\d+) ok (\d+) skipped (\d+) bytes (\d+) "
    r"wire (\d+) ms (\d+)$"
)
INST_DONE_FAIL_RE = re.compile(
    r"^INST DONE with (\d+) failure\(s\), entries (\d+) ok (\d+) skipped (\d+)$"
)
INST_PLAN_RE = re.compile(r"^INST PLAN entries (\d+) bytes (\d+) wire (\d+)$")
VOL_DONE_RE = re.compile(
    r"^VOL DONE extents (\d+) bytes (\d+) wire (\d+) ms (\d+)$"
)
VOL_PLAN_RE = re.compile(
    r"^VOL PLAN extents (\d+) bytes (\d+) wire (\d+) volume (\d+)$"
)
INST_STATUS_RE = re.compile(
    r"^INST\? gated (\d+) active (\d+) dry (\d+) root (rw|ro) chunk (\d+)$"
)
INST_SPACE_RE = re.compile(r"^INST\? space (\d+) free of (\d+) bytes$")
INST_GEOM_RE = re.compile(r"^INST\? bsize (\d+) blocks (\d+) free (\d+)$")
VOL_STATUS_RE = re.compile(
    r"^VOL\? gated (\d+) active (\d+) dry (\d+) chunk (\d+) writable (\d+)$"
)
VOL_PART_RE = re.compile(r"^VOL\? partition (\d+) bytes \((\d+) MiB\)$")
VOL_DISK_RE = re.compile(
    r"^VOL\? DISK vh bsize (\d+) blocks (\d+) free (\d+)"
)
HFS_RE = re.compile(
    r"^KEBAB HFS: (\S+): block size (\d+), total blocks (\d+) "
    r"\((\d+) MiB\), free blocks (\d+)"
)
CHECK_LINE_PREFIX = "INST CHECK "
SKEL_STREAM_RE = re.compile(
    r"^INST STREAM v(\d+)( skeleton)? entries (\d+) bytes (\d+) wire (\d+)$")
SKEL_OK_RE = re.compile(
    r"^OK (/\S+) (\d+) (?:(dir)|link -> (\S+))? ?mode 0([0-7]+) "
    r"uid (\d+) gid (\d+)$")
SKEL_SKIP_RE = re.compile(r"^SKIP (/\S+) (\d+) link exists$")


class BringUpError(Exception):
    """A checkpoint failed, or the board did something unexpected."""

    kind = "failure"

    def __init__(
        self,
        checkpoint: str,
        expected: str,
        seen: str,
        hint: str = "",
        lines: Sequence[str] | None = None,
    ) -> None:
        super().__init__(seen)
        self.checkpoint = checkpoint
        self.expected = expected
        self.seen = seen
        self.hint = hint
        self.lines = list(lines or [])


class PreconditionError(BringUpError):
    kind = "precondition"


class PanicDetected(BringUpError):
    kind = "panic"


class ConsoleLost(BringUpError):
    kind = "console"


class DeviceRefused(BringUpError):
    kind = "device"


class DryRunError(BringUpError):
    kind = "dry-run"


# ---------------------------------------------------------------------------
# Logging
# ---------------------------------------------------------------------------


class RunLog:
    """stdout + run.log + console.log, with a bounded in-memory tail."""

    def __init__(self, log_dir: str, quiet: bool = False) -> None:
        self.dir = log_dir
        os.makedirs(log_dir, exist_ok=True)
        self.path = os.path.join(log_dir, "run.log")
        self.console_path = os.path.join(log_dir, "console.log")
        self._run = open(self.path, "w", encoding="utf-8", buffering=1)
        self._console = open(self.console_path, "w", encoding="utf-8", buffering=1)
        self.quiet = quiet
        self.lines: deque[str] = deque(maxlen=4000)
        self.start = time.time()

    def close(self) -> None:
        for f in (self._run, self._console):
            try:
                f.close()
            except OSError:
                pass

    def _stamp(self) -> str:
        return "%8.2f" % (time.time() - self.start)

    def info(self, msg: str) -> None:
        line = "[%s] %s" % (self._stamp(), msg)
        self._run.write(line + "\n")
        if not self.quiet:
            print(line, flush=True)

    def console(self, line: str) -> None:
        self.lines.append(line)
        self._console.write("%s %s\n" % (self._stamp(), line))
        if not self.quiet:
            print("  | " + line, flush=True)

    def raw_console(self, text: str) -> None:
        self._console.write("%s %s\n" % (self._stamp(), text))

    def tail(self, n: int = 30) -> list[str]:
        return list(self.lines)[-n:]

    def write_text(self, name: str, text: str) -> str:
        path = os.path.join(self.dir, name)
        with open(path, "w", encoding="utf-8") as f:
            f.write(text)
        return path


def sha256_file(path: str, progress: Callable[[int], None] | None = None) -> str:
    h = hashlib.sha256()
    total = 0
    with open(path, "rb") as f:
        while True:
            b = f.read(1 << 20)
            if not b:
                break
            h.update(b)
            total += len(b)
            if progress:
                progress(total)
    return h.hexdigest()


def human(n: int) -> str:
    if n >= 1 << 30:
        return "%.2f GiB" % (n / float(1 << 30))
    if n >= 1 << 20:
        return "%.1f MiB" % (n / float(1 << 20))
    if n >= 1 << 10:
        return "%.1f KiB" % (n / float(1 << 10))
    return "%d B" % n


# ---------------------------------------------------------------------------
# Artifact readers: KIST, VEXT and the flashed image's embedded boot args
# ---------------------------------------------------------------------------


@dataclass
class KistEntry:
    path: str
    mode: int
    flags: int
    wire: int
    size: int
    sha: bytes
    offset: int  # payload offset in the stream file
    kind: str = "file"  # "file", "dir" or "link" (KIST v2)
    target: str = ""  # symlink target, decoded by a deep read


@dataclass
class KistStream:
    path: str
    file_size: int
    entries: list[KistEntry]
    total_usize: int
    total_wire: int
    flags: int
    version: int = 1
    skeleton: bool = False
    file_sha256: str = ""

    @property
    def entry_count(self) -> int:
        return len(self.entries)

    def kinds(self) -> dict[str, int]:
        out = {"file": 0, "dir": 0, "link": 0}
        for entry in self.entries:
            out[entry.kind] += 1
        return out


def read_kist(path: str, deep: bool = False,
              progress: Callable[[str, int, int], None] | None = None) -> KistStream:
    """Parse a KIST v1 or v2 stream.

    v2 adds DIR entries (wire=size=0, all-zero hash) and SYMLINK entries (the
    payload is the target, wire=size=len(target)) in a stream whose flags carry
    KIST_SF_SKELETON.  The entry mode mask becomes 07777, so the skeleton's
    sticky 01777 survives instead of being masked to 0777.  v1 streams are
    still accepted unchanged, exactly like the kernel and the packer.

    Header-only is the runtime path (fast, enough to build INST CHECK
    manifests).  deep=True hashes every entry's uncompressed payload, checks
    deflate framing, decodes symlink targets and verifies the padding; it is
    the dry-run path and is also used for the 64 KiB skel.kist.
    """
    size = os.path.getsize(path)
    with open(path, "rb") as f:
        hdr = f.read(KIST_STREAM_HDR)
        if len(hdr) != KIST_STREAM_HDR:
            raise ValueError("%s: short KIST header" % path)
        magic, version, sflags, count, hsize, usize, wire = struct.unpack(
            "<4sHHIIQQ", hdr
        )
        if magic != KIST_MAGIC or not KIST_VERSION_MIN <= version <= KIST_VERSION \
                or hsize != KIST_STREAM_HDR:
            raise ValueError(
                "%s: not a KIST v1/v2 stream (magic=%r version=%d hsize=%d)"
                % (path, magic, version, hsize)
            )
        if size != KIST_STREAM_HDR + wire:
            raise ValueError(
                "%s: file is %d bytes, header declares %d"
                % (path, size, KIST_STREAM_HDR + wire)
            )
        if size % CHUNK:
            raise ValueError("%s: size %d is not a multiple of %d" % (path, size, CHUNK))
        if count > 100000:
            raise ValueError("%s: implausible entry count %d" % (path, count))
        if version == 1:
            if sflags & ~KIST_F_DEFLATE:
                raise ValueError("%s: v1 stream flags 0x%04x" % (path, sflags))
            skeleton = False
        else:
            if sflags & ~(KIST_F_DEFLATE | KIST_SF_SKELETON):
                raise ValueError("%s: unknown stream flags 0x%04x" % (path, sflags))
            skeleton = bool(sflags & KIST_SF_SKELETON)
        mode_mask = 0o777 if version == 1 else KIST_MODE_MASK

        entries: list[KistEntry] = []
        payload_end = KIST_STREAM_HDR
        for i in range(count):
            eh = f.read(KIST_ENTRY_HDR)
            if len(eh) != KIST_ENTRY_HDR:
                raise ValueError("%s: short entry header %d" % (path, i))
            (magic, plen, mode, eflags, rsvd, ewire, esize,
             digest, rawpath, rsvd2) = struct.unpack("<4sHHHHQQ32s128sI", eh)
            if magic != KENT_MAGIC:
                raise ValueError("%s: entry %d has magic %r" % (path, i, magic))
            if rsvd != 0 or rsvd2 != 0:
                raise ValueError("%s: entry %d reserved fields are not zero" % (path, i))
            if not 1 <= plen < 128:
                raise ValueError("%s: entry %d path length %d" % (path, i, plen))
            if plen > len(rawpath):
                raise ValueError("%s: entry %d path overruns header" % (path, i))
            epath = rawpath[:plen].decode("utf-8", "replace")
            if not epath.startswith("/"):
                raise ValueError("%s: entry %d destination %r is not absolute"
                                 % (path, i, epath))
            if ewire > wire:
                raise ValueError("%s: entry %d wire length exceeds the stream"
                                 % (path, i))
            etype = eflags & KIST_F_TYPE
            if eflags & ~(KIST_F_DEFLATE | KIST_F_TYPE):
                raise ValueError("%s: entry %d unknown flags 0x%04x"
                                 % (path, i, eflags))
            if etype == KIST_F_TYPE:
                raise ValueError("%s: entry %d is both a directory and a symlink"
                                 % (path, i))
            if etype and version < 2:
                raise ValueError("%s: entry %d needs a KIST v2 stream" % (path, i))
            if etype and not skeleton:
                raise ValueError("%s: entry %d is DIR/LINK outside a skeleton stream"
                                 % (path, i))
            kind = "dir" if etype == KIST_F_DIR else \
                "link" if etype == KIST_F_SYMLINK else "file"
            if etype and eflags & KIST_F_DEFLATE:
                raise ValueError("%s: entry %d deflates a %s" % (path, i, kind))
            if kind == "dir":
                if ewire != 0 or esize != 0 or digest != b"\0" * 32:
                    raise ValueError("%s: entry %d directory has a payload or hash"
                                     % (path, i))
            elif kind == "link":
                if ewire != esize or not 1 <= ewire < KIST_PATH_MAX:
                    raise ValueError("%s: entry %d symlink target length %d"
                                     % (path, i, ewire))
            elif ewire != esize and not (eflags & KIST_F_DEFLATE):
                raise ValueError(
                    "%s: entry %d stored payload %d != file size %d"
                    % (path, i, ewire, esize)
                )
            entry = KistEntry(path=epath, mode=mode & mode_mask, flags=eflags,
                              wire=ewire, size=esize, sha=digest,
                              offset=payload_end, kind=kind)
            entries.append(entry)

            if deep:
                if progress:
                    progress(epath, i + 1, count)
                if kind != "dir":
                    h = hashlib.sha256()
                    left = ewire
                    target = bytearray()
                    if kind == "file" and eflags & KIST_F_DEFLATE:
                        d = zlib.decompressobj()
                        produced = 0
                        while left:
                            b = f.read(min(1 << 20, left))
                            if not b:
                                raise ValueError("%s: short payload for %s"
                                                 % (path, epath))
                            left -= len(b)
                            out = d.decompress(b)
                            produced += len(out)
                            h.update(out)
                        out = d.flush()
                        produced += len(out)
                        h.update(out)
                        if not d.eof:
                            raise ValueError("%s: deflate stream for %s did not end"
                                             % (path, epath))
                        if produced != esize:
                            raise ValueError(
                                "%s: entry %s produced %d bytes, header says %d"
                                % (path, epath, produced, esize)
                            )
                    else:
                        while left:
                            b = f.read(min(1 << 20, left))
                            if not b:
                                raise ValueError("%s: short payload for %s"
                                                 % (path, epath))
                            left -= len(b)
                            h.update(b)
                            if kind == "link":
                                target += b
                    if h.digest() != digest:
                        raise ValueError("%s: payload hash mismatch for %s"
                                         % (path, epath))
                    if kind == "link":
                        entry.target = bytes(target).decode("utf-8")
            else:
                f.seek(ewire, os.SEEK_CUR)
            payload_end += KIST_ENTRY_HDR + ewire

        pad = size - payload_end
        if not (0 <= pad < CHUNK):
            raise ValueError("%s: %d bytes of padding, expected less than %d"
                             % (path, pad, CHUNK))
        if deep:
            tail = f.read()
            if len(tail) != pad or tail.strip(b"\0"):
                raise ValueError("%s: padding is not zero" % path)
            if sum(e.size for e in entries) != usize:
                raise ValueError(
                    "%s: entries total %d bytes, stream header says %d"
                    % (path, sum(e.size for e in entries), usize)
                )
        return KistStream(path=path, file_size=size, entries=entries,
                          total_usize=usize, total_wire=wire, flags=sflags,
                          version=version, skeleton=skeleton)


@dataclass
class VextExtent:
    offset: int
    length: int


@dataclass
class VextStream:
    path: str
    file_size: int
    volume_bytes: int
    block_size: int
    extents: list[VextExtent]
    data_bytes: int
    file_sha256: str = ""

    @property
    def extent_count(self) -> int:
        return len(self.extents)


def read_vext(path: str, deep: bool = False) -> VextStream:
    size = os.path.getsize(path)
    with open(path, "rb") as f:
        hdr = f.read(VEXT_HDR)
        if len(hdr) != VEXT_HDR:
            raise ValueError("%s: short VEXT header" % path)
        magic, version, hsize, volume, bsize, count, dbytes = struct.unpack(
            "<4sHHQIIQ", hdr
        )
        if magic != VEXT_MAGIC or version != VEXT_VERSION or hsize != VEXT_HDR:
            raise ValueError("%s: not a VEXT v1 stream" % path)
        if size % CHUNK:
            raise ValueError("%s: size %d is not a multiple of %d" % (path, size, CHUNK))
        extents: list[VextExtent] = []
        body = 0
        prev_end = 0
        for i in range(count):
            eh = f.read(VEXT_EXT_HDR)
            if len(eh) != VEXT_EXT_HDR:
                raise ValueError("%s: short extent header %d" % (path, i))
            off, length, rsvd = struct.unpack("<QII", eh)
            body += VEXT_EXT_HDR + length
            if rsvd:
                raise ValueError("%s: extent %d reserved field" % (path, i))
            if length == 0 or off % bsize or length % bsize:
                raise ValueError("%s: extent %d is not block aligned" % (path, i))
            if i == 0 and off != 0:
                raise ValueError("%s: first extent does not start at 0" % path)
            if off < prev_end or off > volume or length > volume - off:
                raise ValueError("%s: extent %d is out of order or outside the volume"
                                 % (path, i))
            prev_end = off + length
            extents.append(VextExtent(off, length))
            f.seek(length, os.SEEK_CUR)
        if sum(e.length for e in extents) != dbytes:
            raise ValueError("%s: extents carry %d bytes, header says %d"
                             % (path, sum(e.length for e in extents), dbytes))
        pad = size - VEXT_HDR - body
        if not (0 <= pad < CHUNK):
            raise ValueError("%s: %d bytes of padding" % (path, pad))
        if deep:
            tail = f.read()
            if len(tail) != pad or tail.strip(b"\0"):
                raise ValueError("%s: padding is not zero" % path)
        return VextStream(path=path, file_size=size, volume_bytes=volume,
                          block_size=bsize, extents=extents, data_bytes=dbytes)


def read_vext_volume_header(path: str) -> dict[str, int]:
    """Read the HFS+ header embedded at volume offset 1024."""
    vs = read_vext(path)
    first = vs.extents[0]
    if first.offset != 0 or first.length < 1536:
        raise ValueError("%s: first extent does not contain the volume header" % path)
    with open(path, "rb") as f:
        f.seek(VEXT_HDR + VEXT_EXT_HDR + 1024)
        vh = f.read(512)
    if len(vh) != 512:
        raise ValueError("%s: cannot read the embedded volume header" % path)
    sig, ver = struct.unpack_from(">HH", vh, 0)
    bsize, blocks, free = struct.unpack_from(">III", vh, 40)
    attrs = struct.unpack_from(">I", vh, 4)[0]
    journal = struct.unpack_from(">I", vh, 12)[0]
    return {"signature": sig, "version": ver, "block_size": bsize,
            "blocks": blocks, "free": free, "attributes": attrs,
            "journal": journal}


def extract_boot_args(path: str) -> list[str]:
    """Return the kernel argument strings embedded in a flashed image.

    The image is an Android boot image whose payload volume starts at
    0x800000; the payload's TOML entry for Payloads/kernel.macho contains
        args = "-v ... kebab_cs=15"
    The boot header itself has an empty cmdline, so this is the only place the
    phase arguments can be checked before flashing.
    """
    with open(path, "rb") as f:
        data = f.read()
    found: list[str] = []
    for m in re.finditer(rb'args = "([^"\r\n]*)"', data):
        try:
            found.append(m.group(1).decode("utf-8"))
        except UnicodeDecodeError:
            continue
    return found


@dataclass
class ImageInfo:
    path: str
    size: int
    kernel_size: int
    ramdisk_size: int
    page_size: int
    header_version: int
    args: list[str]
    args_tokens: list[set[str]]
    file_sha256: str = ""


def read_image(path: str, hash_file: bool = False) -> ImageInfo:
    size = os.path.getsize(path)
    if size < IMAGE_PAYLOAD_OFFSET:
        raise ValueError("%s: image is %d bytes, shorter than the payload offset"
                         % (path, size))
    with open(path, "rb") as f:
        hdr = f.read(64)
    if len(hdr) < 64 or hdr[:8] != b"ANDROID!":
        raise ValueError("%s: not an Android boot image" % path)
    kernel_size, kernel_addr, ramdisk_size = struct.unpack_from("<III", hdr, 8)
    page_size = struct.unpack_from("<I", hdr, 36)[0]
    header_version = struct.unpack_from("<I", hdr, 40)[0]
    if page_size not in (2048, 4096):
        raise ValueError("%s: implausible page size %d" % (path, page_size))
    if header_version not in (0, 1, 2):
        raise ValueError("%s: unsupported boot header version %d"
                         % (path, header_version))
    args = extract_boot_args(path)
    tokens = [set(a.split()) for a in args]
    return ImageInfo(path=path, size=size, kernel_size=kernel_size,
                     ramdisk_size=ramdisk_size, page_size=page_size,
                     header_version=header_version, args=args,
                     args_tokens=tokens,
                     file_sha256=sha256_file(path) if hash_file else "")


# ---------------------------------------------------------------------------
# Console line channel
# ---------------------------------------------------------------------------


class LineChannel:
    """Line-oriented channel with a cursor, condition variable and tail.

    Console subclasses own a reader thread; the fake used by the tests feeds
    lines synchronously.  Both share wait/collect so the phase code is the same
    in both cases.
    """

    def __init__(self, logger: RunLog) -> None:
        self.logger = logger
        self._cv = threading.Condition()
        self._lines: list[str] = []
        self._cursor = 0
        self._seq = 0
        self.connected = False
        self.epoch = 0
        self.reconnects = 0
        self._closed_at: float | None = None

    # -- producer side -----------------------------------------------------
    def feed(self, line: str) -> None:
        line = line.rstrip("\r")
        with self._cv:
            self._lines.append(line)
            self._seq += 1
            self.logger.console(line)
            self._cv.notify_all()

    def feed_host(self, text: str) -> None:
        self.feed("[host] " + text)

    def set_connected(self, on: bool) -> None:
        was = self.connected
        with self._cv:
            if on and not self.connected:
                self.epoch += 1
                self.reconnects += 1
                self._closed_at = None
            elif not on and self.connected:
                self._closed_at = time.time()
            self.connected = on
            self._cv.notify_all()
        if on and not was:
            self.feed_host("console reopened (epoch %d)" % self.epoch)

    # -- consumer side -----------------------------------------------------
    def mark(self) -> int:
        with self._cv:
            return self._seq

    def closed_for(self) -> float:
        with self._cv:
            if self.connected or self._closed_at is None:
                return 0.0
            return max(0.0, time.time() - self._closed_at)

    def since(self, mark: int) -> list[str]:
        with self._cv:
            return self._lines[mark:]

    def _advance_to_end(self) -> list[str]:
        out = self._lines[self._cursor:]
        self._cursor = len(self._lines)
        return out

    def collect(self, quiet: float = 0.4, max_time: float = 6.0,
                detector: "PanicDetector | None" = None) -> list[str]:
        """Return the lines that arrive until the channel is quiet."""
        deadline = time.time() + max_time
        quiet_deadline = None
        out: list[str] = []
        with self._cv:
            while time.time() < deadline:
                while self._cursor < len(self._lines):
                    line = self._lines[self._cursor]
                    self._cursor += 1
                    out.append(line)
                    quiet_deadline = time.time() + quiet
                    if detector:
                        detector.feed(line)
                if quiet_deadline is not None and time.time() >= quiet_deadline:
                    break
                if detector:
                    detector.check_link()
                self._cv.wait(min(0.1, max(0.0, deadline - time.time())))
        return out

    def wait(self, predicate: Callable[[str], bool], timeout: float,
             detector: "PanicDetector | None" = None,
             what: str = "line") -> str:
        deadline = time.time() + timeout
        with self._cv:
            while True:
                while self._cursor < len(self._lines):
                    line = self._lines[self._cursor]
                    self._cursor += 1
                    if detector:
                        detector.feed(line)
                    if predicate(line):
                        return line
                remaining = deadline - time.time()
                if remaining <= 0:
                    raise TimeoutError("timed out after %.1fs waiting for %s"
                                       % (timeout, what))
                if detector:
                    detector.check_link()
                self._cv.wait(min(0.1, remaining))

    def send(self, data: bytes) -> None:
        raise NotImplementedError

    def start(self) -> None:
        pass

    def stop(self) -> None:
        pass

    def close(self) -> None:
        self.stop()


if serial is not None:
    class SerialConsole(LineChannel):
        """CDC-ACM console reader that survives panic reboots.

        The reader thread is started before fastboot reboot and keeps trying to
        reopen the port.  Every line is logged with a timestamp, so the capture
        starts at the first byte the console emits, not at the first command the
        orchestration sends.
        """

        def __init__(self, logger: RunLog, port: str = "", baud: int = 115200,
                     preferred_port: str = "") -> None:
            super().__init__(logger)
            self.port = port or preferred_port
            self.baud = baud
            self._ser = None
            self._stop = threading.Event()
            self._thread: threading.Thread | None = None
            self._write_lock = threading.Lock()
            self._buf = b""
            self._last_error = ""
            self._open_attempts = 0

        def start(self) -> None:
            if self._thread and self._thread.is_alive():
                return
            self._stop.clear()
            self._thread = threading.Thread(target=self._reader, name="console",
                                            daemon=True)
            self._thread.start()

        def stop(self) -> None:
            self._stop.set()
            self._close_ser()
            t = self._thread
            if t:
                t.join(timeout=2.0)

        def _choose_port(self) -> str | None:
            if self.port:
                return self.port
            if list_ports is None:
                return None
            for p in list_ports.comports():
                if p.vid == VENDOR_ID and p.pid == PRODUCT_ID:
                    return p.device
            return None

        def _open_ser(self):
            port = self._choose_port()
            if not port:
                return None
            try:
                ser = serial.Serial(port, self.baud, timeout=0.1,
                                    write_timeout=30)
                self._last_error = ""
                return ser
            except Exception as exc:  # serial.SerialException, OSError
                self._last_error = str(exc)
                return None

        def _close_ser(self) -> None:
            with self._write_lock:
                ser, self._ser = self._ser, None
            if ser is not None:
                try:
                    ser.close()
                except Exception:
                    pass

        def _reader(self) -> None:
            while not self._stop.is_set():
                if self._ser is None:
                    self._open_attempts += 1
                    ser = self._open_ser()
                    if ser is None:
                        if self.connected:
                            self.set_connected(False)
                        time.sleep(0.3)
                        continue
                    with self._write_lock:
                        self._ser = ser
                    self._buf = b""
                    self.set_connected(True)
                    self.feed_host("console %s opened (attempt %d)"
                                   % (self._choose_port(), self._open_attempts))
                try:
                    ser = self._ser
                    if ser is None:
                        continue
                    n = getattr(ser, "in_waiting", 0)
                    data = ser.read(n if n else 1)
                except Exception as exc:
                    self._last_error = str(exc)
                    self._close_ser()
                    self.set_connected(False)
                    self.feed_host("console closed: %s" % exc)
                    continue
                if data:
                    self._buf += data
                    while b"\n" in self._buf:
                        raw, self._buf = self._buf.split(b"\n", 1)
                        line = raw.decode("utf-8", "replace").rstrip("\r")
                        if line:
                            self.feed(line)
                    if len(self._buf) > 65536:
                        self._buf = self._buf[-4096:]

        def send(self, data: bytes) -> None:
            if not self.connected or self._ser is None:
                # A command sent before the port enumerates is a host bug, not
                # a reason to sleep forever; give the reader a short grace.
                deadline = time.time() + 5.0
                while time.time() < deadline and not self.connected:
                    time.sleep(0.05)
            with self._write_lock:
                ser = self._ser
                if ser is None:
                    raise ConsoleLost(
                        "console", "a writable CDC-ACM port",
                        "port %s is not open (%s)"
                        % (self._choose_port(), self._last_error or "no device"),
                        hint="the kernel may be halted or in fastboot; use "
                             "fastboot devices to distinguish the states",
                    )
                try:
                    ser.write(data)
                    ser.flush()
                except Exception as exc:
                    self._close_ser()
                    self.set_connected(False)
                    raise ConsoleLost(
                        "console", "the command or chunk to reach the device",
                        "serial write failed: %s" % exc,
                        hint="a CDC-ACM port can exist while the kernel is "
                             "halted; check fastboot devices",
                    )
else:
    class SerialConsole(LineChannel):  # pragma: no cover - import-time shim
        def __init__(self, *a, **kw):
            raise RuntimeError("pyserial is not installed")


# ---------------------------------------------------------------------------
# Panic / reboot detection
# ---------------------------------------------------------------------------


class PanicDetector:
    """Feed every console line here while a phase is in a known-good state.

    With patch 0134 a panic that cannot be serviced reboots through PSCI.  A
    fresh boot banner after readiness, or the console re-enumerating, is
    therefore a panic report, not a timeout.
    """

    def __init__(self, console: LineChannel, armed: bool = False,
                 phase: str = "") -> None:
        self.console = console
        self.armed = armed
        self.phase = phase
        self.epoch = console.epoch
        self.connected = console.connected

    def arm(self) -> None:
        self.armed = True
        self.epoch = self.console.epoch

    def check_link(self) -> None:
        if not self.armed:
            return
        gone = self.console.closed_for()
        if gone > 10.0:
            raise PanicDetected(
                self.phase, "no panic and no reboot while %s is running"
                % self.phase,
                "console has been gone for %.0fs" % gone,
                hint="with 0134 a panic reboots and the CDC-ACM port comes "
                     "back; a port that stays gone is a reset/hang, not a slow "
                     "ACK",
            )

    def feed(self, line: str) -> None:
        if line.startswith("[host]"):
            if self.armed and "console reopened" in line:
                raise PanicDetected(
                    self.phase, "no panic and no reboot while %s is running"
                    % self.phase,
                    "console re-enumerated: %s" % line,
                    hint="with 0134 a panic reboots; a reconnect after the "
                         "phase became ready is a reboot, not a slow ACK",
                )
            return
        if PANIC_RE.search(line):
            raise PanicDetected(
                self.phase, "no panic while this checkpoint is running",
                "panic line: %s" % line,
                hint="patch 0134 makes an unserviced panic reboot; the console "
                     "will re-enumerate and the next boot starts again",
            )
        if FAIL_RE.search(line):
            raise DeviceRefused(
                self.phase, "the device not to refuse or time out",
                "device line: %s" % line,
                hint="the kernel prints the reason after FAIL/ABORT; do not "
                     "retry the same stream blindly",
            )
        if self.armed and BOOT_BANNER_RE.search(line):
            raise PanicDetected(
                self.phase, "no new boot banner after the phase became ready",
                "new boot banner: %s" % line,
                hint="the board rebooted mid-phase (panic recovery or reset); "
                     "this is not a transfer timeout",
            )
        if self.armed and self.console.epoch != self.epoch:
            self.epoch = self.console.epoch
            if self.console.connected:
                raise PanicDetected(
                    self.phase, "the console to stay up for the whole phase",
                    "console re-enumerated (epoch %d)" % self.epoch,
                    hint="with 0134 a panic reboots; the reconnect is the "
                         "reboot, not a slow ACK",
                )


# ---------------------------------------------------------------------------
# Fastboot
# ---------------------------------------------------------------------------


class Fastboot:
    def __init__(self, exe: str, logger: RunLog, serial_no: str = "",
                 timeout: float = 300.0) -> None:
        self.exe = exe
        self.logger = logger
        self.serial_no = serial_no
        self.timeout = timeout

    def _run(self, args: Sequence[str], timeout: float | None = None,
             ok_codes: Sequence[int] = (0,)) -> tuple[int, str]:
        cmd = [self.exe]
        if self.serial_no:
            cmd += ["-s", self.serial_no]
        cmd += list(args)
        self.logger.info("+ " + " ".join(cmd))
        try:
            proc = subprocess.run(
                cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                timeout=timeout or self.timeout, text=True, errors="replace",
            )
        except FileNotFoundError:
            raise PreconditionError(
                "precondition.fastboot", "the fastboot executable at %s" % self.exe,
                "FileNotFoundError", hint="install platform-tools or pass --fastboot",
            )
        except subprocess.TimeoutExpired:
            raise PreconditionError(
                "precondition.fastboot", "fastboot to answer within %.0fs"
                % (timeout or self.timeout), "command timed out",
                hint="the board may be stranded; a physical power cycle is needed",
            )
        out = proc.stdout or ""
        self.logger.info("  rc=%d %s" % (proc.returncode, out.strip().replace("\n", "\n  ")))
        if proc.returncode not in ok_codes:
            raise PreconditionError(
                "precondition.fastboot", "fastboot %s to succeed" % " ".join(args),
                "rc=%d: %s" % (proc.returncode, out.strip()),
            )
        return proc.returncode, out

    def devices(self) -> list[str]:
        _, out = self._run(["devices"], timeout=15)
        found = []
        for line in out.splitlines():
            line = line.strip()
            if not line or line.startswith("List of devices"):
                continue
            parts = line.split()
            if len(parts) >= 2 and parts[-1] == "fastboot":
                found.append(parts[0])
        return found

    def require_device(self) -> str:
        found = self.devices()
        if not found:
            raise PreconditionError(
                "precondition.fastboot", "fastboot devices to list the board",
                "no device in fastboot mode",
                hint="the CDC-ACM port may still enumerate while the kernel is "
                     "halted; only fastboot devices distinguishes the states",
            )
        if self.serial_no:
            if self.serial_no not in found:
                raise PreconditionError(
                    "precondition.fastboot", "serial %s in fastboot devices"
                    % self.serial_no, "found %s" % ", ".join(found),
                )
            return self.serial_no
        self.serial_no = found[0]
        if len(found) > 1:
            self.logger.info("note: multiple fastboot devices, using %s" % found[0])
        return found[0]

    def getvar(self, name: str) -> str:
        _, out = self._run(["getvar", name], timeout=20)
        return out

    def current_slot(self) -> str:
        out = self.getvar("current-slot")
        m = re.search(r"current-slot:\s*([ab])", out)
        if not m:
            m = re.search(r"current-slot:\s*(\S+)", out)
        if not m:
            raise PreconditionError(
                "precondition.slot", "fastboot getvar current-slot to report a slot",
                out.strip() or "(no output)",
            )
        return m.group(1).strip()

    def product(self) -> str:
        out = self.getvar("product")
        m = re.search(r"product:\s*(\S+)", out)
        return m.group(1) if m else ""

    def flash(self, partition: str, image: str) -> str:
        if not os.path.isfile(image):
            raise PreconditionError(
                "precondition.artifacts", "image %s to exist" % image, "missing",
            )
        _, out = self._run(["flash", partition, image], timeout=600)
        low = out.lower()
        if "failed" in low or "error" in low:
            raise PreconditionError(
                "precondition.flash", "fastboot flash %s to report OKAY" % partition,
                out.strip(),
            )
        if "okay" not in low:
            raise PreconditionError(
                "precondition.flash", "fastboot flash %s to report OKAY" % partition,
                out.strip(),
            )
        return out

    def reboot(self) -> str:
        _, out = self._run(["reboot"], timeout=60)
        return out


# ---------------------------------------------------------------------------
# Configuration, checkpoints and the orchestration object
# ---------------------------------------------------------------------------


@dataclass
class Options:
    from_phase: str = "auto"
    to_phase: str = "final"
    dry_run: bool = False
    work_dir: str = "work/p1"
    log_dir: str = ""
    fastboot: str = ""
    port: str = ""
    baud: int = 115200
    vol_image: str = ""
    install_image: str = ""
    final_image: str = ""
    volume_stream: str = ""
    skel_stream: str = ""
    base_stream: str = ""
    cache_stream: str = ""
    force_vol: bool = False
    no_precheck: bool = False
    allow_kdp: bool = False
    image_size: int = IMAGE_BYTES
    volume_bytes: int = VOLUME_BYTES
    volume_blocks: int = VOLUME_BLOCKS
    expected_vol_stream: int = VOLUME_STREAM_BYTES
    expected_vol_extents: int = VOLUME_STREAM_EXTENTS
    expected_vol_data: int = VOLUME_STREAM_DATA
    expected_base_entries: int = BASE_ENTRIES
    expected_cache_entries: int = CACHE_ENTRIES
    boot_timeout: float = 120.0
    phase_timeout: float = 180.0
    final_timeout: float = 180.0
    quiet: bool = False
    keep_going: bool = False
    json_summary: str = ""

    def resolve(self) -> "Options":
        root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        work = self.work_dir
        if not os.path.isabs(work):
            work = os.path.join(root, work)
        self.work_dir = os.path.abspath(work)
        if not self.log_dir:
            self.log_dir = os.path.join(
                self.work_dir, "logs",
                "run-" + time.strftime("%Y%m%d-%H%M%S"))
        elif not os.path.isabs(self.log_dir):
            self.log_dir = os.path.join(root, self.log_dir)
        def pick(value: str, name: str) -> str:
            if value:
                return value if os.path.isabs(value) else os.path.join(root, value)
            return os.path.join(self.work_dir, name)
        self.vol_image = pick(self.vol_image, "boot-vol.img")
        self.install_image = pick(self.install_image, "boot-inst.img")
        self.final_image = pick(self.final_image, "boot-final.img")
        self.volume_stream = pick(self.volume_stream, "KebabRoot.vext")
        self.skel_stream = pick(self.skel_stream, "skel.kist")
        self.base_stream = pick(self.base_stream, "base.kist")
        self.cache_stream = pick(self.cache_stream, "cache.kist")
        if not self.fastboot:
            self.fastboot = FASTBOOT_DEFAULT if os.name == "nt" else "fastboot"
        if not self.port:
            self.port = PORT_DEFAULT if os.name == "nt" else "/dev/ttyACM0"
        return self

    def image_for(self, phase: str) -> str:
        return {"vol": self.vol_image, "install": self.install_image,
                "final": self.final_image}[phase]


@dataclass
class Checkpoint:
    cid: str
    name: str
    expected: str
    ok: bool = True
    seen: str = ""
    phase: str = ""
    elapsed: float = 0.0


class Orchestrator:
    def __init__(self, opts: Options, logger: RunLog,
                 fastboot: Fastboot | None = None,
                 console: LineChannel | None = None,
                 artifacts: dict[str, Any] | None = None) -> None:
        self.opts = opts
        self.log = logger
        self.fastboot = fastboot
        self.console = console
        self.artifacts = artifacts or {}
        self.checkpoints: list[Checkpoint] = []
        self.phase = "start"
        self.volume_imprinted: bool | None = None
        self.volume_probe: str = ""
        self.streams: dict[str, KistStream] = {}
        self.detector: PanicDetector | None = None
        self.start_phase = opts.from_phase
        self._auto_vol_done = False
        self.start_time = time.time()
        self.summary: dict[str, Any] = {"phases": [], "checkpoints": []}

    # -- reporting ---------------------------------------------------------
    def checkpoint(self, cid: str, name: str, expected: str, seen: str = "",
                   ok: bool = True, elapsed: float = 0.0) -> None:
        cp = Checkpoint(cid=cid, name=name, expected=expected, ok=ok,
                        seen=seen, phase=self.phase, elapsed=elapsed)
        self.checkpoints.append(cp)
        self.summary["checkpoints"].append({
            "id": cid, "name": name, "phase": self.phase, "ok": ok,
            "expected": expected, "seen": seen, "elapsed": round(elapsed, 3),
        })
        if ok:
            self.log.info("[ OK ] %s %-24s %s" % (cid, name, seen or expected))
        else:
            self.log.info("[FAIL] %s %-24s expected=%s seen=%s"
                          % (cid, name, expected, seen))

    def phase_start(self, phase: str, detail: str) -> None:
        self.phase = phase
        self.summary["phases"].append({"phase": phase, "detail": detail,
                                       "start": round(time.time() - self.start_time, 3)})
        self.log.info("")
        self.log.info("== phase %s: %s" % (phase, detail))

    def phase_done(self, phase: str) -> None:
        for row in reversed(self.summary["phases"]):
            if row["phase"] == phase and "end" not in row:
                row["end"] = round(time.time() - self.start_time, 3)
                break

    def fail(self, cid: str, expected: str, seen: str, hint: str = "",
             cls: type[BringUpError] = BringUpError) -> None:
        raise cls(cid, expected, seen, hint=hint, lines=self.log.tail(30))

    # -- console helpers ---------------------------------------------------
    def send_cmd(self, data: bytes, what: str) -> None:
        self.log.info("-> %s" % what or data.decode("ascii", "replace").strip())
        self.console.send(data)

    def collect(self, quiet: float = 0.35, max_time: float = 8.0) -> list[str]:
        return self.console.collect(quiet=quiet, max_time=max_time,
                                    detector=self.detector)

    def wait(self, pred: Callable[[str], bool], timeout: float,
             what: str) -> str:
        return self.console.wait(pred, timeout, detector=self.detector, what=what)

    def probe_imem(self, attempts: int = 30, timeout: float = 2.0) -> str:
        last = ""
        for _ in range(attempts):
            try:
                self.send_cmd(CMD_IMEM, "IMEM? probe")
                line = self.wait(lambda l: l.startswith("KEBAB IMEM:"), timeout,
                                 "KEBAB IMEM: response")
                return line
            except TimeoutError as exc:
                last = str(exc)
        self.fail("precondition.console", "the kernel console to answer IMEM?",
                  last or "no answer", hint="the PnP port can exist while the "
                  "kernel is halted; use fastboot devices to distinguish states")
        raise AssertionError("unreachable")

    def query_inst(self) -> dict[str, Any] | None:
        self.send_cmd(CMD_INST_STATUS, "INST? status")
        lines = self.collect(quiet=0.4, max_time=8.0)
        if any("INST disabled" in l for l in lines):
            return None
        info: dict[str, Any] = {"lines": lines}
        for line in lines:
            m = INST_STATUS_RE.match(line)
            if m:
                info.update(gated=int(m.group(1)), active=int(m.group(2)),
                            dry=int(m.group(3)), root=m.group(4),
                            chunk=int(m.group(5)))
            m = INST_SPACE_RE.match(line)
            if m:
                info["free"] = int(m.group(1))
                info["total"] = int(m.group(2))
            m = INST_GEOM_RE.match(line)
            if m:
                info["bsize"] = int(m.group(1))
                info["blocks"] = int(m.group(2))
                info["bavail"] = int(m.group(3))
        if "blocks" not in info:
            return None
        return info

    def query_vol(self) -> dict[str, Any] | None:
        self.send_cmd(CMD_VOL_STATUS, "VOL? status")
        lines = self.collect(quiet=0.4, max_time=8.0)
        if any("VOL disabled" in l for l in lines):
            return None
        info: dict[str, Any] = {"lines": lines}
        for line in lines:
            m = VOL_STATUS_RE.match(line)
            if m:
                info.update(gated=int(m.group(1)), active=int(m.group(2)),
                            dry=int(m.group(3)), chunk=int(m.group(4)),
                            writable=int(m.group(5)))
            m = VOL_DISK_RE.match(line)
            if m:
                info.update(bsize=int(m.group(1)), blocks=int(m.group(2)),
                            bavail=int(m.group(3)))
        if "blocks" not in info:
            return None
        return info

    # -- boot helpers ------------------------------------------------------
    def ensure_fastboot(self, phase: str) -> None:
        """Make sure the board is in the bootloader before flashing.

        A phase normally ends in the kernel: the vol image is left running so
        the new volume can be mounted, and the install image is left running so
        the streams can be sent.  The next flash therefore needs the console
        FASTBOOT command, not an assumed state.  fastboot devices is the only
        trusted check; a PnP console can exist while the kernel is halted.
        """
        try:
            found = self.fastboot.devices()
        except PreconditionError:
            found = []
        if found:
            self.fastboot.require_device()
            return
        self.detector = None
        self.log.info("board is not in fastboot; sending FASTBOOT through the "
                      "console before flashing %s" % phase)
        self.send_cmd(CMD_FASTBOOT, "FASTBOOT")
        try:
            self.wait(lambda l: l.startswith("KEBAB FASTBOOT: IMEM read back"),
                      15, "KEBAB FASTBOOT: IMEM read back")
        except TimeoutError:
            self.fail("precondition.fastboot",
                      "FASTBOOT to write the PON reason and the IMEM cookie",
                      "no FASTBOOT confirmation; last lines: %s"
                      % " | ".join(self.log.tail(5)),
                      hint="FASTBOOT needs kebab_pon_write=1; if the console "
                           "said 'disabled', this kernel cannot enter fastboot",
                      cls=PreconditionError)
        deadline = time.time() + 40
        while time.time() < deadline:
            try:
                found = self.fastboot.devices()
            except PreconditionError:
                found = []
            if found:
                self.fastboot.require_device()
                self.checkpoint("C-REENTER", "precondition.reenter",
                                "console FASTBOOT reaches the bootloader",
                                found[0])
                return
            time.sleep(0.5)
        self.fail("precondition.fastboot",
                  "the board to enumerate in fastboot after FASTBOOT",
                  "no fastboot device within 40s",
                  hint="if the console printed 'disabled', kebab_pon_write=1 "
                       "was not on the running image",
                  cls=PreconditionError)

    def boot(self, phase: str, image: str, required_args: set[str],
             noinit: bool, boot_timeout: float = 0.0,
             forbidden_args: set[str] | None = None) -> int:
        """Flash boot_b, reboot, and return the console mark after readiness."""
        self.phase_start(phase, os.path.basename(image))
        self.detector = None
        if not os.path.isfile(image):
            self.fail("precondition.artifacts", "image %s to exist" % image,
                      "missing", hint="put the phase image in --work-dir or pass "
                      "--%s-image" % ("vol" if phase == "vol" else phase))
        self.require_args(image, required_args, forbidden_args or set())
        if self.opts.dry_run:
            return 0
        self.ensure_fastboot(phase)
        self.fastboot.flash("boot_b", image)
        self.fastboot.reboot()
        mark = self.console.mark()
        # The reader was already running, so the boot log is captured from the
        # first byte the CDC-ACM console emits after reset.
        timeout = boot_timeout or self.opts.boot_timeout
        banner = ""
        try:
            banner = self.wait(lambda l: "KEBAB USB: event thread started" in l
                               or "KEBAB USB: running (DSTS" in l, timeout,
                               "USB console startup")
            if "KEBAB USB: running (DSTS" in banner:
                # kebab_usb_start() prints "running" and then "event thread
                # started"; both belong to this boot, and the panic detector
                # must not mistake the second line for a second boot.
                banner = self.wait(
                    lambda l: "KEBAB USB: event thread started" in l, 15,
                    "USB event thread startup")
        except TimeoutError:
            # The USB console may enumerate after its startup lines are printed.
            # IMEM? is the authoritative liveness probe; a fresh boot banner is
            # preferred evidence, but silence with a working console is still a
            # usable boot, while silence with no console answer is not.
            self.probe_imem(attempts=10, timeout=2.0)
            self.checkpoint("C-BOOT-BANNER", "boot.banner",
                            "a boot banner or a live IMEM? console",
                            "startup banner missed; IMEM? answers",
                            elapsed=0)
        if banner:
            self.checkpoint("C-BOOT-BANNER", "boot.banner",
                            "a kernel boot banner on the new image", banner,
                            elapsed=0)
        if noinit:
            try:
                line = self.wait(lambda l: l.startswith("KEBAB: kebab_noinit=1:"),
                                 timeout, "kebab_noinit=1 marker")
                self.checkpoint("C-BOOT-NOINIT-" + phase.upper(), "boot.noinit",
                                "KEBAB: kebab_noinit=1 parks init", line, elapsed=0)
            except TimeoutError:
                self.fail("boot.noinit", "KEBAB: kebab_noinit=1: staying in the "
                          "kernel, no init program",
                          "no kebab_noinit marker within %.0fs" % timeout,
                          hint="the image was built without kebab_noinit=1")
            # The noinit line is only printed once init runs, which is after the
            # USB console is live; probe a command path before the phase starts.
            self.probe_imem()
        self.detector = PanicDetector(self.console, armed=False, phase=phase)
        return mark

    def arm_panic_detector(self, phase: str) -> None:
        if self.detector is None:
            self.detector = PanicDetector(self.console, armed=False, phase=phase)
        self.detector.phase = phase
        self.detector.arm()

    # -- status probes -----------------------------------------------------
    def probe_volume_state(self) -> None:
        """Record whether the running root is the 16 GiB volume.

        INST? is tried first (it reports f_blocks directly), then VOL? (which
        reads the on-disk HFS+ header) because VOL-image boots have
        kebab_install=1 but the install image may not have kebab_vol=1.
        """
        info = self.query_inst()
        source = "INST?"
        if info is None:
            info = self.query_vol()
            source = "VOL?"
        if info is None:
            self.volume_imprinted = None
            self.volume_probe = "no INST?/VOL? geometry while the console answers"
            self.log.info("volume probe: unknown (%s)" % self.volume_probe)
            return
        blocks = int(info.get("blocks", 0))
        self.volume_probe = "%s reports %d blocks" % (source, blocks)
        self.volume_imprinted = blocks >= self.opts.volume_blocks // 2
        self.log.info("volume probe: %s -> %s" % (
            self.volume_probe,
            "imprinted" if self.volume_imprinted else "not imprinted"))

    # -- streams -----------------------------------------------------------
    @staticmethod
    def skel_shape_problem(st: KistStream) -> str:
        """Return why st is not the fixed skel.kist shape, or "" when it is."""
        if st.version != 2 or not st.skeleton:
            return ("%s is KIST v%d%s, not a v2 skeleton stream"
                    % (st.path, st.version,
                       " skeleton" if st.skeleton else ""))
        kinds = st.kinds()
        if (st.entry_count, kinds["file"], kinds["dir"], kinds["link"]) != \
                (SKEL_ENTRIES, SKEL_FILES, SKEL_DIRS, SKEL_LINKS):
            return ("%s has %d entries (%d files, %d dirs, %d links), wanted %d "
                    "(%d files, %d dirs, %d links)"
                    % (st.path, st.entry_count, kinds["file"], kinds["dir"],
                       kinds["link"], SKEL_ENTRIES, SKEL_FILES, SKEL_DIRS,
                       SKEL_LINKS))
        if st.total_usize != SKEL_INSTALLED:
            return ("%s installs %d bytes, wanted %d (file contents plus symlink "
                    "target lengths)" % (st.path, st.total_usize, SKEL_INSTALLED))
        if st.file_size != SKEL_WIRE:
            return ("%s is %d bytes on the wire, wanted %d"
                    % (st.path, st.file_size, SKEL_WIRE))
        return ""

    def load_streams(self, need_base: bool, need_cache: bool,
                     need_skel: bool = False) -> None:
        for key, path, entries, label, deep in (
            ("base", self.opts.base_stream, self.opts.expected_base_entries,
             "base", False),
            ("cache", self.opts.cache_stream, self.opts.expected_cache_entries,
             "cache", False),
            ("skel", self.opts.skel_stream, SKEL_ENTRIES, "skel", True),
        ):
            need = {"base": need_base, "cache": need_cache, "skel": need_skel}[key]
            if not need:
                continue
            if not os.path.isfile(path):
                self.fail("precondition.artifacts", "%s stream %s to exist"
                          % (label, path), "missing",
                          hint="build it with scripts/make-minimal-userland.py")
            try:
                st = read_kist(path, deep=deep)
            except ValueError as exc:
                self.fail("precondition.artifacts",
                          "%s to be a valid KIST v1/v2 stream" % path, str(exc))
                raise AssertionError("unreachable")
            if key == "skel":
                problem = self.skel_shape_problem(st)
                if problem:
                    self.fail("precondition.artifacts",
                              "skel.kist to be the 16-entry v2 skeleton",
                              problem,
                              hint="rebuild it with "
                                   "scripts/make-minimal-userland.py --only skel")
            elif entries and st.entry_count != entries:
                self.fail("precondition.artifacts",
                          "%s to carry %d entries" % (label, entries),
                          "%d entries" % st.entry_count,
                          hint="the minimal-userland inventory changed; rebuild "
                               "or override --expected-%s-entries" % label)
            self.streams[key] = st
            self.log.info("loaded %s: %d entries, %s installed, %s on the wire, "
                          "sha256 %s" % (label, st.entry_count,
                                         human(st.total_usize), human(st.file_size),
                                         st.file_sha256 or "(not hashed)"))

    def check_entries(self, stream: KistStream, label: str,
                      only: Iterable[KistEntry] | None = None,
                      phase_name: str = "") -> dict[str, dict[str, Any]]:
        entries = list(only) if only is not None else stream.entries
        results: dict[str, dict[str, Any]] = {}
        total = len(entries)
        if self.detector:
            self.detector.phase = phase_name or ("precheck." + label)
        for i, e in enumerate(entries, 1):
            self.log.info("INST CHECK [%d/%d] %s" % (i, total, e.path))
            self.send_cmd((CHECK_LINE_PREFIX + e.path + "\n").encode("utf-8"),
                          "INST CHECK")
            try:
                line = self.wait(
                    lambda l, p=e.path: l.startswith("INST CHECK " + p + " ")
                    and (l.endswith("MISSING") or l.endswith("refused")
                         or l.endswith("no buffer") or " ERROR " in l
                         or re.search(r" \d+ [0-9a-f]{64}$", l) is not None),
                    self.opts.phase_timeout, "INST CHECK %s" % e.path)
            except TimeoutError:
                self.fail("verify.%s" % label, "INST CHECK %s to answer" % e.path,
                          "no answer within %.0fs" % self.opts.phase_timeout)
                raise AssertionError("unreachable")
            rest = line[len("INST CHECK " + e.path + " "):]
            if rest == "MISSING":
                results[e.path] = {"state": "missing", "line": line}
            elif rest.startswith("ERROR") or rest in ("refused", "no buffer"):
                results[e.path] = {"state": "error", "line": line}
            else:
                parts = rest.split()
                got_size = int(parts[0])
                got_sha = parts[1].lower()
                match = got_size == e.size and got_sha == e.sha.hex()
                results[e.path] = {"state": "same" if match else "different",
                                   "line": line, "size": got_size,
                                   "sha": got_sha}
        return results

    def send_stream(self, stream: KistStream, kind: str,
                    label: str = "") -> dict[str, Any]:
        """Send one KIST stream with the kernel's 64 KiB ACK flow control."""
        label = label or kind
        if self.detector:
            self.detector.phase = "send." + label
        size = stream.file_size
        if kind == "inst":
            cmd = b"INST BEGIN %d\n" % size
            begin_re = INST_BEGIN_RE
            ack_re = INST_ACK_RE
            done_re = INST_DONE_RE
        else:
            raise ValueError(kind)
        self.send_cmd(cmd, "INST BEGIN %d" % size)
        try:
            line = self.wait(lambda l: begin_re.match(l) is not None,
                             self.opts.phase_timeout, "INST BEGIN response")
        except TimeoutError:
            self.fail("send.begin", "INST BEGIN %d write chunk %d" % (size, CHUNK),
                      "no INST BEGIN within %.0fs" % self.opts.phase_timeout,
                      hint="check that kebab_install=1 and rootrw=1 are on the "
                           "running image")
            raise AssertionError("unreachable")
        m = begin_re.match(line)
        assert m
        if int(m.group(1)) != size or m.group(2) != "write" or int(m.group(3)) != CHUNK:
            self.fail("send.begin", "INST BEGIN %d write chunk %d" % (size, CHUNK),
                      line)
        sent = 0
        start = time.time()
        last_report = start
        with open(stream.path, "rb") as f:
            while sent < size:
                data = f.read(CHUNK)
                if len(data) != CHUNK:
                    self.fail("send.data", "a full %d-byte chunk" % CHUNK,
                              "stream ended at %d of %d bytes" % (sent, size))
                self.console.send(data)
                sent += len(data)
                target = sent
                while True:
                    try:
                        ack = self.wait(lambda l: ack_re.match(l) is not None,
                                        self.opts.phase_timeout, "INST ACK")
                    except TimeoutError:
                        self.fail("send.ack", "INST ACK %d" % target,
                                  "no ACK after %.0fs" % self.opts.phase_timeout,
                                  hint="the kernel's watchdog prints INST TIMEOUT "
                                       "after 60 s without a chunk; a silent "
                                       "port usually means the kernel reset")
                        raise AssertionError("unreachable")
                    acked = int(ack_re.match(ack).group(1))
                    if acked >= target:
                        break
                now = time.time()
                if now - last_report >= 5.0:
                    rate = sent / max(1e-6, now - start) / (1 << 20)
                    self.log.info("  %6.1f / %6.1f MiB  %5.2f MiB/s  %.0fs"
                                  % (sent / (1 << 20), size / (1 << 20), rate,
                                     now - start))
                    last_report = now
        try:
            done = self.wait(lambda l: done_re.match(l) is not None
                             or l.startswith("INST DONE with"), 600,
                             "INST DONE")
        except TimeoutError:
            self.fail("send.done", "INST DONE entries %d ..." % stream.entry_count,
                      "no INST DONE after the last chunk",
                      hint="the last ACK arrived but the kernel did not finish "
                           "the stream")
            raise AssertionError("unreachable")
        if done.startswith("INST DONE with"):
            self.fail("send.done", "all entries to be written", done)
        m = done_re.match(done)
        if not m:
            self.fail("send.done", "INST DONE entries ...", done)
        entries, ok, skipped, nbytes, wire, ms = (int(x) for x in m.groups())
        elapsed = (time.time() - start)
        if entries != stream.entry_count or ok + skipped != entries:
            self.fail("send.done", "%d entries all accounted for" % stream.entry_count,
                      done)
        if nbytes != stream.total_usize:
            self.fail("send.done", "bytes %d" % stream.total_usize, done)
        if wire != size:
            self.fail("send.done", "wire %d" % size, done)
        self.checkpoint("C-SEND-" + label.upper(), "send." + label,
                        "INST DONE entries %d ok %d skipped %d" % (
                            entries, ok, skipped),
                        "%s (%d ok, %d skipped, %.1fs)" % (
                            done, ok, skipped, elapsed), elapsed=elapsed)
        return {"entries": entries, "ok": ok, "skipped": skipped,
                "bytes": nbytes, "wire": wire, "ms": ms, "elapsed": elapsed,
                "done": done}

    def verify_entries(self, stream: KistStream, label: str,
                       sample: Sequence[KistEntry]) -> None:
        results = self.check_entries(stream, label, only=sample,
                                     phase_name="verify." + label)
        bad = {p: r for p, r in results.items() if r["state"] != "same"}
        if bad:
            first = next(iter(bad.items()))
            self.fail("verify.%s" % label,
                      "every sampled destination to have the KIST size and SHA-256",
                      "%d/%d differ: %s -> %s" % (len(bad), len(sample),
                                                  first[0], first[1]["line"]),
                      hint="the stream was accepted but the read-back differs; "
                           "do not continue to the next phase")
        self.checkpoint("C-VERIFY-" + label.upper(), "verify." + label,
                        "%d sampled destinations match size and SHA-256" % len(sample),
                        "%d/%d identical" % (len(sample), len(sample)))

    # -- run phases --------------------------------------------------------
    def run(self) -> None:
        if self.opts.dry_run:
            self.run_dry()
            return
        self.console.start()
        try:
            self.precondition()
            self.choose_start()
            self.load_streams(need_base=self.want_phase("base"),
                              need_cache=self.want_phase("cache"),
                              need_skel=self.want_phase("skel"))
            if self.want_phase("vol"):
                self.phase_vol()
            # Every path that reaches skel/base/cache/final must boot the
            # install image first, even when --from starts after it.  The only
            # case with no install boot is --from final, and --to vol stops
            # early.
            if self.start_phase != "final" and self.opts.to_phase != "vol":
                self.phase_install()
            if self.want_phase("skel"):
                self.phase_skel()
            if self.want_phase("base"):
                self.phase_userland("base")
            if self.want_phase("cache"):
                self.phase_userland("cache")
            if self.want_phase("final"):
                self.phase_final()
            self.finish()
        finally:
            self.console.stop()

    def want_phase(self, phase: str) -> bool:
        order = ["vol", "install", "skel", "base", "cache", "final"]
        if phase not in order:
            return False
        start = self.start_phase
        end = self.opts.to_phase
        if end not in order:
            end = "final"
        return order.index(phase) >= order.index(start) and \
            order.index(phase) <= order.index(end)

    def precondition(self) -> None:
        self.phase_start("precondition", "find the board in fastboot or wake it "
                          "through the console")
        # 1. fastboot is the only state that can be trusted: the CDC-ACM port
        # can enumerate while the kernel is halted.
        try:
            devices = self.fastboot.devices()
        except PreconditionError:
            raise
        if devices:
            self.fastboot.require_device()
            self.checkpoint("C01", "precondition.fastboot",
                            "fastboot devices lists the board", devices[0])
        else:
            self.log.info("no fastboot device; trying the CDC-ACM console")
            if not self.console_answers():
                self.fail("precondition.console",
                          "either fastboot devices or a responding console",
                          "no fastboot device and no IMEM? answer on %s"
                          % self.opts.port,
                          hint="the board is stranded; a physical power cycle "
                               "is required before this sequence can start",
                          cls=PreconditionError)
            self.probe_volume_state()
            self.send_cmd(CMD_FASTBOOT, "FASTBOOT")
            try:
                self.wait(lambda l: l.startswith("KEBAB FASTBOOT: IMEM read back"),
                          15, "KEBAB FASTBOOT: IMEM read back")
            except TimeoutError:
                lines = " | ".join(self.log.tail(5))
                self.fail("precondition.fastboot",
                          "FASTBOOT to write the PON reason and the IMEM cookie",
                          "no FASTBOOT confirmation (%s)" % lines,
                          hint="FASTBOOT needs kebab_pon_write=1; if the line "
                               "says disabled, this kernel cannot enter fastboot",
                          cls=PreconditionError)
            deadline = time.time() + 40
            serial_no = ""
            while time.time() < deadline:
                try:
                    found = self.fastboot.devices()
                except PreconditionError:
                    found = []
                if found:
                    serial_no = self.fastboot.require_device()
                    break
                time.sleep(0.5)
            if not serial_no:
                self.fail("precondition.fastboot",
                          "the board to enumerate in fastboot after FASTBOOT",
                          "no fastboot device within 40s",
                          hint="FASTBOOT resets through PSCI; if the console "
                               "printed 'disabled' the gate was off",
                          cls=PreconditionError)
            self.checkpoint("C01", "precondition.fastboot",
                            "FASTBOOT reaches fastboot", serial_no)
        slot = self.fastboot.current_slot()
        if slot != "b":
            self.fail("precondition.slot", "current-slot b (writes go to boot_b)",
                      "current-slot %s" % slot,
                      hint="this procedure only flashes boot_b; switch slots "
                           "explicitly rather than guessing",
                      cls=PreconditionError)
        self.checkpoint("C02", "precondition.slot", "current-slot b", "b")
        product = self.fastboot.product()
        self.log.info("fastboot product: %s" % (product or "(not reported)"))

    def console_answers(self) -> bool:
        try:
            self.console.start()
            self.send_cmd(CMD_IMEM, "IMEM? liveness")
            self.wait(lambda l: l.startswith("KEBAB IMEM:"), 3.0,
                      "IMEM? liveness")
            return True
        except (TimeoutError, ConsoleLost):
            return False

    def choose_start(self) -> None:
        if self.volume_imprinted is None and self.opts.from_phase in (
                "auto", "install", "skel", "base", "cache"):
            self.log.info("volume state unknown: flashing the install image to "
                          "probe INST? before touching the volume")
        # From vol: skip only when the console already reported the large
        # geometry.  If the state is unknown, the vol phase must run; wiping a
        # volume is destructive, so never assume it is already imprinted.
        if self.opts.from_phase == "auto":
            self.start_phase = "install" if self.volume_imprinted else "vol"
            if self.volume_imprinted is None:
                self.start_phase = "install"
        elif self.opts.from_phase == "vol":
            if self.volume_imprinted and not self.opts.force_vol:
                self.start_phase = "install"
                self.log.info("volume is already imprinted; skipping the vol "
                              "phase (use --force-vol to imprint again)")
            else:
                self.start_phase = "vol"
        else:
            self.start_phase = self.opts.from_phase
        if self.start_phase not in ("vol", "install", "skel", "base", "cache",
                                    "final"):
            self.start_phase = "vol"
        self.log.info("start phase: %s -> %s" % (self.start_phase, self.opts.to_phase))

    # -- vol phase ---------------------------------------------------------
    def phase_vol(self) -> None:
        self.boot("vol", self.opts.vol_image, VOL_ARGS, noinit=True,
                  forbidden_args=set())
        self.arm_panic_detector("vol")
        try:
            vs = read_vext(self.opts.volume_stream, deep=True)
        except ValueError as exc:
            self.fail("vol.stream", "a valid VEXT v1 stream", str(exc))
            raise AssertionError("unreachable")
        if vs.file_size != self.opts.expected_vol_stream:
            self.fail("vol.stream", "%d-byte VEXT stream"
                      % self.opts.expected_vol_stream,
                      "%d bytes" % vs.file_size,
                      hint="regenerate with scripts/make-root-volume.py")
        if vs.volume_bytes != self.opts.volume_bytes:
            self.fail("vol.stream", "volume_bytes %d" % self.opts.volume_bytes,
                      "%d" % vs.volume_bytes)
        vh = read_vext_volume_header(vs.path)
        if vh["signature"] != 0x482B or vh["version"] != 4 or \
                vh["block_size"] != VOLUME_BLOCK_SIZE or \
                vh["blocks"] != self.opts.volume_blocks:
            self.fail("vol.stream", "embedded HFS+ header sig 0x482b version 4 "
                      "bsize 4096 blocks %d" % self.opts.volume_blocks, str(vh))
        if vh["journal"]:
            self.fail("vol.stream", "a non-journaled HFS+ volume",
                      "journal field 0x%x" % vh["journal"],
                      hint="mkfs with newfs_hfs without journaling")
        self.checkpoint("C-VOL-STREAM", "vol.stream",
                        "VEXT %d bytes, %d extents, volume header 4,194,304 blocks"
                        % (vs.file_size, vs.extent_count),
                        "%d bytes, %d extents, sha256 %s"
                        % (vs.file_size, vs.extent_count,
                           vs.file_sha256 or sha256_file(vs.path)))

        self.vol_status()
        self.vol_send(vs, dry=True)
        self.vol_send(vs, dry=False)
        self.reboot_and_check_new_volume()
        self.phase_done("vol")

    def vol_status(self) -> None:
        self.send_cmd(CMD_VOL_STATUS, "VOL? status")
        lines = self.collect()
        got: dict[str, Any] = {}
        for line in lines:
            m = VOL_STATUS_RE.match(line)
            if m:
                got.update(gated=int(m.group(1)), writable=int(m.group(5)),
                           chunk=int(m.group(4)))
            m = VOL_PART_RE.match(line)
            if m:
                got["partition"] = int(m.group(1))
            m = VOL_DISK_RE.match(line)
            if m:
                got.update(disk_bsize=int(m.group(1)), disk_blocks=int(m.group(2)))
        if not got:
            self.fail("vol.status", "VOL? gated 1 ... writable 1, partition and "
                      "on-disk volume header", " | ".join(lines) or "no answer",
                      hint="the vol image must carry kebab_vol=1")
        if got.get("gated") != 1 or got.get("writable") != 1:
            self.fail("vol.status", "VOL? gated 1 ... writable 1", " | ".join(lines),
                      hint="kebab_vol=1 and ufsrw=1 must both be present")
        if got.get("chunk") != CHUNK:
            self.fail("vol.status", "chunk %d" % CHUNK, " | ".join(lines))
        self.checkpoint("C-VOL-STATUS", "vol.status",
                        "VOL? gated 1 writable 1 chunk 65536",
                        "partition %s, disk %s blocks"
                        % (human(got.get("partition", 0)), got.get("disk_blocks")))

    def vol_send(self, vs: VextStream, dry: bool) -> None:
        size = vs.file_size
        if self.detector:
            self.detector.phase = "vol.dry" if dry else "vol.write"
        if dry:
            cmd = b"VOL DRY %d\n" % size
            expect_mode = "dry"
            done_re = VOL_PLAN_RE
            done_name = "VOL PLAN"
        else:
            cmd = b"VOL BEGIN %d %d CONFIRM\n" % (size, vs.volume_bytes)
            expect_mode = "write"
            done_re = VOL_DONE_RE
            done_name = "VOL DONE"
        self.send_cmd(cmd, cmd.decode("ascii").strip())
        old_fields: list[str] = []
        old_bytes = 0
        begin_line = ""
        try:
            # The kernel prints VOL partition, then the old header (3 fields +
            # 32 hex lines), then VOL BEGIN.  Nothing is sent until BEGIN.
            while True:
                line = self.wait(
                    lambda l: l.startswith("VOL OLD vh ")
                    or l.startswith("VOL OLD bytes ")
                    or l.startswith("VOL BEGIN ")
                    or l.startswith("VOL refused")
                    or l.startswith("VOL ABORT")
                    or l.startswith("VOL disabled"),
                    self.opts.phase_timeout, "VOL header / VOL BEGIN")
                if line.startswith("VOL OLD vh "):
                    old_fields.append(line)
                elif line.startswith("VOL OLD bytes "):
                    old_bytes += 1
                elif line.startswith("VOL BEGIN "):
                    begin_line = line
                    break
                else:
                    self.fail("vol.%s" % expect_mode,
                              "the old header and VOL BEGIN", line,
                              hint="VOL requires kebab_vol=1, ufsrw=1 and a "
                                   "readable old HFS+ header")
        except TimeoutError:
            self.fail("vol.%s" % expect_mode, "the old header and VOL BEGIN",
                      "no VOL BEGIN within %.0fs" % self.opts.phase_timeout,
                      hint="a wrong CONFIRM or a missing volume size is refused "
                           "before BEGIN")
        m = VOL_BEGIN_RE.match(begin_line)
        if not m:
            self.fail("vol.%s" % expect_mode, "VOL BEGIN %d volume %d %s chunk 65536"
                      % (size, vs.volume_bytes, expect_mode), begin_line)
        # VOL DRY carries no volume size on the command line (the kernel prints
        # the volume field as 0 until it parses the stream header).  Only the
        # real write can be required to echo the 16 GiB size.
        want_volume = 0 if dry else vs.volume_bytes
        if int(m.group(1)) != size or int(m.group(2)) != want_volume \
                or m.group(3) != expect_mode or int(m.group(4)) != CHUNK:
            self.fail("vol.%s" % expect_mode,
                      "VOL BEGIN %d volume %d %s chunk %d"
                      % (size, want_volume, expect_mode, CHUNK), begin_line)
        if len(old_fields) != 3 or old_bytes != 32:
            self.fail("vol.old-header",
                      "VOL OLD vh x3 and 32 VOL OLD bytes lines (512 bytes)",
                      "%d field lines, %d byte lines" % (len(old_fields), old_bytes),
                      hint="the old volume header is printed before the write; "
                           "keep those 512 bytes in the log")
        sig = re.search(r"sig 0x([0-9a-f]+) version (\d+)", old_fields[0])
        if not sig or int(sig.group(1), 16) != 0x482B or int(sig.group(2)) != 4:
            self.fail("vol.old-header", "old HFS+ header sig 0x482b version 4",
                      old_fields[0])
        self.checkpoint("C-VOL-OLD", "vol.old-header",
                        "old HFS+ header: 3 fields + 32 raw lines",
                        "sig 0x482b version 4, 512 raw bytes")

        sent = 0
        start = time.time()
        with open(vs.path, "rb") as f:
            while sent < size:
                data = f.read(CHUNK)
                self.console.send(data)
                sent += len(data)
                target = sent
                while True:
                    try:
                        ack = self.wait(lambda l: VOL_ACK_RE.match(l) is not None,
                                        self.opts.phase_timeout, "VOL ACK")
                    except TimeoutError:
                        self.fail("vol.%s" % expect_mode, "VOL ACK %d" % target,
                                  "no ACK after %.0fs" % self.opts.phase_timeout)
                        raise AssertionError("unreachable")
                    if int(VOL_ACK_RE.match(ack).group(1)) >= target:
                        break
        try:
            if dry:
                plan = self.wait(lambda l: done_re.match(l) is not None, 120,
                                 done_name)
                m = done_re.match(plan)
                if int(m.group(1)) != vs.extent_count or \
                        int(m.group(2)) != vs.data_bytes or \
                        int(m.group(3)) != size or \
                        int(m.group(4)) != vs.volume_bytes:
                    self.fail("vol.dry", "VOL PLAN extents %d bytes %d wire %d "
                              "volume %d" % (vs.extent_count, vs.data_bytes, size,
                                             vs.volume_bytes), plan)
                self.checkpoint("C-VOL-DRY", "vol.dry",
                                "VOL DRY accepts and plans the stream", plan,
                                elapsed=time.time() - start)
                return
            # A real write prints VOL NEW (read back and byte-compared) then
            # VOL DONE.  VOL NEW is three field lines plus 32 raw lines; the
            # geometry is on the third field line, so collect the whole group.
            new_lines: list[str] = []
            while True:
                line = self.wait(
                    lambda l: l.startswith("VOL NEW vh ")
                    or done_re.match(l) is not None, 120,
                    "VOL NEW vh / VOL DONE")
                if done_re.match(line):
                    done_line = line
                    break
                new_lines.append(line)
        except TimeoutError:
            self.fail("vol.%s" % expect_mode, "VOL NEW and VOL DONE",
                      "the stream ended but the writer did not finish",
                      hint="a write failure leaves VOL FAIL before this point; "
                           "check the last console lines")
            raise AssertionError("unreachable")
        m = done_re.match(done_line)
        if int(m.group(1)) != vs.extent_count or int(m.group(2)) != vs.data_bytes \
                or int(m.group(3)) != size:
            self.fail("vol.write", "VOL DONE extents %d bytes %d wire %d"
                      % (vs.extent_count, vs.data_bytes, size), done_line)
        new_vh = next((l for l in new_lines if " bsize " in l), "")
        nm = re.search(r"bsize (\d+) blocks (\d+) free (\d+)", new_vh)
        if not nm or int(nm.group(1)) != VOLUME_BLOCK_SIZE or \
                int(nm.group(2)) != self.opts.volume_blocks:
            self.fail("vol.write", "VOL NEW bsize 4096 blocks %d"
                      % self.opts.volume_blocks, new_vh or " | ".join(new_lines))
        self.checkpoint("C-VOL-WRITE", "vol.write",
                        "VOL DONE with the new 4,194,304-block header", done_line,
                        elapsed=time.time() - start)

    def reboot_and_check_new_volume(self) -> None:
        self.phase_start("reboot", "REBOOT and mount the new volume read-only")
        # This boot is intentional: do not let the panic detector mistake the
        # expected banner for a reboot after a panic.
        self.detector = PanicDetector(self.console, armed=False, phase="reboot")
        self.send_cmd(CMD_REBOOT, "REBOOT")
        try:
            line = self.wait(lambda l: l.startswith("KEBAB REBOOT: PEHaltRestart"),
                             15, "REBOOT acknowledgement")
            self.checkpoint("C-VOL-REBOOT", "vol.reboot",
                            "REBOOT reaches PEHaltRestart(kPERestartCPU)", line)
        except TimeoutError:
            self.fail("vol.reboot", "KEBAB REBOOT: PEHaltRestart(kPERestartCPU)",
                      "no REBOOT acknowledgement; the kernel may already be gone")
        # The same vol image boots again, still with kebab_vol=1 and
        # kebab_noinit=1.  Its HFS+ mount line proves the new geometry.
        try:
            self.wait(lambda l: "KEBAB USB: event thread started" in l
                      or "KEBAB USB: running (DSTS" in l,
                      self.opts.boot_timeout, "post-REBOOT boot banner")
            line = self.wait(lambda l: l.startswith("KEBAB HFS: "),
                             self.opts.boot_timeout, "new KebabRoot mount")
        except TimeoutError:
            self.fail("vol.reboot", "a fresh boot with KEBAB HFS: KebabRoot ... "
                      "total blocks %d" % self.opts.volume_blocks,
                      "no HFS mount line within %.0fs" % self.opts.boot_timeout,
                      hint="the old volume may still be mounted, or the reboot "
                           "did not complete")
            raise AssertionError("unreachable")
        m = HFS_RE.match(line)
        if not m or int(m.group(2)) != VOLUME_BLOCK_SIZE or \
                int(m.group(3)) != self.opts.volume_blocks:
            self.fail("vol.reboot", "KEBAB HFS: KebabRoot: block size 4096, "
                      "total blocks %d" % self.opts.volume_blocks, line)
        self.probe_imem()
        self.checkpoint("C-VOL-MOUNT", "vol.mount",
                        "the new volume mounts with 4,194,304 blocks", line)
        self.phase_done("reboot")

    # -- install phase -----------------------------------------------------
    def phase_install(self) -> None:
        self.boot("install", self.opts.install_image, INSTALL_ARGS, noinit=True,
                  forbidden_args={"kebab_vol=1"})
        self.arm_panic_detector("install")
        info = self.query_inst()
        if info is None:
            lines = " | ".join(self.log.tail(10))
            self.fail("install.geometry", "INST? gated 1 root rw with the 16 GiB "
                      "geometry", lines or "INST? produced no geometry",
                      hint="the image must carry kebab_install=1 rootrw=1 "
                           "ufsrw=1; a read-only root prints 'boot with rootrw=1'")
        if info.get("gated") != 1 or info.get("root") != "rw":
            self.fail("install.geometry", "INST? gated 1 ... root rw",
                      "gated=%s root=%s" % (info.get("gated"), info.get("root")),
                      hint="check kebab_install=1 and rootrw=1 in the image args")
        if info.get("chunk") != CHUNK:
            self.fail("install.geometry", "chunk %d" % CHUNK, str(info.get("chunk")))
        blocks = info.get("blocks")
        total = info.get("total")
        free = info.get("free")
        if not blocks or blocks < self.opts.volume_blocks:
            if self.opts.from_phase == "auto" and not self._auto_vol_done:
                # The precondition could not tell (the board was already in
                # fastboot).  The install image boots fine on the old volume;
                # its small geometry is the signal to run vol and come back.
                self.log.info("volume is not imprinted (INST? reports %s blocks); "
                              "running the vol phase and retrying install"
                              % blocks)
                self._auto_vol_done = True
                self.phase_vol()
                self.phase_install()
                return
            self.fail("install.geometry",
                      "about %d blocks and %d bytes total"
                      % (self.opts.volume_blocks, self.opts.volume_bytes),
                      "blocks=%s total=%s free=%s" % (blocks, total, free),
                      hint="this is an old/small volume; run --from vol to "
                           "imprint the 16 GiB root first")
        if blocks != self.opts.volume_blocks:
            self.fail("install.geometry", "exactly %d blocks"
                      % self.opts.volume_blocks, "blocks=%d" % blocks)
        if total != self.opts.volume_bytes:
            self.fail("install.geometry", "exactly %d total bytes"
                      % self.opts.volume_bytes, "total=%s" % total)
        if not free or free > total:
            self.fail("install.geometry", "a plausible free-space figure",
                      "free=%s total=%s" % (free, total))
        self.volume_imprinted = True
        self.checkpoint("C-INST-GEOM", "install.geometry",
                        "INST? root rw, 4194304 blocks, 17179869184 bytes, free > 0",
                        "blocks %d, %s free, %d-byte chunks"
                        % (blocks, human(free), info["chunk"]))
        self.phase_done("install")

    # -- skeleton phase ----------------------------------------------------
    def phase_skel(self) -> None:
        """Send skel.kist on the running install image, then check the read-back.

        INST CHECK is a file-size-and-hash probe and cannot describe a
        directory or a symlink, so the skeleton is never prechecked and is
        always sent whole: it is one 64 KiB chunk, and the kernel re-applies
        the exact mode and owner on a re-run.  What matters is the console
        evidence after the write: every created inode must report uid 0/gid 0
        and the expected mode (including the sticky 01777 on /private/tmp).
        """
        stream = self.streams.get("skel")
        if stream is None:
            stream = read_kist(self.opts.skel_stream, deep=True)
            self.streams["skel"] = stream
        self.phase_start("skel", "install skel.kist (%d entries, %s)"
                         % (stream.entry_count, human(stream.file_size)))
        self.arm_panic_detector("skel")
        mark = self.console.mark()
        result = self.send_stream(stream, "inst", "skel")
        lines = self.console.since(mark)
        self.check_skel_console(stream, lines)
        self.log.info("skel.kist: %d entries, %d ok, %d skipped, %.1fs"
                      % (result["entries"], result["ok"], result["skipped"],
                         result["elapsed"]))
        self.phase_done("skel")

    def check_skel_console(self, stream: KistStream,
                           lines: Sequence[str]) -> None:
        want_stream = ("INST STREAM v2 skeleton entries %d bytes %d wire %d"
                       % (stream.entry_count, stream.total_usize,
                          stream.file_size))
        stream_line = next((l for l in lines if SKEL_STREAM_RE.match(l)), "")
        if stream_line != want_stream:
            self.fail("skel.stream", want_stream,
                      stream_line or "no INST STREAM line during the send",
                      hint="a v1 or non-skeleton header means skel.kist was "
                           "rebuilt with an old packer or without --skeleton")
        self.checkpoint("C-SKEL-STREAM", "skel.stream", want_stream, stream_line)

        by_path = {e.path: e for e in stream.entries}
        checked: dict[str, str] = {}
        for line in lines:
            m = SKEL_OK_RE.match(line)
            if m:
                path = m.group(1)
                size = int(m.group(2))
                is_dir = m.group(3) is not None
                target = m.group(4) or ""
                mode = int(m.group(5), 8)
                uid, gid = int(m.group(6)), int(m.group(7))
                entry = by_path.get(path)
                if entry is None:
                    self.fail("skel.attrs", "only paths from skel.kist",
                              "unexpected read-back: %s" % line)
                if path in checked:
                    self.fail("skel.attrs", "one read-back line per entry",
                              "duplicate: %s" % line)
                if uid != 0 or gid != 0:
                    self.fail("skel.attrs",
                              "every created inode to report uid 0 gid 0",
                              "%s reports uid %d gid %d" % (path, uid, gid),
                              hint="the kernel's ki_set_attrs did not take "
                                   "effect on this filesystem; do not continue")
                if entry.kind == "dir":
                    if not is_dir or size != 0 or mode != entry.mode:
                        self.fail("skel.attrs",
                                  "%s dir mode 0%o" % (path, entry.mode), line)
                elif entry.kind == "link":
                    if is_dir or size != entry.size or target != entry.target:
                        self.fail("skel.attrs",
                                  "%s link -> %s" % (path, entry.target), line)
                    # A symlink's mode is fixed at creation and is not part of
                    # the kernel's check; only the owner is enforced.
                else:
                    if is_dir or size != entry.size or mode != entry.mode:
                        self.fail("skel.attrs",
                                  "%s mode 0%o size %d"
                                  % (path, entry.mode, entry.size), line)
                checked[path] = line
                continue
            m = SKEL_SKIP_RE.match(line)
            if m:
                path, size = m.group(1), int(m.group(2))
                entry = by_path.get(path)
                if entry is None or entry.kind != "link" or size != entry.size:
                    self.fail("skel.attrs", "a SKIP only for an existing "
                              "symlink with the right target length", line)
                checked.setdefault(path, line)
        missing = [e.path for e in stream.entries if e.path not in checked]
        if missing:
            self.fail("skel.attrs", "one read-back line per skeleton entry",
                      "%d of %d missing: %s" % (len(missing), len(stream.entries),
                                                ", ".join(missing[:4])),
                      hint="the kernel stopped before creating every entry; "
                           "look for the first FAIL line")
        links_skipped = sum(1 for e in stream.entries
                            if e.kind == "link"
                            and checked.get(e.path, "").startswith("SKIP "))
        self.checkpoint("C-SKEL-ATTRS", "skel.attrs",
                        "16 inodes report uid 0 gid 0 and their exact modes "
                        "(01777 included)",
                        "%d/%d read back, uid 0 gid 0%s"
                        % (len(checked), len(stream.entries),
                           ", %d link(s) already present" % links_skipped))

    # -- userland phases ---------------------------------------------------
    def phase_userland(self, label: str) -> None:
        stream = self.streams.get(label)
        if stream is None:
            path = self.opts.base_stream if label == "base" else self.opts.cache_stream
            stream = read_kist(path)
            self.streams[label] = stream
        self.phase_start(label, "install %s.kist (%d entries, %s)"
                         % (label, stream.entry_count, human(stream.file_size)))
        self.arm_panic_detector(label)
        # Precheck every destination.  If all of them already verify, the
        # stream is complete and must not be sent again; partial completion is
        # left to the kernel's per-entry SKIP.
        if self.opts.no_precheck:
            pre: dict[str, dict[str, Any]] = {}
            self.checkpoint("C-%s-PRECHECK" % label.upper(),
                            "precheck." + label, "skipped by --no-precheck",
                            "sending the stream without a precheck")
        else:
            pre = self.check_entries(stream, label)
            same = sum(1 for r in pre.values() if r["state"] == "same")
            missing = sum(1 for r in pre.values() if r["state"] == "missing")
            diff = sum(1 for r in pre.values() if r["state"] != "same")
            self.checkpoint("C-%s-PRECHECK" % label.upper(), "precheck." + label,
                            "INST CHECK each destination; skip a complete stream",
                            "%d/%d same, %d missing/different"
                            % (same, len(pre), diff))
            if same == len(pre) and pre:
                self.checkpoint("C-%s-SKIP" % label.upper(), "send." + label,
                                "do not re-send a stream whose every entry verifies",
                                "complete; skipped")
                self.phase_done(label)
                return
            if diff == 0 and missing:
                self.log.info("all destinations missing; sending the full stream")
        result = self.send_stream(stream, "inst", label)
        sample = self.sample(stream)
        self.verify_entries(stream, label, sample)
        self.log.info("%s.kist: %d entries, %d ok, %d skipped, %.1fs, %.2f MiB/s"
                      % (label, result["entries"], result["ok"], result["skipped"],
                         result["elapsed"],
                         stream.file_size / max(1e-6, result["elapsed"]) / (1 << 20)))
        self.phase_done(label)

    def sample(self, stream: KistStream) -> list[KistEntry]:
        entries = stream.entries
        if len(entries) <= SAMPLE_PATHS:
            return list(entries)
        idx = {0, len(entries) - 1}
        idx.add(max(range(len(entries)), key=lambda i: entries[i].size))
        for k in range(1, SAMPLE_PATHS - len(idx) + 1):
            idx.add((k * len(entries)) // SAMPLE_PATHS)
        return [entries[i] for i in sorted(i for i in idx if 0 <= i < len(entries))]

    # -- final phase -------------------------------------------------------
    def phase_final(self) -> None:
        mark = self.boot("final", self.opts.final_image, FINAL_ARGS, noinit=False,
                         forbidden_args={"kebab_vol=1", "kebab_noinit=1"})
        # boot() has already waited for the USB console; from here a new banner
        # is a reboot, and a panic line is a failure.
        self.arm_panic_detector("final")
        if self.detector:
            self.detector.phase = "final.launchd"
        started = time.time()
        last: list[str] = []
        success = False
        verdict = ""
        panic: PanicDetected | None = None
        # A late-opening port can make boot() consume the first launchd lines
        # while it probes IMEM?.  The line list still has them; inspect the
        # window from the reboot mark before waiting for more.
        for line in self.console.since(mark):
            if not final_line_matches(line):
                continue
            last.append(line)
            if PANIC_RE.search(line):
                panic = PanicDetected(
                    "final.launchd", "no panic on the final boot", line,
                    hint="patch 0134 reboots after an unserviced panic")
                verdict = "FAILED: kernel panic"
                break
            if is_final_failure(line):
                verdict = final_verdict(line)
                break
            if is_final_success(line):
                success = True
                verdict = "LAUNCHD RUNNING"
                break
        try:
            while not success and not verdict and \
                    time.time() - started < self.opts.final_timeout:
                try:
                    line = self.console.wait(
                        lambda l: final_line_matches(l),
                        2.0, detector=self.detector, what="launchd activity")
                except TimeoutError:
                    continue
                last.append(line)
                if is_final_failure(line):
                    verdict = final_verdict(line)
                    break
                if is_final_success(line):
                    success = True
                    verdict = "LAUNCHD RUNNING"
                    break
        except PanicDetected as exc:
            panic = exc
            verdict = "PANIC: %s" % exc.seen
            self.log.info("final boot panic: %s" % exc.seen)
        if success:
            # Give zprint a moment to emit its table if launchd's own line came
            # first; the table is the strongest spawn evidence.
            try:
                line = self.console.wait(
                    lambda l: "zone name" in l or "ZONE TOTALS" in l,
                    20, detector=self.detector, what="zprint table")
                last.append(line)
                verdict = "LAUNCHD RUNNING (zprint zone table)"
            except TimeoutError:
                pass
            except PanicDetected as exc:
                panic = exc
                success = False
                verdict = "PANIC: %s" % exc.seen
        lines = self.log.tail(100)
        if success:
            self.checkpoint("C-FINAL", "final.launchd",
                            "launchd runs and zprint prints its zone table",
                            verdict)
            self.phase_done("final")
            self.final_report(True, verdict, lines)
            return
        self.checkpoint("C-FINAL", "final.launchd", "launchd running",
                        verdict or "no launchd/zprint marker", ok=False)
        self.final_report(False, verdict or "INCONCLUSIVE", lines)
        if panic is not None:
            raise PanicDetected(
                "final.launchd",
                "launchd to reach main without the kernel rebooting",
                panic.seen,
                hint=panic.hint,
                lines=lines,
            )
        raise BringUpError(
            "final.launchd", "launchd to reach main and spawn the zprint job",
            verdict or "the boot produced no launchd/zprint marker within %.0fs"
            % self.opts.final_timeout,
            hint="read the verdict and the last 100 lines; CS_KILLED, initproc "
                 "and DSC failures have different first lines",
            lines=lines,
        )

    def final_report(self, ok: bool, verdict: str, lines: Sequence[str]) -> None:
        self.log.info("")
        self.log.info("final boot verdict: %s" % verdict)
        for line in list(lines)[-100:]:
            self.log.info("  # " + line)
        self.summary["final"] = {"ok": ok, "verdict": verdict,
                                 "lines": list(lines)[-100:]}

    def require_args(self, image: str, required: set[str],
                     forbidden: set[str]) -> None:
        forbidden = set(forbidden)
        if not self.opts.allow_kdp:
            forbidden.add("kebabkdp=1")
        variants = self.artifacts.get(image)
        if variants is None:
            try:
                info = read_image(image, hash_file=False)
            except (ValueError, OSError) as exc:
                self.fail("precondition.artifacts", "%s to be a boot image" % image,
                          str(exc))
                raise AssertionError("unreachable")
            variants = info.args
        good = []
        for args in variants:
            tokens = set(args.split())
            if required <= tokens and not (forbidden & tokens):
                good.append(args)
        if not good:
            seen = " || ".join(variants) or "(no args string in the image)"
            self.fail("precondition.args",
                      "%s to contain %s%s"
                      % (os.path.basename(image),
                         " ".join(sorted(required)),
                         (" and not " + " ".join(sorted(forbidden))) if forbidden else ""),
                      seen,
                      hint="the arguments live in the payload TOML "
                           "(args = \"...\"), not in the Android boot header; "
                           "rebuild the phase image with the right kernel_args")
        self.checkpoint("C-ARGS", "precondition.args",
                        "image args match the phase", "ok")

    def finish(self) -> None:
        """Close the run with a summary of every checkpoint that was observed."""
        self.phase = "done"
        self.summary["elapsed"] = round(time.time() - self.start_time, 3)
        self.summary["volume_imprinted"] = self.volume_imprinted
        self.log.info("")
        self.log.info("checkpoint summary:")
        for cp in self.checkpoints:
            self.log.info("  %s %-24s %s" % (
                cp.cid, cp.name, "OK" if cp.ok else "FAIL"))
        self.log.info("all %d checkpoints passed" % len(self.checkpoints))

    # -- dry run -----------------------------------------------------------
    def run_dry(self) -> None:
        self.phase_start("dry-run", "verify every artifact and print the plan")
        errors: list[str] = []
        checks: list[tuple[str, str, str]] = []
        counter = [0]

        def check(name: str, expected: str, fn: Callable[[], str]) -> None:
            counter[0] += 1
            cid = "D-%02d" % counter[0]
            try:
                seen = fn()
                checks.append((name, expected, seen))
                self.checkpoint(cid, name, expected, seen)
            except (ValueError, OSError) as exc:
                errors.append("%s: %s" % (name, exc))
                self.checkpoint(cid, name, expected, str(exc), ok=False)

        check("dry.image.vol", "%d-byte boot image with kebab_vol=1"
              % self.opts.image_size,
              lambda: self.dry_image(self.opts.vol_image, VOL_ARGS, set()))
        check("dry.image.install", "boot image without kebab_vol=1",
              lambda: self.dry_image(self.opts.install_image, INSTALL_ARGS,
                                     {"kebab_vol=1"}))
        check("dry.image.final", "boot image without kebab_noinit=1 / kebab_vol=1",
              lambda: self.dry_image(self.opts.final_image, FINAL_ARGS,
                                     {"kebab_vol=1", "kebab_noinit=1"}))
        check("dry.stream.volume", "%d-byte VEXT with a 4,194,304-block HFS+ header"
              % self.opts.expected_vol_stream,
              lambda: self.dry_volume_stream())
        check("dry.stream.skel", "%d-entry KIST v2 skeleton, %d B installed, "
              "%d B wire" % (SKEL_ENTRIES, SKEL_INSTALLED, SKEL_WIRE),
              lambda: self.dry_skel(self.opts.skel_stream))
        check("dry.stream.base", "%d-entry KIST stream, hashes verified"
              % self.opts.expected_base_entries,
              lambda: self.dry_kist(self.opts.base_stream, "base",
                                    self.opts.expected_base_entries))
        check("dry.stream.cache", "%d-entry KIST stream, hashes verified"
              % self.opts.expected_cache_entries,
              lambda: self.dry_kist(self.opts.cache_stream, "cache",
                                    self.opts.expected_cache_entries))
        check("dry.fastboot", "the fastboot executable is present",
              lambda: self.dry_fastboot())
        if errors:
            for e in errors:
                self.log.info("dry-run error: %s" % e)
            raise DryRunError(
                "dry-run", "every artifact to verify",
                "%d check(s) failed: %s" % (len(errors), "; ".join(errors[:3])),
                lines=self.log.tail(30),
            )
        self.print_plan()
        self.log.info("")
        self.log.info("DRY RUN OK: all artifacts and command strings verified; "
                      "nothing was written")

    def dry_image(self, path: str, required: set[str],
                  forbidden: set[str]) -> str:
        if not os.path.isfile(path):
            raise ValueError("missing %s" % path)
        size = os.path.getsize(path)
        if size != self.opts.image_size:
            raise ValueError("%s is %d bytes, expected %d"
                             % (path, size, self.opts.image_size))
        info = read_image(path)
        forbidden = set(forbidden)
        if not self.opts.allow_kdp:
            forbidden.add("kebabkdp=1")
        good = [a for a in info.args
                if required <= set(a.split()) and not (forbidden & set(a.split()))]
        if not good:
            raise ValueError("%s does not carry required args %s (found %s)"
                             % (path, " ".join(sorted(required)),
                                " || ".join(info.args) or "none"))
        return "%d bytes, %d args string(s), %s" % (
            size, len(info.args), " ".join(sorted(required)))

    def dry_volume_stream(self) -> str:
        path = self.opts.volume_stream
        if not os.path.isfile(path):
            raise ValueError("missing %s" % path)
        vs = read_vext(path, deep=True)
        if vs.file_size != self.opts.expected_vol_stream:
            raise ValueError("%s is %d bytes, expected %d"
                             % (path, vs.file_size, self.opts.expected_vol_stream))
        if vs.volume_bytes != self.opts.volume_bytes:
            raise ValueError("%s declares volume %d, expected %d"
                             % (path, vs.volume_bytes, self.opts.volume_bytes))
        if vs.block_size != VOLUME_BLOCK_SIZE:
            raise ValueError("%s block size %d" % (path, vs.block_size))
        if vs.extent_count != self.opts.expected_vol_extents:
            raise ValueError("%s has %d extents, expected %d"
                             % (path, vs.extent_count,
                                self.opts.expected_vol_extents))
        if vs.data_bytes != self.opts.expected_vol_data:
            raise ValueError("%s carries %d data bytes, expected %d"
                             % (path, vs.data_bytes, self.opts.expected_vol_data))
        vh = read_vext_volume_header(path)
        if (vh["signature"], vh["version"], vh["block_size"], vh["blocks"]) != \
                (0x482B, 4, VOLUME_BLOCK_SIZE, self.opts.volume_blocks):
            raise ValueError("%s embedded HFS+ header: %s" % (path, vh))
        if vh["journal"]:
            raise ValueError("%s is journaled; the kernel mounts a non-journaled root"
                             % path)
        return "%d bytes, %d extents, %s data, HFS+ %d blocks, sha256 %s" % (
            vs.file_size, vs.extent_count, human(vs.data_bytes), vh["blocks"],
            sha256_file(path))

    def dry_skel(self, path: str) -> str:
        if not os.path.isfile(path):
            raise ValueError("missing %s" % path)
        st = read_kist(path, deep=True,
                       progress=lambda p, i, n: self.log.info(
                           "  verify skel [%d/%d] %s" % (i, n, p)))
        problem = self.skel_shape_problem(st)
        if problem:
            raise ValueError(problem)
        return ("%d entries (%d files, %d dirs, %d links), %d B installed, "
                "%d B wire, sha256 %s"
                % (st.entry_count, SKEL_FILES, SKEL_DIRS, SKEL_LINKS,
                   st.total_usize, st.file_size, sha256_file(path)))

    def dry_kist(self, path: str, label: str, expected: int) -> str:
        if not os.path.isfile(path):
            raise ValueError("missing %s" % path)
        st = read_kist(path, deep=True,
                       progress=lambda p, i, n: self.log.info(
                           "  verify %s [%d/%d] %s" % (label, i, n, p)))
        if st.entry_count != expected:
            raise ValueError("%s has %d entries, expected %d"
                             % (path, st.entry_count, expected))
        return "%d entries, %s installed, %s wire, sha256 %s" % (
            st.entry_count, human(st.total_usize), human(st.file_size),
            sha256_file(path))

    def dry_fastboot(self) -> str:
        if os.path.isabs(self.opts.fastboot):
            if not os.path.isfile(self.opts.fastboot):
                raise ValueError("missing %s" % self.opts.fastboot)
            return self.opts.fastboot
        found = shutil.which(self.opts.fastboot)
        if not found:
            raise ValueError("%s is not on PATH" % self.opts.fastboot)
        return found

    def print_plan(self) -> None:
        self.log.info("")
        self.log.info("PLAN (%s -> %s):" % (self.opts.from_phase, self.opts.to_phase))
        rows = [
            ("1", "precondition", "fastboot devices; current-slot == b; FASTBOOT "
             "through the console only if needed"),
            ("2", "vol", "flash %s; VOL DRY; VOL BEGIN ... CONFIRM; require OLD "
             "512 bytes, VOL DONE and the new HFS+ mount" % os.path.basename(self.opts.vol_image)),
            ("3", "install", "flash %s; require INST? root rw, 4,194,304 blocks, "
             "17,179,869,184 bytes and free space" % os.path.basename(self.opts.install_image)),
            ("4", "skel", "send skel.kist (%d v2 entries: %d files, %d dirs, %d "
             "links, %d B installed); require 'INST STREAM v2 skeleton' and "
             "uid 0/gid 0 read-back" % (SKEL_ENTRIES, SKEL_FILES, SKEL_DIRS,
                                        SKEL_LINKS, SKEL_INSTALLED)),
            ("5", "base", "INST CHECK all %d entries; send only if incomplete; "
             "verify a sample" % self.opts.expected_base_entries),
            ("6", "cache", "INST CHECK all %d entries; send only if incomplete; "
             "verify a sample; report elapsed" % self.opts.expected_cache_entries),
            ("7", "final", "flash %s; capture to the first failure or launchd; "
             "print the last 100 lines" % os.path.basename(self.opts.final_image)),
        ]
        for row in rows:
            self.log.info("  %s. %-12s %s" % row)
        if self.volume_imprinted is True:
            self.log.info("  resume: INST? reports the 16 GiB geometry, so the "
                          "vol phase would be skipped")
        elif self.volume_imprinted is False:
            self.log.info("  resume: volume is not imprinted; start at vol")


# ---------------------------------------------------------------------------
# Failure report and entry point
# ---------------------------------------------------------------------------


def is_final_failure(line: str) -> bool:
    return ("unexpected SIGKILL of launchd" in line
            or "CS_KILLED" in line
            or line.startswith("initproc failed")
            or line.startswith("initproc exited")
            or line.startswith("pid 1 exited")
            or "Library not loaded" in line
            or "devfs_kernel_mount" in line
            or ("KEBAB DSC:" in line
                and ("NOT mapped" in line or "rejected" in line))
            or line.startswith("panic(")
            or line.startswith("Debugger called"))


def final_verdict(line: str) -> str:
    if "CS_KILLED" in line:
        return "FAILED: launchd killed by codesign (CS_KILLED)"
    if line.startswith("initproc") or line.startswith("pid 1 exited"):
        return "FAILED: init process died"
    if "Library not loaded" in line:
        return "FAILED: dyld cannot find the shared cache"
    if "devfs_kernel_mount" in line:
        return "FAILED: /dev is not on the root volume"
    if "NOT mapped" in line or "rejected" in line:
        return "FAILED: shared cache plan was not mapped"
    if line.startswith("panic(") or line.startswith("Debugger called"):
        return "FAILED: kernel panic"
    return "FAILED: " + line


def is_final_success(line: str) -> bool:
    return ("launchd logging initialized" in line
            or "zone name" in line
            or "ZONE TOTALS" in line)


def final_line_matches(line: str) -> bool:
    return ("launchd" in line or "zone name" in line or "ZONE TOTALS" in line
            or line.startswith("load_init_program:")
            or line.startswith("KEBAB DSC:")
            or line.startswith("dyld[")
            or line.startswith("pid 1 exited")
            or line.startswith("initproc")
            or "CS_KILLED" in line
            or "Library not loaded" in line
            or "devfs_kernel_mount" in line
            or line.startswith("panic(")
            or line.startswith("Debugger called"))


def print_failure(opts: Options, log: RunLog, exc: BringUpError) -> None:
    lines = exc.lines or log.tail(30)
    print("", file=sys.stderr)
    print("=" * 74, file=sys.stderr)
    print("BRING-UP STOPPED", file=sys.stderr)
    print("checkpoint: %s" % exc.checkpoint, file=sys.stderr)
    print("expected:   %s" % exc.expected, file=sys.stderr)
    print("seen:       %s" % exc.seen, file=sys.stderr)
    if exc.hint:
        print("hint:       %s" % exc.hint, file=sys.stderr)
    print("", file=sys.stderr)
    print("last %d console lines:" % len(lines), file=sys.stderr)
    for line in lines:
        print("  | " + line, file=sys.stderr)
    print("", file=sys.stderr)
    print("run log:     %s" % log.path, file=sys.stderr)
    print("console log: %s" % log.console_path, file=sys.stderr)
    print("=" * 74, file=sys.stderr)
    text = [
        "checkpoint: %s" % exc.checkpoint,
        "kind:       %s" % exc.kind,
        "expected:   %s" % exc.expected,
        "seen:       %s" % exc.seen,
        "hint:       %s" % exc.hint,
        "",
        "last %d console lines:" % len(lines),
    ] + ["  | " + l for l in lines]
    log.write_text("failure.txt", "\n".join(text) + "\n")


def make_parser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(
        description="One-command P1 bring-up for the kebab board",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    ap.add_argument("--from", dest="from_phase", default="auto",
                    choices=["auto", "vol", "install", "skel", "base", "cache",
                             "final"],
                    help="first phase to run (default auto: use INST? if known)")
    ap.add_argument("--to", dest="to_phase", default="final",
                    choices=["vol", "install", "skel", "base", "cache", "final"],
                    help="last phase to run (default final)")
    ap.add_argument("--dry-run", action="store_true",
                    help="verify artifacts, streams and boot args; write nothing")
    ap.add_argument("--work-dir", default="work/p1",
                    help="where boot-*.img, *.kist and KebabRoot.vext live")
    ap.add_argument("--log-dir", default="",
                    help="run log directory (default <work-dir>/logs/run-...)")
    ap.add_argument("--fastboot", default="", help="fastboot executable")
    ap.add_argument("--port", default="", help="CDC-ACM port (default COM7)")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--vol-image", default="")
    ap.add_argument("--install-image", default="")
    ap.add_argument("--final-image", default="")
    ap.add_argument("--volume-stream", default="")
    ap.add_argument("--skel-stream", default="")
    ap.add_argument("--base-stream", default="")
    ap.add_argument("--cache-stream", default="")
    ap.add_argument("--force-vol", action="store_true",
                    help="imprint even if INST? already reports the large volume")
    ap.add_argument("--no-precheck", action="store_true",
                    help="do not INST CHECK all destinations before sending")
    ap.add_argument("--allow-kdp", action="store_true",
                    help="permit kebabkdp=1 (it disables automatic panic reboot)")
    ap.add_argument("--expected-base-entries", type=int, default=BASE_ENTRIES)
    ap.add_argument("--expected-cache-entries", type=int, default=CACHE_ENTRIES)
    ap.add_argument("--expected-vol-stream", type=int, default=VOLUME_STREAM_BYTES,
                    help="expected KebabRoot.vext size (default %d)"
                    % VOLUME_STREAM_BYTES)
    ap.add_argument("--expected-vol-extents", type=int,
                    default=VOLUME_STREAM_EXTENTS)
    ap.add_argument("--expected-vol-data", type=int, default=VOLUME_STREAM_DATA)
    ap.add_argument("--boot-timeout", type=float, default=120.0)
    ap.add_argument("--phase-timeout", type=float, default=180.0)
    ap.add_argument("--final-timeout", type=float, default=180.0)
    ap.add_argument("--quiet", action="store_true")
    ap.add_argument("--json-summary", default="",
                    help="write a machine-readable run summary here")
    return ap


def main(argv: Sequence[str] | None = None,
         fastboot_factory: Callable[[Options, RunLog], Any] | None = None,
         console_factory: Callable[[Options, RunLog], Any] | None = None) -> int:
    argv = list(sys.argv[1:] if argv is None else argv)
    ap = make_parser()
    args = ap.parse_args(argv)
    opts = Options(
        from_phase=args.from_phase, to_phase=args.to_phase,
        dry_run=args.dry_run, work_dir=args.work_dir, log_dir=args.log_dir,
        fastboot=args.fastboot, port=args.port, baud=args.baud,
        vol_image=args.vol_image, install_image=args.install_image,
        final_image=args.final_image, volume_stream=args.volume_stream,
        skel_stream=args.skel_stream,
        base_stream=args.base_stream, cache_stream=args.cache_stream,
        force_vol=args.force_vol, no_precheck=args.no_precheck,
        allow_kdp=args.allow_kdp,
        expected_base_entries=args.expected_base_entries,
        expected_cache_entries=args.expected_cache_entries,
        expected_vol_stream=args.expected_vol_stream,
        expected_vol_extents=args.expected_vol_extents,
        expected_vol_data=args.expected_vol_data,
        boot_timeout=args.boot_timeout, phase_timeout=args.phase_timeout,
        final_timeout=args.final_timeout, quiet=args.quiet,
        json_summary=args.json_summary,
    ).resolve()
    log = RunLog(opts.log_dir, quiet=opts.quiet)
    if not opts.dry_run and serial is None:
        print("bring-up: pyserial is required for the console (pip install pyserial)",
              file=sys.stderr)
        log.close()
        return 2
    fastboot = None
    console = None
    orchestrator = None
    try:
        if fastboot_factory:
            fastboot = fastboot_factory(opts, log)
        elif not opts.dry_run:
            fastboot = Fastboot(opts.fastboot, log)
        if console_factory:
            console = console_factory(opts, log)
        elif not opts.dry_run:
            console = SerialConsole(log, port=opts.port, baud=opts.baud)
        orchestrator = Orchestrator(opts, log, fastboot=fastboot, console=console)
        orchestrator.run()
        if opts.json_summary:
            path = opts.json_summary
            with open(path, "w", encoding="utf-8") as f:
                json.dump(orchestrator.summary, f, indent=2)
        log.info("")
        if opts.dry_run:
            log.info("DRY RUN COMPLETE: nothing was written")
        else:
            log.info("BRING-UP COMPLETE (%s -> %s)"
                     % (orchestrator.start_phase, opts.to_phase))
        return 0
    except KeyboardInterrupt:
        print("\ninterrupted", file=sys.stderr)
        return 130
    except BringUpError as exc:
        print_failure(opts, log, exc)
        if opts.json_summary and orchestrator is not None:
            orchestrator.summary["failure"] = {
                "checkpoint": exc.checkpoint, "kind": exc.kind,
                "expected": exc.expected, "seen": exc.seen,
                "hint": exc.hint, "lines": exc.lines or log.tail(30),
            }
            try:
                with open(opts.json_summary, "w", encoding="utf-8") as f:
                    json.dump(orchestrator.summary, f, indent=2)
            except OSError:
                pass
        return {"dry-run": 6, "precondition": 2, "panic": 4,
                "console": 5, "device": 3}.get(exc.kind, 3)
    finally:
        log.close()


if __name__ == "__main__":
    sys.exit(main())
