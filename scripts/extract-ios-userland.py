#!/usr/bin/env python3
"""Pull individual members out of an Apple IPSW over HTTP range requests.

An IPSW is an ordinary ZIP, but a 5.9 GB one, and its biggest member is a
4.4 GB root filesystem DMG.  Fetching something small such as
`BuildManifest.plist` therefore costs an hour of transfer and gigabytes of
disk if you just download the archive.  This script reads the ZIP central
directory and the members you asked for, and nothing else.

What it handles
  * range requests over `http.client`, with redirects, retries and short reads
  * the archive is larger than 4 GiB, so the end-of-central-directory record
    is ZIP64 and the central directory offset lives in the ZIP64 locator
  * stored and deflated members, streamed, with CRC32 verified on the way out
  * members already on disk with the right size are skipped; a half-written
    member (`*.part`) is continued instead of restarted (stored members only,
    a deflate stream cannot be seeked into)
  * Mach-O and IM4P payloads in the manifest -- the 32-byte Mach-O header is
    parsed here, because no `file(1)` on Linux knows an arm64 Mach-O
  * the `koly` trailer of a DMG member plus its BLKX partition table, without
    downloading the DMG at all

Examples
    # what is in there?
    python3 scripts/extract-ios-userland.py URL --list

    # fetch a few small members
    python3 scripts/extract-ios-userland.py URL -o out \
        -m BuildManifest.plist -m RestoreVersion.plist

    # the root filesystem DMG: format, partitions, sizes (tail + plist only)
    python3 scripts/extract-ios-userland.py URL --dmg-info

    # ... and what filesystem it carries (inflates the first few MiB)
    python3 scripts/extract-ios-userland.py URL --dmg-probe
"""
import argparse
import bisect
import bz2
import http.client
import plistlib
import struct
import sys
import time
import urllib.parse
from collections import OrderedDict
import zlib
from dataclasses import dataclass
from pathlib import Path

try:  # only needed for LZFSE-compressed DMGs (see --dmg-probe)
    import lzfse as _lzfse
except ImportError:  # pragma: no cover
    _lzfse = None

USER_AGENT = "kebab-dev/ios-userland (+extract-ios-userland.py)"
CHUNK = 4 << 20
TAIL_PROBE = 96 << 10
UUID_LEN = 16


# --------------------------------------------------------------------------
# byte sources
# --------------------------------------------------------------------------
class SourceError(RuntimeError):
    pass


class HttpRangeFile:
    """A read-only random access view of a remote file, via Range requests."""

    def __init__(self, url, timeout=60, retries=6):
        self.url = url
        self.timeout = timeout
        self.retries = retries
        self._conn = None
        self._target = None
        self.requests = 0
        self.bytes_fetched = 0
        self._connect()
        self.size = self._probe_size()
        if not self.size:
            raise SourceError("server did not report a length for %s" % url)

    # -- connection -------------------------------------------------------
    def _connect(self):
        parts = urllib.parse.urlsplit(self.url)
        if parts.scheme not in ("http", "https"):
            raise SourceError("not an http(s) URL: %s" % self.url)
        host = parts.hostname
        port = parts.port or (443 if parts.scheme == "https" else 80)
        if parts.scheme == "https":
            self._conn = http.client.HTTPSConnection(host, port, timeout=self.timeout)
        else:
            self._conn = http.client.HTTPConnection(host, port, timeout=self.timeout)
        path = parts.path or "/"
        if parts.query:
            path += "?" + parts.query
        self._target = path

    def _close(self):
        if self._conn is not None:
            try:
                self._conn.close()
            except Exception:
                pass
        self._conn = None

    def _request(self, headers):
        """One HTTP GET, following redirects.  Returns (status, headers, body)."""
        for _ in range(6):
            try:
                self._conn.request("GET", self._target,
                                   headers={"User-Agent": USER_AGENT,
                                            "Accept-Encoding": "identity",
                                            **headers})
                resp = self._conn.getresponse()
                body = resp.read()
                status = resp.status
                hdrs = {k.lower(): v for k, v in resp.getheaders()}
                self.requests += 1
                self.bytes_fetched += len(body)
            except (http.client.HTTPException, OSError) as exc:
                self._close()
                self._connect()
                raise SourceError("request failed: %s" % exc)
            if status in (301, 302, 303, 307, 308) and "location" in hdrs:
                self.url = urllib.parse.urljoin(self.url, hdrs["location"])
                self._close()
                self._connect()
                continue
            return status, hdrs, body
        raise SourceError("too many redirects for %s" % self.url)

    def _probe_size(self):
        status, hdrs, body = self._request({"Range": "bytes=0-0"})
        if status == 206:
            cr = hdrs.get("content-range", "")
            if "/" in cr:
                total = cr.rsplit("/", 1)[1].strip()
                if total.isdigit():
                    return int(total)
            raise SourceError("206 without a usable Content-Range: %r" % cr)
        if status == 200:
            raise SourceError("server ignores Range requests (got 200), refusing "
                              "to fall back to a full download")
        raise SourceError("unexpected HTTP %d for %s" % (status, self.url))

    # -- reads ------------------------------------------------------------
    def read_at(self, offset, length):
        """Return `length` bytes at `offset`, or fewer at EOF."""
        if length <= 0:
            return b""
        want = min(length, self.size - offset)
        if want <= 0:
            return b""
        out = bytearray()
        while len(out) < want:
            out += self._read_once(offset + len(out), want - len(out))
        return bytes(out)

    def _read_once(self, offset, length):
        last = None
        for attempt in range(self.retries):
            try:
                status, hdrs, body = self._request(
                    {"Range": "bytes=%d-%d" % (offset, offset + length - 1)})
                if status == 206:
                    return body
                if status == 200:
                    raise SourceError("server dropped Range support mid-transfer")
                if status in (429, 500, 502, 503, 504):
                    last = "HTTP %d" % status
                else:
                    last = "HTTP %d (range %d+%d)" % (status, offset, length)
            except SourceError as exc:
                last = str(exc)
                self._close()
                self._connect()
            time.sleep(min(2 ** attempt * 0.25, 8.0))
        raise SourceError("giving up after %d tries: %s" % (self.retries, last))

    def close(self):
        self._close()


class LocalRangeFile:
    """Same interface, backed by a local file (a downloaded IPSW)."""

    def __init__(self, path):
        self.path = Path(path)
        self._fh = open(self.path, "rb")
        self._fh.seek(0, 2)
        self.size = self._fh.tell()
        self.requests = 0
        self.bytes_fetched = 0

    def read_at(self, offset, length):
        self._fh.seek(offset)
        data = self._fh.read(length)
        self.requests += 1
        self.bytes_fetched += len(data)
        return data

    def close(self):
        self._fh.close()


def open_source(src, timeout=60, retries=6):
    if src.startswith("http://") or src.startswith("https://"):
        return HttpRangeFile(src, timeout=timeout, retries=retries)
    return LocalRangeFile(src)


class LocalDmg:
    """A DMG that is already on disk, wearing the same interface as RemoteZip.

    `--dmg-info` and `--dmg-probe` only ever ask the archive for a member's
    data offset and then for byte ranges, so one extracted DMG can be
    inspected by exactly the same code.  That matters for members the archive
    stores *deflated*: a deflate stream cannot be seeked into, so their `koly`
    trailer is out of reach of a range request and the member has to be
    inflated first.  The SystemOS cryptex is such a member.
    """

    local = True

    def __init__(self, path):
        self.path = Path(path)
        self.src = LocalRangeFile(self.path)
        self.entry = Entry(name=self.path.name, method=0, crc=0,
                           csize=self.src.size, usize=self.src.size,
                           header_offset=0)

    def data_offset(self, entry):
        return 0

    def find(self, name):
        return self.entry

    def largest_dmg(self):
        return self.entry


# --------------------------------------------------------------------------
# ZIP, read through the byte source
# --------------------------------------------------------------------------
@dataclass
class Entry:
    name: str
    method: int
    crc: int
    csize: int
    usize: int
    header_offset: int

    @property
    def is_dir(self):
        return self.name.endswith("/")


ZIP64_EXTRA = 0x0001


