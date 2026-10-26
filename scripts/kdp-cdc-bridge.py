#!/usr/bin/env python3
"""
kdp-cdc-bridge.py -- carry serial KDP over a USB CDC-ACM console.

The device end of this bridge is xnu's kdp_serial.c state machine, reached
through the CDC-ACM port that bsd/dev/kebab_usb.c exposes.  On that wire a KDP
packet is not sent as a bare UDP datagram: it is framed as

    START  escape(crc32(packet), little endian)  START  escape(packet)  END

with START = 0xfa, END = 0xfb and ESC = 0xfe.  A byte that is START, END, ESC
or '\\n' is sent as ESC followed by its bitwise complement.  The CRC is zlib's
crc32 of the inner packet.  Kernel log text shares the same byte stream, so the
decoder simply ignores anything outside a frame.

This tool opens the serial port, decodes those frames, and relays the inner
Ethernet/IPv4/UDP packet to and from a local UDP socket.  Point LLDB at that
socket with:

    lldb
    (lldb) kdp-remote 127.0.0.1:41139

Only the Python standard library is used.  Serial access is termios on POSIX
and the Win32 comm API through ctypes on Windows; neither pyserial nor any
other package is needed.

Examples:
    python3 kdp-cdc-bridge.py --list-ports
    python3 kdp-cdc-bridge.py /dev/ttyACM0 --verbose
    python3 kdp-cdc-bridge.py COM7 --udp-port 41139
    python3 kdp-cdc-bridge.py --selftest
"""

import argparse
import glob
import os
import platform
import socket
import struct
import sys
import threading
import time
import zlib

# ---------------------------------------------------------------- protocol

KDP_START = 0xFA
KDP_END = 0xFB
KDP_ESC = 0xFE

# xnu's kdp_serial.c uses a 1518-byte buffer: 14-byte Ethernet header, 1500-byte
# MTU and a 4-byte Ethernet CRC that KDP never actually transmits.
KDP_MAX_PACKET = 1518

DEFAULT_BAUD = 115200
DEFAULT_UDP_PORT = 41139          # KDP_REMOTE_PORT in osfmk/kdp/kdp_protocol.h
DEFAULT_UDP_BIND = "127.0.0.1"

# KDP request numbers, osfmk/kdp/kdp_protocol.h.  Only used for --verbose.
KDP_REQUESTS = {
    0: "KDP_CONNECT",
    1: "KDP_DISCONNECT",
    2: "KDP_HOSTINFO",
    3: "KDP_VERSION",
    4: "KDP_MAXBYTES",
    5: "KDP_READMEM",
    6: "KDP_WRITEMEM",
    7: "KDP_READREGS",
    8: "KDP_WRITEREGS",
    9: "KDP_LOAD",
    10: "KDP_IMAGEPATH",
    11: "KDP_SUSPEND",
    12: "KDP_RESUMECPUS",
    13: "KDP_EXCEPTION",
    14: "KDP_TERMINATION",
    15: "KDP_BREAKPOINT_SET",
    16: "KDP_BREAKPOINT_REMOVE",
    17: "KDP_REGIONS",
    18: "KDP_REATTACH",
    19: "KDP_HOSTREBOOT",
    20: "KDP_READMEM64",
    21: "KDP_WRITEMEM64",
    22: "KDP_BREAKPOINT64_SET",
    23: "KDP_BREAKPOINT64_REMOVE",
    24: "KDP_KERNELVERSION",
    25: "KDP_READPHYSMEM64",
    26: "KDP_WRITEPHYSMEM64",
}


def crc32(data):
    """CRC used by kdp_serial.c (zlib polynomial, initial value 0)."""
    return zlib.crc32(data) & 0xFFFFFFFF


def escape_bytes(data):
    """Escape one run of bytes the way kdp_serial_out() does."""
    out = bytearray()
    for byte in data:
        if byte in (KDP_START, KDP_END, KDP_ESC, 0x0A):
            out.append(KDP_ESC)
            out.append((~byte) & 0xFF)
        else:
            out.append(byte)
    return bytes(out)


def serialize_packet(packet):
    """Build one serial-KDP frame around an inner Ethernet/IP/UDP packet."""
    if len(packet) > KDP_MAX_PACKET:
        raise ValueError("packet is %d bytes, the KDP serial limit is %d" %
                         (len(packet), KDP_MAX_PACKET))
    crc = struct.pack("<I", crc32(packet))
    return (bytes([KDP_START]) + escape_bytes(crc) + bytes([KDP_START]) +
            escape_bytes(packet) + bytes([KDP_END]))


