#!/usr/bin/env python3
"""Offline validation for scripts/bring-up.py.

Two independent checks run here:

  * a fake kebab board: a KIST/VOL-aware device model, so the whole phase
    sequence, resume logic, the 64 KiB ACK flow control and panic detection can
    be exercised without hardware;
  * a command-lint pass over bsd/dev/kebab_usb.c, when a kernel source tree is
    supplied with --kernel-source.

The fake board is deliberately strict about the wire format: it only accepts
the exact command forms the kernel accepts (one completion, newline included),
and it acknowledges exactly one chunk per write.  A host that pipelines or
sends a wrong argument form fails the same way it would on the board.

    python3 scripts/test-bring-up.py
    python3 scripts/test-bring-up.py --kernel-source ~/xnu/bsd/dev/kebab_usb.c
    python3 scripts/test-bring-up.py --make-fixtures /tmp/p1-fixtures
"""

from __future__ import annotations

import argparse
import contextlib
import hashlib
import importlib.util
import io
import os
import re
import struct
import sys
import tempfile
import traceback
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
BRING_UP = os.path.join(HERE, "bring-up.py")


def load_bring_up():
    spec = importlib.util.spec_from_file_location("kebab_bring_up", BRING_UP)
    mod = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    sys.modules["kebab_bring_up"] = mod
    spec.loader.exec_module(mod)
    return mod


bu = load_bring_up()


# ---------------------------------------------------------------------------
# Fixtures
# ---------------------------------------------------------------------------


def sha256(data: bytes) -> bytes:
    return hashlib.sha256(data).digest()


def write_kist(path: str, entries: list[tuple[str, bytes, int, bool]]) -> None:
    """entries: (destination, content, mode, deflate); v1, files only."""
    payload = bytearray()
    body = bytearray()
    for dest, content, mode, deflate in entries:
        raw = zlib.compress(content, 9) if deflate else content
        flags = bu.KIST_F_DEFLATE if deflate else 0
        bh = dest.encode("utf-8")
        hdr = struct.pack(
            "<4sHHHHQQ32s128sI", b"KENT", len(bh), mode, flags, 0,
            len(raw), len(content), sha256(content), bh, 0)
        body += hdr
        body += raw
        payload += raw
    pad = (-(32 + len(body))) % bu.CHUNK
    total_wire = len(body) + pad
    with open(path, "wb") as f:
        f.write(struct.pack("<4sHHIIQQ", b"KIST", 1, 0, len(entries), 32,
                            sum(len(c) for _, c, _, _ in entries), total_wire))
        f.write(body)
        f.write(b"\0" * pad)


def write_kist_v2(path: str, entries: list[tuple[str, str, int, object]],
                  skeleton: bool = True, version: int = 2) -> None:
    """v2 stream: entries are (kind, destination, mode, payload).

    kind is "file" (payload bytes), "dir" (payload None) or "link" (payload is
    the target string).  total_usize is file bytes plus symlink target lengths,
    exactly like pack-userland.py.
    """
    body = bytearray()
    total_usize = 0
    for kind, dest, mode, data in entries:
        bh = dest.encode("utf-8")
        if kind == "dir":
            flags, wire, size, digest, payload = bu.KIST_F_DIR, 0, 0, b"\0" * 32, b""
        elif kind == "link":
            payload = data.encode("utf-8")
            flags = bu.KIST_F_SYMLINK
            wire = size = len(payload)
            digest = sha256(payload)
        elif kind == "file":
            assert isinstance(data, bytes), data
            payload = data
            flags = 0
            wire = size = len(data)
            digest = sha256(data)
        else:
            raise AssertionError(kind)
        total_usize += size
        body += struct.pack("<4sHHHHQQ32s128sI", b"KENT", len(bh), mode,
                            flags, 0, wire, size, digest, bh, 0)
        body += payload
    pad = (-(32 + len(body))) % bu.CHUNK
    total_wire = len(body) + pad
    stream_flags = bu.KIST_SF_SKELETON if skeleton else 0
    with open(path, "wb") as f:
        f.write(struct.pack("<4sHHIIQQ", b"KIST", version, stream_flags,
                            len(entries), 32, total_usize, total_wire))
        f.write(body)
        f.write(b"\0" * pad)


def make_volume_header(blocks: int, free: int) -> bytes:
    vh = bytearray(512)
    struct.pack_into(">HH", vh, 0, 0x482B, 4)
    struct.pack_into(">III", vh, 40, 4096, blocks, free)
    struct.pack_into(">I", vh, 4, 0x80000100)
    name = "KebabRoot".encode("utf-16-be")
    struct.pack_into(">H", vh, 0x70, 9)
    vh[0x72:0x72 + len(name)] = name
    return bytes(vh)


def write_vext(path: str, blocks: int = 4194304) -> None:
    """The three-extent 1,769,472-byte shape documented in section 63."""
    volume = blocks * 4096
    extents = [
        (0x0, 0xA9000),
        (0xF9000, 0x100000),
        (0x3FFFFF000, 0x1000),
    ]
    data = bytearray()
    for off, length in extents:
        chunk = bytearray(length)
        if off == 0:
            chunk[1024:1536] = make_volume_header(blocks, 4193878)
        data += chunk
    assert len(data) == 1744896, len(data)
    body = bytearray()
    for (off, length), start in zip(extents, (0, 0xA9000, 0x1A9000)):
        body += struct.pack("<QII", off, length, 0)
        body += data[start:start + length]
    pad = (-(32 + len(body))) % bu.CHUNK
    assert 32 + len(body) + pad == 1769472, 32 + len(body) + pad
    with open(path, "wb") as f:
        f.write(struct.pack("<4sHHQIIQ", b"VEXT", 1, 32, volume, 4096,
                            len(extents), 1744896))
        f.write(body)
        f.write(b"\0" * pad)


def write_image(path: str, args: str) -> None:
    """A 96 MiB Android boot image with the payload TOML args at 0x800000."""
    size = 96 * 1024 * 1024
    with open(path, "wb") as f:
        f.truncate(size)
        f.seek(0)
        hdr = bytearray(4096)
        struct.pack_into("<8sIII", hdr, 0, b"ANDROID!", 0x300000, 0x8000,
                         0x2BC000)
        struct.pack_into("<I", hdr, 36, 4096)
        struct.pack_into("<I", hdr, 40, 2)
        f.write(hdr)
        f.seek(0x800000)
        toml = (
            '"xnu"\npath = "Payloads/kernel.macho"\nargs = "%s"\n'
            "[[entry]]\nid = \"reset\"\n" % args
        ).encode()
        f.write(toml)