class RemoteZip:
    def __init__(self, src):
        self.src = src
        self.comment = b""
        self._parse()

    # -- directory --------------------------------------------------------
    def _find_eocd(self):
        tail_len = min(self.src.size, TAIL_PROBE)
        tail = self.src.read_at(self.src.size - tail_len, tail_len)
        pos = tail.rfind(b"PK\x05\x06")
        while pos != -1 and pos + 22 > len(tail):
            pos = tail.rfind(b"PK\x05\x06", 0, pos)
        if pos == -1:
            raise SourceError("no end-of-central-directory record; not a ZIP?")
        eocd_off = self.src.size - tail_len + pos
        eocd = tail[pos:pos + 22]
        (sig, disk, cd_disk, n_disk, n_total,
         cd_size, cd_off, comment_len) = struct.unpack("<4s4H2LH", eocd)
        self.comment = tail[pos + 22:pos + 22 + comment_len]
        self._n_total, self._cd_size, self._cd_off = n_total, cd_size, cd_off
        # ZIP64: any saturated field means the real value is in the ZIP64 EOCD
        if 0xFFFFFFFF in (cd_off, cd_size) or 0xFFFF in (n_total,):
            self._read_zip64(eocd_off)

    def _read_zip64(self, eocd_off):
        loc = self.src.read_at(eocd_off - 20, 20)
        (sig, disk, z64_off, ndisks) = struct.unpack("<4sIQI", loc)
        if sig != b"PK\x06\x07":
            raise SourceError("ZIP64 locator missing at %d (got %r)"
                              % (eocd_off - 20, sig))
        rec = self.src.read_at(z64_off, 56)
        (sig, size, vmade, vneed, disk, cd_disk, n_disk, n_total,
         cd_size, cd_off) = struct.unpack("<4sQHHIIQQQQ", rec)
        if sig != b"PK\x06\x06":
            raise SourceError("ZIP64 EOCD not at %d (got %r)" % (z64_off, sig))
        self._n_total, self._cd_size, self._cd_off = n_total, cd_size, cd_off
        self.zip64 = True

    def _parse(self):
        self.zip64 = False
        self._find_eocd()
        blob = self.src.read_at(self._cd_off, self._cd_size)
        self.entries = []
        pos = 0
        while pos + 46 <= len(blob) and blob[pos:pos + 4] == b"PK\x01\x02":
            (sig, vmade, vneed, flags, method, mtime, mdate, crc, csize,
             usize, nlen, elen, clen, disk, iattr, eattr,
             hoff) = struct.unpack("<4s6H3L5H2L", blob[pos:pos + 46])
            name = blob[pos + 46:pos + 46 + nlen].decode("utf-8", "replace")
            extra = blob[pos + 46 + nlen:pos + 46 + nlen + elen]
            pos += 46 + nlen + elen + clen
            if 0xFFFFFFFF in (csize, usize, hoff):
                usize, csize, hoff = self._zip64_extra(extra, usize, csize, hoff)
            self.entries.append(Entry(name, method, crc, csize, usize, hoff))

    @staticmethod
    def _zip64_extra(extra, usize, csize, hoff):
        i = 0
        while i + 4 <= len(extra):
            tag, size = struct.unpack("<HH", extra[i:i + 4])
            if tag == ZIP64_EXTRA:
                body = extra[i + 4:i + 4 + size]
                vals = []
                for j in range(0, len(body) - 7, 8):
                    vals.append(struct.unpack("<Q", body[j:j + 8])[0])
                if usize == 0xFFFFFFFF and vals:
                    usize = vals.pop(0)
                if csize == 0xFFFFFFFF and vals:
                    csize = vals.pop(0)
                if hoff == 0xFFFFFFFF and vals:
                    hoff = vals.pop(0)
                return usize, csize, hoff
            i += 4 + size
        return usize, csize, hoff

    # -- members ----------------------------------------------------------
    def find(self, wanted):
        norm = wanted.lstrip("/")
        for e in self.entries:
            if e.name == wanted or e.name == norm:
                return e
        return None

    def data_offset(self, entry):
        """Offset of the member's payload in the archive."""
        local = self.src.read_at(entry.header_offset, 30)
        (sig, ver, flags, method, mtime, mdate, crc, csize, usize,
         nlen, elen) = struct.unpack("<4s5H3L2H", local)
        if sig != b"PK\x03\x04":
            raise SourceError("bad local header for %s at %d"
                              % (entry.name, entry.header_offset))
        return entry.header_offset + 30 + nlen + elen

    def read_member(self, entry, limit=None):
        """Whole member in memory -- only for small members."""
        start = self.data_offset(entry)
        raw = self.src.read_at(start, entry.csize)
        if entry.method == 0:
            data = raw
        elif entry.method == 8:
            data = zlib.decompressobj(-15).decompress(raw)
        else:
            raise SourceError("unsupported compression method %d for %s"
                              % (entry.method, entry.name))
        if limit is not None:
            data = data[:limit]
        if len(data) != entry.usize and limit is None:
            raise SourceError("short read for %s: %d of %d bytes"
                              % (entry.name, len(data), entry.usize))
        return data

    def largest_dmg(self):
        dmgs = [e for e in self.entries
                if e.name.lower().endswith(".dmg") and not e.is_dir]
        if not dmgs:
            return None
        return max(dmgs, key=lambda e: e.usize)


METHOD_NAMES = {0: "stored", 8: "deflate", 12: "bzip2", 14: "lzma", 93: "zstd"}


# --------------------------------------------------------------------------
# payload sniffing: Mach-O, IM4P, DMG
# --------------------------------------------------------------------------
CPU_TYPES = {
    7: "x86", 0x01000007: "x86_64", 12: "arm", 0x0100000C: "arm64",
    0x0200000C: "arm64_32", 18: "ppc", 0x01000012: "ppc64",
}
FILE_TYPES = {
    1: "object", 2: "execute", 3: "fvmlib", 4: "core", 5: "preload",
    6: "dylib", 7: "dylinker", 8: "bundle", 9: "dylib_stub",
    10: "dsym", 11: "kext", 12: "fileset",
}
# magic on disk -> (label, header size, struct endianness)
MH_MAGICS = {
    b"\xfe\xed\xfa\xce": ("Mach-O 32", 28, ">"),
    b"\xce\xfa\xed\xfe": ("Mach-O 32", 28, "<"),
    b"\xfe\xed\xfa\xcf": ("Mach-O 64", 32, ">"),
    b"\xcf\xfa\xed\xfe": ("Mach-O 64", 32, "<"),
}
FAT_MAGICS = {b"\xca\xfe\xba\xbe": ">", b"\xbe\xba\xfe\xca": "<",
              b"\xca\xfe\xba\xbf": ">", b"\xbf\xba\xfe\xca": "<"}


def describe_macho(data):
    """Return a one-line description of the Mach-O header, or None.

    The header is 28 bytes (32-bit) or 32 bytes (64-bit): magic, cputype,
    cpusubtype, filetype, ncmds, sizeofcmds, flags [, reserved].
    """
    if len(data) < 32:
        return None
    magic = data[0:4]
    if magic in MH_MAGICS:
        label, hdr_size, endian = MH_MAGICS[magic]
        (cputype, cpusubtype, filetype, ncmds, sizeofcmds,
         flags) = struct.unpack(endian + "iIIIII", data[4:28])
        ctype = CPU_TYPES.get(cputype & 0xFFFFFFFF, "0x%x" % (cputype & 0xFFFFFFFF))
        ftype = FILE_TYPES.get(filetype, "type%d" % filetype)
        extra = ""
        if hdr_size == 32 and len(data) >= 32:
            reserved = struct.unpack(endian + "I", data[28:32])[0]
            if reserved:
                extra = " reserved=0x%x" % reserved
        return ("%s %s %s, %d cmds, %d bytes of load commands, flags 0x%x%s"
                % (label, ctype, ftype, ncmds, sizeofcmds, flags, extra))
    if magic in FAT_MAGICS:
        endian = FAT_MAGICS[magic]
        nfat = struct.unpack(endian + "I", data[4:8])[0]
        if nfat > 32:
            return None
        parts = []
        for i in range(nfat):
            off = 8 + i * 20
            if off + 20 > len(data):
                break
            cputype, cpusub, offset, size, align = struct.unpack(
                endian + "iIIII", data[off:off + 20])
            ctype = CPU_TYPES.get(cputype & 0xFFFFFFFF,
                                  "0x%x" % (cputype & 0xFFFFFFFF))
            parts.append("%s@%d(%d bytes)" % (ctype, offset, size))
        return "Mach-O fat, %d arches: %s" % (nfat, ", ".join(parts))
    return None


def der_items(buf):
    """Yield (tag, value, declared length) for elements inside a DER body.

    Apple's IMG4 files are DER: SEQUENCE { IA5String "IM4P", IA5String type,
    IA5String description, OCTET STRING payload, ... }.  A truncated buffer is
    accepted, because the caller may only have the first few KiB of a member.
    """
    i = 0
    while i + 2 <= len(buf):
        tag = buf[i]
        i += 1
        length = buf[i]
        i += 1
        if length & 0x80:
            n = length & 0x7F
            if n == 0 or n > 4 or i + n > len(buf):
                return
            length = int.from_bytes(buf[i:i + n], "big")
            i += n
        yield tag, buf[i:i + length], length
        i += length
        if i >= len(buf):
            return


def describe_im4p(data):
    """Describe an IMG4/IM4P container and whatever payload it holds."""
    # DER form, which is what an IPSW actually contains
    if len(data) >= 4 and data[0] == 0x30:
        body = next(der_items(data), (None, None, 0))[1]
        if body is None:
            return None
        items = list(der_items(body))
        if len(items) >= 2 and items[0][1] == b"IM4P":
            kind = items[1][1].decode("latin-1", "replace")
            desc = items[2][1].decode("latin-1", "replace") if len(items) > 2 else ""
            payload = b""
            payload_len = 0
            for tag, val, declared in items[3:]:
                if tag == 0x04:
                    payload, payload_len = val, declared
                    break
            text = "IMG4/DER IM4P '%s'%s, %d byte payload" % (
                kind, " (%s)" % desc if desc else "", payload_len)
            if payload[:3] == b"bvx":
                if _lzfse is None:
                    text += ", lzfse-compressed (install the lzfse module to "
                    text += "see inside)"
                elif payload_len > len(payload):
                    text += ", lzfse-compressed (the whole member is needed "
                    text += "to decode it)"
                else:
                    try:
                        inner = _lzfse.decompress(payload)
                        text += ", lzfse -> %d bytes" % len(inner)
                        macho = describe_macho(inner[:4096])
                        if macho:
                            text += " -> " + macho
                    except Exception as exc:
                        text += ", lzfse decode failed: %s" % exc
                return text
            macho = describe_macho(payload[:4096])
            return text + (" -> " + macho if macho else
                           " -> starts %r" % payload[:8])

    # bare IM4P (the binary header some tools write)
    if len(data) < 12 or data[0:4] != b"IM4P":
        return None
    kind = data[4:8].decode("latin-1")
    desc_len = struct.unpack(">I", data[8:12])[0]
    if 12 + desc_len + 4 > len(data) or desc_len > 4096:
        return "IM4P '%s' (truncated header)" % kind
    desc = data[12:12 + desc_len].decode("latin-1", "replace")
    payload_off = 12 + desc_len
    payload_len = struct.unpack(">I", data[payload_off:payload_off + 4])[0]
    payload = data[payload_off + 4:payload_off + 4 + payload_len]
    inner = describe_macho(payload)
    text = "IM4P '%s'%s, %d bytes" % (kind, " (%s)" % desc if desc else "",
                                      payload_len)
    if inner:
        text += " -> " + inner
    else:
        text += " -> not Mach-O (payload starts %r)" % payload[:8]
    return text


def describe_payload(data):
    return describe_macho(data) or describe_im4p(data) or "-"


# ---- DMG (UDIF) ------------------------------------------------------------
# The trailer is 512 bytes.  hdiutil writes it little-endian, but Apple's IPSW
# images ship it big-endian, so both are parsed and scored against the member
# size instead of assuming one.
KOLY_LE = struct.Struct("<4sIIIQQQQQII16sII128sQQ120sII128sIQ12x")
KOLY_BE = struct.Struct(">4sIIIQQQQQII16sII128sQQ120sII128sIQ12x")
CHECKSUM_NAMES = {0: "none", 1: "CRC32", 2: "MD5", 3: "SHA1", 4: "SHA256"}
# Which codec a chunk type carries is only a convention; the chunk's own magic
# is authoritative.  This image's 0x80000007 chunks start with 'bvx2'/'bvxn',
# i.e. they are LZFSE, although several format tables call that code LZMA.
CHUNK_TYPES = {
    0x00000000: "zero", 0x00000001: "raw", 0x00000002: "zero",
    0x80000004: "adc", 0x80000005: "zlib", 0x80000006: "bzip2",
    0x80000007: "lzfse", 0x80000008: "lzfse?",
    0x7FFFFFFE: "comment", 0xFFFFFFFF: "end",
}
FORMAT_NAMES = {
    0x80000005: "UDZO (zlib)", 0x80000006: "UDBZ (bzip2)",
    0x80000007: "ULFO (lzfse)", 0x80000008: "ULFO (lzfse)",
}
CODEC_NAMES = {"lzfse": "ULFO (lzfse)", "zlib": "UDZO (zlib)",
               "bzip2": "UDBZ (bzip2)", "lzma": "ULMO (lzma)"}