class FrameDecoder:
    """Incremental decoder for the serial KDP framing.

    This mirrors kdp_unserialize_packet() in osfmk/kdp/kdp_serial.c, including
    its resynchronisation rule: a START seen while reading discards the partial
    frame and, if at least four bytes were collected, treats them as the CRC of
    a new frame.  That is what lets a frame be recovered after log text or a
    truncated transfer without resetting the port.  One deliberate difference:
    the kernel still accepts a frame that has no CRC prefix for compatibility
    with old clients, while this decoder rejects it.  This firmware always
    emits the CRC, so accepting a CRC-less frame would only hide corruption.
    """

    WAIT_START = 0
    READING = 1
    ESCAPED = 2

    def __init__(self, max_packet=KDP_MAX_PACKET):
        self.max_packet = max_packet
        self._state = self.WAIT_START
        self._buf = bytearray()
        self._have_crc = False
        self._crc = 0
        self.frames_ok = 0
        self.frames_bad_crc = 0
        self.frames_oversize = 0
        self.bytes_in = 0

    def reset(self):
        self._state = self.WAIT_START
        self._buf = bytearray()
        self._have_crc = False
        self._crc = 0

    def feed(self, data):
        """Consume bytes and return a list of complete, CRC-checked packets."""
        packets = []
        self.bytes_in += len(data)
        for byte in data:
            if self._state == self.WAIT_START:
                if byte == KDP_START:
                    self._state = self.READING
                    self._buf = bytearray()
                    self._have_crc = False
                continue

            if self._state == self.ESCAPED:
                self._buf.append((~byte) & 0xFF)
                self._state = self.READING
                continue

            # READING
            if byte == KDP_ESC:
                self._state = self.ESCAPED
                continue
            if byte == KDP_START:
                if len(self._buf) >= 4:
                    self._have_crc = True
                    self._crc = struct.unpack("<I", bytes(self._buf[:4]))[0]
                self._buf = bytearray()
                continue
            if byte == KDP_END:
                if self._have_crc and self._buf:
                    if crc32(self._buf) == self._crc:
                        self.frames_ok += 1
                        packets.append(bytes(self._buf))
                    else:
                        self.frames_bad_crc += 1
                elif self._buf:
                    # END without a CRC prefix: not a frame this firmware can
                    # have produced.
                    self.frames_bad_crc += 1
                self._state = self.WAIT_START
                self._buf = bytearray()
                self._have_crc = False
                continue

            if len(self._buf) >= self.max_packet:
                # Same policy as the kernel: forget the oversized frame and
                # wait for the next START rather than growing without bound.
                self.frames_oversize += 1
                self._state = self.WAIT_START
                self._buf = bytearray()
                self._have_crc = False
                continue
            self._buf.append(byte)
        return packets


def _mac(raw):
    return ":".join("%02x" % byte for byte in raw)


def describe_packet(packet):
    """Return a one-line human summary of an inner Ethernet/IP/UDP frame."""
    if len(packet) < 14:
        return "short inner packet (%d bytes)" % len(packet)
    dst, src, ethertype = struct.unpack("!6s6sH", packet[:14])
    text = "%s > %s ethertype 0x%04x" % (_mac(src), _mac(dst), ethertype)
    if ethertype != 0x0800 or len(packet) < 34:
        return text + " (not IPv4)"
    (version_ihl, _tos, total_len, _ident, _frag, ttl, proto, _csum,
     ip_src, ip_dst) = struct.unpack("!BBHHHBBH4s4s", packet[14:34])
    ihl = (version_ihl & 0x0F) * 4
    text += " ip %s > %s proto %u ttl %u len %u" % (
        socket.inet_ntoa(ip_src), socket.inet_ntoa(ip_dst), proto, ttl,
        total_len)
    if proto != 17 or len(packet) < 14 + ihl + 8:
        return text
    sport, dport, udp_len, _udp_csum = struct.unpack(
        "!HHHH", packet[14 + ihl:14 + ihl + 8])
    text += " udp %u > %u len %u" % (sport, dport, udp_len)
    kdp_off = 14 + ihl + 8
    if len(packet) >= kdp_off + 4:
        first, seq, length = struct.unpack("<BBH", packet[kdp_off:kdp_off + 4])
        request = first & 0x7F
        name = KDP_REQUESTS.get(request, "KDP_%u" % request)
        if first & 0x80:
            name += " reply"
        text += " %s seq %u len %u" % (name, seq, length)
    return text