class Fixture:
    def __init__(self, dirpath: str) -> None:
        self.dir = dirpath
        os.makedirs(dirpath, exist_ok=True)
        self.vol_image = os.path.join(dirpath, "boot-vol.img")
        self.install_image = os.path.join(dirpath, "boot-inst.img")
        self.final_image = os.path.join(dirpath, "boot-final.img")
        self.volume_stream = os.path.join(dirpath, "KebabRoot.vext")
        self.skel_stream = os.path.join(dirpath, "skel.kist")
        self.base_stream = os.path.join(dirpath, "base.kist")
        self.cache_stream = os.path.join(dirpath, "cache.kist")
        self.bad_install_image = os.path.join(dirpath, "boot-bad.img")
        self.corrupt_base = os.path.join(dirpath, "base-corrupt.kist")
        base_args = ("kebabusb=1 ufsrw=1 rootrw=1 kebab_pon_write=1 "
                     "kebab_cs=15 ufsroot=userdata rd=ufsroot0")
        write_image(self.vol_image,
                    base_args + " kebab_vol=1 kebab_noinit=1 kebab_install=1")
        write_image(self.install_image,
                    base_args + " kebab_noinit=1 kebab_install=1")
        write_image(self.final_image,
                    base_args + " kebab_install=1 kebab_dsc=1 kebab_dsc_map=1")
        # The same image with kebab_vol=1 is a tempting but wrong install
        # image; dry-run must reject it before a flash.
        write_image(self.bad_install_image,
                    base_args + " kebab_vol=1 kebab_noinit=1 kebab_install=1")
        write_vext(self.volume_stream)

        base_entries = [
            ("/sbin/launchd", b"launchd" * 400, 0o755, True),
            ("/usr/lib/dyld", b"dyld" * 600, 0o755, True),
            ("/System/Library/Caches/com.apple.kernelcaches/static.trustcache",
             b"tc" * 300, 0o644, False),
            ("/usr/lib/zprint", b"zprint" * 200, 0o755, True),
            ("/usr/libexec/xpcproxy", b"xpcproxy" * 200, 0o755, True),
            ("/System/Library/LaunchDaemons/com.kebab.zprint.plist",
             b"<plist/>", 0o644, False),
        ]
        cache_entries = []
        for i in range(44):
            name = ("/System/Library/Caches/com.apple.dyld/dyld_shared_cache_arm64"
                    + ("%02d" % i if i else ""))
            cache_entries.append((name, (("cache-%02d" % i) * 1000).encode(),
                                  0o644, i % 3 == 0))
        write_kist(self.base_stream, base_entries)
        write_kist(self.cache_stream, cache_entries)
        # The v2 skeleton, byte-for-byte the shape make-minimal-userland.py
        # builds: 10 directories, 3 symlinks, 3 files, 16 entries, 153 bytes
        # installed and one 64 KiB chunk on the wire.
        skel_entries = [
            ("dir", "/dev", 0o755, None),
            ("link", "/etc", 0o777, "private/etc"),
            ("dir", "/private", 0o755, None),
            ("dir", "/private/etc", 0o755, None),
            ("file", "/private/etc/group", 0o644, b"wheel:*:0:root\n"),
            ("file", "/private/etc/master.passwd", 0o644,
             b"root:*:0:0::0:0:System Administrator:/var/root:/bin/sh\n"),
            ("file", "/private/etc/passwd", 0o644,
             b"root:*:0:0:System Administrator:/var/root:/bin/sh\n"),
            ("dir", "/private/tmp", 0o1777, None),
            ("dir", "/private/var", 0o755, None),
            ("dir", "/private/var/db", 0o755, None),
            ("dir", "/private/var/log", 0o755, None),
            ("dir", "/private/var/root", 0o700, None),
            ("dir", "/private/var/run", 0o755, None),
            ("dir", "/private/var/tmp", 0o1777, None),
            ("link", "/tmp", 0o777, "private/tmp"),
            ("link", "/var", 0o777, "private/var"),
        ]
        write_kist_v2(self.skel_stream, skel_entries)
        skel = bu.read_kist(self.skel_stream, deep=True)
        assert (skel.entry_count, skel.total_usize, skel.file_size) == \
            (16, 153, 65536), (skel.entry_count, skel.total_usize, skel.file_size)
        assert skel.kinds() == {"file": 3, "dir": 10, "link": 3}, skel.kinds()
        # A stream whose payload no longer matches its SHA-256 header.
        st = bu.read_kist(self.base_stream)
        data = bytearray(open(self.base_stream, "rb").read())
        data[st.entries[0].offset + 3] ^= 0xFF
        open(self.corrupt_base, "wb").write(data)
        self.base_entries = base_entries
        self.cache_entries = cache_entries
        self.streams: list = []
        for path in (self.skel_stream, self.base_stream, self.cache_stream):
            st = bu.read_kist(path, deep=(path == self.skel_stream))
            self.streams.append(st)


# ---------------------------------------------------------------------------
# Fake board
# ---------------------------------------------------------------------------


class FakeConsole(bu.LineChannel):
    def __init__(self, logger, device):
        super().__init__(logger)
        self.device = device
        self.connected = True

    def start(self):
        pass

    def stop(self):
        pass

    def send(self, data: bytes):
        if not self.connected:
            raise bu.ConsoleLost("console", "a writable console",
                                 "fake console is down")
        self.device.receive(data)


class FakeFastboot:
    def __init__(self, board):
        self.board = board
        self.serial_no = ""
        self.flashed = []
        self.commands = []

    def devices(self):
        return [self.board.serial] if self.board.mode == "fastboot" else []

    def require_device(self):
        if self.board.mode != "fastboot":
            raise bu.PreconditionError("precondition.fastboot",
                                       "fastboot devices", "not in fastboot")
        return self.board.serial

    def getvar(self, name):
        if name == "current-slot":
            return "current-slot: %s\nFinished. Total time: 0.001s" % self.board.slot
        if name == "product":
            return "product: kona\nFinished. Total time: 0.001s"
        return "Finished. Total time: 0.001s"

    def current_slot(self):
        return self.board.slot

    def product(self):
        return "kona"

    def flash(self, partition, image):
        assert partition == "boot_b"
        self.flashed.append(image)
        self.board.pending_image = image
        self.board.log.append("flash " + os.path.basename(image))
        return "Sending 'boot_b' OKAY\nWriting 'boot_b' OKAY\nFinished."

    def reboot(self):
        image = self.board.pending_image
        self.board.reboot(image)
        return "Rebooting OKAY"


