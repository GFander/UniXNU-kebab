#!/usr/bin/env python3
"""Drive the kebab userland installer over the CDC-ACM console.

The kernel side (bsd/dev/kebab_install.c) takes a KIST stream on the bulk-OUT
endpoint one chunk at a time and acknowledges each chunk with a line:

    INST ACK <n>        n = bytes consumed so far

so the host writes exactly one chunk, waits for the ACK to reach the number it
sent, and only then writes the next.  Nothing is pipelined: the device has one
chunk buffer and does not re-arm the endpoint until the chunk is written or
discarded, so this wait is the flow control rather than a politeness.

    send-userland.py userland.kist                 # INST BEGIN, writes files
    send-userland.py --dry userland.kist           # INST DRY, writes nothing
    send-userland.py --probe plan.txt --skip-list have.txt

`--probe` asks the device about each path in a manifest (INST CHECK) and writes
the ones that already have the right size and SHA-256 to the skip list, so the
next pack can leave their payloads out entirely.

Windows: the console is COM7.  Linux/macOS: pass --port.
"""

import argparse
import hashlib
import os
import re
import sys
import time

try:
    import serial
except ImportError:
    print("send-userland: pyserial is required (pip install pyserial)",
          file=sys.stderr)
    sys.exit(2)

CHUNK_DEFAULT = 64 * 1024
ACK_RE = re.compile(r"^INST ACK (\d+)$")
BEGIN_RE = re.compile(r"^INST BEGIN (\d+) (dry|write) chunk (\d+)$")
PLAN_RE = re.compile(r"^INST PLAN entries (\d+) bytes (\d+) wire (\d+)$")
DONE_RE = re.compile(r"^INST DONE(.*)$")
STREAM_RE = re.compile(r"^INST STREAM v(\d+) entries (\d+) bytes (\d+) wire (\d+)$")
CHECK_RE = re.compile(r"^INST CHECK (\S+) (MISSING|ERROR \d+|\d+ [0-9a-f]{64})$")
# Lines worth showing even while waiting for an ACK.
INFO_RE = re.compile(r"^(INST STREAM|OK |SKIP |PLAN |INST PLAN|FAIL |INST DONE)")


class Console(object):
    def __init__(self, port, baud=115200):
        self.ser = serial.Serial(port, baud, timeout=0, write_timeout=120)
        self.buf = b""
        self.pending = []
        self.verbose = False
        self.log = None

    def close(self):
        try:
            self.ser.close()
        except Exception:
            pass

    def _pump(self):
        n = self.ser.in_waiting
        if not n:
            return
        self.buf += self.ser.read(n)
        while b"\n" in self.buf:
            raw, self.buf = self.buf.split(b"\n", 1)
            line = raw.decode("utf-8", "replace").rstrip("\r")
            if line:
                self.pending.append(line)

    def write(self, data):
        self.ser.write(data)
        self.ser.flush()

    def wait(self, match, timeout, quiet=False, show=None):
        """Return the first line matching `match`; other lines are shown."""
        deadline = time.time() + timeout
        while time.time() < deadline:
            self._pump()
            while self.pending:
                line = self.pending.pop(0)
                m = match(line)
                if m:
                    return m
                if self.log is not None:
                    self.log.write(line + "\n")
                    self.log.flush()
                if not quiet or (show is not None and show.match(line)):
                    print("  %s" % line)
                if line.startswith("INST ABORT") or line.startswith("FAIL"):
                    raise RuntimeError("device refused: %s" % line)
            time.sleep(0.0005)
        raise TimeoutError("no reply within %.1f s" % timeout)

    def drain(self, seconds=0.3):
        deadline = time.time() + seconds
        out = []
        while time.time() < deadline:
            self._pump()
            while self.pending:
                out.append(self.pending.pop(0))
            time.sleep(0.002)
        return out


def stream_size(path):
    return os.path.getsize(path)