# ------------------------------------------------------------ serial links


class MemoryLink:
    """A serial link backed by two byte arrays; used by --selftest."""

    def __init__(self):
        self.rx = bytearray()          # bytes the bridge should read
        self.tx = bytearray()          # bytes the bridge wrote
        self.closed = False

    def read(self, max_bytes, timeout):
        if not self.rx:
            return b""
        data = bytes(self.rx[:max_bytes])
        del self.rx[:max_bytes]
        return data

    def write(self, data):
        self.tx.extend(data)

    def close(self):
        self.closed = True


class PosixSerial:
    """termios-based CDC-ACM serial port (Linux and macOS)."""

    def __init__(self, path, baud):
        import termios

        self.path = path
        self.baud = baud
        self.fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        try:
            attrs = termios.tcgetattr(self.fd)
            # raw 8N1, no flow control: the port carries bytes, not a terminal
            attrs[0] = 0                                     # iflag
            attrs[1] = 0                                     # oflag
            attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
            attrs[3] = 0                                     # lflag
            speed = self._speed_constant(baud)
            attrs[4] = speed
            attrs[5] = speed
            attrs[6][termios.VMIN] = 0
            attrs[6][termios.VTIME] = 0
            termios.tcsetattr(self.fd, termios.TCSANOW, attrs)
            try:
                termios.tcflush(self.fd, termios.TCIOFLUSH)
            except termios.error:
                pass
        except Exception:
            os.close(self.fd)
            raise

    @staticmethod
    def _speed_constant(baud):
        import termios

        common = (50, 75, 110, 134, 150, 200, 300, 600, 1200, 1800, 2400,
                  4800, 9600, 19200, 38400, 57600, 115200, 230400, 460800,
                  500000, 576000, 921600, 1000000, 1152000, 1500000, 2000000,
                  2500000, 3000000, 3500000, 4000000)
        if baud in common:
            value = getattr(termios, "B%d" % baud, None)
            if value is not None:
                return value
        # CDC-ACM ignores the line coding; 115200 is a safe fallback and keeps
        # the tool usable with odd --baud values on drivers that reject them.
        print("warning: baud %d is not a standard termios rate, using 115200" %
              baud, file=sys.stderr)
        return termios.B115200

    def read(self, max_bytes, timeout):
        import select

        ready, _, _ = select.select([self.fd], [], [], timeout)
        if not ready:
            return b""
        try:
            return os.read(self.fd, max_bytes)
        except BlockingIOError:
            return b""
        except OSError as exc:
            if exc.errno in (5, 6):        # EIO/ENXIO: device went away
                raise
            return b""

    def write(self, data):
        import select

        view = memoryview(data)
        while view:
            try:
                written = os.write(self.fd, view)
            except BlockingIOError:
                select.select([], [self.fd], [], 1.0)
                continue
            if written <= 0:
                raise OSError("serial write returned %d" % written)
            view = view[written:]

    def close(self):
        try:
            os.close(self.fd)
        except OSError:
            pass