class FakeDevice:
    def __init__(self, fixture: Fixture, console: FakeConsole, *,
                 slot: str = "b", volume_blocks: int = 16384,
                 files: dict | None = None, final: str = "success",
                 panic_after: int | None = None,
                 silent_panic: bool = False,
                 bad_attrs: str = "",
                 panel_banner: bool = False,
                 mount_line: bool = True,
                 vol_root: str = "ro",
                 inst_busy: bool = False,
                 inst_refused: bool = False,
                 no_free_delta: bool = False):
        self.fx = fixture
        self.console = console
        self.serial = "kebab-test"
        self.slot = slot
        self.volume_blocks = volume_blocks
        self.free_blocks = max(0, volume_blocks - 426)
        self.files = dict(files or {})
        self.final = final
        self.panic_after = panic_after
        self.silent_panic = silent_panic
        # A path whose read-back reports uid 501/gid 20 instead of 0:0.  The
        # real kernel aborts; this models the case where it does not, so the
        # orchestrator's own checkpoint has to catch it.
        self.bad_attrs = bad_attrs
        # By default the fake transcript carries only lines the USB gate can
        # deliver.  panel_banner=True adds the panel-only startup lines
        # ("running (DSTS ...)", "Darwin Kernel Version") so a test can prove
        # the orchestrator never needs them.
        self.panel_banner = panel_banner
        # mount_line=False omits the KEBAB HFS geometry line while still
        # printing the registration line, which is the mandatory-line failure.
        self.mount_line = mount_line
        # kebab_vol=1 must keep the old root read-only.  vol_root="rw" models
        # the safety precondition failing, which VOL? reports and the
        # orchestrator now requires.
        self.vol_root = vol_root
        # A stale INST session prints "busy" instead of BEGIN; a gated or
        # misconfigured boot prints "refused".  Both used to be invisible to
        # the waiter and cost a full phase timeout.
        self.inst_busy = inst_busy
        self.inst_refused = inst_refused
        # no_free_delta=True reproduces the 2026-10-28 board result: INST DONE
        # reports ok=N but the volume header's free count never moves, so the
        # next reboot loses every file.
        self.no_free_delta = no_free_delta
        self.mode = "kernel"
        self.pending_image = None
        self.current_image = None
        self.log: list[str] = []
        self.active_kind = ""
        self.active_size = 0
        self.active_remaining = 0
        self.active_data = bytearray()
        self.active_dry = False
        self.active_chunks = 0
        self.active_stream_printed = False
        self.sent_log: list[tuple[str, int]] = []
        self.acks = 0
        self.panicked = False

    # -- mode transitions --------------------------------------------------
    def set_fastboot(self):
        self.mode = "fastboot"
        self.console.set_connected(False)

    def reboot(self, image: str | None):
        self.current_image = image
        args = self.args_for(image)
        self.mode = "kernel"
        self.console.set_connected(True)
        self.console.feed_host("fake boot %s" % os.path.basename(image or "?"))
        if self.panel_banner:
            # These lines are printed before ud.running is set, so the kernel's
            # USB tee drops them.  They are here only as noise for the test
            # that proves a checkpoint must not depend on them.
            self.console.feed("Darwin Kernel Version 22.1.0: fake")
            self.console.feed("KEBAB USB: running (DSTS 0x00000003 after 120 ms), "
                              "DMA at 0x12340000")
        # The first line that can reach USB: printed after ud.running = true.
        self.console.feed("KEBAB USB: event thread started, waiting for the host")
        if "kebab_vol=1" in args:
            # The real bsd_init prints the HFS registration before mountroot;
            # it must not be mistaken for the mount geometry line.
            self.console.feed("KEBAB HFS: hfs-627.100.6 registered")
            if self.mount_line:
                self.console.feed("KEBAB HFS: KebabRoot: block size 4096, total "
                                  "blocks %d (%d MiB), free blocks %d"
                                  % (self.volume_blocks,
                                     self.volume_blocks * 4096 >> 20,
                                     max(0, self.volume_blocks - 426)))
        if "kebab_noinit=1" in args:
            self.console.feed("KEBAB: kebab_noinit=1: staying in the kernel, "
                              "no init program")
        elif self.final == "success":
            self.console.feed("load_init_program: attempting to load /sbin/launchd")
            self.console.feed("KEBAB DSC: mapping complete; dyld will find the "
                              "cache already in the shared region")
            self.console.feed("launchd logging initialized. name: launchd pid: 1")
            self.console.feed("zone name                 size     free")
            self.console.feed("ZONE TOTALS")
        elif self.final == "cs_killed":
            self.console.feed("unexpected SIGKILL of launchd (CS_KILLED) with "
                              "reason -- namespace 9 code 0x6")
            self.console.feed("initproc failed to start")
        elif self.final == "dsc":
            self.console.feed("KEBAB DSC: plan rejected (error 2); shared cache "
                              "NOT mapped")
            self.console.feed("dyld[1]: Library not loaded: "
                              "/usr/lib/libSystem.B.dylib")

    def args_for(self, image: str | None) -> str:
        path = image or ""
        if path == self.fx.vol_image:
            return "kebab_vol=1 kebab_noinit=1 kebab_install=1"
        if path == self.fx.install_image:
            return "kebab_noinit=1 kebab_install=1"
        if path == self.fx.final_image:
            return "kebab_install=1 kebab_dsc=1 kebab_dsc_map=1"
        return ""

    # -- input -------------------------------------------------------------
    def receive(self, data: bytes):
        if self.active_kind:
            self.chunk(data)
            return
        try:
            text = data.decode("ascii")
        except UnicodeDecodeError:
            return
        if not text.endswith("\n"):
            return
        self.command(text.rstrip("\n"))

    def command(self, cmd: str):
        self.log.append("cmd " + cmd)
        if cmd == "IMEM?":
            self.console.feed("KEBAB IMEM: restart_reason at 0x%x+0x%x is 0x%08x"
                              % (0x9300000, 0x10, 0x836991FC))
        elif cmd == "FASTBOOT":
            self.console.feed("KEBAB FASTBOOT: writing IMEM restart_reason "
                              "0x00000000 -> 0x77665500")
            self.console.feed("KEBAB FASTBOOT: IMEM read back 0x77665500 (MATCH)")
            self.console.feed("KEBAB REBOOT: PEHaltRestart(kPERestartCPU)")
            self.set_fastboot()
        elif cmd == "REBOOT":
            self.console.feed("KEBAB REBOOT: PEHaltRestart(kPERestartCPU)")
            self.reboot(self.current_image)
        elif cmd == "SYNC":
            self.console.feed("SYNC VFS vfs 0 device 0")
            self.console.feed("SYNC VH before %d after %d delta 0 bsize 4096 "
                              "blocks %d"
                              % (self.free_blocks, self.free_blocks,
                                 self.volume_blocks))
            self.console.feed("SYNC DONE vfs 0 device 0 ms 1")
        elif cmd == "INST?":
            total = self.volume_blocks * 4096
            self.console.feed("INST? gated 1 active 0 dry 0 root rw chunk 65536")
            self.console.feed("INST? wire 0/0 entries 0/0 ok 0 skip 0 fail 0")
            self.console.feed("INST? space %d free of %d bytes"
                              % (self.free_blocks * 4096, total))
            self.console.feed("INST? bsize 4096 blocks %d free %d"
                              % (self.volume_blocks, self.free_blocks))
            self.console.feed("INST? vh bsize 4096 blocks %d free %d"
                              % (self.volume_blocks, self.free_blocks))
        elif cmd == "VOL?":
            total = self.volume_blocks * 4096
            self.console.feed("VOL? gated 1 active 0 dry 0 chunk 65536 writable 1")
            self.console.feed("VOL? partition 245895331840 bytes (234500 MiB)")
            self.console.feed("VOL? root %s space %d free of %d bytes bsize "
                              "4096 blocks %d free %d"
                              % (self.vol_root,
                                 max(0, self.volume_blocks - 426) * 4096, total,
                                 self.volume_blocks,
                                 max(0, self.volume_blocks - 426)))
            # kv_print_vh("DISK", ...) does not repeat the "VOL?" command
            # prefix: the kernel prints "VOL DISK vh ...".
            self.console.feed("VOL DISK vh sig 0x482b version 4 attributes "
                              "0x80000100 lastMount 0x0 journal 0x0")
            self.console.feed("VOL DISK vh created 0 modified 0 checked 0 "
                              "files 0 folders 0 nextcnid 16")
            self.console.feed("VOL DISK vh bsize 4096 blocks %d free %d "
                              "freebytes %d volbytes %d"
                              % (self.volume_blocks,
                                 max(0, self.volume_blocks - 426),
                                 max(0, self.volume_blocks - 426) * 4096, total))
        elif cmd.startswith("VOL DRY "):
            size = int(cmd.split()[2])
            self.begin_vol(size, dry=True)
        elif cmd.startswith("VOL BEGIN "):
            parts = cmd.split()
            size, volume = int(parts[2]), int(parts[3])
            assert parts[4] == "CONFIRM", cmd
            assert volume == 17179869184, (volume, self.volume_blocks)
            self.begin_vol(size, dry=False)
        elif cmd.startswith("INST BEGIN "):
            size = int(cmd.split()[2])
            if self.inst_busy:
                self.console.feed("INST busy: 123 of 456 bytes already consumed")
            elif self.inst_refused:
                self.console.feed("INST refused: a stream needs a non-zero length")
            else:
                self.begin_inst(size)
        elif cmd.startswith("INST CHECK "):
            path = cmd[len("INST CHECK "):]
            cur = self.files.get(path)
            if cur is None:
                self.console.feed("INST CHECK %s MISSING" % path)
            else:
                self.console.feed("INST CHECK %s %d %s"
                                  % (path, cur[0], cur[1]))
        else:
            self.log.append("unknown " + cmd)

    # -- VOL stream --------------------------------------------------------
    def begin_vol(self, size: int, dry: bool):
        self.active_kind = "vol"
        self.active_size = size
        self.active_remaining = size
        self.active_data = bytearray()
        self.active_dry = dry
        self.active_chunks = 0
        self.console.feed("VOL partition 245895331840 bytes (234500 MiB), writable 1")
        self.console.feed("VOL OLD vh sig 0x482b version 4 attributes 0x80000100 "
                          "lastMount 0x0 journal 0x0")
        self.console.feed("VOL OLD vh created 0 modified 0 checked 0 files 0 "
                          "folders 0 nextcnid 16")
        self.console.feed("VOL OLD vh bsize 4096 blocks %d free %d freebytes %d "
                          "volbytes %d" % (self.volume_blocks,
                                           max(0, self.volume_blocks - 426), 0,
                                           self.volume_blocks * 4096))
        for i in range(32):
            self.console.feed("VOL OLD bytes %03x %s" % (i * 16, "00 " * 15 + "00"))
        self.console.feed("VOL BEGIN %d volume %d %s chunk 65536"
                          % (size, 0 if dry else 17179869184,
                             "dry" if dry else "write"))

    # -- INST stream -------------------------------------------------------
    def begin_inst(self, size: int):
        self.active_kind = "inst"
        self.active_size = size
        self.active_remaining = size
        self.active_data = bytearray()
        self.active_dry = False
        self.active_chunks = 0
        self.active_stream_printed = False
        self.inst_free_before = self.free_blocks
        self.inst_written = 0
        self.inst_replaced = 0
        self.console.feed("INST BEGIN %d write chunk 65536" % size)

    def chunk(self, data: bytes):
        n = len(data)
        if n > self.active_remaining:
            n = self.active_remaining
        self.active_data += data[:n]
        self.active_remaining -= n
        self.active_chunks += 1
        if self.active_kind == "vol":
            self.console.feed("VOL ACK %d" % (self.active_size - self.active_remaining))
        else:
            self.console.feed("INST ACK %d" % (self.active_size - self.active_remaining))
            # The kernel parses the 32-byte stream header out of the first
            # chunk and prints the version, skeleton flag and totals.
            if not self.active_stream_printed and len(self.active_data) >= 32:
                self.active_stream_printed = True
                (magic, version, sflags, count, hsize, usize,
                 wire) = struct.unpack_from("<4sHHIIQQ", self.active_data)
                self.console.feed("INST STREAM v%d%s entries %d bytes %d "
                                  "wire %d" % (version,
                                               " skeleton" if sflags & 0x2 else "",
                                               count, usize, self.active_size))
        if self.panic_after is not None and self.active_chunks >= self.panic_after:
            self.panic()
            return
        if self.active_remaining == 0:
            self.finish_stream()

    def panic(self):
        self.panicked = True
        self.active_kind = ""
        if not self.silent_panic:
            self.console.feed("panic(cpu 0 caller 0xfffffff012345678): fake "
                              "panic during transfer")
            self.console.feed("KEBAB USB: panic console cannot be serviced "
                              "(transport takeover refused); rebooting instead "
                              "of parking")
        self.console.set_connected(False)
        self.console.set_connected(True)
        self.reboot(self.current_image)

    def finish_stream(self):
        kind = self.active_kind
        self.active_kind = ""
        if kind == "vol":
            if self.active_dry:
                self.console.feed("VOL PLAN extents 3 bytes 1744896 wire %d "
                                  "volume 17179869184" % self.active_size)
            else:
                self.volume_blocks = 4194304
                self.free_blocks = 4193878
                self.console.feed("VOL NEW vh sig 0x482b version 4 attributes "
                                  "0x80000100 lastMount 0x0 journal 0x0")
                self.console.feed("VOL NEW vh created 0 modified 0 checked 0 "
                                  "files 0 folders 0 nextcnid 16")
                self.console.feed("VOL NEW vh bsize 4096 blocks 4194304 free "
                                  "4193878 freebytes 0 volbytes 17179869184")
                self.console.feed("VOL DONE extents 3 bytes 1744896 wire %d "
                                  "ms 100" % self.active_size)
            return
        st = self.select_stream()
        if st is None:
            self.console.feed("INST ABORT bad test stream size")
            return
        ok = skipped = 0
        for e in st.entries:
            cur = self.files.get(e.path)
            if e.kind == "dir":
                # A directory entry is always re-applied, never SKIPped.
                self.files[e.path] = (0, "dir")
                ok += 1
                self.feed_ok(e)
                continue
            same = cur is not None and cur[0] == e.size and cur[1] == e.sha.hex()
            if e.kind == "link" and same:
                skipped += 1
                self.console.feed("SKIP %s %d link exists" % (e.path, e.size))
                continue
            if e.kind == "file" and same and not st.skeleton:
                skipped += 1
                self.console.feed("SKIP %s %d" % (e.path, e.size))
                continue
            self.files[e.path] = (e.size, e.sha.hex())
            ok += 1
            if e.kind == "file":
                self.inst_written += e.size
                if cur is not None and isinstance(cur[0], int):
                    self.inst_replaced += cur[0]
            self.feed_ok(e)
        net = max(0, self.inst_written - self.inst_replaced)
        expected = (net + 4095) // 4096
        expected = min(expected, self.free_blocks)
        delta = 0 if self.no_free_delta else expected
        after = self.free_blocks - delta
        self.console.feed("INST FREE before %d after %d delta %d expected %d "
                          "bsize 4096 written %d replaced %d"
                          % (self.inst_free_before, after, delta, expected,
                             self.inst_written, self.inst_replaced))
        self.free_blocks = after
        self.console.feed("INST DONE entries %d ok %d skipped %d bytes %d "
                          "wire %d ms 100"
                          % (st.entry_count, ok, skipped, st.total_usize,
                             self.active_size))
        self.sent_log.append((self.active_size, ok + skipped))

    def select_stream(self):
        """Identify the stream by size and by the header the board received.

        skel.kist and the 6-entry base fixture are both one 64 KiB chunk, so
        the INST BEGIN size alone is ambiguous in the tests.
        """
        try:
            magic, version, sflags, count, hsize, usize, wire = \
                struct.unpack_from("<4sHHIIQQ", self.active_data)
        except struct.error:
            return None
        for st in self.fx.streams:
            if st.file_size == self.active_size and st.version == version \
                    and st.entry_count == count:
                return st
        return None

    def feed_ok(self, e):
        uid, gid = (501, 20) if e.path == self.bad_attrs else (0, 0)
        if e.kind == "dir":
            self.console.feed("OK %s 0 dir mode 0%o uid %d gid %d"
                              % (e.path, e.mode, uid, gid))
        elif e.kind == "link":
            self.console.feed("OK %s %d link -> %s mode 0%o uid %d gid %d"
                              % (e.path, e.size, e.target, e.mode, uid, gid))
        else:
            self.console.feed("OK %s %d mode 0%o uid %d gid %d"
                              % (e.path, e.size, e.mode, uid, gid))