def sniff_codec(head):
    """Guess the chunk codec from the first bytes of the data fork."""
    if head[:3] == b"bvx":
        return "lzfse"
    if head[:2] in (b"\x78\x01", b"\x78\x5e", b"\x78\x9c", b"\x78\xda"):
        return "zlib"
    if head[:3] == b"BZh":
        return "bzip2"
    if head[:1] == b"\x5d":
        return "lzma"
    return "unknown"


def _koly_dict(vals):
    (sig, version, header_size, flags, running_off, data_off, data_len,
     rsrc_off, rsrc_len, seg_no, seg_count, seg_id, ck_type, ck_size,
     ck_data, xml_off, xml_len, _res1, sum_type, sum_size, sum_data,
     image_variant, sector_count) = vals
    return {
        "version": version, "header_size": header_size, "flags": flags,
        "data_fork_offset": data_off, "data_fork_length": data_len,
        "rsrc_fork_offset": rsrc_off, "rsrc_fork_length": rsrc_len,
        "segment": (seg_no, seg_count), "segment_id": seg_id,
        "checksum": ck_type,
        "checksum_name": CHECKSUM_NAMES.get(ck_type, "?"),
        "checksum_size": ck_size,
        "xml_offset": xml_off, "xml_length": xml_len,
        "image_variant": image_variant, "sector_count": sector_count,
        "running_data_fork_offset": running_off,
    }


def _koly_score(k, member_size):
    """How plausible does this reading of the trailer look?"""
    score = 0
    if 1 <= k["version"] <= 8:
        score += 2
    if k["header_size"] == 512:
        score += 2
    if 0 < k["data_fork_length"] <= member_size:
        score += 3
    if k["data_fork_offset"] + k["data_fork_length"] <= member_size:
        score += 2
    if 0 < k["sector_count"] and k["sector_count"] * 512 >= k["data_fork_length"]:
        score += 2
    if 0 < k["xml_length"] and (k["data_fork_length"] + k["xml_length"] <=
                                member_size):
        score += 2
    if 1 <= k["image_variant"] <= 8:
        score += 1
    return score


def parse_koly(blob, member_size):
    """Return (koly, '<' or '>') for a 512-byte trailer, or (None, None)."""
    if len(blob) < 512 or blob[0:4] != b"koly":
        return None, None
    best, best_endian, best_score = None, None, -1
    for endian, st in (("<", KOLY_LE), (">", KOLY_BE)):
        k = _koly_dict(st.unpack(blob[:512]))
        score = _koly_score(k, member_size)
        if score > best_score:
            best, best_endian, best_score = k, endian, score
    return best, best_endian


# one BLKX table header: sig, version, first sector, sector count, data start,
# buffers requested, block descriptors, 6 reserved, checksum (type + size +
# 128-byte digest), number of runs.  204 bytes; the runs follow immediately.
# Apple writes the tables with the 'mish' signature, other producers use 'blkx'.
BLKX_SIGNATURES = (b"mish", b"blkx")
BLKX_HEAD = {
    "<": struct.Struct("<4sIQQQIIIIIIIIII128sI"),
    ">": struct.Struct(">4sIQQQIIIIIIIIII128sI"),
}


def parse_blkx(data, endian="<"):
    """Parse one BLKX table: partition header + run list."""
    st = BLKX_HEAD[endian]
    if len(data) < st.size or data[0:4] not in BLKX_SIGNATURES:
        return None
    (sig, version, first_sector, sector_count, data_start, buffers,
     blocks_desc, _r1, _r2, _r3, _r4, _r5, _r6, _ck_type, _ck_size,
     _ck_data, nblocks) = st.unpack(data[:st.size])
    runs = []
    pos = st.size
    for _ in range(nblocks):
        if pos + 40 > len(data):
            break
        ctype, _res, sector, count, comp_off, comp_len = struct.unpack(
            endian + "IIQQQQ", data[pos:pos + 40])
        runs.append({"type": ctype, "sector": sector, "count": count,
                     "comp_offset": comp_off, "comp_length": comp_len})
        pos += 40
    return {"first_sector": first_sector, "sector_count": sector_count,
            "data_start": data_start, "runs": runs}


def dmg_summary(koly, blkx_tables):
    """Compression format + per-partition sizes, derived from the run tables."""
    types = set()
    parts = []
    for name, table in blkx_tables:
        runs = table["runs"]
        csum = sum(r["comp_length"] for r in runs)
        for r in runs:
            if r["type"] in (0x7FFFFFFE, 0xFFFFFFFF):
                continue
            types.add(r["type"])
        parts.append({
            "name": name,
            "first_sector": table["first_sector"],
            "sectors": table["sector_count"],
            "bytes": table["sector_count"] * 512,
            "compressed": csum,
            "runs": len([r for r in runs if r["type"] not in
                         (0x7FFFFFFE, 0xFFFFFFFF)]),
            "types": sorted({r["type"] for r in runs if r["type"] not in
                             (0x7FFFFFFE, 0xFFFFFFFF)}),
        })
    data_types = types - {0x00000000, 0x00000002, 0x00000001}
    if len(data_types) == 1:
        fmt = FORMAT_NAMES.get(next(iter(data_types)),
                               "unknown (0x%x)" % next(iter(data_types)))
    elif not data_types:
        fmt = "uncompressed (raw/zero only)"
    else:
        fmt = "mixed: " + ", ".join(
            FORMAT_NAMES.get(t, "0x%x" % t) for t in sorted(data_types))
    return fmt, parts


# ---- partition tables + filesystem headers --------------------------------
def guid_str(b):
    """GUID bytes -> the usual 8-4-4-4-12 text (first three fields LE)."""
    if len(b) != 16:
        return "?"
    a, c, d = struct.unpack("<IHH", b[0:8])
    return "%08X-%04X-%04X-%s-%s" % (a, c, d, b[8:10].hex().upper(),
                                     b[10:16].hex().upper())


# partition type GUIDs that matter here
GPT_TYPES = {
    "7C3457EF-0000-11AA-AA11-00306543ECAC": "Apple_APFS",
    "48465300-0000-11AA-AA11-00306543ECAC": "Apple_HFS",
    "C12A7328-F81F-11D2-BA4B-00A0C93EC93B": "EFI System",
    "426F6F74-0000-11AA-AA11-00306543ECAC": "Apple_Boot",
}