class WindowsSerial:
    """Win32 comm API through ctypes; stdlib only."""

    GENERIC_READ = 0x80000000
    GENERIC_WRITE = 0x40000000
    OPEN_EXISTING = 3
    INVALID_HANDLE_VALUE = -1
    ERROR_OPERATION_ABORTED = 995

    def __init__(self, path, baud):
        import ctypes
        import ctypes.wintypes as wt

        class DCB(ctypes.Structure):
            # Windows DCB, but with the bitfield word kept as a plain DWORD so
            # no compiler/ABI bitfield rules are involved.
            _fields_ = [
                ("DCBlength", wt.DWORD),
                ("BaudRate", wt.DWORD),
                ("Flags", wt.DWORD),
                ("wReserved", wt.WORD),
                ("XonLim", wt.WORD),
                ("XoffLim", wt.WORD),
                ("ByteSize", wt.BYTE),
                ("Parity", wt.BYTE),
                ("StopBits", wt.BYTE),
                ("XonChar", ctypes.c_char),
                ("XoffChar", ctypes.c_char),
                ("ErrorChar", ctypes.c_char),
                ("EofChar", ctypes.c_char),
                ("EvtChar", ctypes.c_char),
                ("wReserved1", wt.WORD),
            ]

        class COMMTIMEOUTS(ctypes.Structure):
            _fields_ = [
                ("ReadIntervalTimeout", wt.DWORD),
                ("ReadTotalTimeoutMultiplier", wt.DWORD),
                ("ReadTotalTimeoutConstant", wt.DWORD),
                ("WriteTotalTimeoutMultiplier", wt.DWORD),
                ("WriteTotalTimeoutConstant", wt.DWORD),
            ]

        self._ctypes = ctypes
        self._wt = wt
        self._kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        self.path = path
        self.baud = baud
        self._kernel32.CreateFileW.restype = wt.HANDLE
        self._kernel32.CreateFileW.argtypes = [
            wt.LPCWSTR, wt.DWORD, wt.DWORD, wt.LPVOID, wt.DWORD, wt.DWORD,
            wt.HANDLE]
        self._kernel32.GetCommState.argtypes = [
            wt.HANDLE, ctypes.POINTER(DCB)]
        self._kernel32.SetCommState.argtypes = [
            wt.HANDLE, ctypes.POINTER(DCB)]
        self._kernel32.SetupComm.argtypes = [
            wt.HANDLE, wt.DWORD, wt.DWORD]
        self._kernel32.SetCommTimeouts.argtypes = [
            wt.HANDLE, ctypes.POINTER(COMMTIMEOUTS)]
        self._kernel32.ReadFile.argtypes = [
            wt.HANDLE, ctypes.c_void_p, wt.DWORD,
            ctypes.POINTER(wt.DWORD), ctypes.c_void_p]
        self._kernel32.WriteFile.argtypes = [
            wt.HANDLE, ctypes.c_void_p, wt.DWORD,
            ctypes.POINTER(wt.DWORD), ctypes.c_void_p]
        self._kernel32.CloseHandle.argtypes = [wt.HANDLE]
        device = path if path.startswith("\\\\.\\") else "\\\\.\\" + path
        self.handle = self._kernel32.CreateFileW(
            device, self.GENERIC_READ | self.GENERIC_WRITE, 0,
            None, self.OPEN_EXISTING, 0, None)
        if self.handle in (None, ctypes.c_void_p(self.INVALID_HANDLE_VALUE).value):
            raise ctypes.WinError(ctypes.get_last_error())
        try:
            self._configure(DCB, COMMTIMEOUTS)
        except Exception:
            self.close()
            raise

    def _configure(self, DCB, COMMTIMEOUTS):
        import ctypes

        kernel32 = self._kernel32
        dcb = DCB()
        dcb.DCBlength = ctypes.sizeof(DCB)
        if not kernel32.GetCommState(self.handle, ctypes.byref(dcb)):
            raise ctypes.WinError(ctypes.get_last_error())
        dcb.BaudRate = self.baud
        dcb.ByteSize = 8
        dcb.Parity = 0                    # NOPARITY
        dcb.StopBits = 0                  # ONESTOPBIT
        # fBinary = 1, fDtrControl = DTR_CONTROL_ENABLE (1), fRtsControl =
        # RTS_CONTROL_ENABLE (1), all software/hardware flow control off.
        dcb.Flags = 0x1 | (1 << 4) | (1 << 12)
        if not kernel32.SetCommState(self.handle, ctypes.byref(dcb)):
            raise ctypes.WinError(ctypes.get_last_error())
        if not kernel32.SetupComm(self.handle, 1 << 16, 1 << 16):
            raise ctypes.WinError(ctypes.get_last_error())
        # Non-blocking-ish reads: return immediately with whatever is queued.
        timeouts = COMMTIMEOUTS(0xFFFFFFFF, 0, 0, 0, 0)
        if not kernel32.SetCommTimeouts(self.handle, ctypes.byref(timeouts)):
            raise ctypes.WinError(ctypes.get_last_error())

    def read(self, max_bytes, timeout):
        buf = self._ctypes.create_string_buffer(max_bytes)
        read = self._wt.DWORD(0)
        ok = self._kernel32.ReadFile(self.handle, buf, max_bytes,
                                     self._ctypes.byref(read), None)
        if not ok:
            err = self._ctypes.get_last_error()
            if err == self.ERROR_OPERATION_ABORTED:
                return b""
            raise self._ctypes.WinError(err)
        return buf.raw[:read.value]

    def write(self, data):
        if not data:
            return
        buf = self._ctypes.create_string_buffer(data, len(data))
        written = self._wt.DWORD(0)
        offset = 0
        while offset < len(data):
            ok = self._kernel32.WriteFile(
                self.handle, self._ctypes.byref(buf, offset),
                len(data) - offset, self._ctypes.byref(written), None)
            if not ok:
                raise self._ctypes.WinError(self._ctypes.get_last_error())
            if written.value == 0:
                raise OSError("serial write made no progress")
            offset += written.value

    def close(self):
        if getattr(self, "handle", None):
            self._kernel32.CloseHandle(self.handle)
            self.handle = None