class FakeBoard:
    def __init__(self, fixture: Fixture, logger, **kwargs):
        self.logger = logger
        self.console = FakeConsole(logger, None)
        self.device = FakeDevice(fixture, self.console, **kwargs)
        self.console.device = self.device
        self.fastboot = FakeFastboot(self.device)
        # Preload the running image so an initial INST? can be answered.
        self.device.current_image = (fixture.vol_image
                                     if self.device.volume_blocks < 1000000
                                     else fixture.install_image)

    @property
    def serial(self):
        return self.device.serial


def make_factories(fixture: Fixture, **kwargs):
    holder = {}

    def ff(opts, log):
        board = FakeBoard(fixture, log, **kwargs)
        holder["board"] = board
        return board.fastboot

    def cf(opts, log):
        return holder["board"].console

    return ff, cf, holder


def check_console_markers(tmp: str) -> None:
    """A panel-only line must never satisfy a boot checkpoint.

    kebab_usb_start() prints "KEBAB USB: running (DSTS ...)" before it sets
    ud.running, and kebab_usb_log() drops every byte until then.  Only the
    event-thread line can reach the host, so the matcher and the panic detector
    must both reject the panel line.  This is the regression test for the
    C-BOOT-BANNER defect in docs/BRING-UP-STATE.md section 78.3.
    """
    panel = ("KEBAB USB: running (DSTS 0x00000003 after 120 ms), "
             "DMA at 0x12340000")
    usb = "KEBAB USB: event thread started, waiting for the host"
    if not bu.is_console_boot_banner(usb):
        raise AssertionError("the USB banner was not accepted: %r" % usb)
    if bu.is_console_boot_banner(panel):
        raise AssertionError("the panel-only banner was accepted: %r" % panel)
    if bu.BOOT_BANNER_RE.search(panel):
        raise AssertionError("BOOT_BANNER_RE still matches the panel line")
    if bu.BOOT_BANNER_RE.search("Darwin Kernel Version 22.1.0: fake"):
        raise AssertionError("BOOT_BANNER_RE still matches the pre-USB banner")
    if not bu.BOOT_BANNER_RE.search(usb):
        raise AssertionError("BOOT_BANNER_RE does not match the USB banner")
    if not bu.BOOT_BANNER_RE.search(
            "KEBAB: kebab_noinit=1: staying in the kernel, no init program"):
        raise AssertionError("BOOT_BANNER_RE lost the kebab_noinit marker")
    # The old expectation must be recorded as panel-only so a future edit that
    # promotes it back to a checkpoint is caught here.
    if not any(m in panel for m in bu.PANEL_ONLY_BOOT_MARKERS):
        raise AssertionError("the panel-only list does not name the running line")


