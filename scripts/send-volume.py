#!/usr/bin/env python3
"""Send a VEXT volume stream to the kebab volume writer over the console.

The kernel side (bsd/dev/kebab_vol.c) takes one 64 KiB chunk per bulk-OUT
transfer and acknowledges it with a cumulative byte count:

    VOL ACK <n>

so the host writes exactly one chunk, waits for the ACK to reach the number it
sent, and only then writes the next.  Nothing is pipelined: the device has one
chunk buffer and does not re-arm the endpoint until the chunk has been written
to the userdata partition, so this wait is the flow control, not politeness.

    send-volume.py KebabRoot.vext                     # VOL BEGIN ... CONFIRM
    send-volume.py --dry KebabRoot.vext               # VOL DRY, writes nothing
    send-volume.py --status                           # VOL?

A real send passes the volume size from the stream header and the literal
CONFIRM; the kernel refuses a write without both, and refuses a volume larger
than the userdata partition.  `--yes` only skips the host-side prompt.

Windows: the console is COM7.  Linux/macOS: pass --port.
"""

import argparse
import re
import struct
import sys
import time

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    print("send-volume: pyserial is required (pip install pyserial)",
          file=sys.stderr)
    sys.exit(2)

CHUNK = 64 * 1024
ACK_RE = re.compile(r"^VOL ACK (\d+)$")
DONE_RE = re.compile(r"^VOL DONE(?: extents| with)")
FAIL_RE = re.compile(r"^VOL (ABORT|FAIL|TIMEOUT|refused|disabled)")
BEGIN_RE = re.compile(r"^VOL BEGIN (\d+) volume (\d+) (dry|write) chunk (\d+)$")
INTEREST_RE = re.compile(r"^(VOL (STREAM|EXTENT|DONE|ABORT|FAIL|TIMEOUT|"
                         r"refused|disabled|PLAN)|VOL OLD vh|VOL NEW vh|"
                         r"KEBAB HFS:|hfs: mounted|INST\?)")


def find_port():
    for p in list_ports.comports():
        if p.vid == 0x1209 and p.pid == 0x0001:
            return p.device
    return None


class Console(object):
    def __init__(self, port, baud=115200):
        self.ser = serial.Serial(port, baud, timeout=0, write_timeout=180)
        self.buf = b""
        self.lines = []

    def close(self):
        try:
            self.ser.close()
        except Exception:
            pass

    def pump(self):
        n = self.ser.in_waiting
        if n:
            self.buf += self.ser.read(n)
        while b"\n" in self.buf:
            raw, self.buf = self.buf.split(b"\n", 1)
            line = raw.decode("utf-8", "replace").rstrip("\r")
            if line:
                self.lines.append(line)
                if INTEREST_RE.match(line):
                    print(line, flush=True)

    def write(self, data):
        self.ser.write(data)
        self.ser.flush()

    def wait(self, match, timeout, quiet=False):
        end = time.time() + timeout
        while time.time() < end:
            self.pump()
            while self.lines:
                line = self.lines.pop(0)
                if match(line):
                    return line
                if FAIL_RE.match(line):
                    raise SystemExit("device refused: " + line)
                if not quiet:
                    print("  " + line, flush=True)
            time.sleep(0.002)
        raise SystemExit("timeout after %.1fs waiting for %r" % (timeout, match))


def read_stream(path):
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < 32:
        raise SystemExit("stream too short")
    magic, version, hsize, volume, bsize, count, dbytes = \
        struct.unpack_from("<4sHHQIIQ", data, 0)
    if magic != b"VEXT" or version != 1 or hsize != 32:
        raise SystemExit("not a VEXT v1 stream")
    if len(data) % CHUNK:
        raise SystemExit("stream is not a multiple of %d" % CHUNK)
    expect = 32 + count * 16 + dbytes
    if expect > len(data) or len(data) - expect >= CHUNK:
        raise SystemExit("stream lengths disagree")
    return data, volume, bsize, count, dbytes


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("stream", nargs="?")
    ap.add_argument("--port", default=None)
    ap.add_argument("--dry", action="store_true")
    ap.add_argument("--status", action="store_true")
    ap.add_argument("--yes", action="store_true",
                    help="do not ask before a destructive write")
    args = ap.parse_args()

    port = args.port or find_port()
    if not port:
        raise SystemExit("no kebab console found (VID 1209 PID 0001)")

    con = Console(port)
    try:
        time.sleep(0.2)
        con.ser.reset_input_buffer()
        if args.status:
            con.write(b"VOL?\n")
            end = time.time() + 4
            while time.time() < end:
                con.pump()
                while con.lines:
                    print(con.lines.pop(0), flush=True)
                time.sleep(0.05)
            return 0
        if not args.stream:
            raise SystemExit("a stream path is required (or --status)")

        data, volume, bsize, count, dbytes = read_stream(args.stream)
        wire = len(data)
        print("stream %s: %d bytes, volume %d bytes (%d MiB), block %d, "
              "extents %d, data %d" % (args.stream, wire, volume,
                                       volume >> 20, bsize, count, dbytes),
              flush=True)
        if not args.dry and not args.yes:
            print("this replaces the HFS+ volume on userdata; everything on "
                  "it is destroyed", flush=True)
            answer = input("type the volume size in bytes to confirm: ").strip()
            if answer != str(volume):
                raise SystemExit("not confirmed")
        if args.dry:
            con.write(b"VOL DRY %d\n" % wire)
        else:
            con.write(b"VOL BEGIN %d %d CONFIRM\n" % (wire, volume))

        begin = con.wait(lambda l: BEGIN_RE.match(l) or False, 30)
        print(begin, flush=True)
        # VOL BEGIN is printed after the old header, so the endpoint is about
        # to be armed; a short pause keeps the first chunk out of the tty
        # buffer while the event thread is still finishing the print.
        time.sleep(0.3)

        sent = 0
        done = False
        start = time.time()
        while sent < wire and not done:
            chunk = data[sent:sent + CHUNK]
            con.write(chunk)
            sent += len(chunk)
            want = sent
            line = con.wait(lambda l: (ACK_RE.match(l) and
                                       int(ACK_RE.match(l).group(1)) == want)
                            or DONE_RE.match(l) or False, 180)
            if DONE_RE.match(line):
                done = True
                break
            if sent % (16 * CHUNK) == 0 or sent == wire:
                rate = sent / max(1e-6, time.time() - start) / (1024 * 1024)
                print("  %d/%d bytes (%.1f MiB/s)" % (sent, wire, rate),
                      flush=True)

        if not done:
            con.wait(lambda l: DONE_RE.match(l) or False, 60)
        print("done in %.1fs" % (time.time() - start), flush=True)
        return 0
    finally:
        con.close()


if __name__ == "__main__":
    sys.exit(main())