def open_serial(path, baud):
    if sys.platform.startswith("win"):
        return WindowsSerial(path, baud)
    return PosixSerial(path, baud)


def _read_sysfs(path):
    try:
        with open(path, "r", encoding="ascii", errors="replace") as handle:
            return handle.read().strip()
    except OSError:
        return ""


def _linux_port_description(device):
    name = os.path.basename(os.path.realpath(device))
    base = "/sys/class/tty/%s/device" % name
    if not os.path.isdir(base):
        return ""
    vendor = _read_sysfs(os.path.join(base, "..", "idVendor"))
    product = _read_sysfs(os.path.join(base, "..", "idProduct"))
    manufacturer = _read_sysfs(os.path.join(base, "..", "manufacturer"))
    product_name = _read_sysfs(os.path.join(base, "..", "product"))
    parts = []
    if vendor or product:
        parts.append("%s:%s" % (vendor or "????", product or "????"))
    if manufacturer:
        parts.append(manufacturer)
    if product_name:
        parts.append(product_name)
    return " ".join(parts)


def list_serial_ports():
    """Return [(device, description), ...] for this platform."""
    entries = []
    system = platform.system()
    if system == "Linux":
        seen = set()
        for pattern in ("/dev/ttyACM*", "/dev/ttyUSB*", "/dev/ttyAMA*"):
            for device in sorted(glob.glob(pattern)):
                if device in seen:
                    continue
                seen.add(device)
                entries.append((device, _linux_port_description(device)))
        by_id = "/dev/serial/by-id"
        if os.path.isdir(by_id):
            for link in sorted(glob.glob(by_id + "/*")):
                entries.append((link, "-> %s" % os.path.realpath(link)))
    elif system == "Darwin":
        for pattern in ("/dev/cu.usb*", "/dev/cu.SLAB*", "/dev/cu.wch*",
                        "/dev/cu.usbserial*", "/dev/cu.usbmodem*"):
            for device in sorted(glob.glob(pattern)):
                entries.append((device, "USB serial"))
    elif system == "Windows":
        try:
            import winreg

            key = winreg.OpenKey(
                winreg.HKEY_LOCAL_MACHINE,
                r"HARDWARE\DEVICEMAP\SERIALCOMM")
            try:
                index = 0
                while True:
                    try:
                        name, value, _kind = winreg.EnumValue(key, index)
                    except OSError:
                        break
                    entries.append((str(value), str(name)))
                    index += 1
            finally:
                winreg.CloseKey(key)
        except OSError:
            pass
    else:
        for pattern in ("/dev/ttyACM*", "/dev/ttyUSB*", "/dev/cu.*"):
            for device in sorted(glob.glob(pattern)):
                entries.append((device, ""))
    return entries


# ------------------------------------------------------------------ relay