def check_kist_compressed_large(tmp: str) -> None:
    """A compressed entry's *file* size may exceed the whole stream size.

    This is the real base.kist shape: /usr/lib/dyld is 922,912 bytes on disk but
    311,520 bytes on the wire.  A parser that compares file_size with total_wire
    rejects a perfectly valid Apple userland stream.
    """
    path = os.path.join(tmp, "large-compressed.kist")
    data = b"A" * 200000
    write_kist(path, [("/sbin/large", data, 0o755, True)])
    st = bu.read_kist(path)
    if st.entries[0].size != len(data):
        raise AssertionError("file size %d" % st.entries[0].size)
    if st.entries[0].wire >= st.total_wire:
        raise AssertionError("wire %d total %d" % (st.entries[0].wire, st.total_wire))
    bu.read_kist(path, deep=True)


def check_kist_v2(tmp: str) -> None:
    """v2 DIR/LINK parsing, the widened 07777 mask, and v1 compatibility."""
    path = os.path.join(tmp, "skel-check.kist")
    entries = [
        ("dir", "/dev", 0o755, None),
        ("link", "/etc", 0o777, "private/etc"),
        ("dir", "/private", 0o755, None),
        ("dir", "/private/tmp", 0o1777, None),
        ("file", "/private/etc/passwd", 0o644,
         b"root:*:0:0:System Administrator:/var/root:/bin/sh\n"),
    ]
    write_kist_v2(path, entries)
    st = bu.read_kist(path, deep=True)
    if st.version != 2 or not st.skeleton:
        raise AssertionError("version %d skeleton %s" % (st.version, st.skeleton))
    by_path = {e.path: e for e in st.entries}
    if by_path["/private/tmp"].mode != 0o1777:
        raise AssertionError("sticky bit lost: mode 0%o"
                             % by_path["/private/tmp"].mode)
    if by_path["/private/tmp"].kind != "dir" or by_path["/private/tmp"].size:
        raise AssertionError("directory entry shape: %r"
                             % (by_path["/private/tmp"],))
    if by_path["/etc"].kind != "link" or by_path["/etc"].target != "private/etc":
        raise AssertionError("symlink target: %r" % (by_path["/etc"],))
    if by_path["/etc"].size != len("private/etc"):
        raise AssertionError("symlink size %d" % by_path["/etc"].size)
    if st.total_usize != 50 + len("private/etc"):
        raise AssertionError("total_usize %d" % st.total_usize)

    # v1 is still accepted by the same reader, with the old 0777 mask.
    v1 = os.path.join(tmp, "v1-check.kist")
    write_kist(v1, [("/sbin/v1", b"v1", 0o4755, False)])
    old = bu.read_kist(v1, deep=True)
    if old.version != 1 or old.skeleton:
        raise AssertionError("v1 version/skeleton")
    if old.entries[0].mode != 0o755:
        raise AssertionError("v1 mode mask: 0%o" % old.entries[0].mode)

    # A v1 stream may not carry a DIR entry; the reader must refuse it.
    bad = os.path.join(tmp, "v1-dir.kist")
    write_kist_v2(bad, [("dir", "/dev", 0o755, None)], skeleton=False, version=1)
    try:
        bu.read_kist(bad)
    except ValueError:
        pass
    else:
        raise AssertionError("a v1 stream with a DIR entry was accepted")