def parse_partitions(img):
    """Partition table of a raw image: GPT behind a protective MBR, or APM."""
    out = {"kind": None, "ddm": None, "partitions": []}
    if len(img) < 1024:
        return out

    # Apple partition map (older images): 'ER' at 0, 'PM' entries at block 1
    sig, blk_size, blk_count = struct.unpack(">HHI", img[0:8])
    if sig == 0x4552:
        out["ddm"] = {"block_size": blk_size, "block_count": blk_count}
        off = (blk_size // 512) * 512
        while off + 512 <= len(img):
            e = img[off:off + 512]
            if struct.unpack(">H", e[0:2])[0] != 0x504D:
                break
            map_blocks, start, count = struct.unpack(">III", e[4:16])
            out["partitions"].append({
                "name": e[16:48].split(b"\0")[0].decode("latin-1"),
                "type": e[48:80].split(b"\0")[0].decode("latin-1"),
                "start": start, "blocks": count,
            })
            if len(out["partitions"]) >= map_blocks:
                break
            off += 512
        if out["partitions"]:
            out["kind"] = "apm"
            return out

    # GPT: protective MBR (type 0xEE) at LBA 0, header at LBA 1
    if img[510:512] == b"\x55\xaa" and img[450] == 0xEE and \
            img[512:520] == b"EFI PART":
        entries_lba, n_entries, entry_size = struct.unpack("<QII", img[584:600])
        out["kind"] = "gpt"
        off = entries_lba * 512
        for _ in range(min(n_entries, 128)):
            if off + entry_size > len(img):
                break
            e = img[off:off + entry_size]
            if e[0:16] == b"\0" * 16:
                off += entry_size
                continue
            type_guid = guid_str(e[0:16])
            first, last = struct.unpack("<QQ", e[32:48])
            name = e[56:128].decode("utf-16-le", "replace").split("\0")[0]
            out["partitions"].append({
                "name": name,
                "type": GPT_TYPES.get(type_guid, type_guid),
                "type_guid": type_guid,
                "start": first, "blocks": last - first + 1,
            })
            off += entry_size
        return out
    return out


def parse_apfs_nx(img, base):
    """Parse the APFS container superblock at `base`, if it is one."""
    b = img[base:base + 4096]
    if len(b) < 1024 or b[32:36] != b"NXSB":
        return None
    block_size, block_count = struct.unpack("<IQ", b[36:48])
    oid = struct.unpack("<Q", b[152:160])[0]
    omap = struct.unpack("<Q", b[160:168])[0]
    max_fs = struct.unpack("<I", b[180:184])[0]
    fs_oids = []
    for i in range(min(max_fs, 8)):
        o = struct.unpack("<Q", b[184 + i * 8:192 + i * 8])[0]
        if o:
            fs_oids.append(o)
    return {"block_size": block_size, "block_count": block_count,
            "uuid": b[72:88].hex(), "spaceman_oid": oid, "omap_oid": omap,
            "max_fs": max_fs, "fs_oids": fs_oids}


def parse_apfs_volume(blk):
    """Parse an APFS volume superblock ('APSB') from a 1 KiB buffer."""
    if len(blk) < 960 or blk[32:36] != b"APSB":
        return None
    fs_index = struct.unpack("<I", blk[36:40])[0]
    num_files, num_dirs = struct.unpack("<QQ", blk[184:200])
    num_symlinks = struct.unpack("<Q", blk[200:208])[0]
    vol_uuid = blk[240:256].hex()
    name = blk[704:960].split(b"\0")[0].decode("utf-8", "replace")
    name = "".join(c if c.isprintable() else "?" for c in name).strip()
    return {"fs_index": fs_index, "num_files": num_files,
            "num_dirs": num_dirs, "num_symlinks": num_symlinks,
            "uuid": vol_uuid, "name": name}


def parse_hfsplus(img, base):
    """Parse an HFS+ volume header at base+1024, if it is one."""
    h = img[base + 1024:base + 1024 + 512]
    if len(h) < 512:
        return None
    sig = h[0:2]
    if sig not in (b"H+", b"HX"):
        return None
    version, attributes = struct.unpack(">HH", h[2:6])
    file_count, folder_count = struct.unpack(">II", h[32:40])
    block_size, total_blocks = struct.unpack(">II", h[40:48])
    return {"signature": sig.decode(), "version": version,
            "attributes": attributes, "files": file_count,
            "folders": folder_count, "block_size": block_size,
            "total_blocks": total_blocks,
            "name": ""}


def scan_apfs_volume(reader, base, block_size, limit):
    """Look for an 'APSB' volume superblock in the first `limit` bytes.

    `nx_fs_oid[]` holds virtual oids that have to be resolved through the
    container's omap B-tree; until that walk exists, a bounded scan of the
    head of the container is enough to find the volume superblock in practice.
    """
    win = reader.read_at(base, limit)
    pos = win.find(b"APSB")
    while pos != -1:
        start = pos - 32
        if start >= 0 and start % block_size == 0:
            vol = parse_apfs_volume(win[start:start + 1024])
            if vol:
                return start, vol
        pos = win.find(b"APSB", pos + 1)
    return None, None


# --------------------------------------------------------------------------
# APFS from a bare container
# --------------------------------------------------------------------------
APFS_BS = 4096
APFS_TYPE_INODE = 3
APFS_TYPE_XATTR = 4
APFS_TYPE_FILE_EXTENT = 8
APFS_TYPE_DIR_REC = 9
APFS_ROOT_DIR = 2
APFS_OBJ_BTREE_NODE = 0x3
APFS_OBJ_FSTREE = 0xE          # o_subtype of every fs-tree node
DECMPFS_MAGIC = 0x636D7066
CMP_TYPE_RAW_XATTR = 1
CMP_TYPE_ZLIB_XATTR = 3
CMP_TYPE_ZLIB_RSRC = 4
CMP_TYPE_LZVN_XATTR = 7
CMP_TYPE_LZVN_RSRC = 8
CMP_TYPE_RAW_XATTR2 = 9
CMP_TYPE_RAW_RSRC = 10
CMP_TYPE_LZFSE_XATTR = 11
CMP_TYPE_LZFSE_RSRC = 12
CMP_TYPE_LZBITMAP_XATTR = 13
CMP_TYPE_LZBITMAP_RSRC = 14
DECMPFS_CHUNK = 65536
DECMPFS_NAMES = {3: "zlib xattr", 4: "zlib resource fork",
                 7: "LZVN xattr", 8: "LZVN resource fork",
                 11: "LZFSE xattr", 12: "LZFSE resource fork",
                 13: "LZBITMAP xattr", 14: "LZBITMAP resource fork"}


# ---- LZBITMAP ('ZBM\x09') ------------------------------------------------
# A Python port of the decompressor from libzbitmap by Ernesto A. Fernandez
# (MIT), the same code linux-apfs-rw carries.  Apple's own implementation is
# closed; this is the reference reverse-engineering.  One ZBM buffer holds
# several chunks of at most 32 KiB each, so one 64 KiB decmpfs chunk is two.
ZBM_MAGIC = b"ZBM\x09"
ZBM_MAX_DECMP_CHUNK = 0x8000
ZBM_BITMAP_COUNT = 12
ZBM_BITMAP_BASE = 3
ZBM_BITMAP_BYTECNT = 17
ZBM_MAX_PERIOD_BYTECNT = 2


def _zbm_u24(buf, i):
    return buf[i] | (buf[i + 1] << 8) | (buf[i + 2] << 16)


class ZbmNibblePtr:
    __slots__ = ("buf", "pos", "nibble")

    def __init__(self, buf, pos):
        self.buf = buf
        self.pos = pos
        self.nibble = 0

    def read(self, limit):
        if self.pos >= limit:
            raise SourceError("LZBITMAP: nibble read past end")
        if self.nibble == 0:
            val = self.buf[self.pos] & 0xF
            self.nibble = 1
        else:
            val = (self.buf[self.pos] >> 4) & 0xF
            self.nibble = 0
            self.pos += 1
        return val

    def rewind(self):
        if self.nibble == 0:
            self.nibble = 1
            self.pos -= 1
        else:
            self.nibble = 0


def _zbm_read_bitmaps(chunk):
    if len(chunk) < ZBM_BITMAP_BYTECNT:
        raise SourceError("LZBITMAP: chunk too short for bitmaps")
    start = len(chunk) - ZBM_BITMAP_BYTECNT
    state = [0]     # bit position

    def bit():
        val = (chunk[start + (state[0] >> 3)] >> (state[0] & 7)) & 1
        state[0] += 1
        return val

    out = []
    for _ in range(ZBM_BITMAP_COUNT):
        bmp = 0
        for i in range(8):
            bmp |= bit() << i
        pbc = 0
        for i in range(2):
            pbc |= bit() << i
        if pbc > ZBM_MAX_PERIOD_BYTECNT:
            raise SourceError("LZBITMAP: bad period byte count")
        out.append((bmp, pbc))
    return out


def _zbm_decompress_chunk(chunk, decmp_len, out):
    if len(chunk) < 15:
        raise SourceError("LZBITMAP: short compressed chunk")
    meta1 = _zbm_u24(chunk, 6)
    meta2 = _zbm_u24(chunk, 9)
    meta3 = _zbm_u24(chunk, 12)
    if meta1 >= len(chunk) or meta2 >= len(chunk) or meta3 >= len(chunk):
        raise SourceError("LZBITMAP: metadata offset out of range")
    end = len(chunk)
    data = 15
    p1, p2 = meta1, meta2
    nib = ZbmNibblePtr(chunk, meta3)
    bitmaps = _zbm_read_bitmaps(chunk)
    prewritten = len(out)
    period = 8
    written = 0

    while written < decmp_len:
        bmp_num = nib.read(end)
        if decmp_len - written <= 8:
            repeat = 1
        else:
            val = nib.read(end)
            if val != 0xF:
                nib.rewind()
                repeat = 1
            else:
                total = 4
                while val == 0xF:
                    val = nib.read(end)
                    total += val
                    if total > 0xFFFF:
                        raise SourceError("LZBITMAP: repetition overflow")
                repeat = total
        for _ in range(repeat):
            if bmp_num == 0xF:
                raise SourceError("LZBITMAP: invalid bitmap number")
            if bmp_num > ZBM_MAX_PERIOD_BYTECNT:
                idx = bmp_num - ZBM_BITMAP_BASE
                if idx >= ZBM_BITMAP_COUNT:
                    raise SourceError("LZBITMAP: bitmap index out of range")
                bmp, pbc = bitmaps[idx]
            else:
                if p2 >= end:
                    raise SourceError("LZBITMAP: truncated metadata 2")
                bmp = chunk[p2]
                pbc = bmp_num
                p2 += 1
            if pbc:
                if p1 + pbc > end:
                    raise SourceError("LZBITMAP: truncated metadata 1")
                period = int.from_bytes(chunk[p1:p1 + pbc], "little")
                p1 += pbc
            if period == 0:
                raise SourceError("LZBITMAP: zero period")
            for i in range(8):
                if written == decmp_len:
                    break
                if bmp & (1 << i):
                    if data >= end:
                        raise SourceError("LZBITMAP: truncated data area")
                    out.append(chunk[data])
                    data += 1
                else:
                    if prewritten + written < period:
                        raise SourceError("LZBITMAP: period before stream start")
                    out.append(out[-period])
                written += 1


def zbm_decompress(src):
    """Decompress one 'ZBM\\x09' buffer (one decmpfs chunk) to bytes."""
    if not src.startswith(ZBM_MAGIC):
        raise SourceError("LZBITMAP: bad magic")
    pos = 4
    out = bytearray()
    while True:
        if pos + 6 > len(src):
            raise SourceError("LZBITMAP: truncated chunk header")
        clen = _zbm_u24(src, pos)
        dlen = _zbm_u24(src, pos + 3)
        if clen > len(src) - pos:
            raise SourceError("LZBITMAP: chunk length past end of buffer")
        if dlen > ZBM_MAX_DECMP_CHUNK:
            raise SourceError("LZBITMAP: chunk larger than 32 KiB")
        chunk = src[pos:pos + clen]
        if clen == dlen + 6:
            out += chunk[6:6 + dlen]
        else:
            _zbm_decompress_chunk(chunk, dlen, out)
        pos += clen
        if dlen == 0:
            return bytes(out)


def lzfse_module():
    try:
        import lzfse
        return lzfse
    except ImportError:
        return None


def lzvn_decode(data, expected):
    """Decode one raw LZVN chunk by wrapping it in a one-block LZFSE stream."""
    mod = lzfse_module()
    if mod is None:
        raise SourceError("decmpfs LZVN needs the 'lzfse' Python module")
    out = mod.decompress(b"bvxn" + struct.pack("<II", expected, len(data))
                         + data + b"bvx$")
    if len(out) != expected:
        raise SourceError("LZVN decoded %d bytes, expected %d"
                          % (len(out), expected))
    return out


def lzfse_decode(data, expected):
    mod = lzfse_module()
    if mod is None:
        raise SourceError("decmpfs LZFSE needs the 'lzfse' Python module")
    out = mod.decompress(data)
    if len(out) != expected:
        raise SourceError("LZFSE decoded %d bytes, expected %d"
                          % (len(out), expected))
    return out


def decode_decmpfs_chunk(ctype, blob, expected):
    """Decode one 64 KiB chunk of a compressed resource fork."""
    if ctype == CMP_TYPE_ZLIB_RSRC:
        out = blob[1:] if blob[:1] == b"\xff" else zlib.decompress(blob)
    elif ctype == CMP_TYPE_LZVN_RSRC:
        out = blob[1:] if blob[:1] == b"\x06" else lzvn_decode(blob, expected)
    elif ctype == CMP_TYPE_RAW_RSRC:
        out = blob[1:] if blob[:1] == b"\xcc" else blob
    elif ctype == CMP_TYPE_LZFSE_RSRC:
        out = blob[1:] if blob[:1] == b"\xff" else lzfse_decode(blob, expected)
    elif ctype == CMP_TYPE_LZBITMAP_RSRC:
        if blob[:1] == b"\x5a":
            out = zbm_decompress(blob)
        elif blob and (blob[0] & 0x0F) == 0x0F:
            out = blob[1:]
        else:
            raise SourceError("LZBITMAP: unknown chunk prefix %r" % blob[:1])
    else:
        raise SourceError("unsupported decmpfs resource type %d" % ctype)
    if len(out) != expected:
        raise SourceError("decmpfs chunk decoded to %d bytes, expected %d"
                          % (len(out), expected))
    return out


def decompress_resource(ctype, rsrc, usize):
    """Decode a decmpfs resource fork (type 4 table, or absolute type 8/10/12)."""
    if usize == 0:
        return b""
    out = bytearray()
    if ctype == CMP_TYPE_ZLIB_RSRC:
        if len(rsrc) < 0x108:
            raise SourceError("short zlib resource fork")
        if struct.unpack(">I", rsrc[:4])[0] != 0x100:
            raise SourceError("unexpected zlib resource-fork header size")
        count = struct.unpack("<I", rsrc[0x104:0x108])[0]
        if count != (usize + DECMPFS_CHUNK - 1) // DECMPFS_CHUNK:
            raise SourceError("resource fork has %d chunks, expected %d"
                              % (count, (usize + DECMPFS_CHUNK - 1)
                                 // DECMPFS_CHUNK))
        for i in range(count):
            off, clen = struct.unpack("<II", rsrc[0x108 + i * 8:
                                                0x110 + i * 8])
            blob = rsrc[0x104 + off:0x104 + off + clen]
            if len(blob) != clen:
                raise SourceError("short decmpfs chunk %d" % i)
            out += decode_decmpfs_chunk(
                ctype, blob, min(DECMPFS_CHUNK, usize - len(out)))
        return bytes(out)

    count = (usize + DECMPFS_CHUNK - 1) // DECMPFS_CHUNK
    if len(rsrc) < 4 * (count + 1):
        raise SourceError("short absolute-offset chunk table")
    offs = [struct.unpack("<I", rsrc[4 * i:4 * i + 4])[0]
            for i in range(count + 1)]
    if offs != sorted(offs):
        raise SourceError("decmpfs chunk offsets are not ascending")
    for i in range(count):
        out += decode_decmpfs_chunk(
            ctype, rsrc[offs[i]:offs[i + 1]],
            min(DECMPFS_CHUNK, usize - len(out)))
    return bytes(out)


def parse_inode_val(val):
    """Decode the fixed part of a j_inode_val_t (92 bytes)."""
    if len(val) < 92:
        return None
    return {
        "internal_flags": struct.unpack("<Q", val[48:56])[0],
        "bsd_flags": struct.unpack("<I", val[68:72])[0],
        "mode": struct.unpack("<H", val[80:82])[0],
        "size": struct.unpack("<Q", val[84:92])[0],
    }


def inode_dstream_size(val):
    """Return j_dstream_t.size from an inode's xfields, or None."""
    if len(val) < 96:
        return None
    count, _used = struct.unpack("<HH", val[92:96])
    pos = 96
    for _ in range(count):
        if pos + 4 > len(val):
            break
        xt, _xf, xs = struct.unpack("<BBH", val[pos:pos + 4])
        data = val[pos + 4:pos + 4 + xs]
        if xt == 8 and len(data) >= 40:      # XFIELD_TYPE_DSTREAM
            return struct.unpack("<Q", data[:8])[0]
        pos += 4 + xs
        if xs % 8:
            pos += 8 - (xs % 8)
    # Some inodes carry the name xfield with an extra word, so the fixed walk
    # can miss the dstream; fall back to the shape test apfs-get.py uses.
    for pos in range(92, len(val) - 39, 4):
        size, alloced, crypto, written, read = struct.unpack(
            "<QQQQQ", val[pos:pos + 40])
        if not (0 < size <= alloced <= (1 << 40)):
            continue
        if alloced % APFS_BS or crypto:
            continue
        if alloced != ((size + APFS_BS - 1) // APFS_BS) * APFS_BS:
            continue
        if written > alloced or read > alloced:
            continue
        return size
    return None


class RawApfsImage:
    """Path lookup inside a bare APFS container -- no UDIF, no omap.

    A cryptex member is not a disk image in the UDIF sense: inflated, it *is*
    an APFS container, so block N sits at byte N*4096 and nothing has to be
    translated.  What is more, every fs-tree node is a physical object whose
    o_subtype is OBJECT_TYPE_FSTREE (0xe), so the leaves can be found by
    reading block headers.  The container's omap B-tree -- the structure that
    blocks the OS DMG reader -- is never needed to reach a path or an extent.
    """

    def __init__(self, src, size, window=8 << 20):
        self.src = src
        self.size = size
        head = src.read_at(0, APFS_BS)
        if len(head) < 1024 or head[32:36] != b"NXSB":
            raise SourceError("no APFS container superblock at offset 0")
        self.block_size, self.block_count = struct.unpack("<IQ", head[36:48])
        self.uuid = head[72:88].hex()
        self.children = {}       # dir id -> {name: child id}
        self.extents = {}        # file id -> [(logical, phys, length)]
        self.inodes = {}         # file id -> fixed inode fields + raw value
        self.xattrs = {}         # file id -> {name: {kind, data/oid+size}}
        self.leaves = 0
        self.records = 0
        self._harvest(window)

    def _harvest(self, window):
        best_xid = {}
        bs = self.block_size or APFS_BS
        for base in range(0, self.size, window):
            buf = self.src.read_at(base, min(window, self.size - base))
            for i in range(0, len(buf) - bs + 1, bs):
                blk = buf[i:i + bs]
                if (struct.unpack("<I", blk[24:28])[0] & 0xFFFF
                        != APFS_OBJ_BTREE_NODE):
                    continue
                if struct.unpack("<I", blk[28:32])[0] != APFS_OBJ_FSTREE:
                    continue
                flags, _level, nkeys = struct.unpack("<HHI", blk[32:40])
                if not (flags & 0x2) or not (0 < nkeys < 4000) or (flags & 0x4):
                    continue
                self.leaves += 1
                xid = struct.unpack("<Q", blk[16:24])[0]
                tlen = struct.unpack("<H", blk[42:44])[0]
                data = blk[56:]
                # a root node carries a 40-byte btree_info_t footer, so its
                # value area ends short of the block end (see IOS-USERLAND.md)
                val_end = bs - 40 if (flags & 0x1) else bs
                for k in range(nkeys):
                    koff, klen, voff, vlen = struct.unpack(
                        "<HHHH", data[k * 8:k * 8 + 8])
                    key = data[tlen + koff:tlen + koff + klen]
                    if len(key) < 8 or len(key) < klen:
                        continue
                    self.records += 1
                    j = struct.unpack("<Q", key[:8])[0]
                    start = val_end - voff
                    self._record(j >> 60, j & 0x0FFFFFFFFFFFFFFF, key,
                                 blk[start:start + vlen], xid, best_xid)

    def _record(self, typ, obj, key, val, xid, best_xid):
        if typ == APFS_TYPE_INODE:
            ino = parse_inode_val(val)
            if ino is None:
                return
            old = self.inodes.get(obj)
            if old is not None and old["xid"] > xid:
                return
            ino["xid"] = xid
            ino["val"] = val
            self.inodes[obj] = ino
        elif typ == APFS_TYPE_DIR_REC:
            nlen = struct.unpack("<I", key[8:12])[0] & 0x3FF
            if nlen < 2 or 12 + nlen > len(key) or len(val) < 8:
                return
            name = key[12:12 + nlen - 1]
            if not all(32 <= c < 127 for c in name) or key[11 + nlen] != 0:
                return
            name = name.decode()
            if best_xid.get((obj, name), -1) > xid:
                return
            best_xid[(obj, name)] = xid
            self.children.setdefault(obj, {})[name] = \
                struct.unpack("<Q", val[:8])[0]
        elif typ == APFS_TYPE_FILE_EXTENT:
            if len(key) < 16 or len(val) < 24:
                return
            logical = struct.unpack("<Q", key[8:16])[0]
            if best_xid.get((obj, logical), -1) > xid:
                return
            best_xid[(obj, logical)] = xid
            laf, pblk, _ = struct.unpack("<QQQ", val[:24])
            self.extents.setdefault(obj, []).append(
                (logical, pblk, laf & 0x00FFFFFFFFFFFF))
        elif typ == APFS_TYPE_XATTR:
            if len(key) < 10 or len(val) < 4:
                return
            nlen = struct.unpack("<H", key[8:10])[0]
            name = (key[10:10 + nlen].split(b"\0")[0]
                    .decode("utf-8", "replace"))
            flags, dlen = struct.unpack("<HH", val[:4])
            if flags & 1:                       # XATTR_DATA_STREAM
                if len(val) < 52:
                    return
                self.xattrs.setdefault(obj, {})[name] = {
                    "kind": "stream",
                    "oid": struct.unpack("<Q", val[4:12])[0],
                    "size": struct.unpack("<Q", val[12:20])[0]}
            else:
                self.xattrs.setdefault(obj, {})[name] = {
                    "kind": "inline", "data": val[4:4 + dlen]}

    def lookup(self, path):
        """Return (file id, resolved prefix, missing component)."""
        cur, trail = APFS_ROOT_DIR, []
        for part in [p for p in path.split("/") if p]:
            nxt = self.children.get(cur, {}).get(part)
            if nxt is None:
                return None, "/" + "/".join(trail), part
            trail.append(part)
            cur = nxt
        return cur, "/" + "/".join(trail), None

    def is_dir(self, fid):
        return fid in self.children or (
            fid in self.inodes and self.inodes[fid]["mode"] & 0xF000 == 0x4000)

    def is_compressed(self, fid):
        return self.decmpfs_info(fid) is not None

    def decmpfs_info(self, fid):
        xa = self.xattrs.get(fid, {}).get("com.apple.decmpfs")
        if xa is None or xa["kind"] != "inline" or len(xa["data"]) < 16:
            return None
        magic, ctype, usize = struct.unpack("<IIQ", xa["data"][:16])
        if magic != DECMPFS_MAGIC:
            return None
        return {"type": ctype, "size": usize, "attr": xa["data"][16:]}

    def decompress(self, fid, dec):
        ctype, usize, attr = dec["type"], dec["size"], dec["attr"]
        if ctype in (CMP_TYPE_RAW_XATTR, CMP_TYPE_RAW_XATTR2):
            out = attr
        elif ctype == CMP_TYPE_ZLIB_XATTR:
            out = attr[1:] if attr[:1] == b"\xff" else zlib.decompress(attr)
        elif ctype == CMP_TYPE_LZVN_XATTR:
            out = attr[1:] if attr[:1] == b"\x06" else lzvn_decode(attr, usize)
        elif ctype == CMP_TYPE_LZFSE_XATTR:
            out = attr[1:] if attr[:1] == b"\xff" else lzfse_decode(attr, usize)
        elif ctype == CMP_TYPE_LZBITMAP_XATTR:
            out = zbm_decompress(attr)
        elif ctype in (CMP_TYPE_ZLIB_RSRC, CMP_TYPE_LZVN_RSRC,
                       CMP_TYPE_RAW_RSRC, CMP_TYPE_LZFSE_RSRC,
                       CMP_TYPE_LZBITMAP_RSRC):
            rv = self.xattrs.get(fid, {}).get("com.apple.ResourceFork")
            if rv is None or rv["kind"] != "stream":
                raise SourceError("inode %d: decmpfs resource fork is missing"
                                  % fid)
            rsrc = self._read_extents(rv["oid"])[:rv["size"]]
            if len(rsrc) != rv["size"]:
                raise SourceError("inode %d: resource fork is %d bytes, "
                                  "expected %d" % (fid, len(rsrc), rv["size"]))
            out = decompress_resource(ctype, rsrc, usize)
        else:
            raise SourceError("inode %d: unsupported decmpfs type %d"
                              % (fid, ctype))
        if len(out) != usize:
            raise SourceError("inode %d: decmpfs decoded %d bytes, expected %d"
                              % (fid, len(out), usize))
        return bytes(out)

    def _read_extents(self, fid):
        out = bytearray()
        for logical, phys, length in sorted(self.extents.get(fid, [])):
            if logical > len(out):
                out += b"\0" * (logical - len(out))     # sparse hole
            data = self.src.read_at(phys * self.block_size, length)
            if logical < len(out):
                data = data[len(out) - logical:]
            out += data
        return bytes(out)

    def read_file(self, fid):
        dec = self.decmpfs_info(fid) if self.is_compressed(fid) else None
        if dec is not None:
            return self.decompress(fid, dec)
        out = bytearray(self._read_extents(fid))
        size = self.size_of(fid)
        if len(out) < size:
            out += b"\0" * (size - len(out))
        del out[size:]
        return bytes(out)

    def size_of(self, fid):
        """Logical size: inode, then its dstream, then extent bytes."""
        ino = self.inodes.get(fid)
        if ino:
            if ino["size"]:
                return ino["size"]
            ds = inode_dstream_size(ino["val"])
            if ds is not None:
                return ds
        return sum(e[2] for e in self.extents.get(fid, []))


# --------------------------------------------------------------------------
# extraction
# --------------------------------------------------------------------------
def safe_dest(outdir, name):
    parts = [p for p in name.replace("\\", "/").split("/") if p not in ("", ".")]
    if any(p == ".." for p in parts):
        raise SourceError("refusing member with '..' in the name: %s" % name)
    return outdir.joinpath(*parts)


def extract_member(zipf, entry, outdir, force=False, verify_crc=True):
    dest = safe_dest(outdir, entry.name)
    dest.parent.mkdir(parents=True, exist_ok=True)

    if dest.exists() and not force:
        if dest.stat().st_size == entry.usize:
            crc_ok = True
            if verify_crc and entry.usize <= (64 << 20):
                crc_ok = zlib.crc32(dest.read_bytes()) & 0xFFFFFFFF == entry.crc
            if crc_ok:
                return dest, "skipped (already extracted, size matches)"

    start = zipf.data_offset(entry)
    part = dest.with_name(dest.name + ".part")
    resume = 0
    if part.exists() and not force and entry.method == 0:
        resume = part.stat().st_size
        if resume >= entry.csize:
            part.unlink()
            resume = 0
    elif part.exists() and not force:
        part.unlink()  # a deflate stream cannot be resumed

    dec = zlib.decompressobj(-15) if entry.method == 8 else None
    crc = 0
    written = 0
    head = b""
    mode = "ab" if resume else "wb"
    with open(part, mode) as fh:
        if resume:
            # the bytes already on disk still have to be counted for the CRC
            crc = 0
            written = 0
            with open(part, "rb") as old:
                while True:
                    chunk = old.read(CHUNK)
                    if not chunk:
                        break
                    crc = zlib.crc32(chunk, crc)
                    written += len(chunk)
                    if len(head) < 4096:
                        head = (head + chunk)[:4096]
        pos = resume
        while pos < entry.csize:
            n = min(CHUNK, entry.csize - pos)
            buf = zipf.src.read_at(start + pos, n)
            if not buf:
                raise SourceError("stream ended early at %d of %d for %s"
                                  % (pos, entry.csize, entry.name))
            pos += len(buf)
            raw = buf if dec is None else dec.decompress(buf)
            if raw:
                fh.write(raw)
                crc = zlib.crc32(raw, crc)
                written += len(raw)
                if len(head) < 4096:
                    head = (head + raw)[:4096]
            if entry.usize > (16 << 20):
                pct = 100.0 * pos / entry.csize if entry.csize else 100.0
                print("\r    %s: %5.1f%% (%d / %d bytes)"
                      % (entry.name, pct, pos, entry.csize),
                      end="", file=sys.stderr, flush=True)
        if dec is not None:
            tail = dec.flush()
            if tail:
                fh.write(tail)
                crc = zlib.crc32(tail, crc)
                written += len(tail)
    if entry.usize > (16 << 20):
        print("", file=sys.stderr)

    if written != entry.usize:
        raise SourceError("%s: wrote %d bytes, member says %d"
                          % (entry.name, written, entry.usize))
    if verify_crc and (crc & 0xFFFFFFFF) != entry.crc:
        raise SourceError("%s: CRC32 mismatch (got %08x, member says %08x)"
                          % (entry.name, crc & 0xFFFFFFFF, entry.crc))
    part.replace(dest)
    return dest, "extracted"


# --------------------------------------------------------------------------
# commands
# --------------------------------------------------------------------------
def human(n):
    return "{:,}".format(n)


def table(rows, headers, right=()):
    widths = [len(h) for h in headers]
    for row in rows:
        for i, cell in enumerate(row):
            widths[i] = max(widths[i], len(str(cell)))
    out = []
    out.append("  ".join(
        h.ljust(widths[i]) if i not in right else h.rjust(widths[i])
        for i, h in enumerate(headers)).rstrip())
    out.append("  ".join("-" * widths[i] for i in range(len(headers))))
    for row in rows:
        out.append("  ".join(
            str(c).ljust(widths[i]) if i not in right else str(c).rjust(widths[i])
            for i, c in enumerate(row)).rstrip())
    return "\n".join(out)


def cmd_list(zipf, args):
    entries = [e for e in zipf.entries if not e.is_dir]
    if args.sort_size:
        entries.sort(key=lambda e: -e.usize)
    rows = []
    for e in entries:
        rows.append([e.name, human(e.csize), human(e.usize),
                     METHOD_NAMES.get(e.method, str(e.method))])
    print("archive size: %s bytes" % human(zipf.src.size))
    print("members: %d, central directory: %d bytes at %d%s"
          % (len(entries), zipf._cd_size, zipf._cd_off,
             " (ZIP64)" if zipf.zip64 else ""))
    print()
    print(table(rows, ["member", "compressed", "uncompressed", "method"],
                right=(1, 2)))
    total_c = sum(e.csize for e in entries)
    total_u = sum(e.usize for e in entries)
    print()
    print("total: %s compressed, %s uncompressed" % (human(total_c),
                                                     human(total_u)))
    dmg = zipf.largest_dmg()
    if dmg:
        print("largest .dmg: %s (%s bytes)" % (dmg.name, human(dmg.usize)))
    return 0


class DmgReader:
    """Random access into the decompressed image of a DMG, chunk by chunk.

    Every BLKX run turned out to be a self-contained compressed stream -- each
    one ends with the codec's own end marker ('bvx$' for LZFSE) -- so reading a
    byte range costs one range request per covering chunk, not the whole data
    fork.  That is what makes a 4.7 GB DMG inspectable from a box that cannot
    store it.
    """

    def __init__(self, zipf, entry, koly, tables, codec, cache_chunks=32):
        self.zipf = zipf
        self.base = zipf.data_offset(entry) + koly["data_fork_offset"]
        self.codec = codec
        self.size = koly["sector_count"] * 512
        self.fetched = 0
        self.chunks_read = 0
        self.runs = []
        for _name, t in tables:
            # the runs inside a table are numbered from the partition's own
            # first sector, not from the start of the image
            pstart = t["first_sector"]
            for r in t["runs"]:
                if r["type"] in (0x7FFFFFFE, 0xFFFFFFFF) or r["count"] == 0:
                    continue
                self.runs.append((pstart + r["sector"], r["count"],
                                  r["comp_offset"], r["comp_length"],
                                  r["type"]))
        self.runs.sort(key=lambda r: r[0])
        self._starts = [r[0] for r in self.runs]
        # A 4 KiB fs-tree node costs one compressed chunk (~1 MiB) to read.
        # Walkers revisit the same handful of nodes, so keep a small LRU of
        # decoded chunks; without it one dyld extraction refetched 105 chunks
        # for 16 distinct APFS blocks.
        self._cache_chunks = cache_chunks
        self._cache = OrderedDict()
        if codec == "lzfse" and _lzfse is None:
            raise SourceError(
                "this DMG carries LZFSE chunks, which need a decoder:\n"
                "    python3 -m venv ~/ios-userland/venv\n"
                "    ~/ios-userland/venv/bin/pip install lzfse\n"
                "    ~/ios-userland/venv/bin/python %s ..."
                % Path(sys.argv[0]).name)
        if codec in ("zlib", "bzip2"):
            pass  # both are in the standard library
        elif codec not in ("lzfse", "raw"):
            raise SourceError("no decoder for codec %r" % codec)

    def _run_for(self, sector):
        i = bisect.bisect_right(self._starts, sector) - 1
        if i < 0:
            return None
        run = self.runs[i]
        if sector >= run[0] + run[1]:
            return None
        return run

    def _chunk(self, run):
        key = run[:2]
        cached = self._cache.get(key)
        if cached is not None:
            self._cache.move_to_end(key)
            return cached
        sector, count, comp_off, comp_len, ctype = run
        want = count * 512
        if comp_len == 0 or ctype in (0x00000000, 0x00000002):
            data = b"\0" * want
        else:
            raw = self.zipf.src.read_at(self.base + comp_off, comp_len)
            if len(raw) != comp_len:
                raise SourceError("short read of the chunk at offset %d: "
                                  "%d of %d bytes"
                                  % (comp_off, len(raw), comp_len))
            self.fetched += len(raw)
            self.chunks_read += 1
            if ctype == 0x00000001:
                data = raw
            elif self.codec == "lzfse":
                data = _lzfse.decompress(raw)
            elif self.codec == "zlib":
                data = zlib.decompressobj().decompress(raw)
            elif self.codec == "bzip2":
                data = bz2.decompress(raw)
            else:
                raise SourceError("chunk type 0x%x at offset %d is not "
                                  "decodable (%s)" % (ctype, comp_off,
                                                      self.codec))
        if len(data) != want:
            raise SourceError("chunk at sector %d decoded to %d bytes, "
                              "expected %d" % (sector, len(data), want))
        self._cache[key] = data
        while len(self._cache) > self._cache_chunks:
            self._cache.popitem(last=False)
        return data

    def read_at(self, offset, length):
        out = bytearray()
        while length > 0 and offset < self.size:
            run = self._run_for(offset // 512)
            if run is None:
                raise SourceError("offset %d is not covered by any chunk"
                                  % offset)
            chunk = self._chunk(run)
            within = offset - run[0] * 512
            n = min(length, len(chunk) - within)
            out += chunk[within:within + n]
            offset += n
            length -= n
        return bytes(out)


def cmd_apfs_info(zipf, entry, args, probe=False):
    """A member with no UDIF trailer: is it a bare APFS container?"""
    base = zipf.data_offset(entry)
    head = zipf.src.read_at(base, APFS_BS)
    if len(head) < 1024 or head[32:36] != b"NXSB":
        print("  no 'koly' trailer and no 'NXSB' at offset 0: this member is "
              "neither a UDIF image nor a bare APFS container", file=sys.stderr)
        return 2

    print("  no UDIF wrapper: the member is a bare APFS container, not a DMG")
    print("  (so there is no koly trailer, no BLKX table, no GPT and no "
          "sector count to report)")
    block_size, block_count = struct.unpack("<IQ", head[36:48])
    print("  container superblock at offset 0: block size %d, %s blocks "
          "(%s bytes), uuid %s"
          % (block_size, human(block_count),
             human(block_size * block_count), head[72:88].hex()))
    print("  the payload is block-addressable at %d bytes: block N lives at "
          "byte N*%d" % (block_size, block_size))

    rel, vol = scan_apfs_volume(zipf.src, base, block_size, 64 << 20)
    if vol:
        print("  volume superblock at byte %s: %r, %s files, %s dirs, %s "
              "symlinks, uuid %s"
              % (human(rel), vol["name"], human(vol["num_files"]),
                 human(vol["num_dirs"]), human(vol.get("num_symlinks", 0)),
                 vol["uuid"]))

    if not probe:
        return 0

    try:
        img = RawApfsImage(zipf.src, entry.usize)
    except SourceError as exc:
        print("  probe: %s" % exc, file=sys.stderr)
        return 2
    print("  fs-tree: %d leaf nodes, %d records, %d directories, %d files "
          "with extents, %d inodes"
          % (img.leaves, img.records, len(img.children), len(img.extents),
             len(img.inodes)))

    rc = 0
    if args.apfs_ls:
        fid, prefix, missing = img.lookup(args.apfs_ls)
        if fid is None:
            print("  %s: no such directory (stopped at %s, missing %r)"
                  % (args.apfs_ls, prefix, missing))
            rc = 1
        else:
            kids = img.children.get(fid, {})
            print()
            print("  %s/ (%d entries)" % (prefix, len(kids)))
            for name in sorted(kids):
                child = kids[name]
                print("    %-40s %12s" % (name, human(img.size_of(child))))

    for path in args.apfs_find or []:
        fid, prefix, missing = img.lookup(path)
        if fid is None:
            print()
            print("  %s: ABSENT (resolved %s, no entry %r)"
                  % (path, prefix, missing))
            continue
        exts = sorted(img.extents.get(fid, []))
        print()
        print("  %s: PRESENT, inode %d, %d extent(s), %s bytes"
              % (path, fid, len(exts), human(img.size_of(fid))))
        dec = img.decmpfs_info(fid) if img.is_compressed(fid) else None
        if dec:
            print("    decmpfs type %d (%s), %s bytes uncompressed"
                  % (dec["type"], DECMPFS_NAMES.get(dec["type"], "?"),
                     human(dec["size"])))
        for logical, phys, length in exts[:4]:
            print("    logical %-12d -> block %-8d %s bytes"
                  % (logical, phys, human(length)))
        data = img.read_file(fid)[:32]
        if data:
            print("    first bytes: %s  %r" % (data.hex(), data[:16]))

    if args.apfs_get:
        print()
        rc = max(rc, cmd_apfs_get(img, args.apfs_get, args.out))
    return rc


def cmd_apfs_get(img, root, outdir):
    """Extract every file below one directory of a bare APFS container."""
    fid, prefix, missing = img.lookup(root)
    if fid is None:
        print("  %s: no such directory (stopped at %s, missing %r)"
              % (root, prefix, missing), file=sys.stderr)
        return 2
    outdir = Path(outdir).expanduser()
    rows = []

    def walk(dfid, rel):
        for name in sorted(img.children.get(dfid, {})):
            child = img.children[dfid][name]
            child_rel = rel + name
            if img.is_dir(child):
                walk(child, child_rel + "/")
                continue
            data = img.read_file(child)
            dest = outdir.joinpath(*child_rel.split("/"))
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(data)
            rows.append([child_rel, human(len(data)), len(data)])

    walk(fid, "")
    print("  extracted %d files, %d bytes, from %s/ into %s"
          % (len(rows), sum(r[2] for r in rows), prefix, outdir))
    for rel, size, _n in rows:
        print("    %-48s %10s" % (rel, size))
    return 0


def cmd_dmg_info(zipf, args, probe=False):
    entry = zipf.find(args.dmg_member) if args.dmg_member else zipf.largest_dmg()
    if entry is None:
        print("no .dmg member in this archive", file=sys.stderr)
        return 2
    if entry.method != 0:
        print("%s is compressed inside the ZIP (%s); its trailer cannot be "
              "reached with a range request -- extract it first"
              % (entry.name, METHOD_NAMES.get(entry.method)), file=sys.stderr)
        return 2

    start = zipf.data_offset(entry)
    if getattr(zipf, "local", False):
        print("DMG file: %s" % entry.name)
        print("  %s bytes on disk, read in place" % human(entry.usize))
    else:
        print("DMG member: %s" % entry.name)
        print("  stored in the ZIP at offset %s, %s bytes"
              % (human(start), human(entry.usize)))

    koly, endian = parse_koly(
        zipf.src.read_at(start + entry.usize - 512, 512), entry.usize)
    if koly is None:
        return cmd_apfs_info(zipf, entry, args, probe=probe)

    print("  koly: version %d, header %d, flags 0x%x, variant %d, %s endian"
          % (koly["version"], koly["header_size"], koly["flags"],
             koly["image_variant"], "big" if endian == ">" else "little"))
    print("  data fork: %s bytes at %s, checksum type %d (%d bytes)"
          % (human(koly["data_fork_length"]), human(koly["data_fork_offset"]),
             koly["checksum"], koly["checksum_size"]))
    print("  image: %s sectors (%s bytes), segment %d of %d"
          % (human(koly["sector_count"]), human(koly["sector_count"] * 512),
             koly["segment"][0], koly["segment"][1]))
    print("  xml plist: %s bytes at %s"
          % (human(koly["xml_length"]), human(koly["xml_offset"])))
    print("  trailer arithmetic: %s + %s + 512 = %s (member is %s)"
          % (human(koly["data_fork_length"]), human(koly["xml_length"]),
             human(koly["data_fork_length"] + koly["xml_length"] + 512),
             human(entry.usize)))

    codec = sniff_codec(
        zipf.src.read_at(start + koly["data_fork_offset"], 64))
    print("  codec, from the first chunk's magic: %s"
          % CODEC_NAMES.get(codec, codec))

    xml_abs = start + koly["data_fork_offset"] + koly["xml_offset"]
    xml = zipf.src.read_at(xml_abs, koly["xml_length"])
    if len(xml) != koly["xml_length"]:
        print("  short read on the xml plist (%d of %d)"
              % (len(xml), koly["xml_length"]), file=sys.stderr)
        return 2
    try:
        plist = plistlib.loads(xml)
    except Exception as exc:
        print("  xml plist did not parse: %s" % exc, file=sys.stderr)
        return 2

    tables = []
    for item in plist.get("resource-fork", {}).get("blkx", []):
        parsed = parse_blkx(item.get("Data", b""), endian)
        if parsed:
            tables.append((item.get("Name") or item.get("CFName") or "?", parsed))
    fmt, parts = dmg_summary(koly, tables)
    print()
    print("  format: %s, %d partitions in the BLKX table"
          % (CODEC_NAMES.get(codec, fmt), len(parts)))
    print()
    rows = [[p["name"], human(p["first_sector"]), human(p["sectors"]),
             human(p["bytes"]), human(p["compressed"]),
             ",".join(CHUNK_TYPES.get(t, "0x%x" % t) for t in p["types"])]
            for p in parts]
    print(table(rows, ["partition", "first sector", "sectors", "bytes",
                       "compressed", "chunk types"], right=(1, 2, 3, 4)))
    total_c = sum(p["compressed"] for p in parts)
    total_s = sum(p["sectors"] for p in parts)
    print()
    print("  compressed bytes over all partitions: %s (data fork: %s)"
          % (human(total_c), human(koly["data_fork_length"])))
    print("  sectors over all partitions: %s (koly: %s)"
          % (human(total_s), human(koly["sector_count"])))

    if not probe:
        return 0
    return cmd_dmg_probe(zipf, entry, koly, tables, codec, args)


def cmd_dmg_probe(zipf, entry, koly, tables, codec, args):
    """Read the GPT and the filesystem superblocks straight out of the chunks."""
    try:
        reader = DmgReader(zipf, entry, koly, tables, codec)
    except SourceError as exc:
        print("  probe: %s" % exc, file=sys.stderr)
        return 2

    head = reader.read_at(0, 1 << 20)
    parts = parse_partitions(head)
    print()
    print("  partition table: %s" % (parts["kind"] or "not recognised").upper())
    for p in parts["partitions"]:
        print("    %-16s %-22s start %-10s %s sectors (%s bytes)"
              % (p["name"], p["type"], human(p["start"]), human(p["blocks"]),
                 human(p["blocks"] * 512)))

    found = False
    for p in parts["partitions"]:
        if "APFS" not in p["type"] and "HFS" not in p["type"]:
            continue
        base = p["start"] * 512
        blk = reader.read_at(base, 4096)
        nx = parse_apfs_nx(blk, 0)
        if nx:
            found = True
            print()
            print("  APFS container at byte %s:" % human(base))
            print("    block size %d, %s blocks (%s bytes), uuid %s"
                  % (nx["block_size"], human(nx["block_count"]),
                     human(nx["block_size"] * nx["block_count"]), nx["uuid"]))
            print("    max file systems %d, omap oid %d, spaceman oid %d"
                  % (nx["max_fs"], nx["omap_oid"], nx["spaceman_oid"]))
            omap = reader.read_at(base + nx["omap_oid"] * nx["block_size"],
                                  nx["block_size"])
            om_type = struct.unpack("<I", omap[24:28])[0]
            if om_type == 0x4000000B:  # OBJECT_TYPE_OMAP, a container object
                om_flags, om_snaps = struct.unpack("<II", omap[32:40])
                om_tree = struct.unpack("<Q", omap[48:56])[0]
                print("    omap object at block %d: flags 0x%x, snaps %d, "
                      "tree root block %d"
                      % (nx["omap_oid"], om_flags, om_snaps, om_tree))
                root = reader.read_at(base + om_tree * nx["block_size"],
                                      nx["block_size"])
                n_flags, n_level, n_keys = struct.unpack("<HHI", root[32:40])
                print("      tree root: level %d, %d keys (keys are 16 bytes, "
                      "a value carries the physical block of the object)"
                      % (n_level, n_keys))
            for oid in nx["fs_oids"]:
                off = base + oid * nx["block_size"]
                vol = parse_apfs_volume(reader.read_at(off, 1024))
                if vol is None:
                    # virtual oid: needs the omap, so fall back to a scan
                    rel, vol = scan_apfs_volume(reader, base, nx["block_size"],
                                                64 << 20)
                    if vol is not None:
                        print("    fs oid %d is virtual; found the volume "
                              "superblock at +%s by scanning the first 64 MiB"
                              % (oid, human(rel)))
                if vol:
                    print("    volume %d: %r, %s files, %s dirs, uuid %s"
                          % (vol["fs_index"], vol["name"],
                             human(vol["num_files"]), human(vol["num_dirs"]),
                             vol["uuid"]))
                else:
                    print("    volume object %d at byte %s: no 'APSB' magic, "
                          "and no scan hit" % (oid, human(off)))
            continue
        hfs = parse_hfsplus(reader.read_at(base, 1536), 0)
        if hfs:
            found = True
            print("  HFS+ volume: %s v%d, block size %d, %s blocks, %s files"
                  % (hfs["signature"], hfs["version"], hfs["block_size"],
                     human(hfs["total_blocks"]), human(hfs["files"])))
            continue
        print("  %s at byte %s: neither NXSB nor H+ where expected"
              % (p["type"], human(base)))

    print()
    print("  fetched %s bytes in %d chunks, for the whole probe"
          % (human(reader.fetched), reader.chunks_read))
    if not found:
        print("  probe: no filesystem signature recognised", file=sys.stderr)
        return 2
    print("""
  next step: the paths themselves live in the APFS volume's B-tree.  The
  chunks are addressable (see DmgReader), so a reader that walks the fs-tree
  and the omap can pull /usr/lib/dyld out with a few more megabytes of range
  requests -- no 4.7 GB download, no root, no mount.  Until that exists this
  probe only proves the container and the volume are readable.""")
    return 0


def cmd_extract(zipf, args):
    outdir = Path(args.out).expanduser()
    outdir.mkdir(parents=True, exist_ok=True)

    wanted = list(args.member or [])
    if args.member_file:
        for line in Path(args.member_file).read_text().splitlines():
            line = line.strip()
            if line and not line.startswith("#"):
                wanted.append(line)
    if not wanted:
        print("nothing to extract: pass -m/--member or --member-file",
              file=sys.stderr)
        return 2

    rows = []
    missing = []
    failed = 0
    for name in wanted:
        entry = zipf.find(name)
        if entry is None:
            missing.append(name)
            continue
        try:
            dest, status = extract_member(zipf, entry, outdir,
                                          force=args.force,
                                          verify_crc=not args.no_verify)
        except SourceError as exc:
            print("  %s: %s" % (name, exc), file=sys.stderr)
            failed += 1
            continue
        head = b""
        # a compressed IMG4 payload can only be described when the whole
        # member is in hand, so read it in full when that is cheap
        limit = (32 << 20) if _lzfse is not None else 4096
        if entry.usize <= limit:
            head = Path(dest).read_bytes()
        else:
            with open(dest, "rb") as fh:
                head = fh.read(4096)
        rows.append([entry.name, human(entry.csize), human(entry.usize),
                     METHOD_NAMES.get(entry.method, str(entry.method)),
                     status, describe_payload(head) if head else "-"])
        print("%-60s %s" % (entry.name, status))

    if rows:
        print()
        print(table(rows, ["member", "compressed", "uncompressed", "method",
                           "state", "payload"], right=(1, 2)))
        print()
        print("output: %s" % outdir.resolve())

    if missing:
        print()
        dmg = zipf.largest_dmg()
        for name in missing:
            print("not a zip member: %s" % name, file=sys.stderr)
        hint = ("  the iOS root filesystem is inside the largest .dmg (%s, %s "
                "bytes), not a top-level member;\n"
                "  run --dmg-info to inspect it, then --dmg-probe to see what "
                "filesystem it holds" % (dmg.name, human(dmg.usize))) if dmg else ""
        if hint:
            print(hint, file=sys.stderr)
        if failed or rows:
            return 1
        return 2
    return 1 if failed else 0


def main(argv=None):
    ap = argparse.ArgumentParser(
        description="Fetch individual members of an Apple IPSW with HTTP "
                    "range requests.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="The archive is never downloaded whole.")
    ap.add_argument("source", nargs="?",
                    help="IPSW URL, or a path to a local .ipsw; omit when "
                         "using --dmg-file")
    ap.add_argument("-l", "--list", action="store_true",
                    help="list members and their sizes")
    ap.add_argument("--sort-size", action="store_true",
                    help="with --list, biggest first")
    ap.add_argument("-o", "--out", default="ios-userland-out",
                    help="output directory (default: %(default)s)")
    ap.add_argument("-m", "--member", action="append", metavar="PATH",
                    help="member to extract, repeatable; a leading / is "
                         "ignored")
    ap.add_argument("-f", "--member-file", help="file with one member per line")
    ap.add_argument("--dmg-info", action="store_true",
                    help="parse the koly trailer and BLKX table of a DMG member")
    ap.add_argument("--dmg-member", help="which .dmg (default: the largest)")
    ap.add_argument("--dmg-file",
                    help="inspect a DMG that is already extracted on disk, "
                         "instead of one inside the archive (needed for "
                         "members the ZIP stores deflated)")
    ap.add_argument("--dmg-probe", action="store_true",
                    help="like --dmg-info, then read the partition table and "
                         "the filesystem superblocks out of the DMG")
    ap.add_argument("--apfs-find", action="append", metavar="PATH",
                    help="resolve a path inside a bare APFS container and "
                         "print its inode, extents and first bytes; repeatable")
    ap.add_argument("--apfs-ls", metavar="DIR",
                    help="list one directory of a bare APFS container")
    ap.add_argument("--apfs-get", metavar="DIR",
                    help="extract every file below DIR of a bare APFS "
                         "container into the -o/--out directory")
    ap.add_argument("--force", action="store_true",
                    help="re-extract members that already exist")
    ap.add_argument("--no-verify", action="store_true",
                    help="skip the CRC32 check")
    ap.add_argument("--timeout", type=float, default=60.0)
    ap.add_argument("--retries", type=int, default=6)
    args = ap.parse_args(argv)

    if args.dmg_file:
        if not (args.dmg_info or args.dmg_probe or args.apfs_find
                or args.apfs_ls or args.apfs_get):
            print("--dmg-file needs --dmg-info, --dmg-probe, --apfs-find, "
                  "--apfs-ls or --apfs-get", file=sys.stderr)
            return 2
        probe = bool(args.dmg_probe or args.apfs_find or args.apfs_ls
                     or args.apfs_get)
        try:
            dmg = LocalDmg(args.dmg_file)
        except OSError as exc:
            print("cannot open %s: %s" % (args.dmg_file, exc), file=sys.stderr)
            return 2
        try:
            print("source: %s (%s bytes)" % (args.dmg_file, human(dmg.src.size)))
            return cmd_dmg_info(dmg, args, probe=probe)
        finally:
            dmg.src.close()

    if not args.source:
        ap.error("give a source IPSW, or --dmg-file to inspect an extracted DMG")

    try:
        src = open_source(args.source, timeout=args.timeout,
                          retries=args.retries)
    except SourceError as exc:
        print("cannot open source: %s" % exc, file=sys.stderr)
        return 2

    try:
        print("source: %s (%s bytes)" % (args.source, human(src.size)))
        zipf = RemoteZip(src)
        if args.list:
            return cmd_list(zipf, args)
        if args.dmg_probe:
            return cmd_dmg_info(zipf, args, probe=True)
        if args.dmg_info:
            return cmd_dmg_info(zipf, args)
        return cmd_extract(zipf, args)
    except SourceError as exc:
        print("error: %s" % exc, file=sys.stderr)
        return 2
    finally:
        src.close()


if __name__ == "__main__":
    sys.exit(main())