class Relay:
    """Framing + UDP relay, independent of how the serial link is opened."""

    def __init__(self, link, udp_socket, verbose=False):
        self.link = link
        self.udp = udp_socket
        self.udp.setblocking(False)
        self.verbose = verbose
        self.decoder = FrameDecoder()
        self.peer = None
        self.rx_frames = 0
        self.tx_frames = 0
        self.dropped = 0

    def _log(self, message):
        if self.verbose:
            print(message, flush=True)

    def serial_to_udp(self, data):
        """Decode serial bytes and forward every complete frame to the peer."""
        for packet in self.decoder.feed(data):
            self.rx_frames += 1
            self._log("[serial] frame %d: %d bytes: %s" %
                      (self.rx_frames, len(packet), describe_packet(packet)))
            if self.peer is None:
                self.dropped += 1
                self._log("[udp] dropping frame: no debugger has sent a packet")
                continue
            try:
                self.udp.sendto(packet, self.peer)
            except OSError as exc:
                self.dropped += 1
                self._log("[udp] send to %s failed: %s" % (self.peer, exc))

    def udp_to_serial(self):
        """Receive one UDP datagram (non-blocking) and frame it to the device."""
        try:
            packet, peer = self.udp.recvfrom(65535)
        except BlockingIOError:
            return False
        except OSError as exc:
            # WSAEWOULDBLOCK-style errors are also OSError on Windows.
            if getattr(exc, "errno", None) in (11, 35, 10035):
                return False
            raise
        if not packet:
            return False
        self.peer = peer
        self.tx_frames += 1
        self._log("[udp] packet %d from %s:%d: %d bytes: %s" %
                  (self.tx_frames, peer[0], peer[1], len(packet),
                   describe_packet(packet)))
        try:
            self.link.write(serialize_packet(packet))
        except ValueError as exc:
            self._log("[serial] dropping packet: %s" % exc)
            self.dropped += 1
        return True


class Bridge:
    """Owns the serial port, the UDP socket and the two relay directions."""

    def __init__(self, link, udp_port=DEFAULT_UDP_PORT,
                 udp_bind=DEFAULT_UDP_BIND, verbose=False):
        self.link = link
        self.udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.udp.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.udp.bind((udp_bind, udp_port))
        self.relay = Relay(link, self.udp, verbose=verbose)
        self.verbose = verbose
        self._stop = threading.Event()
        self._error = None

    def _reader(self):
        while not self._stop.is_set():
            try:
                data = self.link.read(8192, 0.1)
            except Exception as exc:          # device unplugged, etc.
                self._error = exc
                self._stop.set()
                return
            if data:
                self.relay.serial_to_udp(data)
            else:
                # Windows ReadFile() with the timeout setup above returns
                # immediately; do not turn the reader into a busy loop.
                time.sleep(0.005)

    def run(self):
        reader = threading.Thread(target=self._reader, name="kdp-serial-reader",
                                  daemon=True)
        reader.start()
        try:
            while not self._stop.is_set():
                ready, _, _ = _select_readable(self.udp, 0.05)
                if ready:
                    self.relay.udp_to_serial()
        except KeyboardInterrupt:
            print("\ninterrupted", file=sys.stderr)
        finally:
            self._stop.set()
            reader.join(timeout=1.5)
            self.udp.close()
            self.link.close()
        if self._error is not None:
            raise self._error


def _select_readable(sock, timeout):
    import select

    return select.select([sock], [], [], timeout)


# --------------------------------------------------------------- selftest


def _synthetic_kdp_packet(payload, request=7, seq=1,
                          src="10.0.0.2", dst="10.0.0.1",
                          sport=41139, dport=41140):
    """Build Ethernet/IPv4/UDP/KDP like the host stack does for LLDB."""
    eth = (bytes.fromhex("020000000002") + bytes.fromhex("020000000001") +
           struct.pack("!H", 0x0800))
    kdp = struct.pack("<BBHI", request & 0x7F, seq & 0xFF,
                      8 + len(payload), 0) + payload
    udp = struct.pack("!HHHH", sport, dport, 8 + len(kdp), 0)
    ip = struct.pack("!BBHHHBBH4s4s", 0x45, 0, 20 + len(udp) + len(kdp),
                     0, 0, 64, 17, 0,
                     socket.inet_aton(src), socket.inet_aton(dst))
    return eth + ip + udp + kdp


def _selftest_packet(rng, index):
    """A printable-ish synthetic KDP packet, seeded with escape-heavy bytes."""
    length = rng.randrange(8, 300)
    body = bytearray(rng.randrange(256) for _ in range(length))
    # Force every byte that needs escaping into the payload.
    for offset, byte in enumerate((KDP_START, KDP_END, KDP_ESC, 0x0A)):
        body[(index * 7 + offset) % length] = byte
    return _synthetic_kdp_packet(bytes(body), request=index % 27,
                                 seq=index & 0xFF)


def _check(condition, name, checks):
    checks.append((bool(condition), name))
    print("[%s] %s" % ("ok" if condition else "FAIL", name))
    return bool(condition)