# ---------------------------------------------------------------------------
# Test harness
# ---------------------------------------------------------------------------


class Case:
    def __init__(self, name, argv, fixture, expected=0, **kwargs):
        self.name = name
        self.argv = argv
        self.fixture = fixture
        self.expected = expected
        self.kwargs = kwargs


def run_case(case: Case, fixture: Fixture):
    ff, cf, holder = make_factories(fixture, **case.kwargs)
    out = io.StringIO()
    err = io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        rc = bu.main(case.argv, fastboot_factory=ff, console_factory=cf)
    return rc, out.getvalue(), err.getvalue(), holder.get("board")


def make_fixtures(dirpath: str) -> Fixture:
    return Fixture(dirpath)


# ---------------------------------------------------------------------------
# Command lint
# ---------------------------------------------------------------------------


COMMAND_TABLE = [
    (b"FASTBOOT\n", "ud_command_seen(\"FASTBOOT\\n\")"),
    (b"REBOOT\n", "ud_command_seen(\"REBOOT\\n\")"),
    (b"SYNC\n", "ud_command_seen(\"SYNC\\n\")"),
    (b"IMEM?\n", "ud_command_seen(\"IMEM?\\n\")"),
    (b"INST?\n", "ud_inst_command(\"INST?\\n\")"),
    (b"INST BEGIN ", "ud_inst_command prefix, unsigned decimal + newline"),
    (b"INST CHECK ", "ud_inst_command prefix, path up to newline"),
    (b"VOL?\n", "ud_vol_command(\"VOL?\\n\")"),
    (b"VOL DRY ", "ud_vol_command prefix, unsigned decimal + newline"),
    (b"VOL BEGIN ", "ud_vol_command prefix, wire, space, volume, space, CONFIRM"),
]