def send_stream(con, path, dry, chunk, timeout):
    size = stream_size(path)
    if size % chunk:
        raise SystemExit(
            "%s is %d bytes, not a multiple of the %d-byte chunk; re-pack with "
            "--chunk %d" % (path, size, chunk, chunk))
    cmd = "INST DRY %d\n" % size if dry else "INST BEGIN %d\n" % size
    print("== %s (%d bytes, %d chunks)" % (cmd.strip(), size, size // chunk))
    con.write(cmd.encode())
    m = con.wait(BEGIN_RE.match, 20)
    if int(m.group(1)) != size:
        raise SystemExit("device armed %s bytes, stream is %d" % (m.group(1), size))
    if int(m.group(3)) != chunk:
        raise SystemExit(
            "device chunk is %s bytes, host uses %d; rebuild with %s"
            % (m.group(3), chunk, m.group(3)))
    # The device prints INST STREAM once it has parsed the stream header,
    # which happens on the first chunk, so it cannot be waited for here.

    sent = 0
    t0 = time.time()
    last_report = t0
    with open(path, "rb") as f:
        while sent < size:
            data = f.read(chunk)
            if not data:
                raise SystemExit("stream ended at %d of %d bytes" % (sent, size))
            con.write(data)
            sent += len(data)
            target = sent
            # The device may complete in smaller pieces if the transport ever
            # falls back to the 512-byte arm; accept any ACK and wait for the
            # cumulative count to reach what was written.
            while True:
                m = con.wait(ACK_RE.match, timeout, quiet=True, show=INFO_RE)
                acked = int(m.group(1))
                if acked >= target:
                    break
            now = time.time()
            if now - last_report >= 5.0 or sent == size:
                el = now - t0
                print("   %6.1f / %6.1f MiB  %6.2f MiB/s  %.0f s"
                      % (sent / 1048576.0, size / 1048576.0,
                         sent / 1048576.0 / max(el, 1e-6), el))
                last_report = now
    el = time.time() - t0
    tail = con.wait(lambda l: DONE_RE.match(l) or PLAN_RE.match(l), 600)
    print("== %s" % tail.group(0))
    print("== %d bytes in %.1f s = %.2f MiB/s (%.2f MB/s)"
          % (sent, el, sent / 1048576.0 / max(el, 1e-6),
             sent / 1e6 / max(el, 1e-6)))
    return sent, el


def probe(con, manifest, skip_path):
    rows = []
    with open(manifest, "r") as f:
        for line in f:
            parts = line.rstrip("\n").split("\t")
            if len(parts) >= 3:
                rows.append((parts[0], int(parts[1]), parts[2].lower()))
    print("== probing %d destinations" % len(rows))
    have = []
    for path, size, digest in rows:
        con.write(("INST CHECK %s\n" % path).encode())
        m = con.wait(CHECK_RE.match, 60)
        got = m.group(2)
        if got.startswith("MISSING") or got.startswith("ERROR"):
            print("   MISSING %s" % path)
            continue
        gsize, ghash = got.split()
        if int(gsize) == size and ghash == digest:
            print("   SAME    %s" % path)
            have.append("%s\t%d\t%s" % (path, size, digest))
        else:
            print("   DIFFER  %s (%s vs %d bytes)" % (path, gsize, size))
    with open(skip_path, "w") as f:
        f.write("# destinations already correct on the device\n")
        for row in have:
            f.write(row + "\n")
    print("== %d of %d already correct -> %s" % (len(have), len(rows), skip_path))


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("stream", nargs="?", help="KIST stream to send")
    ap.add_argument("--port", default="COM7", help="console port (default COM7)")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--dry", action="store_true", help="send INST DRY")
    ap.add_argument("--chunk", type=int, default=CHUNK_DEFAULT,
                    help="chunk size, must equal the packer's (default %d)" % CHUNK_DEFAULT)
    ap.add_argument("--timeout", type=float, default=180.0,
                    help="seconds to wait for one ACK (default 180)")
    ap.add_argument("--probe", metavar="MANIFEST", help="INST CHECK each path")
    ap.add_argument("--skip-list", metavar="FILE", help="where --probe writes")
    ap.add_argument("--status", action="store_true", help="print INST? and exit")
    ap.add_argument("--verbose", action="store_true")
    ap.add_argument("--log", metavar="FILE", help="write every console line here")
    args = ap.parse_args()

    con = Console(args.port, args.baud)
    con.verbose = args.verbose
    if args.log:
        con.log = open(args.log, "w")
    try:
        left = con.drain(0.4)
        for line in left[-10:]:
            print("  [console] %s" % line)
        if args.status:
            con.write(b"INST?\n")
            time.sleep(0.5)
            for line in con.drain(0.5):
                print(line)
            return 0
        if args.probe:
            if not args.skip_list:
                raise SystemExit("--probe needs --skip-list FILE")
            probe(con, args.probe, args.skip_list)
            return 0
        if not args.stream:
            raise SystemExit("need a stream, or --probe/--status")
        send_stream(con, args.stream, args.dry, args.chunk, args.timeout)
        return 0
    except (TimeoutError, RuntimeError) as ex:
        print("send-userland: %s" % ex, file=sys.stderr)
        for line in con.drain(0.5):
            print("  [console] %s" % line)
        return 1
    finally:
        con.close()


if __name__ == "__main__":
    sys.exit(main())