def run_selftest():
    import random

    print("KDP CDC-ACM bridge selftest (no hardware)")
    checks = []

    # The CRC value the kernel's z_crc32() produces for the standard vector.
    _check(crc32(b"123456789") == 0xCBF43926,
           "crc32 reference vector 0xCBF43926", checks)

    # Escaping: no raw frame delimiter may survive inside the escaped runs.
    escaped = escape_bytes(bytes(range(256)) * 2)
    body = escaped
    _check(body.count(bytes([KDP_ESC])) > 0,
           "escaping used for reserved bytes", checks)
    unescaped = bytearray()
    index = 0
    ok = True
    while index < len(body):
        if body[index] == KDP_ESC:
            if index + 1 >= len(body):
                ok = False
                break
            unescaped.append((~body[index + 1]) & 0xFF)
            index += 2
        else:
            unescaped.append(body[index])
            index += 1
    _check(ok and bytes(unescaped) == bytes(range(256)) * 2,
           "escape/un-escape round-trip over all 256 byte values", checks)

    # Basic frame round-trip and exact byte layout.
    packet = _synthetic_kdp_packet(b"hello kdp")
    frame = serialize_packet(packet)
    _check(frame[0] == KDP_START and frame[-1] == KDP_END,
           "frame starts with 0xfa and ends with 0xfb", checks)
    decoder = FrameDecoder()
    got = decoder.feed(frame)
    _check(got == [packet], "single frame round-trip", checks)
    _check(decoder.frames_ok == 1 and decoder.frames_bad_crc == 0,
           "decoder statistics after a good frame", checks)

    # Chunk boundaries: feed the same frame in random-sized pieces.
    rng = random.Random(0xC0DEC)
    decoder = FrameDecoder()
    got = []
    offset = 0
    while offset < len(frame):
        step = rng.randrange(1, 17)
        got.extend(decoder.feed(frame[offset:offset + step]))
        offset += step
    _check(got == [packet], "frame split across arbitrary read chunks", checks)

    # Log text shares the stream; every frame must still come out.
    noisy = (b"KEBAB USB: configured, notify=5 bulk_out=2 bulk_in=3\r\n"
             b"panic(cpu 0 caller 0xfffffe0008123456): test\r\n"
             + frame + b"bsd_init: still booting\r\n" + frame)
    decoder = FrameDecoder()
    got = decoder.feed(noisy)
    _check(got == [packet, packet], "log text between and around frames", checks)

    # 200 random packets, fed in one stream, must all survive in order.
    decoder = FrameDecoder()
    sent = [_selftest_packet(rng, i) for i in range(200)]
    stream = b"".join(serialize_packet(item) for item in sent)
    got = []
    offset = 0
    while offset < len(stream):
        step = rng.randrange(1, 97)
        got.extend(decoder.feed(stream[offset:offset + step]))
        offset += step
    _check(got == sent, "200 synthetic packets, random chunking", checks)

    # Corruption must be detected and must not poison the next frame.
    bad = bytearray(frame)
    bad[len(bad) // 2] ^= 0x01
    decoder = FrameDecoder()
    got = decoder.feed(bytes(bad) + frame)
    _check(got == [packet] and decoder.frames_bad_crc >= 1,
           "corrupted CRC rejected, following frame recovered", checks)

    # Truncated frame at end of stream yields nothing, the next one does.
    decoder = FrameDecoder()
    got = decoder.feed(frame[:-1] + frame)
    _check(got == [packet], "truncated frame followed by a good frame", checks)

    # Garbage with stray STARTs must resynchronise.
    garbage = (b"\x00\xff\xfa\x11\x22\xfb" + b"x" * 33 + frame)
    decoder = FrameDecoder()
    got = decoder.feed(garbage)
    _check(got == [packet], "stray delimiters and garbage resynchronise", checks)

    # An oversized packet is dropped, and the decoder continues afterwards.
    oversize = bytes([KDP_START]) + escape_bytes(b"\x00" * 4) + \
        bytes([KDP_START]) + escape_bytes(b"\x55" * (KDP_MAX_PACKET + 10)) + \
        bytes([KDP_END])
    decoder = FrameDecoder()
    got = decoder.feed(oversize + frame)
    _check(got == [packet] and decoder.frames_oversize >= 1,
           "oversized packet dropped, next frame parsed", checks)

    # Relay in both directions, using a real localhost UDP socket pair.
    link = MemoryLink()
    bridge_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    bridge_sock.bind(("127.0.0.1", 0))
    peer_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    peer_sock.bind(("127.0.0.1", 0))
    peer_sock.settimeout(1.0)
    try:
        relay = Relay(link, bridge_sock, verbose=False)
        relay.peer = peer_sock.getsockname()
        relay.serial_to_udp(serialize_packet(packet))
        from_device, _addr = peer_sock.recvfrom(65535)
        _check(from_device == packet, "serial frame relayed to UDP peer",
               checks)

        from_host = _synthetic_kdp_packet(b"reply payload", request=2, seq=9)
        peer_sock.sendto(from_host, bridge_sock.getsockname())
        _check(relay.udp_to_serial(), "UDP datagram accepted from peer", checks)
        _check(decoder.feed(bytes(link.tx)) == [from_host],
               "UDP datagram framed to the serial link", checks)
    finally:
        bridge_sock.close()
        peer_sock.close()

    failed = [name for good, name in checks if not good]
    print()
    if failed:
        print("selftest: %d/%d passed, FAILED: %s" %
              (len(checks) - len(failed), len(checks), ", ".join(failed)))
        return 1
    print("selftest: %d/%d passed" % (len(checks), len(checks)))
    return 0


# -------------------------------------------------------------------- main


def _print_ports():
    ports = list_serial_ports()
    if not ports:
        print("no serial ports found", file=sys.stderr)
        return 1
    for device, description in ports:
        if description:
            print("%-32s %s" % (device, description))
        else:
            print(device)
    return 0


def _pick_port(explicit):
    if explicit:
        return explicit
    ports = list_serial_ports()
    devices = [device for device, _desc in ports]
    if len(devices) == 1:
        return devices[0]
    if not devices:
        raise SystemExit("no serial port given and none was found; "
                         "run with --list-ports")
    raise SystemExit("several serial ports found; pass one explicitly:\n  " +
                     "\n  ".join(devices))


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Relay serial KDP from a CDC-ACM port to LLDB over UDP.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="LLDB: (lldb) kdp-remote 127.0.0.1:41139")
    parser.add_argument("port", nargs="?",
                        help="CDC-ACM device, e.g. /dev/ttyACM0 or COM7")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD,
                        help="line rate requested from the port "
                             "(default: %d; CDC-ACM usually ignores it)" %
                             DEFAULT_BAUD)
    parser.add_argument("--udp-port", type=int, default=DEFAULT_UDP_PORT,
                        help="local UDP port for LLDB (default: %d)" %
                             DEFAULT_UDP_PORT)
    parser.add_argument("--udp-bind", default=DEFAULT_UDP_BIND,
                        help="address to bind the UDP socket to "
                             "(default: %s)" % DEFAULT_UDP_BIND)
    parser.add_argument("--verbose", action="store_true",
                        help="print every frame and packet summary")
    parser.add_argument("--list-ports", action="store_true",
                        help="list serial ports and exit")
    parser.add_argument("--selftest", action="store_true",
                        help="run the framing/relay selftest without hardware")
    args = parser.parse_args(argv)

    if args.selftest:
        return run_selftest()

    if args.list_ports:
        return _print_ports()

    if args.baud <= 0:
        parser.error("--baud must be positive")
    if not 0 < args.udp_port < 65536:
        parser.error("--udp-port must be between 1 and 65535")

    port = _pick_port(args.port)
    try:
        link = open_serial(port, args.baud)
    except Exception as exc:
        print("cannot open %s: %s" % (port, exc), file=sys.stderr)
        return 1

    print("KDP CDC-ACM bridge")
    print("  serial: %s @ %d" % (port, args.baud))
    print("  udp:    %s:%d (LLDB: kdp-remote %s:%d)" %
          (args.udp_bind, args.udp_port, args.udp_bind, args.udp_port))
    print("  waiting for the debugger; Ctrl-C to stop")

    bridge = None
    try:
        bridge = Bridge(link, args.udp_port, args.udp_bind, args.verbose)
    except OSError as exc:
        link.close()
        print("cannot bind UDP %s:%d: %s" %
              (args.udp_bind, args.udp_port, exc), file=sys.stderr)
        return 1
    try:
        bridge.run()
    except Exception as exc:
        print("bridge stopped: %s" % exc, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