def lint_commands(source: str) -> list[str]:
    errors = []
    def has(c_literal: str) -> bool:
        return c_literal in source or c_literal.replace('"', '"') in source
    for literal, _ in COMMAND_TABLE:
        text = literal.decode("ascii").replace("\\", "\\\\").replace("\n", "\\n")
        if ('"%s"' % text) not in source:
            # INST/VOL prefixes are C strings without the trailing newline in
            # some comparisons (the argument parser adds it).
            if literal.endswith(b" ") and ('"%s"' % text.rstrip()).replace(
                    ' \\"', '"') not in source:
                errors.append("missing C literal %r" % literal)
    # The VOL BEGIN order is the subtle one: wire, volume, CONFIRM.  Prove it
    # from the parser body rather than from a comment.
    m = re.search(r"ud_vol_command\(.*?\n\}", source, re.S)
    if not m:
        errors.append("ud_vol_command() not found")
    else:
        body = m.group(0)
        order = []
        for tok in ("VOL BEGIN ", "ud_u64_span", "CONFIRM\\n"):
            order.append(body.find(tok))
        if any(x < 0 for x in order) or not (order[0] < order[1] < order[2]):
            errors.append("VOL BEGIN does not parse wire/volume/CONFIRM in "
                          "that order: %s" % order)
        if "p[off] != ' '" not in body:
            errors.append("VOL BEGIN does not require a space between arguments")
    if "ud_inst_u64" in source:
        body = re.search(r"ud_inst_u64\(.*?\n\}", source, re.S)
        if body and "p[i] != '\\n'" not in body.group(0):
            errors.append("ud_inst_u64 does not require the trailing newline")
    # The command scanner has to run inside the non-KDP branch too; a panic
    # console that takes the KDP branch would otherwise eat INST/VOL.
    if "if (!kebab_usb_kdp_enabled())" not in source:
        errors.append("the non-KDP command branch is missing")
    return errors


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--kernel-source", default="",
                    help="path to bsd/dev/kebab_usb.c for the command lint")
    ap.add_argument("--make-fixtures", metavar="DIR",
                    help="write a fixture tree and exit")
    ap.add_argument("--keep", action="store_true", help="keep the fixture tree")
    args = ap.parse_args(argv)

    if args.make_fixtures:
        make_fixtures(args.make_fixtures)
        print("fixtures written to %s" % args.make_fixtures)
        return 0

    failures = 0
    tmp = tempfile.mkdtemp(prefix="kebab-p1-")
    try:
        fixture = make_fixtures(os.path.join(tmp, "fixtures"))
        log_root = os.path.join(tmp, "logs")
        try:
            check_kist_compressed_large(tmp)
            print("PASS kist-compressed-size")
        except Exception:
            failures += 1
            print("FAIL kist-compressed-size")
            traceback.print_exc()
        try:
            check_kist_v2(tmp)
            print("PASS kist-v2")
        except Exception:
            failures += 1
            print("FAIL kist-v2")
            traceback.print_exc()
        try:
            check_console_markers(tmp)
            print("PASS console-markers")
        except Exception:
            failures += 1
            print("FAIL console-markers")
            traceback.print_exc()

        cases = [
            Case("dry-run", ["--dry-run", "--work-dir", fixture.dir,
                             "--fastboot", sys.executable,
                             "--log-dir", os.path.join(log_root, "dry")],
                 fixture, 0),
            Case("happy-path", ["--from", "vol", "--work-dir", fixture.dir,
                                "--log-dir", os.path.join(log_root, "happy")],
                 fixture, 0),
            # kebab_usb_start() prints "running (DSTS ...)" before ud.running is
            # set, so it never reaches USB.  A console-only transcript must still
            # satisfy C-BOOT-BANNER via the event-thread line.
            Case("no-panel-banner", ["--from", "vol", "--to", "vol",
                                     "--work-dir", fixture.dir,
                                     "--log-dir", os.path.join(log_root, "nopanel")],
                 fixture, 0, panel_banner=False),
            # The converse: a stray panel line in the transcript must not become
            # the checkpoint's evidence, and must not confuse the panic detector.
            Case("panel-banner-noise", ["--from", "vol", "--to", "vol",
                                        "--work-dir", fixture.dir,
                                        "--log-dir", os.path.join(log_root, "panelnoise")],
                 fixture, 0, panel_banner=True),
            # The mount geometry line is mandatory: its absence must still fail
            # the phase even though the HFS registration line is present.
            Case("missing-mount-line", ["--from", "vol", "--to", "vol",
                                        "--boot-timeout", "3",
                                        "--work-dir", fixture.dir,
                                        "--log-dir", os.path.join(log_root, "nomount")],
                 fixture, 3, mount_line=False),
            # kebab_vol=1 must leave the old root read-only.  VOL? prints that
            # state; the orchestrator must fail the checkpoint when it is rw.
            Case("vol-root-rw", ["--from", "vol", "--to", "vol",
                                 "--work-dir", fixture.dir,
                                 "--log-dir", os.path.join(log_root, "rootrw")],
                 fixture, 3, vol_root="rw"),
            # A stale stream prints busy/refused instead of BEGIN.  Both must
            # fail immediately with the device line, not after 180 s.
            Case("inst-busy", ["--from", "base", "--to", "base",
                               "--work-dir", fixture.dir,
                               "--log-dir", os.path.join(log_root, "instbusy")],
                 fixture, 3, volume_blocks=4194304, inst_busy=True),
            Case("inst-refused", ["--from", "base", "--to", "base",
                                  "--work-dir", fixture.dir,
                                  "--log-dir", os.path.join(log_root, "instrefused")],
                 fixture, 3, volume_blocks=4194304, inst_refused=True),
            # The 2026-10-28 failure: ok=N but no allocation reached the
            # volume header.  This must fail even though INST DONE is clean.
            Case("free-stuck", ["--from", "base", "--to", "base",
                                "--work-dir", fixture.dir,
                                "--log-dir", os.path.join(log_root, "freestuck")],
                 fixture, 3, volume_blocks=4194304, no_free_delta=True),
            Case("resume", ["--from", "auto", "--work-dir", fixture.dir,
                            "--log-dir", os.path.join(log_root, "resume")],
                 fixture, 0,
                 volume_blocks=4194304,
                 files={e.path: (e.size, e.sha.hex())
                        for e in bu.read_kist(fixture.base_stream).entries} |
                       {e.path: (e.size, e.sha.hex())
                        for e in bu.read_kist(fixture.cache_stream).entries}),
            Case("segment", ["--from", "base", "--to", "base",
                             "--work-dir", fixture.dir,
                             "--log-dir", os.path.join(log_root, "segment")],
                 fixture, 0, volume_blocks=4194304),
            Case("skel-only", ["--from", "skel", "--to", "skel",
                               "--work-dir", fixture.dir,
                               "--log-dir", os.path.join(log_root, "skel")],
                 fixture, 0, volume_blocks=4194304),
            Case("skel-bad-attrs", ["--from", "skel", "--to", "skel",
                                    "--work-dir", fixture.dir,
                                    "--log-dir", os.path.join(log_root, "skelbad")],
                 fixture, 3, volume_blocks=4194304, bad_attrs="/private/tmp"),
            Case("partial-base", ["--from", "base", "--to", "base",
                                  "--work-dir", fixture.dir,
                                  "--log-dir", os.path.join(log_root, "partial")],
                 fixture, 0, volume_blocks=4194304,
                 files={bu.read_kist(fixture.base_stream).entries[1].path:
                        (bu.read_kist(fixture.base_stream).entries[1].size,
                         bu.read_kist(fixture.base_stream).entries[1].sha.hex())}),
            Case("from-vol-resume", ["--from", "vol", "--work-dir", fixture.dir,
                                     "--log-dir", os.path.join(log_root, "volskip")],
                 fixture, 0, volume_blocks=4194304),
            Case("install-small", ["--from", "install", "--to", "install",
                                   "--work-dir", fixture.dir,
                                   "--log-dir", os.path.join(log_root, "small")],
                 fixture, 3, volume_blocks=16384),
            Case("bad-install-args", ["--dry-run", "--work-dir", fixture.dir,
                                      "--install-image", fixture.bad_install_image,
                                      "--log-dir", os.path.join(log_root, "badargs")],
                 fixture, 6),
            Case("corrupt-stream", ["--dry-run", "--work-dir", fixture.dir,
                                    "--base-stream", fixture.corrupt_base,
                                    "--log-dir", os.path.join(log_root, "corrupt")],
                 fixture, 6),
            Case("slot-a", ["--from", "vol", "--work-dir", fixture.dir,
                            "--log-dir", os.path.join(log_root, "slot")],
                 fixture, 2, slot="a"),
            Case("cs-killed", ["--from", "final", "--work-dir", fixture.dir,
                               "--log-dir", os.path.join(log_root, "cs")],
                 fixture, 3, volume_blocks=4194304, final="cs_killed"),
            Case("dsc-missing", ["--from", "final", "--work-dir", fixture.dir,
                                 "--log-dir", os.path.join(log_root, "dsc")],
                 fixture, 3, volume_blocks=4194304, final="dsc"),
            Case("panic-mid-cache", ["--from", "vol", "--work-dir", fixture.dir,
                                     "--log-dir", os.path.join(log_root, "panic")],
                 fixture, 4, panic_after=4),
            Case("silent-panic", ["--from", "vol", "--work-dir", fixture.dir,
                                  "--log-dir", os.path.join(log_root, "panic2")],
                 fixture, 4, panic_after=4, silent_panic=True),
        ]
        for case in cases:
            try:
                rc, out, err, board = run_case(case, fixture)
                ok = rc == case.expected
                if not ok:
                    failures += 1
                print("%s %-18s rc=%d expected=%d" % (
                    "PASS" if ok else "FAIL", case.name, rc, case.expected))
                if not ok:
                    print(err[-4000:])
                if case.name == "happy-path" and board is not None:
                    flashed = [os.path.basename(x) for x in board.fastboot.flashed]
                    assert os.path.basename(fixture.vol_image) in flashed, flashed
                    assert os.path.basename(fixture.install_image) in flashed, flashed
                    assert os.path.basename(fixture.final_image) in flashed
                    assert flashed.index(os.path.basename(fixture.install_image)) < \
                        flashed.index(os.path.basename(fixture.final_image)), flashed
                    assert "phase install: boot-inst.img" in out, out[-3000:]
                    assert "C-INST-GEOM" in out, out[-3000:]
                    assert "C-SKEL-ATTRS" in out, out[-3000:]
                    assert "phase skel: install skel.kist" in out, out[-3000:]
                    assert "INST STREAM v2 skeleton entries 16 bytes 153 wire 65536" \
                        in out, out[-3000:]
                    assert "OK /private/tmp 0 dir mode 01777 uid 0 gid 0" in out, \
                        out[-3000:]
                    assert len(board.device.sent_log) == 3, board.device.sent_log
                    assert board.device.sent_log[0][0] == 65536, board.device.sent_log
                    assert board.device.volume_blocks == 4194304
                if case.name == "resume" and board is not None:
                    assert os.path.basename(fixture.vol_image) not in \
                        [os.path.basename(x) for x in board.fastboot.flashed], \
                        board.fastboot.flashed
                    # base/cache are complete and are not re-sent; the 64 KiB
                    # skeleton is always re-applied by design.
                    assert len(board.device.sent_log) == 1, board.device.sent_log
                    assert board.device.sent_log[0][0] == 65536, board.device.sent_log
                if case.name == "from-vol-resume" and board is not None:
                    assert os.path.basename(fixture.vol_image) not in \
                        [os.path.basename(x) for x in board.fastboot.flashed], \
                        board.fastboot.flashed
                if case.name == "partial-base" and board is not None:
                    assert board.device.sent_log, "partial stream was not sent"
                    assert "skipped 1" in out, out[-2000:]
                if case.name == "segment" and board is not None:
                    assert len(board.device.sent_log) == 1, board.device.sent_log
                if case.name == "skel-only" and board is not None:
                    assert len(board.device.sent_log) == 1, board.device.sent_log
                    assert board.device.sent_log[0][0] == 65536, board.device.sent_log
                    assert "C-SKEL-ATTRS" in out, out[-3000:]
                    assert "mode 01777" in out, out[-3000:]
                if case.name in ("no-panel-banner", "panel-banner-noise"):
                    # The checkpoint must have been satisfied by the
                    # console-visible line, never by the panel-only one.
                    ok_lines = [l for l in out.splitlines()
                                if "[ OK ] C-BOOT-BANNER" in l]
                    assert ok_lines, out[-3000:]
                    assert "event thread started" in ok_lines[0], ok_lines[0]
                    assert "running (DSTS" not in ok_lines[0], ok_lines[0]
                    if case.name == "no-panel-banner":
                        assert "running (DSTS" not in out, out[-3000:]
                if case.name == "missing-mount-line":
                    assert "vol.reboot" in err, err[-3000:]
                    assert "KEBAB HFS" in err, err[-3000:]
                if case.name == "skel-bad-attrs":
                    assert "skel.attrs" in err, err[-3000:]
                    assert "uid 501 gid 20" in err, err[-3000:]
                if case.name == "vol-root-rw":
                    assert "VOL? root ro" in err, err[-3000:]
                    assert "root rw" in err or "VOL? root rw" in err, err[-3000:]
                if case.name == "inst-busy":
                    assert "INST busy" in err, err[-3000:]
                    assert "60 s" in err, err[-3000:]
                if case.name == "inst-refused":
                    assert "INST refused" in err, err[-3000:]
                if case.name == "free-stuck":
                    assert "send.free" in err, err[-3000:]
                    assert "free count barely moved" in err, err[-3000:]
            except Exception:
                failures += 1
                print("FAIL %-18s exception" % case.name)
                traceback.print_exc()

        if args.kernel_source:
            src = open(args.kernel_source, encoding="utf-8", errors="replace").read()
            errors = lint_commands(src)
            if errors:
                failures += 1
                print("FAIL command-lint")
                for e in errors:
                    print("  - " + e)
            else:
                print("PASS command-lint      %d commands in %s"
                      % (len(COMMAND_TABLE), args.kernel_source))
        else:
            print("SKIP command-lint      (pass --kernel-source)")
    finally:
        if not args.keep:
            import shutil
            shutil.rmtree(tmp, ignore_errors=True)
        else:
            print("kept %s" % tmp)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
